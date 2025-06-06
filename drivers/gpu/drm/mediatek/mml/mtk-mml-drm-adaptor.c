// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Dennis YC Hsieh <dennis-yc.hsieh@mediatek.com>
 */

#include <linux/atomic.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/sync_file.h>
#include <linux/time64.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include <mtk_sync.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "mtk-mml-drm-adaptor.h"
#include "mtk-mml-buf.h"
#include "mtk-mml-color.h"
#include "mtk-mml-core.h"
#include "mtk-mml-driver.h"
#include "mtk-mml-sys.h"
#include "mtk-mml-mmp.h"

#define MML_DEFAULT_END_NS	15000000

/* set to 0 to disable reuse config */
int mml_reuse = 1;
module_param(mml_reuse, int, 0644);

int mml_max_cache_task = 4;
module_param(mml_max_cache_task, int, 0644);

int mml_max_cache_cfg = 2;
module_param(mml_max_cache_cfg, int, 0644);

struct mml_drm_ctx {
	struct list_head configs;
	u32 config_cnt;
	atomic_t racing_cnt;	/* ref count for racing tasks */
	struct mutex config_mutex;
	struct mml_dev *mml;
	const struct mml_task_ops *task_ops;
	const struct mml_config_ops *cfg_ops;
	atomic_t job_serial;
	atomic_t config_serial;
	struct kthread_worker *kt_config[MML_PIPE_CNT];
	struct workqueue_struct *wq_destroy;
	struct kthread_worker *kt_done;
	struct task_struct *kt_done_task;
	struct sync_timeline *timeline;
	u32 panel_pixel;
	bool kt_priority;
	bool disp_dual;
	bool disp_vdo;
	bool racing_begin;
	void (*submit_cb)(void *cb_param);
	void (*ddren_cb)(struct cmdq_pkt *pkt, bool enable, void *ddren_param);
	void *ddren_param;
	void (*dispen_cb)(bool enable, void *dispen_param);
	void *dispen_param;
	struct mml_tile_cache tile_cache[MML_PIPE_CNT];
};

enum mml_mode mml_drm_query_cap(struct mml_drm_ctx *ctx,
				struct mml_frame_info *info)
{
	u8 i;
	struct mml_topology_cache *tp = mml_topology_get_cache(ctx->mml);
	const u32 srcw = info->src.width;
	const u32 srch = info->src.height;
	enum mml_mode mode;
	u32 reason = 0;

	if (mml_pq_disable) {
		for (i = 0; i < MML_MAX_OUTPUTS; i++) {
			memset(&info->dest[i].pq_config, 0,
				sizeof(info->dest[i].pq_config));
		}
	}

	if (info->dest_cnt > MML_MAX_OUTPUTS)
		info->dest_cnt = MML_MAX_OUTPUTS;

	if (!info->src.format) {
		mml_err("[drm]invalid src mml color format %#010x", info->src.format);
		goto not_support;
	}

	if (MML_FMT_BLOCK(info->src.format)) {
		if ((info->src.width & 0x0f) || (info->src.height & 0x1f)) {
			mml_err(
				"[drm]invalid blk width %u height %u must alignment width 16x height 32x",
				info->src.width, info->src.height);
			goto not_support;
		}
	}

	/* for alpha rotate */
	if (info->alpha &&
	    MML_FMT_IS_ARGB(info->src.format) &&
	    MML_FMT_IS_ARGB(info->dest[0].data.format)) {
		const struct mml_frame_dest *dest = &info->dest[0];
		u32 srccw = dest->crop.r.width;
		u32 srcch = dest->crop.r.height;
		u32 destw = dest->data.width;
		u32 desth = dest->data.height;

		if (dest->rotate == MML_ROT_90 || dest->rotate == MML_ROT_270)
			swap(destw, desth);

		if (srcw < 9) {
			mml_err("exceed HW limitation src width %u < 9", srcw);
			goto not_support;
		}
		if (srccw != destw || srcch != desth) {
			mml_err("unsupport alpha rotation for resize case crop %u,%u to dest %u,%u",
				srccw, srcch, destw, desth);
			goto not_support;
		}
	}

	for (i = 0; i < info->dest_cnt; i++) {
		const struct mml_frame_dest *dest = &info->dest[i];
		u32 destw = dest->data.width;
		u32 desth = dest->data.height;
		u32 crop_srcw = dest->crop.r.width ? dest->crop.r.width : srcw;
		u32 crop_srch = dest->crop.r.height ? dest->crop.r.height : srch;

		if (dest->rotate == MML_ROT_90 || dest->rotate == MML_ROT_270)
			swap(destw, desth);

		if (crop_srcw / destw > 20 || crop_srch / desth > 255 ||
			destw / crop_srcw > 32 || desth / crop_srch > 32) {
			mml_err("[drm]exceed HW limitation src %ux%u dest %ux%u",
				crop_srcw, crop_srch, destw, desth);
			goto not_support;
		}

		if ((crop_srcw * desth) / (destw * crop_srch) > 16 ||
			(destw * crop_srch) / (crop_srcw * desth) > 16) {
			mml_err("[drm]exceed tile ratio limitation src %ux%u dest %ux%u",
				crop_srcw, crop_srch, destw, desth);
			goto not_support;
		}

		/* check crop and pq combination */
		if (dest->pq_config.en && dest->crop.r.width < 48) {
			mml_err("[drm]exceed HW limitation crop width %u < 48 with pq",
				dest->crop.r.width);
			goto not_support;
		}
	}

	if (!tp || !tp->op->query_mode)
		goto not_support;

	mode = tp->op->query_mode(ctx->mml, info, &reason);
	if (atomic_read(&ctx->racing_cnt) && mode == MML_MODE_MML_DECOUPLE) {
		/* if mml hw running racing mode and query info need dc,
		 * go back to MDP decouple to avoid hw conflict.
		 *
		 * Note: no mutex lock here cause disp call query/submit on
		 * same kernel thread, thus racing_cnt can only decrease and
		 * not increase after read. And it's safe to do one more mdp
		 * decouple w/o mml racing/dc conflict.
		 */
		mml_log("%s mode %u to mdp dc", __func__, mode);
		mode = MML_MODE_MDP_DECOUPLE;
	}

	mml_mmp(query_mode, MMPROFILE_FLAG_PULSE,
		(info->mode << 16) | mode, reason);
	return mode;

not_support:
	mml_mmp(query_mode, MMPROFILE_FLAG_PULSE, MML_MODE_NOT_SUPPORT, info->mode);
	return MML_MODE_NOT_SUPPORT;
}
EXPORT_SYMBOL_GPL(mml_drm_query_cap);

static void mml_adjust_src(struct mml_frame_data *src)
{
	const u32 srcw = src->width;
	const u32 srch = src->height;

	if (MML_FMT_H_SUBSAMPLE(src->format) && (srcw & 0x1))
		src->width &= ~1;

	if (MML_FMT_V_SUBSAMPLE(src->format) && (srch & 0x1))
		src->height &= ~1;
}

static void mml_adjust_dest(struct mml_frame_data *src, struct mml_frame_dest *dest)
{
	if (dest->rotate == MML_ROT_90 || dest->rotate == MML_ROT_270) {
		if (MML_FMT_H_SUBSAMPLE(dest->data.format)) {
			dest->data.width &= ~1; /* WROT HW constraint */
			dest->data.height &= ~1;
		} else if (MML_FMT_V_SUBSAMPLE(dest->data.format)) {
			dest->data.width &= ~1;
		}
	} else {
		if (MML_FMT_H_SUBSAMPLE(dest->data.format))
			dest->data.width &= ~1;

		if (MML_FMT_V_SUBSAMPLE(dest->data.format))
			dest->data.height &= ~1;
	}

	/* help user fill in crop if not give */
	if (!dest->crop.r.width && !dest->crop.r.height) {
		dest->crop.r.width = src->width;
		dest->crop.r.height = src->height;
	}

	if (!dest->compose.width && !dest->compose.height) {
		dest->compose.width = dest->data.width;
		dest->compose.height = dest->data.height;
	}
}

void mml_drm_try_frame(struct mml_drm_ctx *ctx, struct mml_frame_info *info)
{
	u32 i;

	mml_adjust_src(&info->src);

	if (info->dest[0].pq_config.en_region_pq)
		mml_adjust_src(&info->seg_map);

	for (i = 0; i < info->dest_cnt; i++) {
		/* adjust info data directly for user */
		mml_adjust_dest(&info->src, &info->dest[i]);
	}

	if ((MML_FMT_PLANE(info->src.format) > 1) && info->src.uv_stride <= 0)
		info->src.uv_stride = mml_color_get_min_uv_stride(
			info->src.format, info->src.width);

}
EXPORT_SYMBOL_GPL(mml_drm_try_frame);

static u32 afbc_drm_to_mml(u32 drm_format)
{
	switch (drm_format) {
	case MML_FMT_RGBA8888:
		return MML_FMT_RGBA8888_AFBC;
	case MML_FMT_BGRA8888:
		return MML_FMT_BGRA8888_AFBC;
	case MML_FMT_RGBA1010102:
		return MML_FMT_RGBA1010102_AFBC;
	case MML_FMT_BGRA1010102:
		return MML_FMT_BGRA1010102_AFBC;
	case MML_FMT_NV12:
		return MML_FMT_YUV420_AFBC;
	case MML_FMT_NV21:
		return MML_FMT_YVU420_AFBC;
	case MML_FMT_NV12_10L:
		return MML_FMT_YUV420_10P_AFBC;
	case MML_FMT_NV21_10L:
		return MML_FMT_YVU420_10P_AFBC;
	default:
		mml_err("[drm]%s unknown drm format %#x", __func__, drm_format);
		return drm_format;
	}
}

#define MML_AFBC	DRM_FORMAT_MOD_ARM_AFBC( \
	AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | AFBC_FORMAT_MOD_SPLIT)

static u32 format_drm_to_mml(u32 drm_format, u64 modifier)
{
	/* check afbc modifier with rdma/wrot supported
	 * 32x8 block and split mode
	 */
	if (modifier == MML_AFBC && !MML_FMT_COMPRESS(drm_format))
		return afbc_drm_to_mml(drm_format);

	return drm_format;
}

static bool check_frame_wo_change(struct mml_submit *submit,
			       struct mml_frame_config *cfg)
{
	/* Only when both of frame info and dl_out are not changed, return true,
	 * else return false
	 */
	return (!memcmp(&submit->info, &cfg->info, sizeof(submit->info)) &&
		!memcmp(&submit->dl_out[0], &cfg->dl_out[0], sizeof(submit->dl_out)));
}

static struct mml_frame_config *frame_config_find_reuse(
	struct mml_drm_ctx *ctx,
	struct mml_submit *submit)
{
	struct mml_frame_config *cfg;
	u32 idx = 0, mode = MML_MODE_UNKNOWN;

	if (!mml_reuse)
		return NULL;

	mml_trace_ex_begin("%s", __func__);

	list_for_each_entry(cfg, &ctx->configs, entry) {
		if (!idx)
			mode = cfg->info.mode;

		if (submit->update && cfg->last_jobid == submit->job->jobid)
			goto done;

		if (check_frame_wo_change(submit, cfg))
			goto done;

		idx++;
	}

	/* not found, give return value to NULL */
	cfg = NULL;

done:
	if (cfg && idx) {
		if (mode != cfg->info.mode)
			mml_log("[drm]mode change to %hhu", cfg->info.mode);
		list_rotate_to_front(&cfg->entry, &ctx->configs);
	}

	mml_trace_ex_end();
	return cfg;
}

static struct mml_task *task_get_idle(struct mml_frame_config *cfg)
{
	struct mml_task *task = list_first_entry_or_null(
		&cfg->done_tasks, struct mml_task, entry);

	if (task) {
		list_del_init(&task->entry);
		cfg->done_task_cnt--;
		memset(&task->buf, 0, sizeof(task->buf));
	}
	return task;
}

static void task_move_to_destroy(struct kref *kref)
{
	struct mml_task *task = container_of(kref, struct mml_task, ref);

	if (task->config) {
		struct mml_frame_config *cfg = task->config;

		cfg->cfg_ops->put(cfg);
		task->config = NULL;
	}

	mml_core_destroy_task(task);
}

static void frame_config_destroy(struct mml_frame_config *cfg)
{
	struct mml_task *task, *tmp;

	mml_msg("[drm]%s frame config %p", __func__, cfg);

	if (WARN_ON(!list_empty(&cfg->await_tasks))) {
		mml_err("[drm]still waiting tasks in wq during destroy config");
		list_for_each_entry_safe(task, tmp, &cfg->await_tasks, entry) {
			/* unable to handling error,
			 * print error but not destroy
			 */
			mml_err("[drm]busy task:%p", task);
			kref_put(&task->ref, task_move_to_destroy);
		}
	}

	if (WARN_ON(!list_empty(&cfg->tasks))) {
		mml_err("[drm]still busy tasks during destroy config");
		list_for_each_entry_safe(task, tmp, &cfg->tasks, entry) {
			/* unable to handling error,
			 * print error but not destroy
			 */
			mml_err("[drm]busy task:%p", task);
			kref_put(&task->ref, task_move_to_destroy);
		}
	}

	list_for_each_entry_safe(task, tmp, &cfg->done_tasks, entry) {
		list_del_init(&task->entry);
		kref_put(&task->ref, task_move_to_destroy);
	}

	cfg->cfg_ops->put(cfg);
}

static void frame_config_free(struct kref *kref)
{
	struct mml_frame_config *cfg = container_of(kref, struct mml_frame_config, ref);

	mml_core_deinit_config(cfg);
	kfree(cfg);

	mml_msg("[drm]%s frame config %p destroy done", __func__, cfg);
}

static void frame_config_destroy_work(struct work_struct *work)
{
	struct mml_frame_config *cfg = container_of(work,
		struct mml_frame_config, work_destroy);

	frame_config_destroy(cfg);
}

static void frame_config_queue_destroy(struct mml_frame_config *cfg)
{
	struct mml_drm_ctx *ctx = cfg->ctx;

	queue_work(ctx->wq_destroy, &cfg->work_destroy);
}

static struct mml_frame_config *frame_config_create(
	struct mml_drm_ctx *ctx,
	struct mml_submit *submit)
{
	struct mml_frame_info *info = &submit->info;
	struct mml_frame_config *cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);

	if (!cfg)
		return ERR_PTR(-ENOMEM);
	mml_core_init_config(cfg);
	list_add(&cfg->entry, &ctx->configs);
	ctx->config_cnt++;
	cfg->job_id = atomic_inc_return(&ctx->config_serial);
	cfg->info = *info;
	cfg->disp_dual = ctx->disp_dual;
	cfg->disp_vdo = ctx->disp_vdo;
	cfg->ctx = ctx;
	cfg->mml = ctx->mml;
	cfg->task_ops = ctx->task_ops;
	cfg->cfg_ops = ctx->cfg_ops;
	cfg->ctx_kt_done = ctx->kt_done;
	memcpy(cfg->dl_out, submit->dl_out, sizeof(cfg->dl_out));
	INIT_WORK(&cfg->work_destroy, frame_config_destroy_work);
	kref_init(&cfg->ref);

	return cfg;
}

static u32 frame_calc_layer_hrt(struct mml_drm_ctx *ctx, struct mml_frame_info *info,
	u32 layer_w, u32 layer_h)
{
	/* MML HRT bandwidth calculate by
	 *	bw = width * height * Bpp * fps * v-blanking
	 *
	 * for raw data format, data size is
	 *	bpp * width / 8
	 * and for block format (such as UFO), data size is
	 *	bpp * width / 256
	 *
	 * And for resize case total source pixel must read during layer
	 * region (which is compose width and height). So ratio should be:
	 *	ratio = src / layer
	 *
	 * HRT pixel and bandwidth:
	 *	hrt = panel * ratio
	 *	hrt_bw = panel * src / layer * Bpp * fps * v-blanking
	 *
	 * This API returns bandwidth in KBps: panel * bw / layer
	 *
	 * Following api reorder factors to avoid overflow of uint32_t.
	 */
	u32 plane = MML_FMT_PLANE(info->src.format);
	u64 hrt;

	/* calculate source data size as bandwidth */
	hrt = mml_color_get_min_y_size(info->src.format, info->src.width, info->src.height);
	if (!MML_FMT_COMPRESS(info->src.format) && plane > 1) {
		if (plane == 2)
			hrt += mml_color_get_min_uv_size(info->src.format,
				info->src.width, info->src.height);
		else if (plane == 3)
			hrt += mml_color_get_min_uv_size(info->src.format,
				info->src.width, info->src.height) * 2;
	}

	/* calculate panel ratio, v-blanking overhead, fps */
	hrt = hrt * ctx->panel_pixel / layer_w / layer_h * 122 / 100 * MML_HRT_FPS / 1000;

	return (u32)hrt;
}

static s32 frame_buf_to_task_buf(struct mml_file_buf *fbuf,
				  struct mml_buffer *user_buf,
				  const char *name)
{
	u8 i;
	s32 ret = 0;

	if (user_buf->use_dma)
		mml_buf_get(fbuf, user_buf->dmabuf, user_buf->cnt, name);
	else
		ret = mml_buf_get_fd(fbuf, user_buf->fd, user_buf->cnt, name);

	/* also copy size for later use */
	for (i = 0; i < user_buf->cnt; i++)
		fbuf->size[i] = user_buf->size[i];
	fbuf->cnt = user_buf->cnt;
	fbuf->flush = user_buf->flush;
	fbuf->invalid = user_buf->invalid;

	if (user_buf->fence >= 0) {
		fbuf->fence = sync_file_get_fence(user_buf->fence);
		mml_msg("[drm]get dma fence %p by %d", fbuf->fence, user_buf->fence);
	}

	return ret;
}

static void task_move_to_running(struct mml_task *task)
{
	/* Must lock ctx->config_mutex before call
	 * For INITIAL / DUPLICATE state move to running,
	 * otherwise do nothing.
	 */
	if (task->state != MML_TASK_INITIAL &&
		task->state != MML_TASK_DUPLICATE &&
		task->state != MML_TASK_REUSE) {
		mml_msg("[drm]%s task %p state conflict %u",
			__func__, task, task->state);
		return;
	}

	if (list_empty(&task->entry)) {
		mml_err("[drm]%s task %p already leave config",
			__func__, task);
		return;
	}

	list_del_init(&task->entry);
	task->config->await_task_cnt--;
	task->state = MML_TASK_RUNNING;
	list_add_tail(&task->entry, &task->config->tasks);
	task->config->run_task_cnt++;

	mml_msg("[drm]%s task cnt (%u %u %hhu)",
		__func__,
		task->config->await_task_cnt,
		task->config->run_task_cnt,
		task->config->done_task_cnt);
}

static void task_move_to_idle(struct mml_task *task)
{
	struct mml_frame_config *cfg = task->config;
	struct mml_drm_ctx *ctx = task->ctx;

	/* Must lock ctx->config_mutex before call */
	if (task->state == MML_TASK_INITIAL ||
		task->state == MML_TASK_DUPLICATE ||
		task->state == MML_TASK_REUSE) {
		/* move out from awat list */
		task->config->await_task_cnt--;
	} else if (task->state == MML_TASK_RUNNING) {
		/* move out from tasks list */
		task->config->run_task_cnt--;
	} else {
		/* unknown state transition */
		mml_err("[drm]%s state conflict %d", __func__, task->state);
	}

	list_del_init(&task->entry);
	task->state = MML_TASK_IDLE;
	list_add_tail(&task->entry, &task->config->done_tasks);
	task->config->done_task_cnt++;

	/* maintain racing ref count decrease after done */
	if (cfg->info.mode == MML_MODE_RACING)
		atomic_dec(&ctx->racing_cnt);

	mml_msg("[drm]%s task cnt (%u %u %hhu) racing %d",
		__func__,
		task->config->await_task_cnt,
		task->config->run_task_cnt,
		task->config->done_task_cnt,
		atomic_read(&ctx->racing_cnt));
}

static void task_submit_done(struct mml_task *task)
{
	struct mml_drm_ctx *ctx = task->ctx;

	mml_msg("[drm]%s task %p state %u", __func__, task, task->state);

	mml_mmp(submit_cb, MMPROFILE_FLAG_PULSE, task->job.jobid, 0);

	if (ctx->submit_cb)
		ctx->submit_cb(task->cb_param);

	mutex_lock(&ctx->config_mutex);
	task_move_to_running(task);
	kref_put(&task->ref, task_move_to_destroy);
	mutex_unlock(&ctx->config_mutex);
}

static void task_buf_put(struct mml_task *task)
{
	u8 i;

	mml_trace_ex_begin("%s_putbuf", __func__);
	for (i = 0; i < task->buf.dest_cnt; i++) {
		mml_msg("[drm]release dest %hhu iova %#011llx",
			i, task->buf.dest[i].dma[0].iova);
		mml_buf_put(&task->buf.dest[i]);
		if (task->buf.dest[i].fence)
			dma_fence_put(task->buf.dest[i].fence);
	}
	mml_msg("[drm]release src iova %#011llx",
		task->buf.src.dma[0].iova);
	mml_buf_put(&task->buf.src);
	if (task->buf.src.fence)
		dma_fence_put(task->buf.src.fence);
	if (task->config->info.dest[0].pq_config.en_region_pq) {
		mml_msg("[drm]release seg_map iova %#011llx",
			task->buf.seg_map.dma[0].iova);
		mml_buf_put(&task->buf.seg_map);
		if (task->buf.seg_map.fence)
			dma_fence_put(task->buf.seg_map.fence);
	}
	mml_trace_ex_end();
}

static void task_state_dec(struct mml_frame_config *cfg, struct mml_task *task,
	const char *api)
{
	if (list_empty(&task->entry))
		return;

	list_del_init(&task->entry);

	switch (task->state) {
	case MML_TASK_INITIAL:
	case MML_TASK_DUPLICATE:
	case MML_TASK_REUSE:
		cfg->await_task_cnt--;
		break;
	case MML_TASK_RUNNING:
		cfg->run_task_cnt--;
		break;
	case MML_TASK_IDLE:
		cfg->done_task_cnt--;
		break;
	default:
		mml_err("%s conflict state %u", api, task->state);
	}
}

static void task_frame_done(struct mml_task *task)
{
	struct mml_frame_config *cfg = task->config;
	struct mml_frame_config *tmp;
	struct mml_drm_ctx *ctx = task->ctx;
	struct mml_dev *mml = cfg->mml;

	mml_trace_ex_begin("%s", __func__);

	mml_msg("[drm]frame done task %p state %u job %u",
		task, task->state, task->job.jobid);

	/* clean up */
	task_buf_put(task);

	mutex_lock(&ctx->config_mutex);

	if (unlikely(!task->pkts[0] || (cfg->dual && !task->pkts[1]))) {
		task_state_dec(cfg, task, __func__);
		mml_err("[drm]%s task cnt (%u %u %hhu) error from state %d",
			__func__,
			cfg->await_task_cnt,
			cfg->run_task_cnt,
			cfg->done_task_cnt,
			task->state);
		task->err = true;
		mml_record_track(mml, task);
		kref_put(&task->ref, task_move_to_destroy);
	} else {
		/* works fine, safe to move */
		task_move_to_idle(task);
		mml_record_track(mml, task);
	}

	if (cfg->done_task_cnt > mml_max_cache_task) {
		task = list_first_entry(&cfg->done_tasks, typeof(*task), entry);
		list_del_init(&task->entry);
		cfg->done_task_cnt--;
		mml_msg("[drm]%s task cnt (%u %u %hhu)",
			__func__,
			task->config->await_task_cnt,
			task->config->run_task_cnt,
			task->config->done_task_cnt);
		kref_put(&task->ref, task_move_to_destroy);
	}

	/* still have room to cache, done */
	if (ctx->config_cnt <= mml_max_cache_cfg)
		goto done;

	/* must pick cfg from list which is not running */
	list_for_each_entry_safe_reverse(cfg, tmp, &ctx->configs, entry) {
		/* only remove config not running */
		if (!list_empty(&cfg->tasks) || !list_empty(&cfg->await_tasks))
			continue;
		list_del_init(&cfg->entry);
		frame_config_queue_destroy(cfg);
		ctx->config_cnt--;
		mml_msg("[drm]config %p send destroy remain %u",
			cfg, ctx->config_cnt);

		/* check cache num again */
		if (ctx->config_cnt <= mml_max_cache_cfg)
			break;
	}

done:
	mutex_unlock(&ctx->config_mutex);

	mml_lock_wake_lock(mml, false);

	mml_trace_ex_end();
}

static void dump_pq_en(u32 idx, struct mml_pq_param *pq_param,
	struct mml_pq_config *pq_config)
{
	u32 pqen = 0;

	memcpy(&pqen, pq_config, min(sizeof(*pq_config), sizeof(pqen)));

	if (pq_param)
		mml_msg("[drm]PQ %u config %#x param en %u %s",
			idx, pqen, pq_param->enable,
			mml_pq_disable ? "FORCE DISABLE" : "");
	else
		mml_msg("[drm]PQ %u config %#x param NULL %s",
			idx, pqen,
			mml_pq_disable ? "FORCE DISABLE" : "");
}

static void frame_calc_plane_offset(struct mml_frame_data *data,
	struct mml_buffer *buf)
{
	u32 i;

	data->plane_offset[0] = 0;
	for (i = 1; i < MML_FMT_PLANE(data->format); i++) {
		if (buf->fd[i] != buf->fd[i-1] && buf->fd[i] >= 0) {
			/* different buffer for different plane, set to begin */
			data->plane_offset[i] = 0;
			continue;
		}
		data->plane_offset[i] = data->plane_offset[i-1] + buf->size[i-1];
	}
}

static void frame_check_end_time(struct timespec64 *endtime)
{
	if (!endtime->tv_sec && !endtime->tv_nsec) {
		ktime_get_real_ts64(endtime);
		timespec64_add_ns(endtime, MML_DEFAULT_END_NS);
	}
}

s32 mml_drm_submit(struct mml_drm_ctx *ctx, struct mml_submit *submit,
	void *cb_param)
{
	struct mml_frame_config *cfg;
	struct mml_task *task = NULL;
	s32 result = -EINVAL;
	u32 i;
	struct fence_data fence = {0};

	mml_trace_begin("%s", __func__);

	if (mtk_mml_msg || mml_pq_disable) {
		for (i = 0; i < MML_MAX_OUTPUTS; i++) {
			dump_pq_en(i, submit->pq_param[i],
				&submit->info.dest[i].pq_config);

			if (mml_pq_disable) {
				submit->pq_param[i] = NULL;
				memset(&submit->info.dest[i].pq_config, 0,
					sizeof(submit->info.dest[i].pq_config));
			}
		}
	}

	/* always fixup dest_cnt > MML_MAX_OUTPUTS */
	if (submit->info.dest_cnt > MML_MAX_OUTPUTS)
		submit->info.dest_cnt = MML_MAX_OUTPUTS;
	if (submit->buffer.dest_cnt > MML_MAX_OUTPUTS)
		submit->buffer.dest_cnt = MML_MAX_OUTPUTS;

	/* always fixup format/modifier for afbc case
	 * the format in info should change to fourcc format in future design
	 * and store mml format in another structure
	 */
	submit->info.src.format = format_drm_to_mml(
		submit->info.src.format, submit->info.src.modifier);

	/* always fixup plane offset */
	if (likely(submit->info.mode != MML_MODE_SRAM_READ)) {
		frame_calc_plane_offset(&submit->info.src, &submit->buffer.src);
		frame_calc_plane_offset(&submit->info.seg_map, &submit->buffer.seg_map);
		for (i = 0; i < submit->info.dest_cnt; i++)
			frame_calc_plane_offset(&submit->info.dest[i].data,
				&submit->buffer.dest[i]);
	}

	if (MML_FMT_YUV_COMPRESS(submit->info.src.format)) {
		submit->info.src.y_stride =
			mml_color_get_min_y_stride(submit->info.src.format, submit->info.src.width);
		submit->info.src.uv_stride = 0;
		submit->info.src.plane_cnt = 1;
		submit->buffer.src.cnt = 1;
		submit->buffer.src.fd[1] = -1;
		submit->buffer.src.fd[2] = -1;
	}

	for (i = 0; i < submit->info.dest_cnt; i++)
		submit->info.dest[i].data.format = format_drm_to_mml(
			submit->info.dest[i].data.format,
			submit->info.dest[i].data.modifier);

	/* give default act time in case something wrong in disp
	 * the 2604 base on cmd mode
	 *	ns / fps / vtotal = line_time, which expend to
	 *	1000000000 / 120 / 3228 = 2581
	 */
	if (submit->info.mode == MML_MODE_RACING && !submit->info.act_time)
		submit->info.act_time = 2581 * submit->info.dest[0].data.height;

	/* always do frame info adjust for now
	 * but this flow should call from hwc/disp in future version
	 */
	mml_drm_try_frame(ctx, &submit->info);

	/* +1 for real id assign to next task */
	mml_mmp(submit, MMPROFILE_FLAG_PULSE,
		atomic_read(&ctx->job_serial) + 1, submit->info.mode);

	mutex_lock(&ctx->config_mutex);

	cfg = frame_config_find_reuse(ctx, submit);
	if (cfg) {
		mml_msg("[drm]%s reuse config %p", __func__, cfg);
		task = task_get_idle(cfg);
		if (task) {
			/* reuse case change state IDLE to REUSE */
			task->state = MML_TASK_REUSE;
			init_completion(&task->pkts[0]->cmplt);
			if (task->pkts[1])
				init_completion(&task->pkts[1]->cmplt);
			mml_msg("[drm]reuse task %p pkt %p %p",
				task, task->pkts[0], task->pkts[1]);
		} else {
			task = mml_core_create_task();
			if (IS_ERR(task)) {
				result = PTR_ERR(task);
				mml_err("%s create task for reuse frame fail", __func__);
				goto err_unlock_exit;
			}
			task->config = cfg;
			task->state = MML_TASK_DUPLICATE;
			/* add more count for new task create */
			cfg->cfg_ops->get(cfg);
		}
	} else {
		cfg = frame_config_create(ctx, submit);
		mml_msg("[drm]%s create config %p", __func__, cfg);
		if (IS_ERR(cfg)) {
			result = PTR_ERR(cfg);
			mml_err("%s create frame config fail", __func__);
			goto err_unlock_exit;
		}
		task = mml_core_create_task();
		if (IS_ERR(task)) {
			list_del_init(&cfg->entry);
			frame_config_destroy(cfg);
			result = PTR_ERR(task);
			task = NULL;
			mml_err("%s create task fail", __func__);
			goto err_unlock_exit;
		}
		task->config = cfg;
		if (submit->info.mode == MML_MODE_RACING) {
			cfg->layer_w = submit->layer.width;
			if (unlikely(!cfg->layer_w))
				cfg->layer_w = submit->info.dest[0].compose.width;
			cfg->layer_h = submit->layer.height;
			if (unlikely(!cfg->layer_h))
				cfg->layer_h = submit->info.dest[0].compose.height;
			cfg->disp_hrt = frame_calc_layer_hrt(ctx, &submit->info,
				cfg->layer_w, cfg->layer_h);
		}

		/* add more count for new task create */
		cfg->cfg_ops->get(cfg);
	}

	/* maintain racing ref count for easy query mode */
	if (cfg->info.mode == MML_MODE_RACING) {
		/* also mark begin so that disp clear target line event */
		if (atomic_inc_return(&ctx->racing_cnt) == 1)
			ctx->racing_begin = true;
	}

	/* make sure id unique and cached last */
	task->job.jobid = atomic_inc_return(&ctx->job_serial);
	task->cb_param = cb_param;
	cfg->last_jobid = task->job.jobid;
	list_add_tail(&task->entry, &cfg->await_tasks);
	cfg->await_task_cnt++;
	mml_msg("[drm]%s task cnt (%u %u %hhu) racing %d",
		__func__,
		task->config->await_task_cnt,
		task->config->run_task_cnt,
		task->config->done_task_cnt,
		atomic_read(&ctx->racing_cnt));

	mutex_unlock(&ctx->config_mutex);

	/* copy per-frame info */
	task->ctx = ctx;
	if (submit->end.nsec >= cfg->dvfs_boost_time.tv_nsec) {
		task->end_time.tv_sec =
			submit->end.sec - cfg->dvfs_boost_time.tv_sec;
		task->end_time.tv_nsec =
			submit->end.nsec - cfg->dvfs_boost_time.tv_nsec;
	} else {
		task->end_time.tv_sec =
			submit->end.sec - cfg->dvfs_boost_time.tv_sec - 1;
		task->end_time.tv_nsec =
			1000000000 + submit->end.nsec - cfg->dvfs_boost_time.tv_nsec;
	}
	/* give default time if empty */
	frame_check_end_time(&task->end_time);

	result = frame_buf_to_task_buf(&task->buf.src,
			      &submit->buffer.src,
			      "drm_mml_rdma");
	if (result) {
		mml_err("[drm]%s get src dma buf fail", __func__);
		goto err_buf_exit;
	}

	if (submit->info.dest[0].pq_config.en_region_pq) {
		result = frame_buf_to_task_buf(&task->buf.seg_map,
				      &submit->buffer.seg_map,
				      "drm_mml_rdma_segmap");
		if (result) {
			mml_err("[drm]%s get dma buf fail", __func__);
			goto err_buf_exit;
		}
	}

	task->buf.dest_cnt = submit->buffer.dest_cnt;
	for (i = 0; i < submit->buffer.dest_cnt; i++) {
		result = frame_buf_to_task_buf(&task->buf.dest[i],
				      &submit->buffer.dest[i],
				      "drm_mml_wrot");
		if (result) {
			mml_err("[drm]%s get dest %u dma buf fail", __func__, i);
			goto err_buf_exit;
		}
	}

	/* create fence for this task */
	fence.value = task->job.jobid;
#ifndef MML_FPGA
	if (submit->job && ctx->timeline && submit->info.mode != MML_MODE_RACING &&
		mtk_sync_fence_create(ctx->timeline, &fence) >= 0) {
		task->job.fence = fence.fence;
		task->fence = sync_file_get_fence(task->job.fence);
	} else {
		task->job.fence = -1;
	}
#endif
	mml_msg("[drm]mml job %u fence fd %d task %p fence %p config %p mode %hhu%s act_t %u",
		task->job.jobid, task->job.fence, task, task->fence, cfg,
		cfg->info.mode,
		(cfg->info.mode == MML_MODE_RACING && cfg->disp_dual) ? " disp dual" : "",
		submit->info.act_time);

	/* copy job content back, must do before call submit */
	if (submit->job)
		memcpy(submit->job, &task->job, sizeof(*submit->job));

	/* copy pq parameters */
	for (i = 0; i < submit->buffer.dest_cnt && submit->pq_param[i]; i++)
		memcpy(&task->pq_param[i], submit->pq_param[i], sizeof(task->pq_param[i]));

	/* wake lock */
	mml_lock_wake_lock(task->config->mml, true);

	/* submit to core */
	mml_core_submit_task(cfg, task);

	mml_trace_end();
	return 0;

err_unlock_exit:
	mutex_unlock(&ctx->config_mutex);
err_buf_exit:
	mml_trace_end();
	mml_log("%s fail result %d task %p", __func__, result, task);
	if (task) {
		bool is_init_state = task->state == MML_TASK_INITIAL;

		mutex_lock(&ctx->config_mutex);
		list_del_init(&task->entry);
		cfg->await_task_cnt--;

		if (is_init_state) {
			mml_log("dec config %p and del", cfg);
			list_del_init(&cfg->entry);
			ctx->config_cnt--;
			/* revert racing ref count decrease after done */
			if (cfg->info.mode == MML_MODE_RACING)
				atomic_dec(&ctx->racing_cnt);
		} else
			mml_log("dec config %p", cfg);
		mutex_unlock(&ctx->config_mutex);
		kref_put(&task->ref, task_move_to_destroy);

		if (is_init_state)
			cfg->cfg_ops->put(cfg);
	}
	return result;
}
EXPORT_SYMBOL_GPL(mml_drm_submit);

s32 mml_drm_stop(struct mml_drm_ctx *ctx, struct mml_submit *submit, bool force)
{
	struct mml_frame_config *cfg;

	mml_trace_begin("%s", __func__);
	mutex_lock(&ctx->config_mutex);

	cfg = frame_config_find_reuse(ctx, submit);
	if (!cfg) {
		mml_err("[drm]The submit info not found for stop");
		goto done;
	}

	mml_log("[drm]stop config %p", cfg);
	mml_core_stop_racing(cfg, force);

done:
	mutex_unlock(&ctx->config_mutex);
	mml_trace_end();
	return 0;
}
EXPORT_SYMBOL_GPL(mml_drm_stop);

void mml_drm_config_rdone(struct mml_drm_ctx *ctx, struct mml_submit *submit,
	struct cmdq_pkt *pkt)
{
	struct mml_frame_config *cfg;

	mml_trace_begin("%s", __func__);
	mutex_lock(&ctx->config_mutex);

	cfg = frame_config_find_reuse(ctx, submit);
	if (!cfg) {
		mml_err("[drm]The submit info not found for stop");
		goto done;
	}

	cmdq_pkt_write_value_addr(pkt,
		cfg->path[0]->mmlsys->base_pa +
		mml_sys_get_reg_ready_sel(cfg->path[0]->mmlsys),
		0x24, U32_MAX);

done:
	mutex_unlock(&ctx->config_mutex);
	mml_trace_end();
}
EXPORT_SYMBOL_GPL(mml_drm_config_rdone);

void mml_drm_dump(struct mml_drm_ctx *ctx, struct mml_submit *submit)
{
	mml_log("[drm]dump threads for mml, submit job %u",
		submit->job ? submit->job->jobid : 0);
	mml_dump_thread(ctx->mml);
}
EXPORT_SYMBOL_GPL(mml_drm_dump);

static s32 dup_task(struct mml_task *task, u32 pipe)
{
	struct mml_frame_config *cfg = task->config;
	struct mml_drm_ctx *ctx = task->ctx;
	struct mml_task *src;

	if (unlikely(task->pkts[pipe])) {
		mml_err("[drm]%s task %p pipe %hhu already has pkt before dup",
			__func__, task, pipe);
		return -EINVAL;
	}

	mutex_lock(&ctx->config_mutex);

	mml_msg("[drm]%s task cnt (%u %u %hhu) task %p pipe %u config %p",
		__func__,
		task->config->await_task_cnt,
		task->config->run_task_cnt,
		task->config->done_task_cnt,
		task, pipe, task->config);

	/* check if available done task first */
	src = list_first_entry_or_null(&cfg->done_tasks, struct mml_task,
				       entry);
	if (src && src->pkts[pipe])
		goto dup_command;

	/* check running tasks, have to check it is valid task */
	list_for_each_entry(src, &cfg->tasks, entry) {
		if (!src->pkts[0] || (src->config->dual && !src->pkts[1])) {
			mml_err("[drm]%s error running task %p not able to copy",
				__func__, src);
			continue;
		}
		goto dup_command;
	}

	list_for_each_entry_reverse(src, &cfg->await_tasks, entry) {
		/* the first one should be current task, skip it */
		if (src == task) {
			mml_msg("[drm]%s await task %p pkt %p",
				__func__, src, src->pkts[pipe]);
			continue;
		}
		if (!src->pkts[0] || (src->config->dual && !src->pkts[1])) {
			mml_err("[drm]%s error await task %p not able to copy",
				__func__, src);
			continue;
		}
		goto dup_command;
	}

	/* this config may have issue, do not reuse anymore */
	cfg->err = true;

	mutex_unlock(&ctx->config_mutex);
	return -EBUSY;

dup_command:
	task->pkts[pipe] = cmdq_pkt_create(cfg->path[pipe]->clt);
	cmdq_pkt_copy(task->pkts[pipe], src->pkts[pipe]);
	task->pkts[pipe]->user_data = (void *)task;

	task->reuse[pipe].labels = kcalloc(cfg->cache[pipe].label_cnt,
		sizeof(*task->reuse[pipe].labels), GFP_KERNEL);
	if (task->reuse[pipe].labels) {
		memcpy(task->reuse[pipe].labels, src->reuse[pipe].labels,
			sizeof(*task->reuse[pipe].labels) * cfg->cache[pipe].label_cnt);
		task->reuse[pipe].label_idx = src->reuse[pipe].label_idx;
		cmdq_reuse_refresh(task->pkts[pipe], task->reuse[pipe].labels,
			task->reuse[pipe].label_idx);
	} else {
		mml_err("[drm]copy reuse labels fail");
	}

	mutex_unlock(&ctx->config_mutex);
	return 0;
}

static void task_queue(struct mml_task *task, u32 pipe)
{
	struct mml_drm_ctx *ctx = task->ctx;

	kthread_queue_work(ctx->kt_config[pipe], &task->work_config[pipe]);
}

static struct mml_tile_cache *task_get_tile_cache(struct mml_task *task, u32 pipe)
{
	return &((struct mml_drm_ctx *)task->ctx)->tile_cache[pipe];
}

static void kt_setsched(void *adaptor_ctx)
{
	struct mml_drm_ctx *ctx = adaptor_ctx;
	struct sched_param kt_param = { .sched_priority = MAX_RT_PRIO - 1 };
	int ret[3] = {0};

	if (ctx->kt_priority)
		return;

	ret[0] = sched_setscheduler(ctx->kt_done_task, SCHED_FIFO, &kt_param);
	if (ctx->kt_config[0])
		ret[1] = sched_setscheduler(ctx->kt_config[0]->task, SCHED_FIFO, &kt_param);
	if (ctx->kt_config[1])
		ret[2] = sched_setscheduler(ctx->kt_config[1]->task, SCHED_FIFO, &kt_param);
	mml_log("[adpt]%s set kt done priority %d ret %d %d %d",
		__func__, kt_param.sched_priority, ret[0], ret[1], ret[2]);
	ctx->kt_priority = true;
}

static void task_ddren(struct mml_task *task, struct cmdq_pkt *pkt, bool enable)
{
	struct mml_drm_ctx *ctx = task->ctx;

	if (!ctx->ddren_cb)
		return;

	/* no need ddren for srt case */
	if (task->config->info.mode == MML_MODE_MML_DECOUPLE)
		return;

	ctx->ddren_cb(pkt, enable, ctx->ddren_param);
}

static void task_dispen(struct mml_task *task, bool enable)
{
	struct mml_drm_ctx *ctx = task->ctx;

	if (!ctx->dispen_cb)
		return;

	/* no need ddren so no dispen */
	if (task->config->info.mode == MML_MODE_MML_DECOUPLE)
		return;

	ctx->dispen_cb(enable, ctx->dispen_param);
}

static const struct mml_task_ops drm_task_ops = {
	.queue = task_queue,
	.submit_done = task_submit_done,
	.frame_done = task_frame_done,
	.dup_task = dup_task,
	.get_tile_cache = task_get_tile_cache,
	.kt_setsched = kt_setsched,
	.ddren = task_ddren,
	.dispen = task_dispen,
};

static void config_get(struct mml_frame_config *cfg)
{
	kref_get(&cfg->ref);
}

static void config_put(struct mml_frame_config *cfg)
{
	kref_put(&cfg->ref, frame_config_free);
}

static const struct mml_config_ops drm_config_ops = {
	.get = config_get,
	.put = config_put,
};

int mml_ctx_init(struct mml_drm_ctx *ctx, const char * const threads[])
{
	/* create taskdone kthread first cause it is more easy for fail case */
	ctx->kt_done = kthread_create_worker(0, "%s", threads[0]);
	if (IS_ERR(ctx->kt_done)) {
		mml_err("[adpt]fail to create kthread worker %d",
			(s32)PTR_ERR(ctx->kt_done));
		ctx->kt_done = NULL;
		goto err;

	}
	ctx->kt_done_task = ctx->kt_done->task;
	ctx->wq_destroy = alloc_ordered_workqueue("%s", 0, threads[1]);
	if (threads[2]) {
		ctx->kt_config[0] = kthread_create_worker(0, "%s", threads[2]);
		if (IS_ERR(ctx->kt_config[0])) {
			mml_err("[adpt]fail to create config thread 0 %s err %pe",
				threads[2], ctx->kt_config[0]);
			ctx->kt_config[0] = NULL;
			goto err;
		}
	}
	if (threads[3]) {
		ctx->kt_config[1] = kthread_create_worker(0, "%s", threads[3]);
		if (IS_ERR(ctx->kt_config[1])) {
			mml_err("[adpt]fail to create config thread 1 %s err %pe",
				threads[3], ctx->kt_config[1]);
			ctx->kt_config[1] = NULL;
			goto err;
		}
	}

	INIT_LIST_HEAD(&ctx->configs);
	mutex_init(&ctx->config_mutex);
	return 0;

err:
	if (ctx->kt_done) {
		kthread_destroy_worker(ctx->kt_done);
		ctx->kt_done = NULL;
	}
	if (ctx->wq_destroy) {
		destroy_workqueue(ctx->wq_destroy);
		ctx->wq_destroy = NULL;
	}
	if (ctx->kt_config[0]) {
		kthread_destroy_worker(ctx->kt_config[0]);
		ctx->kt_config[0] = NULL;
	}
	if (ctx->kt_config[1]) {
		kthread_destroy_worker(ctx->kt_config[1]);
		ctx->kt_config[1] = NULL;
	}
	return -EIO;
}

static struct mml_drm_ctx *drm_ctx_create(struct mml_dev *mml,
					  struct mml_drm_param *disp)
{
	static const char * const threads[] = {
		"mml_drm_done", "mml_destroy",
		"mml_work0", "mml_work1",
	};
	struct mml_drm_ctx *ctx;
	int ret;

	mml_msg("[drm]%s on dev %p", __func__, mml);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ret = mml_ctx_init(ctx, threads);
	if (ret) {
		kfree(ctx);
		return ERR_PTR(ret);
	}

	ctx->mml = mml;
	ctx->task_ops = &drm_task_ops;
	ctx->cfg_ops = &drm_config_ops;
	ctx->disp_dual = disp->dual;
	ctx->disp_vdo = disp->vdo_mode;
	ctx->submit_cb = disp->submit_cb;
	ctx->ddren_cb = disp->ddren_cb;
	ctx->ddren_param = disp->ddren_param;
	ctx->dispen_cb = disp->dispen_cb;
	ctx->dispen_param = disp->dispen_param;
	ctx->panel_pixel = MML_DEFAULT_PANEL_PX;

#ifndef MML_FPGA
	ctx->timeline = mtk_sync_timeline_create("mml_timeline");
#endif
	if (!ctx->timeline)
		mml_err("[drm]fail to create timeline");
	else
		mml_msg("[drm]timeline for mml %p", ctx->timeline);

	/* return info to display */
	disp->racing_height = mml_sram_get_racing_height(mml);

	return ctx;
}

struct mml_drm_ctx *mml_drm_get_context(struct platform_device *pdev,
					struct mml_drm_param *disp)
{
	struct mml_dev *mml = platform_get_drvdata(pdev);

	mml_msg("[drm]%s", __func__);
	if (!mml) {
		mml_err("[drm]%s not init mml", __func__);
		return ERR_PTR(-EPERM);
	}
	return mml_dev_get_drm_ctx(mml, disp, drm_ctx_create);
}
EXPORT_SYMBOL_GPL(mml_drm_get_context);

bool mml_drm_ctx_idle(struct mml_drm_ctx *ctx)
{
	bool idle = false;

	struct mml_frame_config *cfg;

	mutex_lock(&ctx->config_mutex);
	list_for_each_entry(cfg, &ctx->configs, entry) {
		if (!list_empty(&cfg->await_tasks)) {
			mml_log("%s await_tasks not empty", __func__);
			goto done;
		}

		if (!list_empty(&cfg->tasks)) {
			mml_log("%s tasks not empty", __func__);
			goto done;
		}
	}

	idle = true;
done:
	mutex_unlock(&ctx->config_mutex);
	return idle;
}
EXPORT_SYMBOL_GPL(mml_drm_ctx_idle);

static void drm_ctx_release(struct mml_drm_ctx *ctx)
{
	struct mml_frame_config *cfg, *tmp;
	u32 i, j;
	struct list_head local_list;

	mml_msg("[drm]%s on ctx %p", __func__, ctx);

	INIT_LIST_HEAD(&local_list);

	/* clone list_head first to aviod circular lock */
	mutex_lock(&ctx->config_mutex);
	list_splice_tail_init(&ctx->configs, &local_list);
	mutex_unlock(&ctx->config_mutex);

	list_for_each_entry_safe_reverse(cfg, tmp, &local_list, entry) {
		/* check and remove configs/tasks in this context */
		list_del_init(&cfg->entry);
		frame_config_queue_destroy(cfg);
	}

	destroy_workqueue(ctx->wq_destroy);
	kthread_destroy_worker(ctx->kt_config[0]);
	kthread_destroy_worker(ctx->kt_config[1]);
	kthread_destroy_worker(ctx->kt_done);
#ifndef MML_FPGA
	mtk_sync_timeline_destroy(ctx->timeline);
#endif
	for (i = 0; i < ARRAY_SIZE(ctx->tile_cache); i++) {
		for (j = 0; j < ARRAY_SIZE(ctx->tile_cache[i].func_list); j++)
			kfree(ctx->tile_cache[i].func_list[j]);
		if (ctx->tile_cache[i].tiles)
			vfree(ctx->tile_cache[i].tiles);
	}
	kfree(ctx);
}

void mml_drm_put_context(struct mml_drm_ctx *ctx)
{
	if (IS_ERR_OR_NULL(ctx))
		return;
	mml_log("[drm]%s", __func__);
	mml_sys_put_dle_ctx(ctx->mml);
	mml_dev_put_drm_ctx(ctx->mml, drm_ctx_release);
}
EXPORT_SYMBOL_GPL(mml_drm_put_context);

void mml_drm_set_panel_pixel(struct mml_drm_ctx *ctx, u32 pixel)
{
	struct mml_frame_config *cfg;

	ctx->panel_pixel = pixel;
	mutex_lock(&ctx->config_mutex);
	list_for_each_entry(cfg, &ctx->configs, entry) {
		/* calculate hrt base on new pixel count */
		cfg->disp_hrt = frame_calc_layer_hrt(ctx, &cfg->info,
			cfg->layer_w, cfg->layer_h);
	}
	mutex_unlock(&ctx->config_mutex);
}
EXPORT_SYMBOL_GPL(mml_drm_set_panel_pixel);

s32 mml_drm_racing_config_sync(struct mml_drm_ctx *ctx, struct cmdq_pkt *pkt)
{
	struct cmdq_operand lhs, rhs;
	u16 event_target = mml_ir_get_target_event(ctx->mml);

	mml_msg("[drm]%s for disp", __func__);

	/* debug current task idx */
	cmdq_pkt_assign_command(pkt, CMDQ_THR_SPR_IDX3,
		atomic_read(&ctx->job_serial));

	if (ctx->disp_vdo && event_target) {
		/* non-racing to racing case, force clear target line
		 * to make sure next racing begin from target line to sof
		 */
		if (ctx->racing_begin) {
			cmdq_pkt_clear_event(pkt, event_target);
			ctx->racing_begin = false;
			mml_mmp(racing_enter, MMPROFILE_FLAG_PULSE,
				atomic_read(&ctx->job_serial), 0);
		}
		cmdq_pkt_wait_no_clear(pkt, event_target);
	}

	/* set NEXT bit on, to let mml know should jump next */
	lhs.reg = true;
	lhs.idx = MML_CMDQ_NEXT_SPR;
	rhs.reg = false;
	rhs.value = MML_NEXTSPR_NEXT;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_OR, MML_CMDQ_NEXT_SPR, &lhs, &rhs);

	cmdq_pkt_set_event(pkt, mml_ir_get_disp_ready_event(ctx->mml));
	cmdq_pkt_wfe(pkt, mml_ir_get_mml_ready_event(ctx->mml));

	/* clear next bit since disp with new mml now */
	rhs.value = ~(u16)MML_NEXTSPR_NEXT;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_AND, MML_CMDQ_NEXT_SPR, &lhs, &rhs);

	return 0;
}
EXPORT_SYMBOL_GPL(mml_drm_racing_config_sync);

s32 mml_drm_racing_stop_sync(struct mml_drm_ctx *ctx, struct cmdq_pkt *pkt)
{
	struct cmdq_operand lhs, rhs;

	/* debug current task idx */
	cmdq_pkt_assign_command(pkt, CMDQ_THR_SPR_IDX3,
		atomic_read(&ctx->job_serial));

	/* set NEXT bit on, to let mml know should jump next */
	lhs.reg = true;
	lhs.idx = MML_CMDQ_NEXT_SPR;
	rhs.reg = false;
	rhs.value = MML_NEXTSPR_NEXT;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_OR, MML_CMDQ_NEXT_SPR, &lhs, &rhs);

	cmdq_pkt_wait_no_clear(pkt, mml_ir_get_mml_stop_event(ctx->mml));

	/* clear next bit since disp with new mml now */
	rhs.value = ~(u16)MML_NEXTSPR_NEXT;
	cmdq_pkt_logic_command(pkt, CMDQ_LOGIC_AND, MML_CMDQ_NEXT_SPR, &lhs, &rhs);

	return 0;
}
EXPORT_SYMBOL_GPL(mml_drm_racing_stop_sync);

enum split_side {
	rect_left,
	rect_top,
	rect_right,
	rect_bottom
};

static void mml_check_boundary_w(struct mml_frame_data *src,
	struct mml_frame_dest *dest, u8 *lrtb)
{
	if (!(dest->crop.r.width & 1))
		return;

	dest->crop.r.width += 1;
	if (dest->crop.r.left + dest->crop.r.width > src->width) {
		dest->crop.r.left -= 1;
		lrtb[rect_left] = 1;
	} else {
		lrtb[rect_right] = 1;
	}
}

static void mml_check_boundary_h(struct mml_frame_data *src,
	struct mml_frame_dest *dest, u8 *lrtb)
{
	if (!(dest->crop.r.height & 1))
		return;

	dest->crop.r.height += 1;
	if (dest->crop.r.top + dest->crop.r.height > src->height) {
		dest->crop.r.top -= 1;
		lrtb[rect_top] = 1;
	} else {
		lrtb[rect_bottom] = 1;
	}
}

void mml_drm_split_info(struct mml_submit *submit, struct mml_submit *submit_pq)
{
	struct mml_frame_info *info = &submit->info;
	struct mml_frame_info *info_pq = &submit_pq->info;
	struct mml_frame_dest *dest = &info->dest[0];
	u8 lrtb[4] = {0};
	u32 i;

	/* display layer pixel */
	if (!submit->layer.width || !submit->layer.height) {
		submit->layer.width = dest->compose.width;
		submit->layer.height = dest->compose.height;
	}

	submit_pq->info = submit->info;
	submit_pq->buffer = submit->buffer;
	if (submit_pq->job && submit->job)
		*submit_pq->job = *submit->job;
	for (i = 0; i < MML_MAX_OUTPUTS; i++)
		if (submit_pq->pq_param[i] && submit->pq_param[i])
			*submit_pq->pq_param[i] = *submit->pq_param[i];

	if (dest->rotate == MML_ROT_0 ||
	    dest->rotate == MML_ROT_180) {
		dest->compose.left = 0;
		dest->compose.top = 0;
		dest->compose.width = dest->crop.r.width;
		dest->compose.height = dest->crop.r.height;

		if (MML_FMT_H_SUBSAMPLE(dest->data.format))
			mml_check_boundary_w(&info->src, dest, lrtb);
		if (MML_FMT_V_SUBSAMPLE(dest->data.format))
			mml_check_boundary_h(&info->src, dest, lrtb);

		dest->data.width = dest->crop.r.width;
		dest->data.height = dest->crop.r.height;
	} else {
		dest->compose.left = 0;
		dest->compose.top = 0;
		dest->compose.width = dest->crop.r.height; /* even or odd */
		dest->compose.height = dest->crop.r.width;

		if (MML_FMT_H_SUBSAMPLE(dest->data.format)) {
			mml_check_boundary_w(&info->src, dest, lrtb); /* (wrot) align to even */
			mml_check_boundary_h(&info->src, dest, lrtb);
		} else if (MML_FMT_V_SUBSAMPLE(dest->data.format)) {
			mml_check_boundary_w(&info->src, dest, lrtb);
		}

		dest->data.width = dest->crop.r.height; /* even */
		dest->data.height = dest->crop.r.width;
	}

	/* translate padding side to pq crop left/top */
	if (dest->flip) {
		switch (dest->rotate) {
		case MML_ROT_0:
			info_pq->dest[0].crop.r.left = lrtb[rect_right];
			info_pq->dest[0].crop.r.top = lrtb[rect_top];
			break;
		case MML_ROT_90:
			info_pq->dest[0].crop.r.left = lrtb[rect_top];
			info_pq->dest[0].crop.r.top = lrtb[rect_left];
			break;
		case MML_ROT_180:
			info_pq->dest[0].crop.r.left = lrtb[rect_left];
			info_pq->dest[0].crop.r.top = lrtb[rect_bottom];
			break;
		case MML_ROT_270:
			info_pq->dest[0].crop.r.left = lrtb[rect_bottom];
			info_pq->dest[0].crop.r.top = lrtb[rect_right];
			break;
		default:
			info_pq->dest[0].crop.r.left = 0;
			info_pq->dest[0].crop.r.top = 0;
			break;
		}
	} else {
		switch (dest->rotate) {
		case MML_ROT_0:
			info_pq->dest[0].crop.r.left = lrtb[rect_left];
			info_pq->dest[0].crop.r.top = lrtb[rect_top];
			break;
		case MML_ROT_90:
			info_pq->dest[0].crop.r.left = lrtb[rect_bottom];
			info_pq->dest[0].crop.r.top = lrtb[rect_left];
			break;
		case MML_ROT_180:
			info_pq->dest[0].crop.r.left = lrtb[rect_right];
			info_pq->dest[0].crop.r.top = lrtb[rect_bottom];
			break;
		case MML_ROT_270:
			info_pq->dest[0].crop.r.left = lrtb[rect_top];
			info_pq->dest[0].crop.r.top = lrtb[rect_right];
			break;
		default:
			info_pq->dest[0].crop.r.left = 0;
			info_pq->dest[0].crop.r.top = 0;
			break;
		}
	}

	dest->data.y_stride = mml_color_get_min_y_stride(
		dest->data.format, dest->data.width);
	dest->data.uv_stride = mml_color_get_min_uv_stride(
		dest->data.format, dest->data.width);

	info_pq->src = dest->data;
	/* for better wrot burst 16 bytes performance,
	 * always align output width to 16 pixel
	 */
	if (dest->data.y_stride & 0xf &&
		(dest->rotate == MML_ROT_90 || dest->rotate == MML_ROT_270)) {
		u32 align_w = align_up(dest->data.width, 16);

		dest->data.y_stride = mml_color_get_min_y_stride(
			dest->data.format, align_w);
		dest->compose.left = align_w - dest->data.width;
		/* same as
		 * dest->compose.left + dest->compose.width + info_pq->dest[0].crop.r.left
		 */
		info_pq->src.width = align_w;
		info_pq->src.y_stride = dest->data.y_stride;
	}

	memset(&dest->pq_config, 0, sizeof(dest->pq_config));

	info_pq->dest[0].crop.r.left += dest->compose.left;
	info_pq->dest[0].crop.r.width = dest->compose.width;
	info_pq->dest[0].crop.r.height = dest->compose.height;
	info_pq->dest[0].rotate = 0;
	info_pq->dest[0].flip = 0;
	info_pq->mode = MML_MODE_DDP_ADDON;
	submit_pq->buffer.src = submit->buffer.dest[0];
	submit_pq->buffer.src.cnt = 0;
	submit_pq->buffer.dest[0].cnt = 0;

	if (MML_FMT_PLANE(dest->data.format) > 1)
		mml_err("%s dest plane should be 1 but format %#010x",
			__func__, dest->data.format);
}
EXPORT_SYMBOL_GPL(mml_drm_split_info);

