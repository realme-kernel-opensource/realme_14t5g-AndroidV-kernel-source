// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 *         Tiffany Lin <tiffany.lin@mediatek.com>
 */

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <soc/mediatek/mmdvfs_v3.h>

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_dec.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"
#include "mtk_vcodec_dec_pm.h"
#include "mtk_vcodec_dec_pm_plat.h"
#include "vdec_drv_if.h"

#define MTK_VDEC_MIN_W  64U
#define MTK_VDEC_MIN_H  64U
#define DFT_CFG_WIDTH   MTK_VDEC_MIN_W
#define DFT_CFG_HEIGHT  MTK_VDEC_MIN_H
#define MTK_VDEC_MAX_W  mtk_vdec_max_width
#define MTK_VDEC_MAX_H  mtk_vdec_max_heigh
#define MTK_VDEC_4K_WH  (4096 * 2176)

static unsigned int mtk_vdec_max_width;
static unsigned int mtk_vdec_max_heigh;
struct mtk_video_fmt
	mtk_vdec_formats[MTK_MAX_DEC_CODECS_SUPPORT] = { {0} };
struct mtk_codec_framesizes
	mtk_vdec_framesizes[MTK_MAX_DEC_CODECS_SUPPORT] = { {0} };
static unsigned int default_out_fmt_idx;
static unsigned int default_cap_fmt_idx;

#define NUM_SUPPORTED_FRAMESIZE ARRAY_SIZE(mtk_vdec_framesizes)
#define NUM_FORMATS ARRAY_SIZE(mtk_vdec_formats)
static struct vb2_mem_ops vdec_dma_contig_memops;

static bool mtk_vdec_is_vcu(void)
{
	if (VCU_FPTR(vcu_get_plat_device)) {
		if (mtk_vcodec_vcp & (1 << MTK_INST_DECODER))
			return false;
		else
			return true;
	}
	return false;
}

static inline long long timeval_to_ns(const struct __kernel_v4l2_timeval *tv)
{
	return ((long long) tv->tv_sec * NSEC_PER_SEC) +
		tv->tv_usec * NSEC_PER_USEC;
}

void mtk_vdec_do_gettimeofday(struct timespec64 *tv)
{
	struct timespec64 now;

	ktime_get_real_ts64(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_nsec = now.tv_nsec; // micro sec = ((long)(now.tv_nsec)/1000);
}

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
static void set_vdec_vcp_data(struct mtk_vcodec_ctx *ctx, enum vcp_reserve_mem_id_t id)
{
	char tmp_buf[1024] = "";

	if (id == VDEC_SET_PROP_MEM_ID) {

		sprintf(tmp_buf, "%s", mtk_vdec_property);

		mtk_v4l2_debug(3, "[%d] mtk_vdec_property %s", ctx->id, tmp_buf);
		mtk_v4l2_debug(3, "[%d] mtk_vdec_property_prev %s",
					ctx->id, mtk_vdec_property_prev);

		// set vcp property every time
		if (/* strcmp(mtk_vdec_property_prev, tmp_buf) != 0 &&  */
			strcmp(tmp_buf, "") != 0) {
			if (vdec_if_set_param(ctx,
				SET_PARAM_VDEC_PROPERTY,
				tmp_buf)  != 0) {
				mtk_v4l2_err("Error!! Cannot set vdec property");
				return;
			}
			strcpy(mtk_vdec_property_prev, tmp_buf);
		}
	} else if (id == VDEC_VCP_LOG_INFO_ID) {

		sprintf(tmp_buf, "%s", mtk_vdec_vcp_log);

		mtk_v4l2_debug(3, "[%d] mtk_vdec_vcp_log %s", ctx->id, tmp_buf);
		mtk_v4l2_debug(3, "[%d] mtk_vdec_vcp_log_prev %s", ctx->id, mtk_vdec_vcp_log_prev);

		// set vcp log every time
		if (/* strcmp(mtk_vdec_vcp_log_prev, tmp_buf) != 0 &&  */
			strcmp(tmp_buf, "") != 0) {
			if (vdec_if_set_param(ctx,
				SET_PARAM_VDEC_VCP_LOG_INFO,
				tmp_buf)  != 0) {
				mtk_v4l2_err("Error!! Cannot set vdec vcp log info");
				return;
			}
			strcpy(mtk_vdec_vcp_log_prev, tmp_buf);
		}
	} else {
		mtk_v4l2_err("[%d] id not support %d", ctx->id, id);
	}
}
#endif

static void set_vcu_vpud_log(struct mtk_vcodec_ctx *ctx, void *in)
{
	if (!mtk_vdec_is_vcu()) {
		mtk_v4l2_err("only support on vcu dec path");
		return;
	}

	vdec_if_set_param(ctx, SET_PARAM_VDEC_VCU_VPUD_LOG, in);
}

static void get_vcu_vpud_log(struct mtk_vcodec_ctx *ctx, void *out)
{
	if (!mtk_vdec_is_vcu()) {
		mtk_v4l2_err("only support on vcu dec path");
		return;
	}

	vdec_if_get_param(ctx, GET_PARAM_VDEC_VCU_VPUD_LOG, out);
}

static void get_supported_format(struct mtk_vcodec_ctx *ctx)
{
	unsigned int i;

	if (mtk_vdec_formats[0].fourcc == 0) {
		if (vdec_if_get_param(ctx,
			GET_PARAM_VDEC_CAP_SUPPORTED_FORMATS,
			&mtk_vdec_formats)  != 0) {
			mtk_v4l2_err("Error!! Cannot get supported format");
			return;
		}

		for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
			if (mtk_vdec_formats[i].fourcc != 0)
				mtk_v4l2_debug(1,
				  "fmt[%d] fourcc %d type %d planes %d",
				  i, mtk_vdec_formats[i].fourcc,
				  mtk_vdec_formats[i].type,
				  mtk_vdec_formats[i].num_planes);
		}
		for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
			if (mtk_vdec_formats[i].fourcc != 0 &&
				mtk_vdec_formats[i].type == MTK_FMT_DEC) {
				default_out_fmt_idx = i;
				break;
			}
		}
		for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
			if (mtk_vdec_formats[i].fourcc != 0 &&
				mtk_vdec_formats[i].type == MTK_FMT_FRAME) {
				default_cap_fmt_idx = i;
				break;
			}
		}
	}
}

static void get_supported_framesizes(struct mtk_vcodec_ctx *ctx)
{
	unsigned int i;

	if (mtk_vdec_framesizes[0].fourcc == 0) {
		if (vdec_if_get_param(ctx, GET_PARAM_VDEC_CAP_FRAME_SIZES,
			&mtk_vdec_framesizes) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get frame size",
				ctx->id);
			return;
		}

		for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT; i++) {
			if (mtk_vdec_framesizes[i].fourcc != 0) {
				mtk_v4l2_debug(1,
				"vdec_fs[%d] fourcc %d s %d %d %d %d %d %d\n",
				i, mtk_vdec_framesizes[i].fourcc,
				mtk_vdec_framesizes[i].stepwise.min_width,
				mtk_vdec_framesizes[i].stepwise.max_width,
				mtk_vdec_framesizes[i].stepwise.step_width,
				mtk_vdec_framesizes[i].stepwise.min_height,
				mtk_vdec_framesizes[i].stepwise.max_height,
				mtk_vdec_framesizes[i].stepwise.step_height);
				if (mtk_vdec_framesizes[i].stepwise.max_width > mtk_vdec_max_width)
					mtk_vdec_max_width =
						mtk_vdec_framesizes[i].stepwise.max_width;
				if (mtk_vdec_framesizes[i].stepwise.max_height > mtk_vdec_max_heigh)
					mtk_vdec_max_heigh =
						mtk_vdec_framesizes[i].stepwise.max_height;
			}
		}
	}
}

static void update_src_cnt(struct mtk_vcodec_ctx *ctx)
{
	if (ctx->src_cnt != NULL)
		(*ctx->src_cnt) = v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx);
}

static void update_dst_cnt(struct mtk_vcodec_ctx *ctx)
{
	if (ctx->dst_cnt != NULL)
		(*ctx->dst_cnt) = v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx);
}

static int mtk_vdec_get_align_limit(struct mtk_vcodec_ctx *ctx)
{
	if (mtk_vdec_align_limit > 0)
		return mtk_vdec_align_limit;
	if (ctx->dec_params.svp_mode || ctx->picinfo.buf_w * ctx->picinfo.buf_h > MTK_VDEC_4K_WH)
		return MIN(ctx->dpb_size - 6, 2);
	return MIN(ctx->dpb_size - 6, 4);
}

static struct mtk_video_fmt *mtk_vdec_find_format(struct mtk_vcodec_ctx *ctx,
	struct v4l2_format *f, unsigned int t)
{
	struct mtk_video_fmt *fmt;
	unsigned int k;

	mtk_v4l2_debug(3, "[%d] fourcc %d", ctx->id, f->fmt.pix_mp.pixelformat);
	for (k = 0; k < MTK_MAX_DEC_CODECS_SUPPORT &&
		 mtk_vdec_formats[k].fourcc != 0; k++) {
		fmt = &mtk_vdec_formats[k];
		if (fmt->fourcc == f->fmt.pix_mp.pixelformat &&
			mtk_vdec_formats[k].type == t)
			return fmt;
	}

	return NULL;
}

static struct mtk_video_fmt *mtk_find_fmt_by_pixel(unsigned int pixelformat)
{
	struct mtk_video_fmt *fmt;
	unsigned int k;

	for (k = 0; k < NUM_FORMATS; k++) {
		fmt = &mtk_vdec_formats[k];
		if (fmt->fourcc == pixelformat)
			return fmt;
	}
	mtk_v4l2_err("Error!! Cannot find fourcc: %d use default", pixelformat);

	return &mtk_vdec_formats[default_out_fmt_idx];
}

static struct mtk_q_data *mtk_vdec_get_q_data(struct mtk_vcodec_ctx *ctx,
	enum v4l2_buf_type type)
{
	if (ctx == NULL)
		return NULL;

	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->q_data[MTK_Q_DATA_SRC];

	return &ctx->q_data[MTK_Q_DATA_DST];
}

static bool mtk_vdec_is_ts_valid(u64 timestamp)
{
	struct timespec64 ts;
	u64 timestamp_us;

	ts = ns_to_timespec64(timestamp);
	timestamp_us = ts.tv_sec * USEC_PER_SEC + ts.tv_nsec / NSEC_PER_USEC;

	return timestamp_us != MTK_INVALID_TIMESTAMP;
}

static void mtk_vdec_ts_reset(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_detect_ts_param *param = &ctx->detect_ts_param;

	mutex_lock(&param->lock);
	if (ctx->dec_params.enable_detect_ts)
		param->mode = MTK_TS_MODE_DETECTING;
	else
		param->mode = MTK_TS_MODE_PTS;
	param->read_idx = 0;
	param->recorded_size = 0;
	param->first_disp_ts = MTK_INVALID_TIMESTAMP;
	mtk_v4l2_debug(2, "[%d] reset to mode %d", ctx->id, param->mode);
	mutex_unlock(&param->lock);
}

static void mtk_vdec_ts_insert(struct mtk_vcodec_ctx *ctx, u64 timestamp)
{
	struct mtk_detect_ts_param *param = &ctx->detect_ts_param;
	int write_idx;

	mutex_lock(&param->lock);
	if (param->mode == MTK_TS_MODE_PTS) {
		mutex_unlock(&param->lock);
		return;
	}

	if (param->recorded_size) {
		int last_write_idx;
		u64 last_timestamp;

		last_write_idx = (param->read_idx + param->recorded_size - 1) % VB2_MAX_FRAME;
		last_timestamp = param->record[last_write_idx];
		if (last_timestamp >= timestamp) {
			mtk_v4l2_debug(2, "[%d] not increasing ts %lld -> %lld, choose pts mode",
				ctx->id, last_timestamp, timestamp);
			param->mode = MTK_TS_MODE_PTS;
			mutex_unlock(&param->lock);
			return;
		}
	}

	write_idx = (param->read_idx + param->recorded_size) % VB2_MAX_FRAME;
	param->record[write_idx] = timestamp;
	param->recorded_size++;
	mtk_v4l2_debug(2, "record ts %lld at index %d size %d",
		timestamp, write_idx, param->recorded_size);
	if (param->recorded_size > VB2_MAX_FRAME) {
		mtk_v4l2_err("[%d] ts record size is too large", ctx->id);
		param->recorded_size = VB2_MAX_FRAME;
	}
	mutex_unlock(&param->lock);
}

static void mtk_vdec_ts_remove_last(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_detect_ts_param *param = &ctx->detect_ts_param;
	int remove_idx;

	mutex_lock(&param->lock);
	if (param->mode == MTK_TS_MODE_PTS) {
		mutex_unlock(&param->lock);
		return;
	}

	// in case bitstream is skipped before the first src_chg
	if (param->recorded_size == 0) {
		mtk_v4l2_debug(2, "[%d] skip due to ts record size is 0", ctx->id);
		mutex_unlock(&param->lock);
		return;
	}

	remove_idx = (param->read_idx + param->recorded_size - 1) % VB2_MAX_FRAME;
	mtk_v4l2_debug(2, "remove last ts %lld at index %d size %d",
		param->record[remove_idx], remove_idx, param->recorded_size);
	param->recorded_size--;
	mutex_unlock(&param->lock);
}

static u64 mtk_vdec_ts_update_mode_and_timestamp(struct mtk_vcodec_ctx *ctx, u64 pts)
{
	struct mtk_detect_ts_param *param = &ctx->detect_ts_param;
	u64 dts, timestamp;

	mutex_lock(&param->lock);

	if (param->mode == MTK_TS_MODE_PTS) {
		mutex_unlock(&param->lock);
		return pts;
	}

	/* DTV case, we may have invalid output timestamp even if all valid input timestamp */
	if (!mtk_vdec_is_ts_valid(pts)) {
		mtk_v4l2_debug(2, "[%d] got invalid pts, choose pts mode", ctx->id);
		param->mode = MTK_TS_MODE_PTS;
		mutex_unlock(&param->lock);
		return pts;
	}

	if (param->recorded_size == 0) {
		mtk_v4l2_err("[%d] ts record size is 0", ctx->id);
		mutex_unlock(&param->lock);
		return pts;
	}

	dts = param->record[param->read_idx];
	param->read_idx++;
	if (param->read_idx == VB2_MAX_FRAME)
		param->read_idx = 0;

	param->recorded_size--;

	do {
		if (param->mode != MTK_TS_MODE_DETECTING)
			break;

		if (!mtk_vdec_is_ts_valid(param->first_disp_ts)) {
			WARN_ON(!mtk_vdec_is_ts_valid(pts));
			param->first_disp_ts = pts;
			/* for DTS mode, there should be no reorder, so the first disp frame
			 * must be the first decode frame which means pts == dts
			 */
			if (pts != dts) {
				param->mode = MTK_TS_MODE_PTS;
				mtk_v4l2_debug(2, "[%d] first pts %lld != dts %lld. choose pts mode",
					ctx->id, pts, dts);
				break;
			}
		}

		if (pts < dts) {
			param->mode = MTK_TS_MODE_PTS;
			mtk_v4l2_debug(2, "[%d] pts %lld < dts %lld. choose pts mode",
				ctx->id, pts, dts);
		} else if (dts < pts) {
			param->mode = MTK_TS_MODE_DTS;
			mtk_v4l2_debug(2, "[%d] dts %lld < pts %lld. choose dts mode",
				ctx->id, dts, pts);
		}
	} while (0);

	if (param->mode == MTK_TS_MODE_DTS) {
		timestamp = dts;
		mtk_v4l2_debug(2, "use dts %lld instead of pts %lld", dts, pts);
	} else
		timestamp = pts;

	mutex_unlock(&param->lock);

	return timestamp;
}

static void mtk_vdec_check_vcp_active(struct mtk_vcodec_ctx *ctx, bool from_timer, bool add_cnt,
	char *debug_str)
{
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
#if VDEC_VCP_BACKGROUND_IDLE
	if (mtk_vcodec_vcp & (1 << MTK_INST_DECODER)) {
		if (!from_timer)
			mutex_lock(&ctx->vcp_active_mutex);
		if (!ctx->is_vcp_active) {
			if (!debug_str)
				debug_str = "unknown";
			mtk_v4l2_debug(0, "[%d] vcp active by %s (vcp_action_cnt %d, last %d, %d)",
				ctx->id, debug_str, ctx->vcp_action_cnt,
				ctx->last_vcp_action_cnt, add_cnt);
			ctx->is_vcp_active = true;
			vcp_register_feature(VDEC_FEATURE_ID);
			mtk_mmdvfs_enable_vcp(true, VCP_PWR_USR_VDEC);
		}
		if (add_cnt)
			ctx->vcp_action_cnt++;
		if (!from_timer)
			mutex_unlock(&ctx->vcp_active_mutex);
	}
#endif
#endif
}

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
#if VDEC_VCP_BACKGROUND_IDLE
static void mtk_vdec_check_vcp_inactive(struct mtk_vcodec_ctx *ctx, bool from_timer)
{
	if (mtk_vcodec_vcp & (1 << MTK_INST_DECODER)) {
		if (!from_timer)
			mutex_lock(&ctx->vcp_active_mutex);
		if (ctx->is_vcp_active) {
			mtk_v4l2_debug(0, "[%d] vcp inactive (vcp_action_cnt %d)",
				ctx->id, ctx->vcp_action_cnt);
			if (from_timer)
				mutex_unlock(&ctx->dev->ctx_mutex);
			vcp_deregister_feature(VDEC_FEATURE_ID);
			mtk_mmdvfs_enable_vcp(false, VCP_PWR_USR_VDEC);
			if (from_timer)
				mutex_lock(&ctx->dev->ctx_mutex);
			ctx->is_vcp_active = false;
		}
		if (!from_timer)
			mutex_unlock(&ctx->vcp_active_mutex);
	}
}
#endif
#endif

static int mtk_vdec_set_frame(struct mtk_vcodec_ctx *ctx,
	struct mtk_video_dec_buf *buf)
{
	int ret = 0;

	if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM) {
		mtk_vdec_check_vcp_active(ctx, false, true, "put frm");
		ret = vdec_if_set_param(ctx, SET_PARAM_FRAME_BUFFER, buf);
		if (ret == -EIO) {
			mtk_vdec_error_handle(ctx, "set_frame");
		}
	}

	return ret;
}

static void mtk_vdec_set_frame_handler(struct work_struct *ws)
{
	struct vdec_set_frame_work_struct *sws;
	struct mtk_vcodec_ctx *ctx;
	struct mtk_video_dec_buf *buf;
	struct vb2_v4l2_buffer *dst_vb2_v4l2;

	sws = container_of(ws, struct vdec_set_frame_work_struct, work);
	ctx = sws->ctx;

	if (ctx->input_driven != INPUT_DRIVEN_PUT_FRM || ctx->is_flushing == true ||
	    ctx->state < MTK_STATE_HEADER || ctx->state >= MTK_STATE_ABORT)
		return;

	dst_vb2_v4l2 = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	if (dst_vb2_v4l2 != NULL) {
		buf = container_of(dst_vb2_v4l2, struct mtk_video_dec_buf, vb);
		mtk_vdec_set_frame(ctx, buf);
	}
}

static void mtk_vdec_init_set_frame_wq(struct mtk_vcodec_ctx *ctx)
{
	ctx->vdec_set_frame_wq = create_singlethread_workqueue("vdec_set_frame");
	INIT_WORK(&ctx->vdec_set_frame_work.work, mtk_vdec_set_frame_handler);
	ctx->vdec_set_frame_work.ctx = ctx;
}

static void mtk_vdec_flush_set_frame_wq(struct mtk_vcodec_ctx *ctx)
{
	if (ctx->vdec_set_frame_wq != NULL)
		flush_workqueue(ctx->vdec_set_frame_wq);
}

static void mtk_vdec_deinit_set_frame_wq(struct mtk_vcodec_ctx *ctx)
{
	if (ctx->vdec_set_frame_wq != NULL)
		destroy_workqueue(ctx->vdec_set_frame_wq);
}

static void mtk_vdec_trigger_set_frame(struct mtk_vcodec_ctx *ctx)
{
	if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM && ctx->is_flushing == false && ctx->vdec_set_frame_wq != NULL)
		queue_work(ctx->vdec_set_frame_wq, &ctx->vdec_set_frame_work.work);
}

/*
 * This function tries to clean all display buffers, the buffers will return
 * in display order.
 * Note the buffers returned from codec driver may still be in driver's
 * reference list.
 */
static struct vb2_buffer *get_display_buffer(struct mtk_vcodec_ctx *ctx,
	bool got_early_eos)
{
	struct vdec_fb *disp_frame_buffer = NULL;
	struct mtk_video_dec_buf *dstbuf;
	unsigned int i = 0;
	unsigned int num_planes = 0;
	bool no_output = false;
	u64 max_ts;

	mtk_v4l2_debug(4, "[%d]", ctx->id);

	mutex_lock(&ctx->buf_lock);
	if (vdec_if_get_param(ctx, GET_PARAM_DISP_FRAME_BUFFER, &disp_frame_buffer)) {
		mtk_v4l2_err("[%d]Cannot get param : GET_PARAM_DISP_FRAME_BUFFER", ctx->id);
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}

	if (disp_frame_buffer == NULL) {
		mtk_v4l2_debug(4, "No display frame buffer");
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}
	if (!virt_addr_valid(disp_frame_buffer)) {
		mtk_v4l2_debug(3, "Bad display frame buffer %p",
			disp_frame_buffer);
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}

	if (disp_frame_buffer->status & FB_ST_NO_GENERATED) {
		no_output = true;
		disp_frame_buffer->status &= ~FB_ST_NO_GENERATED;
	}

	dstbuf = container_of(disp_frame_buffer, struct mtk_video_dec_buf,
						  frame_buffer);
	num_planes = dstbuf->vb.vb2_buf.num_planes;
	if (dstbuf->used) {
		for (i = 0; i < num_planes; i++)
			vb2_set_plane_payload(&dstbuf->vb.vb2_buf, i,
				no_output ? 0 : ctx->picinfo.fb_sz[i]);

		dstbuf->ready_to_display = true;

		switch (dstbuf->frame_buffer.frame_type) {
		case MTK_FRAME_I:
			dstbuf->vb.flags |= V4L2_BUF_FLAG_KEYFRAME;
			break;
		case MTK_FRAME_P:
			dstbuf->vb.flags |= V4L2_BUF_FLAG_PFRAME;
			break;
		case MTK_FRAME_B:
			dstbuf->vb.flags |= V4L2_BUF_FLAG_BFRAME;
			break;
		default:
#ifdef TV_INTEGRATION
			mtk_v4l2_debug(2, "[%d] unknown frame type %d",
				ctx->id, dstbuf->frame_buffer.frame_type);
#endif
			break;
		}

		dstbuf->vb.vb2_buf.timestamp =
			disp_frame_buffer->timestamp;

		dstbuf->vb.vb2_buf.timestamp =
			mtk_vdec_ts_update_mode_and_timestamp(ctx, dstbuf->vb.vb2_buf.timestamp);

		if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM)
			max_ts = ctx->early_eos_ts;
		else
			max_ts = ctx->input_max_ts;
		if (got_early_eos && dstbuf->vb.vb2_buf.timestamp == max_ts) {
			mtk_v4l2_debug(1, "[%d]got early eos (type %d) with max_ts %llu",
				ctx->id, ctx->eos_type, max_ts);
			dstbuf->vb.flags |= V4L2_BUF_FLAG_LAST;
			ctx->eos_type = NON_EOS; // clear flag
		}
		dstbuf->vb.field = disp_frame_buffer->field;

		mtk_v4l2_debug(2,
			"[%d]status=%x queue id=%d to done_list %d %d flag=%x field %d pts=%llu",
			ctx->id, disp_frame_buffer->status,
			dstbuf->vb.vb2_buf.index,
			dstbuf->queued_in_vb2, got_early_eos,
			dstbuf->vb.flags, dstbuf->vb.field, dstbuf->vb.vb2_buf.timestamp);

		v4l2_m2m_buf_done(&dstbuf->vb, VB2_BUF_STATE_DONE);
		ctx->decoded_frame_cnt++;
	}
	mutex_unlock(&ctx->buf_lock);

	return &dstbuf->vb.vb2_buf;
}

/*
 * This function tries to clean all capture buffers that are not used as
 * reference buffers by codec driver any more
 * In this case, we need re-queue buffer to vb2 buffer if user space
 * already returns this buffer to v4l2 or this buffer is just the output of
 * previous sps/pps/resolution change decode, or do nothing if user
 * space still owns this buffer
 */
static struct vb2_v4l2_buffer *get_free_buffer(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_video_dec_buf *dstbuf;
	struct vdec_fb *free_frame_buffer = NULL;
	struct vb2_buffer *vb;
	int i;
	bool new_dma = false;

	mutex_lock(&ctx->buf_lock);
	if (vdec_if_get_param(ctx,
						  GET_PARAM_FREE_FRAME_BUFFER,
						  &free_frame_buffer)) {
		mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}

	if (free_frame_buffer == NULL) {
		mtk_v4l2_debug(4, " No free frame buffer");
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}
	if (!virt_addr_valid(free_frame_buffer)) {
		mtk_v4l2_debug(3, "Bad free frame buffer %p",
			free_frame_buffer);
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}

	dstbuf = container_of(free_frame_buffer, struct mtk_video_dec_buf,
						  frame_buffer);
	mtk_v4l2_debug(4, "[%d] tmp_frame_addr = 0x%p, status 0x%x, used %d flags 0x%x, id=%d %d %d %d",
				   ctx->id, free_frame_buffer, free_frame_buffer->status,
				   dstbuf->used, dstbuf->flags, dstbuf->vb.vb2_buf.index,
				   dstbuf->queued_in_vb2, dstbuf->queued_in_v4l2,
				   dstbuf->ready_to_display);

	dstbuf->flags |= REF_FREED;

	vb = &dstbuf->vb.vb2_buf;
	for (i = 0; i < vb->num_planes; i++) {
		// real buffer changed in this slot
		if (free_frame_buffer->fb_base[i].dmabuf != vb->planes[i].dbuf) {
			new_dma = true;
			mtk_v4l2_debug(2, "[%d] id=%d is new buffer: old dma_addr[%d] = %llx %p, new dma struct[%d] = %p",
				ctx->id, vb->index, i,
				(unsigned long)free_frame_buffer->fb_base[i].dma_addr,
				free_frame_buffer->fb_base[i].dmabuf,
				i, vb->planes[i].dbuf);
		}
	}

	if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM && ctx->is_flushing == false && !new_dma &&
	    dstbuf->ready_to_display == false && !(free_frame_buffer->status & FB_ST_EOS)) {
		free_frame_buffer->status &= ~FB_ST_FREE;
		dstbuf->flags &= ~REF_FREED;
		mtk_v4l2_debug(0, "[%d]status=%x not queue id=%d to rdy_queue %d %d since input driven (%d) not ready to display",
			ctx->id, free_frame_buffer->status, dstbuf->vb.vb2_buf.index,
			dstbuf->queued_in_vb2, dstbuf->queued_in_v4l2, ctx->input_driven);
	} else if (dstbuf->used) {
		for (i = 0; i < free_frame_buffer->num_planes; i++) {
			if (free_frame_buffer->fb_base[i].dmabuf)
				fput(free_frame_buffer->fb_base[i].dmabuf->file);
			mtk_v4l2_debug(4, "[Ref cnt] id=%d Ref put dma %p",
				free_frame_buffer->index, free_frame_buffer->fb_base[i].dmabuf);
		}
		dstbuf->used = false;
		if ((dstbuf->queued_in_vb2) && (dstbuf->queued_in_v4l2)) {
			if ((free_frame_buffer->status & FB_ST_EOS) &&
				(ctx->input_driven == INPUT_DRIVEN_PUT_FRM)) {
				/*
				 * Buffer status has EOS flag, which is capture buffer
				 * used for EOS when input driven. So set last buffer flag
				 * and queue to done queue.
				 */
				mtk_v4l2_debug(2, "[%d]status=%x not queue id=%d to rdy_queue %d %d for EOS",
					ctx->id, free_frame_buffer->status,
					dstbuf->vb.vb2_buf.index,
					dstbuf->queued_in_vb2,
					dstbuf->queued_in_v4l2);
				free_frame_buffer->status &= ~FB_ST_EOS;

				dstbuf->vb.vb2_buf.timestamp = 0;
				memset(&dstbuf->vb.timecode, 0, sizeof(struct v4l2_timecode));
				dstbuf->vb.flags |= V4L2_BUF_FLAG_LAST;
				for (i = 0; i < free_frame_buffer->num_planes; i++)
					vb2_set_plane_payload(&dstbuf->vb.vb2_buf, i, 0);
				v4l2_m2m_buf_done(&dstbuf->vb, VB2_BUF_STATE_DONE);
			} else if (free_frame_buffer->status == FB_ST_FREE) {
				/*
				 * After decode sps/pps or non-display buffer, we don't
				 * need to return capture buffer to user space, but
				 * just re-queue this capture buffer to vb2 queue.
				 * This reduce overheads that dq/q unused capture
				 * buffer. In this case, queued_in_vb2 = true.
				 */
				mtk_v4l2_debug(2, "[%d]status=%x queue id=%d to rdy_queue %d",
					ctx->id, free_frame_buffer->status,
					dstbuf->vb.vb2_buf.index,
					dstbuf->queued_in_vb2);
				if (v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &dstbuf->vb) < 0)
					goto err_in_rdyq;
				else
					update_dst_cnt(ctx);
				mtk_vdec_trigger_set_frame(ctx);
			} else {
				mtk_v4l2_debug(4, "[%d]status=%x reference free queue id=%d %d %d",
					ctx->id, free_frame_buffer->status,
					dstbuf->vb.vb2_buf.index,
					dstbuf->queued_in_vb2,
					dstbuf->queued_in_v4l2);
			}
		} else if ((dstbuf->queued_in_vb2 == false) &&
				   (dstbuf->queued_in_v4l2 == true)) {
			/*
			 * If buffer in v4l2 driver but not in vb2 queue yet,
			 * and we get this buffer from free_list, it means
			 * that codec driver do not use this buffer as
			 * reference buffer anymore. We should q buffer to vb2
			 * queue, so later work thread could get this buffer
			 * for decode. In this case, queued_in_vb2 = false
			 * means this buffer is not from previous decode
			 * output.
			 */
			mtk_v4l2_debug(2,
				"[%d]status=%x queue id=%d to rdy_queue",
				ctx->id, free_frame_buffer->status,
				dstbuf->vb.vb2_buf.index);
			dstbuf->queued_in_vb2 = true;
			if (v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &dstbuf->vb) < 0)
				goto err_in_rdyq;
			else
				update_dst_cnt(ctx);
			mtk_vdec_trigger_set_frame(ctx);
		} else {
			/*
			 * Codec driver do not need to reference this capture
			 * buffer and this buffer is not in v4l2 driver.
			 * Then we don't need to do any thing, just add log when
			 * we need to debug buffer flow.
			 * When this buffer q from user space, it could
			 * directly q to vb2 buffer
			 */
			mtk_v4l2_debug(4, "[%d]status=%x reference free queue id=%d %d %d",
				ctx->id, free_frame_buffer->status,
				dstbuf->vb.vb2_buf.index,
				dstbuf->queued_in_vb2,
				dstbuf->queued_in_v4l2);
		}
	}
	mutex_unlock(&ctx->buf_lock);

	return &dstbuf->vb;
err_in_rdyq:
	for (i = 0; i < free_frame_buffer->num_planes; i++) {
		if (free_frame_buffer->fb_base[i].dmabuf)
			get_file(free_frame_buffer->fb_base[i].dmabuf->file);
		mtk_v4l2_debug(4, "[Ref cnt] id=%d Ref get dma %p",
			free_frame_buffer->index, free_frame_buffer->fb_base[i].dmabuf);
	}
	mutex_unlock(&ctx->buf_lock);

	mtk_vdec_trigger_set_frame(ctx);

	return &dstbuf->vb;
}

static struct vb2_buffer *get_free_bs_buffer(struct mtk_vcodec_ctx *ctx,
	struct mtk_vcodec_mem *current_bs)
{
	struct mtk_vcodec_mem *free_bs_buffer;
	struct mtk_video_dec_buf *srcbuf;

	mutex_lock(&ctx->buf_lock);
	if (vdec_if_get_param(ctx, GET_PARAM_FREE_BITSTREAM_BUFFER,
						  &free_bs_buffer) != 0) {
		mtk_v4l2_err("[%d] Cannot get param : GET_PARAM_FREE_BITSTREAM_BUFFER",
					 ctx->id);
		mutex_unlock(&ctx->buf_lock);
		return NULL;
	}
	mutex_unlock(&ctx->buf_lock);

	if (free_bs_buffer == NULL) {
		mtk_v4l2_debug(3, "No free bitstream buffer");
		return NULL;
	}
	if (current_bs == free_bs_buffer) {
		mtk_v4l2_debug(4,
			"No free bitstream buffer except current bs: %p",
			current_bs);
		return NULL;
	}
	if (!virt_addr_valid(free_bs_buffer)) {
		mtk_v4l2_debug(3, "Bad free bitstream buffer %p",
			free_bs_buffer);
		return NULL;
	}

	srcbuf = container_of(free_bs_buffer,
		struct mtk_video_dec_buf, bs_buffer);
	mtk_v4l2_debug(2,
		"[%d] length=%zu size=%zu queue idx=%d to done_list %d",
		ctx->id, free_bs_buffer->length, free_bs_buffer->size,
		srcbuf->vb.vb2_buf.index,
		srcbuf->queued_in_vb2);

	if (srcbuf->vb.flags & V4L2_BUF_FLAG_OUTPUT_NOT_GENERATED)
		mtk_vdec_ts_remove_last(ctx);
	v4l2_m2m_buf_done(&srcbuf->vb, VB2_BUF_STATE_DONE);

	return &srcbuf->vb.vb2_buf;
}

static void clean_free_bs_buffer(struct mtk_vcodec_ctx *ctx,
	struct mtk_vcodec_mem *current_bs)
{
	struct vb2_buffer *framptr;

	do {
		framptr = get_free_bs_buffer(ctx, current_bs);
	} while (framptr);
}


static void clean_display_buffer(struct mtk_vcodec_ctx *ctx, bool got_early_eos)
{
	struct vb2_buffer *framptr;

	do {
		framptr = get_display_buffer(ctx, got_early_eos);
	} while (framptr);
}

static bool clean_free_fm_buffer(struct mtk_vcodec_ctx *ctx)
{
	struct vb2_v4l2_buffer *framptr_vb;
	bool has_eos = false;

	do {
		framptr_vb = get_free_buffer(ctx);
		if (framptr_vb != NULL && (framptr_vb->flags & V4L2_BUF_FLAG_LAST))
			has_eos = true;
	} while (framptr_vb);

	return has_eos;
}

static dma_addr_t create_meta_buffer_info(struct mtk_vcodec_ctx *ctx, int fd)
{
	int i = 0;
	struct dma_buf *dmabuf = NULL;
	struct dma_buf_attachment *buf_att = NULL;
	struct sg_table *sgt = NULL;
	dma_addr_t dma_meta_addr = 0;

	dmabuf = dma_buf_get(fd);
	mtk_v4l2_debug(8, "dmabuf:%p", dmabuf);

	buf_att = dma_buf_attach(
		dmabuf,
		ctx->m2m_ctx->out_q_ctx.q.dev);
	if (IS_ERR_OR_NULL(buf_att)) {
		mtk_v4l2_err("attach fail ret %d", PTR_ERR(buf_att));
		return 0;
	}
	sgt = dma_buf_map_attachment(buf_att, DMA_TO_DEVICE);
	if (IS_ERR_OR_NULL(sgt)) {
		mtk_v4l2_err("map attachment fail ret %d", PTR_ERR(sgt));
		dma_buf_detach(dmabuf, buf_att);
		return 0;
	}
	dma_meta_addr  = sg_dma_address(sgt->sgl);

	mtk_v4l2_debug(4, "map new, dmabuf:%p, dma_addr:%p",
		dmabuf, dma_meta_addr);
	//save va and dmabuf
	if (dma_meta_addr) {
		for (i = 0; i < MAX_META_BUF_CNT; ++i) {
			if (ctx->dma_meta_list[i].dma_meta_addr == 0) {
				ctx->dma_meta_list[i].dmabuf = dmabuf;
				ctx->dma_meta_list[i].dma_meta_addr = dma_meta_addr;
				ctx->dma_meta_list[i].buf_att = buf_att;
				ctx->dma_meta_list[i].sgt = sgt;
				mtk_v4l2_debug(2, "save meta buf dmabuf %p  addr:%p at %d",
					dmabuf, dma_meta_addr, i);
				break;
			}
		}
		if (i == MAX_META_BUF_CNT)
			mtk_v4l2_err("dma_buf_list is overflow!\n");
	}

	return dma_meta_addr;
}

static dma_addr_t get_meta_buffer_dma_addr(struct mtk_vcodec_ctx *ctx, int fd)
{
	dma_addr_t dma_addr = 0;
	int i = 0;
	struct dma_buf *dmabuf = NULL;

	dmabuf = dma_buf_get(fd);
	mtk_v4l2_debug(8, "%s, dmabuf:%p", __func__, dmabuf);

	if (IS_ERR(dmabuf))
		return dma_addr;

	if (dmabuf) {
		for (i = 0; i < MAX_META_BUF_CNT; ++i) {
			if (dmabuf == ctx->dma_meta_list[i].dmabuf) {
				dma_addr = ctx->dma_meta_list[i].dma_meta_addr;
				mtk_v4l2_debug(4, "reuse dma_addr %p at %d", dma_addr, i);
				break;
			}
		}
	}

	if (dma_addr == 0) {
		dma_addr = create_meta_buffer_info(ctx, fd);
		for (i = 0; i < MAX_META_BUF_CNT; ++i) {
			if (dmabuf == ctx->dma_meta_list[i].dmabuf) {
				dma_addr = ctx->dma_meta_list[i].dma_meta_addr;
				mtk_v4l2_debug(4, "reuse dma_addr %p at %d", dma_addr, i);
				break;
			}
		}
	}

	if (dmabuf)
		dma_buf_put(dmabuf);

	return dma_addr;
}
static void *create_general_buffer_info(struct mtk_vcodec_ctx *ctx, int fd)
{
	struct dma_buf_map map;
	void *va = NULL;
	int i = 0;
	struct dma_buf *dmabuf = NULL;
	struct dma_buf_attachment *buf_att = NULL;
	struct sg_table *sgt = NULL;
	dma_addr_t dma_general_addr = 0;
	int ret;

	dmabuf = dma_buf_get(fd);
	mtk_v4l2_debug(8, "%s, dmabuf:%p", __func__, dmabuf);

	dma_buf_begin_cpu_access(dmabuf, DMA_TO_DEVICE);
	ret = dma_buf_vmap(dmabuf, &map);
	va = ret ? NULL : map.vaddr;

	buf_att = dma_buf_attach(
		dmabuf,
		ctx->general_dev);
	if (IS_ERR_OR_NULL(buf_att)) {
		mtk_v4l2_err("attach fail ret %d", PTR_ERR(buf_att));
		return NULL;
	}
	sgt = dma_buf_map_attachment(buf_att, DMA_TO_DEVICE);
	if (IS_ERR_OR_NULL(sgt)) {
		mtk_v4l2_err("map attachment fail ret %d", PTR_ERR(sgt));
		dma_buf_detach(dmabuf, buf_att);
		return NULL;
	}
	dma_general_addr  = sg_dma_address(sgt->sgl);

	mtk_v4l2_debug(4, "map new va %p, dmabuf:%p, dma_addr:%p",
		va, dmabuf, dma_general_addr);
	//save va and dmabuf
	if (va) {
		for (i = 0; i < MAX_GEN_BUF_CNT; ++i) {
			if (ctx->dma_buf_list[i].va == NULL) {
				ctx->dma_buf_list[i].va = va;
				ctx->dma_buf_list[i].dmabuf = dmabuf;
				ctx->dma_buf_list[i].dma_general_addr = dma_general_addr;
				ctx->dma_buf_list[i].buf_att = buf_att;
				ctx->dma_buf_list[i].sgt = sgt;
				mtk_v4l2_debug(4, "save general buf va %p dmabuf %p  addr:%p at %d",
					va, dmabuf, dma_general_addr, i);
				break;
			}
		}
		if (i == MAX_GEN_BUF_CNT)
			mtk_v4l2_err("dma_buf_list is overflow!\n");
	}

	return va;
}

static void *get_general_buffer_va(struct mtk_vcodec_ctx *ctx, int fd)
{
	void *va = NULL;
	int i = 0;
	struct dma_buf *dmabuf = NULL;

	dmabuf = dma_buf_get(fd);
	mtk_v4l2_debug(8, "%s, dmabuf:%p", __func__, dmabuf);

	if (IS_ERR(dmabuf))
		return NULL;

	if (dmabuf) {
		for (i = 0; i < MAX_GEN_BUF_CNT; ++i) {
			if (dmabuf == ctx->dma_buf_list[i].dmabuf) {
				va = ctx->dma_buf_list[i].va;
				mtk_v4l2_debug(4, "reuse va %p at %d", va, i);
				break;
			}
		}
	}

	if (va == NULL)
		va = create_general_buffer_info(ctx, fd);

	if (dmabuf)
		dma_buf_put(dmabuf);

	return va;
}

static dma_addr_t get_general_buffer_dma_addr(struct mtk_vcodec_ctx *ctx, int fd)
{
	dma_addr_t dma_addr = 0;
	int i = 0;
	struct dma_buf *dmabuf = NULL;

	dmabuf = dma_buf_get(fd);
	mtk_v4l2_debug(8, "%s, dmabuf:%p", __func__, dmabuf);

	if (IS_ERR(dmabuf))
		return dma_addr;

	if (dmabuf) {
		for (i = 0; i < MAX_GEN_BUF_CNT; ++i) {
			if (dmabuf == ctx->dma_buf_list[i].dmabuf) {
				dma_addr = ctx->dma_buf_list[i].dma_general_addr;
				mtk_v4l2_debug(4, "reuse dma_addr %p at %d", dma_addr, i);
				break;
			}
		}
	}

	if (dma_addr == 0) {
		create_general_buffer_info(ctx, fd);
		for (i = 0; i < MAX_GEN_BUF_CNT; ++i) {
			if (dmabuf == ctx->dma_buf_list[i].dmabuf) {
				dma_addr = ctx->dma_buf_list[i].dma_general_addr;
				mtk_v4l2_debug(4, "reuse dma_addr %p at %d", dma_addr, i);
				break;
			}
		}
	}

	if (dmabuf)
		dma_buf_put(dmabuf);

	return dma_addr;
}

int mtk_vdec_defer_put_fb_job(struct mtk_vcodec_ctx *ctx, int type)
{
	int ret = -1;

	mutex_lock(&ctx->resched_lock);

	/* skip once due to we are in reschedule progress already */
	if (ctx->resched)
		goto unlock;

	/* allow to schedule m2m ctx if src/dst is empty */
	v4l2_m2m_set_src_buffered(ctx->m2m_ctx, true);
	v4l2_m2m_set_dst_buffered(ctx->m2m_ctx, true);
	v4l2_m2m_try_schedule(ctx->m2m_ctx);
	ctx->resched = true;
	ret = 0;
unlock:
	mutex_unlock(&ctx->resched_lock);
	return ret;
}

int mtk_vdec_cancel_put_fb_job_locked(struct mtk_vcodec_ctx *ctx)
{
	int resched = ctx->resched;

	if (resched) {
		v4l2_m2m_set_src_buffered(ctx->m2m_ctx, false);
		if (!ctx->input_driven)
			v4l2_m2m_set_dst_buffered(ctx->m2m_ctx, false);
		ctx->resched = false;
	}
	return resched;
}

void mtk_vdec_cancel_put_fb_job(struct mtk_vcodec_ctx *ctx)
{
	mutex_lock(&ctx->resched_lock);
	mtk_vdec_cancel_put_fb_job_locked(ctx);
	mutex_unlock(&ctx->resched_lock);
}

void mtk_vdec_process_put_fb_job(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_video_dec_buf *src_buf_info;
	struct vb2_v4l2_buffer *src_vb2_v4l2, *dst_vb2_v4l2;
	int process = 0;

	mutex_lock(&ctx->resched_lock);

	if (!mtk_vdec_cancel_put_fb_job_locked(ctx))
		goto unlock;

	/* do nothing for low latency and input driven */
	if (ctx->use_fence || ctx->input_driven)
		goto unlock;

	src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	dst_vb2_v4l2 = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);

	/* use normal flow ,
	 * if src_vb2_v4l2 or dst_vb2_v4l2 is NULL
	 * we will send msg SET_PARAM_PUT_FB
	 * let LAT get display buffer
	 */
	if (src_vb2_v4l2 && dst_vb2_v4l2)
		goto unlock;

	src_buf_info = container_of(src_vb2_v4l2,
				    struct mtk_video_dec_buf,
				    vb);
	/*
	 * no need to clean disp/free if we are processing EOS,
	 * because EOS is handled synchronously
	 */
	if (src_vb2_v4l2 && src_buf_info->lastframe != NON_EOS)
		goto unlock;

	process = 1;

unlock:
	mutex_unlock(&ctx->resched_lock);

	if (process) {
		vdec_if_set_param(ctx, SET_PARAM_PUT_FB, NULL);
		clean_display_buffer(ctx, 0);
		clean_free_fm_buffer(ctx);
	}
}

static void mtk_vdec_queue_res_chg_event(struct mtk_vcodec_ctx *ctx)
{
	static const struct v4l2_event ev_src_ch = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes =
		V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	mtk_v4l2_debug(1, "[%d]", ctx->id);
	v4l2_event_queue_fh(&ctx->fh, &ev_src_ch);

	v4l2_m2m_set_dst_buffered(ctx->m2m_ctx,
		ctx->input_driven != NON_INPUT_DRIVEN);
}

static void mtk_vdec_queue_stop_play_event(struct mtk_vcodec_ctx *ctx)
{
	static const struct v4l2_event ev_eos = {
		.type = V4L2_EVENT_EOS,
	};

	mtk_v4l2_debug(1, "[%d]", ctx->id);
	v4l2_event_queue_fh(&ctx->fh, &ev_eos);
}

static void mtk_vdec_queue_noseqheader_event(struct mtk_vcodec_ctx *ctx)
{
	static const struct v4l2_event ev_eos = {
		.type = V4L2_EVENT_MTK_VDEC_NOHEADER,
	};

	mtk_v4l2_debug(1, "[%d]", ctx->id);
	v4l2_event_queue_fh(&ctx->fh, &ev_eos);
}

void mtk_vdec_queue_error_event(struct mtk_vcodec_ctx *ctx)
{
	static const struct v4l2_event ev_error = {
		.type = V4L2_EVENT_MTK_VDEC_ERROR,
	};

	if  (ctx->err_msg)
		memcpy((void *)ev_error.u.data, &ctx->err_msg, sizeof(ctx->err_msg));

	mtk_v4l2_debug(0, "[%d] msg %x", ctx->id, ctx->err_msg);
	v4l2_event_queue_fh(&ctx->fh, &ev_error);
}

static void mtk_vdec_dvfs_reset_AO_flag(struct mtk_vcodec_dev *dev)
{
	dev->vdec_dvfs_params.target_freq = 0;
	dev->vdec_dvfs_params.high_loading_scenario = 0;
}

void mtk_vdec_error_handle(struct mtk_vcodec_ctx *ctx, char *debug_str)
{
	struct mtk_vcodec_dev *dev = ctx->dev;
	int i;

	mtk_v4l2_err("[%d] start error handling %s (dvfs freq %d, high %d)(pw ref %d, %d %d)(hw active %d %d)",
		ctx->id, debug_str, dev->vdec_dvfs_params.target_freq,
		dev->vdec_dvfs_params.high_loading_scenario,
		atomic_read(&dev->dec_larb_ref_cnt),
		atomic_read(&dev->dec_clk_ref_cnt[MTK_VDEC_LAT]),
		atomic_read(&dev->dec_clk_ref_cnt[MTK_VDEC_CORE]),
		atomic_read(&dev->dec_hw_active[MTK_VDEC_LAT]),
		atomic_read(&dev->dec_hw_active[MTK_VDEC_CORE]));
	ctx->state = MTK_STATE_ABORT;
	vdec_check_release_lock(ctx);
	for (i = 0; i < MTK_VDEC_HW_NUM; i++)
		atomic_set(&dev->dec_hw_active[i], 0);
	mutex_lock(&dev->dec_dvfs_mutex);
	if (mtk_vdec_dvfs_is_pw_always_on(dev)) {
		if (!dev->dvfs_is_suspend_off)
			mtk_vcodec_dec_pw_off(&dev->pm);
		mtk_vdec_dvfs_reset_AO_flag(dev);
	}
	mutex_unlock(&dev->dec_dvfs_mutex);
	mtk_vdec_queue_error_event(ctx);
}

static void mtk_vdec_reset_decoder(struct mtk_vcodec_ctx *ctx, bool is_drain,
	struct mtk_vcodec_mem *current_bs, enum v4l2_buf_type type)
{
	struct vdec_fb drain_fb;
	int i, ret = 0;
	struct vb2_v4l2_buffer *dst_vb2_v4l2, *src_vb2_v4l2;
	struct mtk_video_dec_buf *dstbuf, *srcbuf;
	struct vb2_queue *dstq, *srcq;

	if (ctx->input_driven == NON_INPUT_DRIVEN && ctx->align_mode) {
		mtk_v4l2_debug(2, "[%d] need to set align_type(%d) to RESET(%d)",
			ctx->id, atomic_read(&ctx->align_type), VDEC_ALIGN_RESET);
		atomic_set(&ctx->align_type, VDEC_ALIGN_RESET);
	}

	mutex_lock(&ctx->gen_buf_va_lock);
	ctx->state = MTK_STATE_FLUSH;
	mtk_vdec_check_vcp_active(ctx, false, false, "reset"); // vcp active for reset msg
	if (ctx->input_driven == INPUT_DRIVEN_CB_FRM)
		wake_up(&ctx->fm_wq);

	for (i = 0; i < MAX_GEN_BUF_CNT; ++i) {
		if (ctx->dma_buf_list[i].va && ctx->dma_buf_list[i].dmabuf) {
			struct dma_buf_map map =
				DMA_BUF_MAP_INIT_VADDR(ctx->dma_buf_list[i].va);
			struct dma_buf *dmabuf = ctx->dma_buf_list[i].dmabuf;
			struct dma_buf_attachment *buf_att = ctx->dma_buf_list[i].buf_att;
			struct sg_table *sgt = ctx->dma_buf_list[i].sgt;

			dma_buf_unmap_attachment(buf_att, sgt, DMA_TO_DEVICE);
			dma_buf_detach(dmabuf, buf_att);
			dma_buf_vunmap(dmabuf, &map);
			dma_buf_end_cpu_access(dmabuf,
				DMA_TO_DEVICE);
			dma_buf_put(dmabuf);
		}
	}
	memset(ctx->dma_buf_list, 0,
		sizeof(struct dma_gen_buf) * MAX_GEN_BUF_CNT);

	for (i = 0; i < MAX_META_BUF_CNT; ++i) {
		if (ctx->dma_meta_list[i].dmabuf) {
			struct dma_buf *dmabuf = ctx->dma_meta_list[i].dmabuf;
			struct dma_buf_attachment *buf_att = ctx->dma_meta_list[i].buf_att;
			struct sg_table *sgt = ctx->dma_meta_list[i].sgt;

			dma_buf_unmap_attachment(buf_att, sgt, DMA_TO_DEVICE);
			dma_buf_detach(dmabuf, buf_att);
			dma_buf_put(dmabuf);
		}
	}
	memset(ctx->dma_meta_list, 0,
		sizeof(struct dma_meta_buf) * MAX_META_BUF_CNT);
	mutex_unlock(&ctx->gen_buf_va_lock);

	if (is_drain) {
		memset(&drain_fb, 0, sizeof(struct vdec_fb));
		ret = vdec_if_flush(ctx, NULL, &drain_fb,
			V4L2_TYPE_IS_OUTPUT(type) ? FLUSH_BITSTREAM : FLUSH_FRAME);
	} else {
		ctx->is_flushing = true;
		mtk_vdec_set_frame(ctx, NULL);
		ret = vdec_if_flush(ctx, NULL, NULL,
			V4L2_TYPE_IS_OUTPUT(type) ? FLUSH_BITSTREAM : FLUSH_FRAME);
	}

	dstq = &ctx->m2m_ctx->cap_q_ctx.q;
	srcq = &ctx->m2m_ctx->out_q_ctx.q;
	if (ret) {
		ctx->is_flushing = false;
		mtk_v4l2_err("DecodeFinal failed, ret=%d", ret);

		if (ret == -EIO) {
			mutex_lock(&ctx->buf_lock);
			for (i = 0; i < dstq->num_buffers; i++) {
				dst_vb2_v4l2 = container_of(
					dstq->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
				dstbuf = container_of(
					dst_vb2_v4l2, struct mtk_video_dec_buf, vb);
				// codec exception handling
				mtk_v4l2_debug(8, "[%d]num_buffers %d status=%x queue id=%d %p %llx q_cnt %d %d %d %d state %d",
					ctx->id, dstq->num_buffers, dstbuf->frame_buffer.status,
					dstbuf->vb.vb2_buf.index, &dstbuf->frame_buffer,
					(unsigned long)(&dstbuf->frame_buffer),
					atomic_read(&dstq->owned_by_drv_count),
					dstbuf->queued_in_vb2, dstbuf->queued_in_v4l2,
					dstbuf->used, dst_vb2_v4l2->vb2_buf.state);
				if (dst_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE) {
					v4l2_m2m_buf_done(&dstbuf->vb, VB2_BUF_STATE_ERROR);
					dstbuf->frame_buffer.status = FB_ST_FREE;
				}
			}
			mutex_unlock(&ctx->buf_lock);

			for (i = 0; i < srcq->num_buffers; i++) {
				src_vb2_v4l2 = container_of(
					srcq->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
				srcbuf = container_of(
					src_vb2_v4l2, struct mtk_video_dec_buf, vb);
				if (src_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
					v4l2_m2m_buf_done(&srcbuf->vb, VB2_BUF_STATE_ERROR);
			}
		}
		return;
	}

	mtk_vdec_cancel_put_fb_job(ctx);
	clean_free_bs_buffer(ctx, current_bs);
	clean_display_buffer(ctx, 0);
	clean_free_fm_buffer(ctx);

	/* check buffer status */
	mutex_lock(&ctx->buf_lock);
	for (i = 0; i < dstq->num_buffers; i++) {
		dst_vb2_v4l2 = container_of(
			dstq->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
		dstbuf = container_of(
			dst_vb2_v4l2, struct mtk_video_dec_buf, vb);
		mtk_v4l2_debug(4, "[%d]num_buffers %d status=%x queue id=%d %p %llx q_cnt %d %d %d %d",
			ctx->id, dstq->num_buffers, dstbuf->frame_buffer.status,
			dstbuf->vb.vb2_buf.index, &dstbuf->frame_buffer,
			(unsigned long)(&dstbuf->frame_buffer),
			atomic_read(&dstq->owned_by_drv_count),
			dstbuf->queued_in_vb2,
			dstbuf->queued_in_v4l2, dstbuf->used);
	}
	mutex_unlock(&ctx->buf_lock);
	ctx->is_flushing = false;
}

static void mtk_vdec_pic_info_update(struct mtk_vcodec_ctx *ctx)
{
	unsigned int dpbsize = 0;
	int ret;
	struct mtk_color_desc color_desc = {.is_hdr = 0};

	if (vdec_if_get_param(ctx,
						  GET_PARAM_PIC_INFO,
						  &ctx->last_decoded_picinfo)) {
		mtk_v4l2_err("[%d]Error!! Cannot get param : GET_PARAM_PICTURE_INFO ERR",
					 ctx->id);
		return;
	}

	if (ctx->last_decoded_picinfo.pic_w == 0 ||
		ctx->last_decoded_picinfo.pic_h == 0 ||
		ctx->last_decoded_picinfo.buf_w == 0 ||
		ctx->last_decoded_picinfo.buf_h == 0) {
		mtk_v4l2_err("Cannot get correct pic info");
		return;
	}

	ret = vdec_if_get_param(ctx, GET_PARAM_DPB_SIZE, &dpbsize);
	if (dpbsize == 0)
		mtk_v4l2_err("Incorrect dpb size, ret=%d", ret);
	ctx->last_dpb_size = dpbsize;

	ret = vdec_if_get_param(ctx, GET_PARAM_COLOR_DESC, &color_desc);
	if (ret == 0)
		ctx->last_is_hdr = color_desc.is_hdr;

	mtk_v4l2_debug(1,
				   "[%d]-> new(%d,%d),dpb(%d), old(%d,%d),dpb(%d), bit(%d) real(%d,%d) hdr(%d,%d)",
				   ctx->id, ctx->last_decoded_picinfo.pic_w,
				   ctx->last_decoded_picinfo.pic_h,
				   ctx->last_dpb_size,
				   ctx->picinfo.pic_w, ctx->picinfo.pic_h,
				   ctx->dpb_size,
				   ctx->picinfo.bitdepth,
				   ctx->last_decoded_picinfo.buf_w,
				   ctx->last_decoded_picinfo.buf_h,
				   ctx->is_hdr, ctx->last_is_hdr);
}


int mtk_vdec_put_fb(struct mtk_vcodec_ctx *ctx, enum mtk_put_buffer_type type, bool no_need_put)
{
	struct mtk_video_dec_buf *dst_buf_info, *src_buf_info;
	struct vb2_v4l2_buffer *dst_vb2_v4l2, *src_vb2_v4l2;
	struct vb2_buffer *src_buf, *dst_buf;
	struct vdec_fb *pfb;
	int i, ret = 0;
	bool has_eos;

	mtk_v4l2_debug(1, "type = %d", type);

	if (ctx->state < MTK_STATE_HEADER) {
		mtk_v4l2_err("type = %d state %d no valid", type);
		return -1;
	}

	if (no_need_put)
		goto not_put_fb;

	/* put fb in core stage? */
	if (type != PUT_BUFFER_WORKER && !(ctx->use_fence || ctx->input_driven))
		return mtk_vdec_defer_put_fb_job(ctx, type);

	src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	src_buf = &src_vb2_v4l2->vb2_buf;
	src_buf_info = container_of(src_vb2_v4l2, struct mtk_video_dec_buf, vb);

	if (src_buf_info == NULL) {
		if (ctx->use_fence && type != PUT_BUFFER_WORKER
			&& !(ctx->input_driven)) {
			clean_display_buffer(ctx, false);
			clean_free_fm_buffer(ctx);
			return 0;
		}

		if (type == PUT_BUFFER_WORKER)
			return 0;
	}

	if (type == PUT_BUFFER_WORKER && src_buf_info->lastframe == EOS) {

		clean_display_buffer(ctx, src_buf->planes[0].bytesused != 0U);
		clean_free_fm_buffer(ctx);

		if (src_buf->planes[0].bytesused == 0U) {
			src_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			vb2_set_plane_payload(&src_buf_info->vb.vb2_buf, 0, 0);
			if (src_buf_info != ctx->dec_flush_buf)
				v4l2_m2m_buf_done(&src_buf_info->vb,
					VB2_BUF_STATE_DONE);

			if (ctx->input_driven == INPUT_DRIVEN_CB_FRM)
				ret = wait_event_interruptible(
					ctx->fm_wq,
					v4l2_m2m_num_dst_bufs_ready(
					ctx->m2m_ctx) > 0 ||
					ctx->state == MTK_STATE_FLUSH);

			/* update dst buf status */
			dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
			dst_buf = &dst_vb2_v4l2->vb2_buf;
			if (ctx->state == MTK_STATE_FLUSH || ret != 0 || dst_buf == NULL) {
				mtk_v4l2_debug(0, "wait EOS dst break!state %d, ret %d, dst_buf %p",
				ctx->state, ret, dst_buf);
				return 0;
			}

			dst_vb2_v4l2 = container_of(dst_buf,
				struct vb2_v4l2_buffer, vb2_buf);
			dst_buf_info = container_of(dst_vb2_v4l2,
				struct mtk_video_dec_buf, vb);

			dst_buf_info->vb.vb2_buf.timestamp = 0;
			memset(&dst_buf_info->vb.timecode, 0, sizeof(struct v4l2_timecode));
			dst_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
			pfb = &dst_buf_info->frame_buffer;
			for (i = 0; i < pfb->num_planes; i++)
				vb2_set_plane_payload(&dst_buf_info->vb.vb2_buf, i, 0);
			v4l2_m2m_buf_done(&dst_buf_info->vb, VB2_BUF_STATE_DONE);
		}

		mtk_vdec_queue_stop_play_event(ctx);
	} else if (no_need_put == false) {
		if (!ctx->input_driven && !ctx->use_fence)
			dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
		clean_display_buffer(ctx,
			(type == PUT_BUFFER_WORKER  &&
			src_buf_info->lastframe == EOS_WITH_DATA) ||
			ctx->eos_type == EOS_WITH_DATA);
		has_eos = clean_free_fm_buffer(ctx);
		if (ctx->input_driven && has_eos)
			mtk_vdec_queue_stop_play_event(ctx);
	}

	update_dst_cnt(ctx);

not_put_fb:
	if (no_need_put) {
		struct list_head *p, *q;
		struct mtk_vcodec_ctx *tmp_ctx;

		if (!mtk_vcodec_is_vcp(MTK_INST_DECODER))
			mutex_lock(&ctx->dev->ctx_mutex);
		list_for_each_safe(p, q, &ctx->dev->ctx_list) {
			tmp_ctx = list_entry(p, struct mtk_vcodec_ctx, list);
			if (tmp_ctx != NULL && tmp_ctx->state == MTK_STATE_HEADER)
				v4l2_m2m_try_schedule(tmp_ctx->m2m_ctx);
		}
		if (!mtk_vcodec_is_vcp(MTK_INST_DECODER))
			mutex_unlock(&ctx->dev->ctx_mutex);
	} else if (ctx->input_driven)
		v4l2_m2m_try_schedule(ctx->m2m_ctx);

	return 0;
}

static void mtk_vdec_worker(struct work_struct *work)
{
	struct mtk_vcodec_ctx *ctx = container_of(work, struct mtk_vcodec_ctx,
		decode_work);
	struct mtk_vcodec_dev *dev = ctx->dev;
	struct vb2_buffer *src_buf, *dst_buf;
	struct mtk_vcodec_mem *buf;
	struct vdec_fb *pfb = NULL;
	unsigned int src_chg = 0;
	bool res_chg = false;
	bool need_more_output = false;
	bool mtk_vcodec_unsupport = false;
	int ret;
	unsigned int i = 0;
	unsigned int num_planes;
	struct timespec64 worktvstart;
	struct timespec64 worktvstart1;
	struct timespec64 vputvend;
	struct timespec64 ts_delta;
	struct mtk_video_dec_buf *dst_buf_info = NULL, *src_buf_info = NULL;
	struct vb2_v4l2_buffer *dst_vb2_v4l2, *src_vb2_v4l2;
	unsigned int dpbsize = 0;
	struct mtk_color_desc color_desc = {.is_hdr = 0};
	struct vdec_fb drain_fb;
	unsigned int pair_cnt, limit_cnt;

	mutex_lock(&ctx->worker_lock);
	if (ctx->state != MTK_STATE_HEADER) {
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		mtk_v4l2_debug(1, " %d", ctx->state);
		mutex_unlock(&ctx->worker_lock);
		return;
	}

	/* process deferred put fb job */
	mtk_vdec_process_put_fb_job(ctx);

	mtk_vdec_do_gettimeofday(&worktvstart);
	src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	if (src_vb2_v4l2 == NULL) {
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		mtk_v4l2_debug(1, "[%d] src_buf empty!!", ctx->id);
		mutex_unlock(&ctx->worker_lock);
		return;
	}
	src_buf = &src_vb2_v4l2->vb2_buf;

	dst_vb2_v4l2 = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	if (dst_vb2_v4l2 == NULL && !ctx->input_driven) {
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		mtk_v4l2_debug(1, "[%d] dst_buf empty!!", ctx->id);
		mutex_unlock(&ctx->worker_lock);
		return;
	}
	dst_buf = &dst_vb2_v4l2->vb2_buf;

	src_buf_info = container_of(src_vb2_v4l2, struct mtk_video_dec_buf, vb);
	if (!ctx->input_driven) {
		dst_buf_info = container_of(dst_vb2_v4l2,
			struct mtk_video_dec_buf, vb);

		pfb = &dst_buf_info->frame_buffer;
		num_planes = dst_vb2_v4l2->vb2_buf.num_planes;
		pfb->num_planes = num_planes;
		pfb->index = dst_buf->index;

		mutex_lock(&ctx->buf_lock);
		for (i = 0; i < num_planes; i++) {
			if (mtk_v4l2_dbg_level > 0)
				pfb->fb_base[i].va = vb2_plane_vaddr(dst_buf, i);
			pfb->fb_base[i].dma_addr =
				vb2_dma_contig_plane_dma_addr(dst_buf, i);
			pfb->fb_base[i].size = ctx->picinfo.fb_sz[i];
			pfb->fb_base[i].length = dst_buf->planes[i].length;
			pfb->fb_base[i].dmabuf = dst_buf->planes[i].dbuf;

			if (dst_buf_info->used == false) {
				if (dst_buf->planes[i].dbuf)
					get_file(dst_buf->planes[i].dbuf->file);
				mtk_v4l2_debug(4, "[Ref cnt] id=%d Ref get dma %p", dst_buf->index,
					dst_buf->planes[i].dbuf);
			}
		}
		pfb->status = FB_ST_INIT;
		dst_buf_info->used = true;
		mtk_vcodec_init_slice_info(ctx, dst_buf_info);
		ctx->fb_list[pfb->index + 1] = (uintptr_t)pfb;
		mutex_unlock(&ctx->buf_lock);

		mtk_v4l2_debug(1,
				"id=%d Framebuf  pfb=%p VA=%p Y_DMA=%lx C_DMA=%lx ion_buffer=(%p %p) Size=%zx, general_buf DMA=%lx fd=%d ",
			dst_buf->index, pfb,
			pfb->fb_base[0].va,
			(unsigned long)pfb->fb_base[0].dma_addr,
			(unsigned long)pfb->fb_base[1].dma_addr,
			pfb->fb_base[0].dmabuf,
			pfb->fb_base[1].dmabuf, pfb->fb_base[0].size,
			(unsigned long)
			dst_buf_info->frame_buffer.dma_general_addr,
			dst_buf_info->general_user_fd);
	}

	mtk_v4l2_debug(4, "===>[%d] vdec_if_decode() ===>", ctx->id);


	if (src_buf_info->lastframe == EOS) {
		mtk_v4l2_debug(4, "===>[%d] vdec_if_decode() EOS ===> %d %d",
			ctx->id, src_buf->planes[0].bytesused,
			src_buf->planes[0].length);

		memset(&drain_fb, 0, sizeof(struct vdec_fb));
		if (src_buf->planes[0].bytesused == 0)
			drain_fb.status = FB_ST_EOS;
		mtk_vdec_check_vcp_active(ctx, false, true, "worker EOS");
		vdec_if_flush(ctx, NULL, &drain_fb, FLUSH_BITSTREAM | FLUSH_FRAME);

		if (!ctx->input_driven) {
			mtk_vdec_cancel_put_fb_job(ctx);
			mtk_vdec_put_fb(ctx, PUT_BUFFER_WORKER, false);
		}
		v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		src_buf_info->lastframe = NON_EOS;
		clean_free_bs_buffer(ctx, NULL);
		update_src_cnt(ctx);

		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		mutex_unlock(&ctx->worker_lock);
		return;
	} else if (src_buf_info->lastframe == EOS_WITH_DATA &&
		ctx->input_driven == INPUT_DRIVEN_PUT_FRM) {
		ctx->eos_type = EOS_WITH_DATA;
		ctx->early_eos_ts = ctx->input_max_ts;
		mtk_v4l2_debug(4, "===>[%d] vdec_if_decode() early EOS ===> %d %d ts %llu",
			ctx->id, src_buf->planes[0].bytesused,
			src_buf->planes[0].length, ctx->early_eos_ts);
	}

	buf = &src_buf_info->bs_buffer;
	if (mtk_v4l2_dbg_level > 0)
		buf->va = vb2_plane_vaddr(src_buf, 0);
	buf->dma_addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	buf->size = (size_t)src_buf->planes[0].bytesused;
	buf->length = (size_t)src_buf->planes[0].length;
	buf->data_offset = (size_t)src_buf->planes[0].data_offset;
	buf->dmabuf = src_buf->planes[0].dbuf;
	buf->flags = src_vb2_v4l2->flags;
	buf->index = src_buf->index;
	buf->hdr10plus_buf = &src_buf_info->hdr10plus_buf;

	if (buf->va == NULL && buf->dmabuf == NULL) {
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		mtk_v4l2_err("[%d] id=%d src_addr is NULL!!",
					 ctx->id, src_buf->index);
		mutex_unlock(&ctx->worker_lock);
		return;
	}
	ctx->bs_list[buf->index + 1] = (uintptr_t)buf;

	ctx->dec_params.timestamp = src_buf_info->vb.vb2_buf.timestamp;
	mtk_v4l2_debug(1,
	"[%d] Bs VA=%p DMA=%lx Size=%zx Len=%zx ion_buf = %p vb=%p eos=%d pts=%llu",
		ctx->id, buf->va, (unsigned long)buf->dma_addr,
		buf->size, buf->length,
		buf->dmabuf, src_buf,
		src_buf_info->lastframe,
		src_buf_info->vb.vb2_buf.timestamp);
	if (!ctx->input_driven) {
		dst_buf_info->flags &= ~CROP_CHANGED;
		dst_buf_info->flags &= ~REF_FREED;

		dst_buf_info->vb.vb2_buf.timestamp
			= src_buf_info->vb.vb2_buf.timestamp;
		dst_buf_info->vb.timecode
			= src_buf_info->vb.timecode;
	}

	if (src_buf_info->used == false)
		mtk_vdec_ts_insert(ctx, ctx->dec_params.timestamp);
	src_buf_info->used = true;
	mtk_vdec_check_vcp_active(ctx, false, true, "worker");
	mtk_vdec_do_gettimeofday(&worktvstart1);
	ret = vdec_if_decode(ctx, buf, pfb, &src_chg);
	mtk_vdec_do_gettimeofday(&vputvend);
	ts_delta = timespec64_sub(vputvend, worktvstart1);
	mtk_vcodec_perf_log("vpud:%lld (ns)", timespec64_to_ns(&ts_delta));

	res_chg = ((src_chg & VDEC_RES_CHANGE) != 0U) ? true : false;
	need_more_output =
		((src_chg & VDEC_NEED_MORE_OUTPUT_BUF) != 0U) ? true : false;
	mtk_vcodec_unsupport = ((src_chg & VDEC_HW_NOT_SUPPORT) != 0) ?
						   true : false;
	if ((src_chg & VDEC_CROP_CHANGED) &&
		(!ctx->input_driven) && dst_buf_info != NULL)
		dst_buf_info->flags |= CROP_CHANGED;


	if (src_chg & VDEC_OUTPUT_NOT_GENERATED)
		src_vb2_v4l2->flags |= V4L2_BUF_FLAG_OUTPUT_NOT_GENERATED;

	if (ctx->use_fence)
		v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	else if (!ctx->input_driven)
		mtk_vdec_put_fb(ctx, PUT_BUFFER_WORKER, false);

	if (ret < 0 || mtk_vcodec_unsupport) {
		mtk_v4l2_err(
			" <===[%d], src_buf[%d] last_frame = %d sz=0x%zx pts=%llu vdec_if_decode() ret=%d src_chg=%d===>",
			ctx->id,
			src_buf->index,
			src_buf_info->lastframe,
			buf->size,
			src_buf_info->vb.vb2_buf.timestamp,
			ret, src_chg);
		src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		clean_free_bs_buffer(ctx, &src_buf_info->bs_buffer);
		if (ret == -EIO) {
			/* ipi timeout / VPUD crashed ctx abort */
			mtk_vdec_error_handle(ctx, "decode");
			v4l2_m2m_buf_done(&src_buf_info->vb,
				VB2_BUF_STATE_ERROR);
		} else if (mtk_vcodec_unsupport) {
			/*
			 * If cncounter the src unsupport (fatal) during play,
			 * egs: width/height, bitdepth, level, then teturn
			 * error event to user to stop play it
			 */
			mtk_v4l2_err(" <=== [%d] vcodec not support the source!===>",
				ctx->id);
			ctx->state = MTK_STATE_ABORT;
			mtk_vdec_queue_error_event(ctx);
			v4l2_m2m_buf_done(&src_buf_info->vb,
				VB2_BUF_STATE_DONE);
		} else
			v4l2_m2m_buf_done(&src_buf_info->vb,
				VB2_BUF_STATE_ERROR);
	} else if (src_buf_info->lastframe == EOS_WITH_DATA &&
		need_more_output == false) {
		/*
		 * Getting early eos bitstream buffer, after decode this
		 * buffer, need to flush decoder. Use the flush_buf
		 * as normal EOS, and flush decoder.
		 */
		mtk_v4l2_debug(0, "[%d] EarlyEos: decode last frame %d",
			ctx->id, src_buf->planes[0].bytesused);
		src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		src_vb2_v4l2->flags |= V4L2_BUF_FLAG_LAST;
		clean_free_bs_buffer(ctx, NULL);
		if (ctx->dec_flush_buf->lastframe == NON_EOS) {
			ctx->dec_flush_buf->lastframe = EOS;
			ctx->dec_flush_buf->vb.vb2_buf.planes[0].bytesused = 1;
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &ctx->dec_flush_buf->vb);
			update_src_cnt(ctx);
		} else {
			mtk_v4l2_debug(1, "Stopping no need to queue dec_flush_buf.");
		}
	} else if ((ret == 0) && (res_chg == false && need_more_output == false)) {
		/*
		 * we only return src buffer with VB2_BUF_STATE_DONE
		 * when decode success without resolution
		 * change.
		 */
		src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		clean_free_bs_buffer(ctx, NULL);
	} else {    /* res_chg == true || need_more_output == true*/
		clean_free_bs_buffer(ctx, &src_buf_info->bs_buffer);
		mtk_v4l2_debug(1, "Need more capture buffer  r:%d n:%d\n",
			res_chg, need_more_output);
	}

	if (ret == 0 && res_chg) {
		mtk_vdec_pic_info_update(ctx);
		/*
		 * On encountering a resolution change in the stream.
		 * The driver must first process and decode all
		 * remaining buffers from before the resolution change
		 * point, so call flush decode here
		 */
		mtk_vdec_reset_decoder(ctx, 1, NULL, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		if (ctx->input_driven != NON_INPUT_DRIVEN)
			*(ctx->ipi_blocked) = true;
		/*
		 * After all buffers containing decoded frames from
		 * before the resolution change point ready to be
		 * dequeued on the CAPTURE queue, the driver sends a
		 * V4L2_EVENT_SOURCE_CHANGE event for source change
		 * type V4L2_EVENT_SRC_CH_RESOLUTION
		 */
		mtk_vdec_queue_res_chg_event(ctx);
	} else if (ret == 0) {
		ret = vdec_if_get_param(ctx, GET_PARAM_DPB_SIZE, &dpbsize);
		if (dpbsize != 0) {
			ctx->dpb_size = dpbsize;
			ctx->last_dpb_size = dpbsize;
		} else {
			mtk_v4l2_err("[%d] GET_PARAM_DPB_SIZE fail=%d",
				 ctx->id, ret);
		}
		ret = vdec_if_get_param(ctx, GET_PARAM_COLOR_DESC, &color_desc);
		if (ret == 0) {
			ctx->is_hdr = color_desc.is_hdr;
			ctx->last_is_hdr = color_desc.is_hdr;
		} else {
			mtk_v4l2_err("[%d] GET_PARAM_COLOR_DESC fail=%d",
				 ctx->id, ret);
		}
	}

	update_src_cnt(ctx);

	if (ctx->input_driven == NON_INPUT_DRIVEN && ctx->align_mode) {
		pair_cnt = MIN((*ctx->src_cnt), (*ctx->dst_cnt));
		mtk_v4l2_debug(8, "[%d] pair cnt %d(%d,%d) align_type(%d) wait_align(%d) align_start_cnt %d",
			ctx->id, pair_cnt, (*ctx->src_cnt), (*ctx->dst_cnt),
			atomic_read(&ctx->align_type), (*ctx->wait_align), ctx->align_start_cnt);
		if (ctx->align_start_cnt > 0) {
			ctx->align_start_cnt--;
			if (ctx->align_start_cnt == 0) {
				atomic_cmpxchg(&ctx->align_type, VDEC_ALIGN_FULL, VDEC_ALIGN_WAIT);
				(*ctx->wait_align) = true;

				limit_cnt = mtk_vdec_get_align_limit(ctx);
				if (pair_cnt >= limit_cnt) {
					mtk_v4l2_debug(2, "[%d] pair cnt %d(%d,%d) >= %d when align mode, need to set align_type(%d)",
						ctx->id, pair_cnt,
						(*ctx->src_cnt), (*ctx->dst_cnt),
						limit_cnt, atomic_read(&ctx->align_type));
					atomic_cmpxchg(&ctx->align_type,
						VDEC_ALIGN_WAIT, VDEC_ALIGN_FULL); // 0->1
				}
			}
		} else if (pair_cnt == 0) {
			if (atomic_cmpxchg(&ctx->align_type,
				VDEC_ALIGN_FULL, VDEC_ALIGN_WAIT) == VDEC_ALIGN_FULL) // 1->0
				mtk_v4l2_debug(2, "[%d] pair cnt %d(%d,%d) set align_type to WAIT(%d)",
					ctx->id, pair_cnt, (*ctx->src_cnt), (*ctx->dst_cnt),
					VDEC_ALIGN_WAIT);
			if (!(*ctx->wait_align)) {
				mtk_v4l2_debug(2, "[%d] pair cnt %d(%d,%d) wait align",
					ctx->id, pair_cnt, (*ctx->src_cnt), (*ctx->dst_cnt));
				(*ctx->wait_align) = true;
			}
		}
	}

	v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
	mtk_vdec_do_gettimeofday(&vputvend);
	ts_delta = timespec64_sub(vputvend, worktvstart);
	mtk_vcodec_perf_log("worker:%lld (ns)", timespec64_to_ns(&ts_delta));

	mutex_unlock(&ctx->worker_lock);
}

static int vidioc_try_decoder_cmd(struct file *file, void *priv,
	struct v4l2_decoder_cmd *cmd)
{
	switch (cmd->cmd) {
	case V4L2_DEC_CMD_STOP:
		cmd->flags = 0; // don't support flags
		break;
	case V4L2_DEC_CMD_START:
		cmd->flags = 0; // don't support flags
		if (cmd->start.speed < 0)
			cmd->start.speed = 0;
		cmd->start.format = V4L2_DEC_START_FMT_NONE;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_decoder_cmd(struct file *file, void *priv,
	struct v4l2_decoder_cmd *cmd)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *src_vq, *dst_vq;
	int ret;

	ret = vidioc_try_decoder_cmd(file, priv, cmd);
	if (ret)
		return ret;

	mtk_v4l2_debug(1, "decoder cmd= %u", cmd->cmd);
	dst_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	switch (cmd->cmd) {
	case V4L2_DEC_CMD_STOP:
		src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
			V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

		if (ctx->state == MTK_STATE_INIT)
			mtk_vdec_queue_error_event(ctx);

		if (!vb2_is_streaming(src_vq)) {
			mtk_v4l2_debug(1, "Output stream is off. No need to flush.");
			return 0;
		}
		if (!vb2_is_streaming(dst_vq)) {
			mtk_v4l2_debug(1, "Capture stream is off. No need to flush.");
			return 0;
		}
		if (ctx->dec_flush_buf->lastframe == NON_EOS) {
			ctx->dec_flush_buf->lastframe = EOS;
			ctx->dec_flush_buf->vb.vb2_buf.planes[0].bytesused = 0;
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, &ctx->dec_flush_buf->vb);
			update_src_cnt(ctx);
			if (ctx->input_driven == NON_INPUT_DRIVEN && ctx->align_mode) {
				mtk_v4l2_debug(2, "[%d] stop cmd EOS when align mode, need to set align_type(%d) to RESET(%d)",
					ctx->id, atomic_read(&ctx->align_type), VDEC_ALIGN_RESET);
				atomic_set(&ctx->align_type, VDEC_ALIGN_RESET);
			}
			v4l2_m2m_try_schedule(ctx->m2m_ctx);
		} else {
			mtk_v4l2_debug(1, "Stopping no need to queue cmd dec_flush_buf.");
		}
		break;

	case V4L2_DEC_CMD_START:
		vb2_clear_last_buffer_dequeued(dst_vq);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

void mtk_vdec_unlock(struct mtk_vcodec_ctx *ctx, u32 hw_id)
{
	if (hw_id >= MTK_VDEC_HW_NUM)
		return;

	mtk_v4l2_debug(4, "ctx %p [%d] hw_id %d sem_cnt %d",
		ctx, ctx->id, hw_id, ctx->dev->dec_sem[hw_id].count);

	ctx->hw_locked[hw_id] = 0;
	up(&ctx->dev->dec_sem[hw_id]);
}

int mtk_vdec_lock(struct mtk_vcodec_ctx *ctx, u32 hw_id)
{
	int ret = -1;

	if (hw_id >= MTK_VDEC_HW_NUM)
		return -1;

	mtk_v4l2_debug(4, "ctx %p [%d] hw_id %d sem_cnt %d",
		ctx, ctx->id, hw_id, ctx->dev->dec_sem[hw_id].count);

	if (mtk_vcodec_is_vcp(MTK_INST_DECODER)) {
		down(&ctx->dev->dec_sem[hw_id]);
		ret = 0;
	} else {
		while (ret != 0)
			ret = down_interruptible(&ctx->dev->dec_sem[hw_id]);
	}

	ctx->hw_locked[hw_id] = 1;

	return ret;
}

void mtk_vcodec_dec_empty_queues(struct file *file, struct mtk_vcodec_ctx *ctx)
{
	struct vb2_buffer *dst_buf = NULL;
	int i = 0;
	struct v4l2_fh *fh = file->private_data;
	struct vb2_v4l2_buffer *dst_vb2_v4l2, *src_vb2_v4l2;

	// error handle for release before stream-off
	// stream off both queue mannually.
	v4l2_m2m_streamoff(file, fh->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	v4l2_m2m_streamoff(file, fh->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	while ((src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx)))
		if (src_vb2_v4l2 != &ctx->dec_flush_buf->vb &&
			src_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(src_vb2_v4l2, VB2_BUF_STATE_ERROR);

	while ((dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))) {
		dst_buf = &dst_vb2_v4l2->vb2_buf;

		for (i = 0; i < dst_buf->num_planes; i++)
			vb2_set_plane_payload(dst_buf, i, 0);

		if (dst_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_ERROR);
	}

	ctx->state = MTK_STATE_FREE;
}

void mtk_vcodec_dec_release(struct mtk_vcodec_ctx *ctx)
{
	mtk_vdec_deinit_set_frame_wq(ctx);
	vdec_if_deinit(ctx);
	vdec_check_release_lock(ctx);
}

static bool mtk_vdec_dvfs_params_change(struct mtk_vcodec_ctx *ctx)
{
	bool need_update = false;

	if (ctx->state == MTK_STATE_HEADER) {
		if (!ctx->is_active)
			need_update = true;

		if (ctx->dec_param_change & MTK_DEC_PARAM_OPERATING_RATE) {
			need_update = true;
			ctx->dec_param_change &= (~MTK_DEC_PARAM_OPERATING_RATE);
		}

	}

	return need_update;
}

/*
 * check decode ctx is active or not
 * active: ctx is decoding frame
 * inactive: ctx is not decoding frame, which is in the background
 * timer pushes this work to workqueue
 */
void mtk_vdec_check_alive_work(struct work_struct *ws)
{
	struct mtk_vcodec_dev *dev = NULL;
	struct mtk_vcodec_ctx *ctx = NULL;
	struct vcodec_inst *inst;
	struct list_head *item;
	bool mmdvfs_in_vcp, need_update = false;
	struct vdec_check_alive_work_struct *caws;
	unsigned long vcp_dvfs_data[1] = {0};

	caws = container_of(ws, struct vdec_check_alive_work_struct, work);
	dev = caws->dev;

	mutex_lock(&dev->check_alive_mutex);

#if VDEC_VCP_BACKGROUND_IDLE
	/* vcp background idle check */
	mutex_lock(&dev->ctx_mutex);
	if (list_empty(&dev->ctx_list) || dev->is_codec_suspending == 1) {
		mutex_unlock(&dev->ctx_mutex);
		mutex_unlock(&dev->check_alive_mutex);
		if (caws->ctx != NULL)
			kfree(caws);
		return;
	}

	list_for_each(item, &dev->ctx_list) {
		ctx = list_entry(item, struct mtk_vcodec_ctx, list);
		mutex_lock(&ctx->vcp_active_mutex);
		mtk_v4l2_debug(8, "ctx:%d, is_vcp_active:%d, vcp_action_cnt:%d, last_vcp_action_cnt:%d",
			ctx->id, ctx->is_vcp_active, ctx->vcp_action_cnt,
			ctx->last_vcp_action_cnt);
		if (ctx->state == MTK_STATE_HEADER) {
			if (ctx->last_vcp_action_cnt == ctx->vcp_action_cnt)
				mtk_vdec_check_vcp_inactive(ctx, true);
			else {
				mtk_vdec_check_vcp_active(ctx, true, false, "timer");
				ctx->last_vcp_action_cnt = ctx->vcp_action_cnt;
			}
		} else if (ctx->state == MTK_STATE_ABORT)
			mtk_v4l2_err("ctx:%d is abort", ctx->id);
		mutex_unlock(&ctx->vcp_active_mutex);
	}
	mutex_unlock(&dev->ctx_mutex);

#endif

	mmdvfs_in_vcp = (dev->vdec_reg == 0 && dev->vdec_mmdvfs_clk == 0);

	mutex_lock(&dev->dec_dvfs_mutex);

	if (list_empty(&dev->vdec_dvfs_inst) || dev->is_codec_suspending == 1) {
		if (caws->ctx != NULL)
			kfree(caws);
		mutex_unlock(&dev->dec_dvfs_mutex);
		mutex_unlock(&dev->check_alive_mutex);
		return;
	}

	if (caws->ctx != NULL) { // ctx retrigger case
		ctx = caws->ctx;

		// cur ctx should be in dvfs list, check firstly to avoid ctx UAF
		list_for_each(item, &dev->vdec_dvfs_inst) {
			inst = list_entry(item, struct vcodec_inst, list);
			if (inst->ctx == ctx)
				need_update = true;
		}

		if (!need_update) {
			mtk_v4l2_debug(0, "[VDVFS] %s ctx [%p] is invalid, skip dvfs check",
				__func__, ctx);
			kfree(caws);
			mutex_unlock(&dev->dec_dvfs_mutex);
			mutex_unlock(&dev->check_alive_mutex);
			return;
		}

		mutex_lock(&ctx->vcp_active_mutex);
		if (ctx->is_vcp_active) {
			ctx->is_active = 1;
			mtk_vdec_dvfs_update_dvfs_params(ctx);
			kfree(caws);
			mtk_v4l2_debug(0, "[VDVFS] %s [%d] is active/update_params now", __func__, ctx->id);
		} else {
			mtk_v4l2_debug(4, "[VDVFS] [%d] vcp inactive, skip dvfs check", ctx->id);
			kfree(caws);
			mutex_unlock(&ctx->vcp_active_mutex);
			mutex_unlock(&dev->dec_dvfs_mutex);
			mutex_unlock(&dev->check_alive_mutex);
			return;
		}
		mutex_unlock(&ctx->vcp_active_mutex);
	} else { // timer trigger case
		list_for_each(item, &dev->vdec_dvfs_inst) {
		inst = list_entry(item, struct vcodec_inst, list);
		ctx = inst->ctx;
		mutex_lock(&ctx->vcp_active_mutex);
		mtk_v4l2_debug(8, "[VDVFS] ctx:%d, active:%d, dec_cnt:%d, last_dec_cnt:%d, vcp_active:%d",
			ctx->id, ctx->is_active, ctx->decoded_frame_cnt,
			ctx->last_decoded_frame_cnt, ctx->is_vcp_active);
		if (ctx->state != MTK_STATE_ABORT) {
			if (ctx->is_vcp_active) {
				if (ctx->last_decoded_frame_cnt >= ctx->decoded_frame_cnt) {
					if (ctx->is_active) {
						need_update = true;
						ctx->is_active = 0;
						mtk_vdec_dvfs_update_dvfs_params(ctx);
						mtk_v4l2_debug(0, "[VDVFS] ctx %d inactive",
							ctx->id);
					}
				} else {
					if (!ctx->is_active) {
						need_update = true;
						ctx->is_active = 1;
						mtk_vdec_dvfs_update_dvfs_params(ctx);
						mtk_v4l2_debug(0, "[VDVFS] ctx %d active", ctx->id);
					}
					ctx->last_decoded_frame_cnt = ctx->decoded_frame_cnt;
				}
			} else
				mtk_v4l2_debug(4, "[VDVFS] [%d] vcp inactive, skip dvfs check",
					ctx->id);
		} else
			mtk_v4l2_err("[VDVFS] %s ctx: %d is abort", __func__, ctx->id);
		mutex_unlock(&ctx->vcp_active_mutex);
		}
	}

	if (need_update) {
		if (mmdvfs_in_vcp) {
			vcp_dvfs_data[0] = MTK_INST_SET;
			if (!mtk_vdec_dvfs_is_pw_always_on(dev))
				mtk_vcodec_dec_pw_on(&dev->pm);
			if (vdec_if_set_param(ctx, SET_PARAM_MMDVFS, vcp_dvfs_data) != 0) {
				mtk_v4l2_err("[VDVFS][%d] alive ipi fail", ctx->id);
				ctx->state = MTK_STATE_ABORT;
				mtk_vdec_dvfs_reset_AO_flag(dev);
			} // any ctx
			mtk_vdec_dvfs_sync_vsi_data(ctx);
			mtk_v4l2_debug(0, "[VDVFS] check alive: freq: %d, h_l: %d, op: %d",
				ctx->dev->vdec_dvfs_params.target_freq,
				ctx->dev->vdec_dvfs_params.high_loading_scenario,
				ctx->dec_params.operating_rate);
			if (!mtk_vdec_dvfs_is_pw_always_on(dev))
				mtk_vcodec_dec_pw_off(&dev->pm);
		} else {
			mtk_vdec_force_update_freq(dev);
		}
		mtk_vdec_pmqos_begin_inst(ctx);
	}

	mutex_unlock(&dev->dec_dvfs_mutex);
	mutex_unlock(&dev->check_alive_mutex);
}

void mtk_vcodec_dec_set_default_params(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_q_data *q_data;

	ctx->m2m_ctx->q_lock = &ctx->q_mutex;
	ctx->fh.m2m_ctx = ctx->m2m_ctx;
	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	INIT_WORK(&ctx->decode_work, mtk_vdec_worker);
	ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->quantization = V4L2_QUANTIZATION_DEFAULT;
	ctx->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	get_supported_format(ctx);
	get_supported_framesizes(ctx);

#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	if (mtk_vcodec_is_vcp(MTK_INST_DECODER)) {
		set_vdec_vcp_data(ctx, VDEC_VCP_LOG_INFO_ID);
		set_vdec_vcp_data(ctx, VDEC_SET_PROP_MEM_ID);
	}
#endif

	q_data = &ctx->q_data[MTK_Q_DATA_SRC];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->visible_width = DFT_CFG_WIDTH;
	q_data->visible_height = DFT_CFG_HEIGHT;
	if (default_out_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT)
		q_data->fmt = &mtk_vdec_formats[default_out_fmt_idx];
	q_data->field = V4L2_FIELD_NONE;

	q_data->sizeimage[0] = DFT_CFG_WIDTH * DFT_CFG_HEIGHT;
	q_data->bytesperline[0] = 0;

	q_data = &ctx->q_data[MTK_Q_DATA_DST];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->visible_width = DFT_CFG_WIDTH;
	q_data->visible_height = DFT_CFG_HEIGHT;
	q_data->coded_width = DFT_CFG_WIDTH;
	q_data->coded_height = DFT_CFG_HEIGHT;
	if (default_cap_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT)
		q_data->fmt = &mtk_vdec_formats[default_cap_fmt_idx];
	q_data->field = V4L2_FIELD_NONE;

	v4l_bound_align_image(&q_data->coded_width,
						  MTK_VDEC_MIN_W,
						  MTK_VDEC_MAX_W, 4,
						  &q_data->coded_height,
						  MTK_VDEC_MIN_H,
						  MTK_VDEC_MAX_H, 5, 6);

	if (q_data->fmt->num_planes == 1) {
		q_data->sizeimage[0] =
			q_data->coded_width * q_data->coded_height * 3/2;
		q_data->bytesperline[0] = q_data->coded_width;

	} else if (q_data->fmt->num_planes == 2) {
		q_data->sizeimage[0] =
			q_data->coded_width * q_data->coded_height;
		q_data->bytesperline[0] = q_data->coded_width;
		q_data->sizeimage[1] = q_data->sizeimage[0] / 2;
		q_data->bytesperline[1] = q_data->coded_width;
	}
}

static int mtk_vdec_set_param(struct mtk_vcodec_ctx *ctx)
{
	unsigned long in[8] = {0};

	mtk_v4l2_debug(4,
		"[%d] param change 0x%x decode mode %d frame width %d frame height %d max width %d max height %d",
		ctx->id, ctx->dec_param_change,
		ctx->dec_params.decode_mode,
		ctx->dec_params.frame_size_width,
		ctx->dec_params.frame_size_height,
		ctx->dec_params.fixed_max_frame_size_width,
		ctx->dec_params.fixed_max_frame_size_height);

	if (ctx->dec_param_change & MTK_DEC_PARAM_DECODE_MODE) {
		in[0] = ctx->dec_params.decode_mode;
		if (vdec_if_set_param(ctx, SET_PARAM_DECODE_MODE, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_param_change &= (~MTK_DEC_PARAM_DECODE_MODE);
	}

	if (ctx->dec_param_change & MTK_DEC_PARAM_FRAME_SIZE) {
		in[0] = ctx->dec_params.frame_size_width;
		in[1] = ctx->dec_params.frame_size_height;
		if (in[0] != 0 && in[1] != 0) {
			if (vdec_if_set_param(ctx,
				SET_PARAM_FRAME_SIZE, in) != 0) {
				mtk_v4l2_err("[%d] Error!! Cannot set param",
					ctx->id);
				return -EINVAL;
			}
		}
		ctx->dec_param_change &= (~MTK_DEC_PARAM_FRAME_SIZE);
	}

	if (ctx->dec_param_change &
		MTK_DEC_PARAM_FIXED_MAX_FRAME_SIZE) {
		in[0] = ctx->dec_params.fixed_max_frame_size_width;
		in[1] = ctx->dec_params.fixed_max_frame_size_height;
		in[2] = ctx->dec_params.fixed_max_frame_buffer_mode;
		if (in[0] != 0 && in[1] != 0) {
			if (vdec_if_set_param(ctx,
				SET_PARAM_SET_FIXED_MAX_OUTPUT_BUFFER,
				in) != 0) {
				mtk_v4l2_err("[%d] Error!! Cannot set param",
					ctx->id);
				return -EINVAL;
			}
		}
		ctx->dec_param_change &= (~MTK_DEC_PARAM_FIXED_MAX_FRAME_SIZE);
	}

	if (ctx->dec_param_change & MTK_DEC_PARAM_CRC_PATH) {
		in[0] = (unsigned long)ctx->dec_params.crc_path;
		if (vdec_if_set_param(ctx, SET_PARAM_CRC_PATH, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_param_change &= (~MTK_DEC_PARAM_CRC_PATH);
	}

	if (ctx->dec_param_change & MTK_DEC_PARAM_GOLDEN_PATH) {
		in[0] = (unsigned long)ctx->dec_params.golden_path;
		if (vdec_if_set_param(ctx, SET_PARAM_GOLDEN_PATH, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_param_change &= (~MTK_DEC_PARAM_GOLDEN_PATH);
	}

	if (ctx->dec_param_change & MTK_DEC_PARAM_WAIT_KEY_FRAME) {
		in[0] = (unsigned long)ctx->dec_params.wait_key_frame;
		if (vdec_if_set_param(ctx, SET_PARAM_WAIT_KEY_FRAME, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_param_change &= (~MTK_DEC_PARAM_WAIT_KEY_FRAME);
	}

	if (ctx->dec_param_change & MTK_DEC_PARAM_DECODE_ERROR_HANDLE_MODE) {
		in[0] = (unsigned long)ctx->dec_params.decode_error_handle_mode;
		if (vdec_if_set_param(ctx, SET_PARAM_DECODE_ERROR_HANDLE_MODE, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_param_change &= (~MTK_DEC_PARAM_DECODE_ERROR_HANDLE_MODE);
	}

	if (ctx->dec_param_change & MTK_DEC_PARAM_NAL_SIZE_LENGTH) {
		in[0] = (unsigned long)ctx->dec_params.wait_key_frame;
		if (vdec_if_set_param(ctx, SET_PARAM_NAL_SIZE_LENGTH,
					in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_param_change &= (~MTK_DEC_PARAM_NAL_SIZE_LENGTH);
	}

	if (ctx->dec_param_change & MTK_DEC_PARAM_OPERATING_RATE) {
		in[0] = (unsigned long)ctx->dec_params.operating_rate;
		if (vdec_if_set_param(ctx, SET_PARAM_OPERATING_RATE, in) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot set param", ctx->id);
			return -EINVAL;
		}
		ctx->dec_param_change &= (~MTK_DEC_PARAM_OPERATING_RATE);
	}

	if (vdec_if_get_param(ctx, GET_PARAM_INPUT_DRIVEN,
		&ctx->input_driven)) {
		mtk_v4l2_err("[%d] Error!! Cannot get param : GET_PARAM_INPUT_DRIVEN ERR",
					 ctx->id);
		return -EINVAL;
	}

	if (vdec_if_get_param(ctx, GET_PARAM_ALIGN_MODE,
		&ctx->align_mode)) {
		mtk_v4l2_err("[%d] Error!! Cannot get param : GET_PARAM_ALIGN_MODE ERR",
					 ctx->id);
		return -EINVAL;
	}

	mtk_v4l2_debug(4, "[%d] input_driven %d ipi_blocked %d, align_mode %d align_type %d",
		ctx->id, ctx->input_driven, *(ctx->ipi_blocked),
		ctx->align_mode, atomic_read(&ctx->align_type));

	return 0;
}

static int vidioc_vdec_qbuf(struct file *file, void *priv,
							struct v4l2_buffer *buf)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct vb2_buffer *vb;
	struct mtk_video_dec_buf *mtkbuf;
	struct vb2_v4l2_buffer  *vb2_v4l2;
#if ENABLE_META_BUF
	void *general_buf_va = NULL;
	int *pFenceFd;
	int metaBufferfd = -1;
#endif

	if (ctx->state == MTK_STATE_ABORT) {
		mtk_v4l2_err("[%d] Call on QBUF after unrecoverable error",
					 ctx->id);
		return -EIO;
	}

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, buf->type);
	if (buf->index >= vq->num_buffers) {
		mtk_v4l2_err("[%d] buffer index %d out of range %d",
			ctx->id, buf->index, vq->num_buffers);
		return -EINVAL;
	}

	vb = vq->bufs[buf->index];
	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	mtkbuf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);

	if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		memcpy(&mtkbuf->hdr10plus_buf, &ctx->dec_params.hdr10plus_buf,
			sizeof(struct hdr10plus_info));
		memset(&ctx->dec_params.hdr10plus_buf, 0,
			sizeof(struct hdr10plus_info)); // invalidate
		ctx->input_max_ts =
			(timeval_to_ns(&buf->timestamp) > ctx->input_max_ts) ?
			timeval_to_ns(&buf->timestamp) : ctx->input_max_ts;
		if (IS_ERR_OR_NULL(buf->m.planes)) {
			mtk_v4l2_err("[%d] buffer planes address %p %llx can not access",
				ctx->id, buf->m.planes, buf->m.planes);
			return -EIO;
		}
		if (buf->m.planes[0].bytesused == 0) {
			mtkbuf->lastframe = EOS;
			mtk_v4l2_debug(1, "[%d] index=%d Eos BS(%d,%d) vb=%p pts=%llu",
				ctx->id, buf->index,
				buf->bytesused,
				buf->length, vb,
				timeval_to_ns(&buf->timestamp));
			if (ctx->state == MTK_STATE_INIT)
				mtk_vdec_queue_error_event(ctx);
		} else if (buf->flags & V4L2_BUF_FLAG_LAST) {
			mtkbuf->lastframe = EOS_WITH_DATA;
			mtk_v4l2_debug(1, "[%d] id=%d EarlyEos BS(%d,%d) vb=%p pts=%llu",
				ctx->id, buf->index, buf->m.planes[0].bytesused,
				buf->length, vb,
				timeval_to_ns(&buf->timestamp));
		} else {
			mtkbuf->lastframe = NON_EOS;
			mtk_v4l2_debug(1, "[%d] id=%d getdata BS(%d,%d) vb=%p pts=%llu %llu",
				ctx->id, buf->index,
				buf->m.planes[0].bytesused,
				buf->length, vb,
				timeval_to_ns(&buf->timestamp),
				ctx->input_max_ts);
		}
	} else {
		if (buf->reserved == 0xFFFFFFFF)
			mtkbuf->general_user_fd = -1;
		else
			mtkbuf->general_user_fd = (int)buf->reserved;
		mtk_v4l2_debug(1, "[%d] id=%d FB (%d) vb=%p, general_buf_fd=%d, mtkbuf->general_buf_fd = %d",
				ctx->id, buf->index,
				buf->length, mtkbuf,
				buf->reserved, mtkbuf->general_user_fd);

		if (mtkbuf->general_user_fd != -1)
			get_general_buffer_va(ctx, mtkbuf->general_user_fd);
#if ENABLE_META_BUF
		mutex_lock(&ctx->gen_buf_va_lock);
		mtkbuf->general_dma_va = NULL;
		if (mtkbuf->general_user_fd != -1) {
			// NOTE: all codec should use a common struct
			//to save dma_va, fencefd should be
			//the 1st member in struct of general buffer
			general_buf_va = get_general_buffer_va(ctx,
				mtkbuf->general_user_fd);
			pFenceFd = (int *)general_buf_va;
			if (general_buf_va != NULL && *pFenceFd == 1 &&
				ctx->dec_params.svp_mode == 0) {
				ctx->use_fence = true;
				mtkbuf->general_dma_va = general_buf_va;
			} else if (general_buf_va != NULL)
				*pFenceFd = -1;
		}
		mtkbuf->meta_user_fd = -1; //default value is -1
		if (mtkbuf->general_user_fd != -1 && general_buf_va != NULL) {
			// metaBufferfd should be
			//the 2st member in struct of general buffer
			metaBufferfd = *((int *)general_buf_va + 1);
			if (metaBufferfd > 0)
				mtkbuf->meta_user_fd = metaBufferfd;
			mtk_v4l2_debug(1, "[%d] id=%d FB (%d) general_buf_va=%p, metaBufferfd=%d, meta_user_fd = %d",
				ctx->id, buf->index,
				buf->length, general_buf_va,
				metaBufferfd, mtkbuf->meta_user_fd);
		}
		mutex_unlock(&ctx->gen_buf_va_lock);
#endif
	}

	if (buf->flags & V4L2_BUF_FLAG_NO_CACHE_CLEAN) {
		mtk_v4l2_debug(4, "[%d] No need for Cache clean, buf->index:%d. mtkbuf:%p",
			ctx->id, buf->index, mtkbuf);
		mtkbuf->flags |= NO_CAHCE_CLEAN;
	}

	if (buf->flags & V4L2_BUF_FLAG_NO_CACHE_INVALIDATE) {
		mtk_v4l2_debug(4, "[%d] No need for Cache invalidate, buf->index:%d. mtkbuf:%p",
			ctx->id, buf->index, mtkbuf);
		mtkbuf->flags |= NO_CAHCE_INVALIDATE;
	}

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_vdec_dqbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct vb2_buffer *vb;
	struct mtk_video_dec_buf *mtkbuf;
	struct vb2_v4l2_buffer  *vb2_v4l2;

	if (ctx->state == MTK_STATE_ABORT) {
		mtk_v4l2_debug(4, "[%d] Call on DQBUF after unrecoverable error",
					 ctx->id);
		return -EIO;
	}

	ret = v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
	if (ctx->errormap_info[buf->index % VB2_MAX_FRAME])
		buf->flags |= V4L2_BUF_FLAG_ERROR;

	if (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
		ret == 0) {
		vq = v4l2_m2m_get_vq(ctx->m2m_ctx, buf->type);
		if (buf->index >= vq->num_buffers) {
			mtk_v4l2_err("[%d] buffer index %d out of range %d",
				ctx->id, buf->index, vq->num_buffers);
			return -EINVAL;
		}
		vb = vq->bufs[buf->index];
		vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
		mtkbuf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);

		if (mtkbuf->flags & CROP_CHANGED)
			buf->flags |= V4L2_BUF_FLAG_CROP_CHANGED;
		if (mtkbuf->flags & REF_FREED)
			buf->flags |= V4L2_BUF_FLAG_REF_FREED;
		if (mtkbuf->general_user_fd < 0)
			buf->reserved = 0xFFFFFFFF;
		else
			buf->reserved = mtkbuf->general_user_fd;
		mtk_v4l2_debug(2,
			"dqbuf index %d mtkbuf->general_buf_fd = %d, flags 0x%x(0x%x)",
			buf->index, mtkbuf->general_user_fd,
			buf->flags, mtkbuf->flags);

		if (ctx->use_fence && mtkbuf->ready_to_display
			&& mtkbuf->general_dma_va) {
#if ENABLE_FENCE
			int *dma_va = mtkbuf->general_dma_va;
			//create fence
			struct mtk_sync_create_fence_data f_data = {0};

			mutex_lock(&ctx->gen_buf_va_lock);
			if (ctx->state == MTK_STATE_FLUSH) {
				mutex_unlock(&ctx->gen_buf_va_lock);
				mtk_v4l2_debug(2, "invalid dma_va %p!\n",
					dma_va);
				return ret;
			}

			f_data.value = ctx->fence_idx;
			sprintf(f_data.name, "vdec_fence%d",
				f_data.value);
			ret = fence_create(ctx->p_timeline_obj,
				&f_data);
			mtk_v4l2_debug(2, "fence_create id %d ret %d",
				ctx->fence_idx, ret);
			if (ret == 0) {
				*dma_va = f_data.fence;
				mtk_v4l2_debug(2, "save fence fd %d  %d at %p disp %d used %d",
					f_data.fence, f_data.value, dma_va,
					mtkbuf->ready_to_display, mtkbuf->used);
				++ctx->fence_idx;
			}
			mutex_unlock(&ctx->gen_buf_va_lock);
#endif
		}
	}

	return ret;
}

static int vidioc_vdec_querycap(struct file *file, void *priv,
	struct v4l2_capability *cap)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_vcodec_dev *dev = ctx->dev;

	strscpy(cap->driver, MTK_VCODEC_DEC_NAME, sizeof(cap->driver));
	strscpy(cap->bus_info, dev->platform, sizeof(cap->bus_info));
	strscpy(cap->card, dev->platform, sizeof(cap->card));

	cap->device_caps  = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_vdec_subscribe_evt(struct v4l2_fh *fh,
	const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 2, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	case V4L2_EVENT_MTK_VDEC_ERROR:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_MTK_VDEC_NOHEADER:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static int vidioc_try_fmt(struct v4l2_format *f, struct mtk_video_fmt *fmt)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = NULL;
	unsigned int i;

	if (IS_ERR_OR_NULL(fmt)) {
		mtk_v4l2_err("fail to get mtk_video_fmt");
		return -EINVAL;
	}
	pix_fmt_mp = &f->fmt.pix_mp;
	pix_fmt_mp->field = V4L2_FIELD_NONE;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		pix_fmt_mp->num_planes = 1;
		pix_fmt_mp->plane_fmt[0].bytesperline = 0;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		int tmp_w, tmp_h;

		pix_fmt_mp->height = clamp(pix_fmt_mp->height,
			MTK_VDEC_MIN_H,
			MTK_VDEC_MAX_H);
		pix_fmt_mp->width = clamp(pix_fmt_mp->width,
			MTK_VDEC_MIN_W,
			MTK_VDEC_MAX_W);

		/*
		 * Find next closer width align 64, heign align 64, size align
		 * 64 rectangle
		 * Note: This only get default value, the real HW needed value
		 *       only available when ctx in MTK_STATE_HEADER state
		 */
		tmp_w = pix_fmt_mp->width;
		tmp_h = pix_fmt_mp->height;
		v4l_bound_align_image(&pix_fmt_mp->width,
							  MTK_VDEC_MIN_W,
							  MTK_VDEC_MAX_W, 6,
							  &pix_fmt_mp->height,
							  MTK_VDEC_MIN_H,
							  MTK_VDEC_MAX_H, 6, 9);

		if (pix_fmt_mp->width < tmp_w &&
			(pix_fmt_mp->width + 64) <= MTK_VDEC_MAX_W)
			pix_fmt_mp->width += 64;
		if (pix_fmt_mp->height < tmp_h &&
			(pix_fmt_mp->height + 64) <= MTK_VDEC_MAX_H)
			pix_fmt_mp->height += 64;

		mtk_v4l2_debug(0,
			"before resize width=%d, height=%d, after resize width=%d, height=%d, sizeimage=%d",
			tmp_w, tmp_h, pix_fmt_mp->width,
			pix_fmt_mp->height,
			pix_fmt_mp->width * pix_fmt_mp->height);

		if (fmt->num_planes > 2)
			pix_fmt_mp->num_planes = 2;
		else
			pix_fmt_mp->num_planes = fmt->num_planes;

		pix_fmt_mp->plane_fmt[0].sizeimage =
				pix_fmt_mp->width * pix_fmt_mp->height;
		pix_fmt_mp->plane_fmt[0].bytesperline = pix_fmt_mp->width;

		if (pix_fmt_mp->num_planes == 2) {
			pix_fmt_mp->plane_fmt[1].sizeimage =
				(pix_fmt_mp->width * pix_fmt_mp->height) / 2;
			pix_fmt_mp->plane_fmt[1].bytesperline =
				pix_fmt_mp->width;
		} else if (pix_fmt_mp->num_planes == 1) {
			pix_fmt_mp->plane_fmt[0].sizeimage +=
				(pix_fmt_mp->width * pix_fmt_mp->height) / 2;
		}

	}

	for (i = 0; i < pix_fmt_mp->num_planes; i++)
		memset(&(pix_fmt_mp->plane_fmt[i].reserved[0]), 0x0,
			   sizeof(pix_fmt_mp->plane_fmt[0].reserved));

	pix_fmt_mp->flags = 0;
	memset(&pix_fmt_mp->reserved, 0x0, sizeof(pix_fmt_mp->reserved));
	return 0;
}

static int vidioc_try_fmt_vid_cap_mplane(struct file *file, void *priv,
	struct v4l2_format *f)
{
	struct mtk_video_fmt *fmt;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_FRAME);
	if (!fmt && default_cap_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT) {
		f->fmt.pix.pixelformat =
			mtk_vdec_formats[default_cap_fmt_idx].fourcc;
		fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_FRAME);
	}
	if (!fmt)
		return -EINVAL;

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_try_fmt_vid_out_mplane(struct file *file, void *priv,
	struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	struct mtk_video_fmt *fmt;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_DEC);
	if (!fmt && default_out_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT) {
		f->fmt.pix.pixelformat =
			mtk_vdec_formats[default_out_fmt_idx].fourcc;
		fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_DEC);
	}

	if (pix_fmt_mp->plane_fmt[0].sizeimage == 0) {
		mtk_v4l2_err("sizeimage of output format must be given");
		return -EINVAL;
	}
	if (!fmt)
		return -EINVAL;

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_vdec_g_selection(struct file *file, void *priv,
	struct v4l2_selection *s)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_q_data *q_data;

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	q_data = &ctx->q_data[MTK_Q_DATA_DST];

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = ctx->picinfo.pic_w;
		s->r.height = ctx->picinfo.pic_h;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = ctx->picinfo.buf_w;
		s->r.height = ctx->picinfo.buf_h;
		break;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_CROP:
		if (vdec_if_get_param(ctx, GET_PARAM_CROP_INFO, &(s->r))) {
			/* set to default value if header info not ready yet*/
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = q_data->visible_width;
			s->r.height = q_data->visible_height;
		}
		break;
	default:
		return -EINVAL;
	}

	if (ctx->state < MTK_STATE_HEADER) {
		/* set to default value if header info not ready yet*/
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = q_data->visible_width;
		s->r.height = q_data->visible_height;
		return 0;
	}

	return 0;
}

static int vidioc_vdec_s_selection(struct file *file, void *priv,
	struct v4l2_selection *s)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_CROP:
		return vdec_if_set_param(ctx, SET_PARAM_CROP_INFO, &s->r);
	default:
		return -EINVAL;
	}

	return 0;
}

#ifdef TV_INTEGRATION
static int vidioc_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_captureparm *cp = &parm->parm.capture;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	if (ctx->state < MTK_STATE_HEADER) {
		mtk_v4l2_err("can't get parm at state %d", ctx->state);
		return -EINVAL;
	}

	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;

	if (vdec_if_get_param(ctx, GET_PARAM_FRAME_INTERVAL,
			&cp->timeperframe) != 0) {
		mtk_v4l2_err("[%d] Error!! Cannot get frame rate",
			ctx->id);
		return -EINVAL;
	}

	return 0;
}
#endif

static int vidioc_vdec_s_fmt(struct file *file, void *priv,
							 struct v4l2_format *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format_mplane *pix_mp;
	struct mtk_q_data *q_data;
	int ret = 0;
	struct mtk_video_fmt *fmt;

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	mtk_v4l2_debug(4, "[%d] type %d", ctx->id, f->type);

	q_data = mtk_vdec_get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	pix_mp = &f->fmt.pix_mp;
	if ((f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) &&
		vb2_is_busy(&ctx->m2m_ctx->out_q_ctx.q)) {
		mtk_v4l2_err("out_q_ctx buffers already requested");
		ret = -EBUSY;
	}

	if ((f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) &&
		vb2_is_busy(&ctx->m2m_ctx->cap_q_ctx.q)) {
		// can change back to mtk_v4l2_err after c2 flow fix
		mtk_v4l2_debug(1, "cap_q_ctx buffers already requested");
		ret = -EBUSY;
	}

	fmt = mtk_vdec_find_format(ctx, f,
		(f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ?
		MTK_FMT_DEC : MTK_FMT_FRAME);
	if (fmt == NULL) {
		if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
			&& default_out_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT) {
			f->fmt.pix.pixelformat =
				mtk_vdec_formats[default_out_fmt_idx].fourcc;
			fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_DEC);
		} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
			&& default_cap_fmt_idx < MTK_MAX_DEC_CODECS_SUPPORT) {
			f->fmt.pix.pixelformat =
				mtk_vdec_formats[default_cap_fmt_idx].fourcc;
			fmt = mtk_vdec_find_format(ctx, f, MTK_FMT_FRAME);
		}
	}
	if (!fmt)
		return -EINVAL;

	q_data->fmt = fmt;
	vidioc_try_fmt(f, q_data->fmt);

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		q_data->sizeimage[0] = pix_mp->plane_fmt[0].sizeimage;
		q_data->coded_width = pix_mp->width;
		q_data->coded_height = pix_mp->height;

		ctx->colorspace = f->fmt.pix_mp.colorspace;
		ctx->ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		ctx->quantization = f->fmt.pix_mp.quantization;
		ctx->xfer_func = f->fmt.pix_mp.xfer_func;
	}

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		vdec_if_set_param(ctx, SET_PARAM_FB_NUM_PLANES,
			(void *) &q_data->fmt->num_planes);

	return 0;
}

#ifdef TV_INTEGRATION
static int vidioc_enum_frameintervals(struct file *file, void *priv,
	struct v4l2_frmivalenum *fival)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_video_frame_frameintervals frameintervals;

	if (fival->index != 0)
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
	frameintervals.fourcc = fival->pixel_format;
	frameintervals.width = fival->width;
	frameintervals.height = fival->height;

	if (vdec_if_get_param(ctx, GET_PARAM_CAPABILITY_FRAMEINTERVALS,
		&frameintervals) != 0) {
		mtk_v4l2_err("[%d] Error!! Cannot get frame interval", ctx->id);
		return -EINVAL;
	}

	fival->stepwise = frameintervals.stepwise;
	mtk_v4l2_debug(1,
		"vdec frm_interval fourcc %d width %d height %d max %d/%d min %d/%d step %d/%d\n",
		fival->pixel_format,
		fival->width,
		fival->height,
		fival->stepwise.max.numerator,
		fival->stepwise.max.denominator,
		fival->stepwise.min.numerator,
		fival->stepwise.min.denominator,
		fival->stepwise.step.numerator,
		fival->stepwise.step.denominator);

	return 0;
}
#endif

static int vidioc_enum_framesizes(struct file *file, void *priv,
	struct v4l2_frmsizeenum *fsize)
{
	int i = 0;

	if (fsize->index != 0)
		return -EINVAL;

	for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT &&
		 mtk_vdec_framesizes[i].fourcc != 0; ++i) {
		if (fsize->pixel_format != mtk_vdec_framesizes[i].fourcc)
			continue;

		fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
		fsize->reserved[0] = mtk_vdec_framesizes[i].profile;
		fsize->reserved[1] = mtk_vdec_framesizes[i].level;
		fsize->stepwise = mtk_vdec_framesizes[i].stepwise;

		mtk_v4l2_debug(1, "%d %d %d %d %d %d %d %d",
					   fsize->stepwise.min_width,
					   fsize->stepwise.max_width,
					   fsize->stepwise.step_width,
					   fsize->stepwise.min_height,
					   fsize->stepwise.max_height,
					   fsize->stepwise.step_height,
					   fsize->reserved[0],
					   fsize->reserved[1]);
		return 0;
	}

	return -EINVAL;
}

static int vidioc_enum_fmt(struct mtk_vcodec_ctx *ctx, struct v4l2_fmtdesc *f,
	bool output_queue)
{
	struct mtk_video_fmt *fmt;
	int i, j = 0;

	for (i = 0; i < MTK_MAX_DEC_CODECS_SUPPORT &&
		 mtk_vdec_formats[i].fourcc != 0; i++) {
		if ((output_queue == true) &&
			(mtk_vdec_formats[i].type != MTK_FMT_DEC))
			continue;
		else if ((output_queue == false) &&
				 (mtk_vdec_formats[i].type != MTK_FMT_FRAME))
			continue;

		if (j == f->index)
			break;
		++j;
	}

	if (i == MTK_MAX_DEC_CODECS_SUPPORT ||
		mtk_vdec_formats[i].fourcc == 0)
		return -EINVAL;

	fmt = &mtk_vdec_formats[i];

	f->pixelformat = fmt->fourcc;
	f->flags = 0;
	memset(f->reserved, 0, sizeof(f->reserved));

	if (mtk_vdec_formats[i].type != MTK_FMT_DEC)
		f->flags |= V4L2_FMT_FLAG_COMPRESSED;

	v4l_fill_mtk_fmtdesc(f);

	return 0;
}

static int vidioc_vdec_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
	struct v4l2_fmtdesc *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	return vidioc_enum_fmt(ctx, f, false);
}

static int vidioc_vdec_enum_fmt_vid_out_mplane(struct file *file, void *priv,
	struct v4l2_fmtdesc *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	return vidioc_enum_fmt(ctx, f, true);
}

static int vidioc_vdec_g_fmt(struct file *file, void *priv,
							 struct v4l2_format *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct vb2_queue *vq;
	struct mtk_q_data *q_data;
	u32     fourcc;
	unsigned int i = 0;

	if (IS_ERR_OR_NULL(f)) {
		mtk_v4l2_err("fail to get v4l2_format");
		return -EINVAL;
	}

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq) {
		mtk_v4l2_err("no vb2 queue for type=%d", f->type);
		return -EINVAL;
	}

	q_data = mtk_vdec_get_q_data(ctx, f->type);

	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->colorspace = ctx->colorspace;
	pix_mp->ycbcr_enc = ctx->ycbcr_enc;
	pix_mp->quantization = ctx->quantization;
	pix_mp->xfer_func = ctx->xfer_func;

	if ((f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) &&
		(ctx->state >= MTK_STATE_HEADER)) {
		/* Until STREAMOFF is called on the CAPTURE queue
		 * (acknowledging the event), the driver operates as if
		 * the resolution hasn't changed yet.
		 * So we just return picinfo yet, and update picinfo in
		 * stop_streaming hook function
		 */
		for (i = 0; i < q_data->fmt->num_planes; i++) {
			q_data->sizeimage[i] = ctx->picinfo.fb_sz[i];
			q_data->bytesperline[i] =
				ctx->last_decoded_picinfo.buf_w;
		}
		q_data->coded_width = ctx->picinfo.buf_w;
		q_data->coded_height = ctx->picinfo.buf_h;
		q_data->field = ctx->picinfo.field;
		fourcc = ctx->picinfo.fourcc;
		q_data->fmt = mtk_find_fmt_by_pixel(fourcc);

		/*
		 * Width and height are set to the dimensions
		 * of the movie, the buffer is bigger and
		 * further processing stages should crop to this
		 * rectangle.
		 */
		fourcc = ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc;
		pix_mp->width = q_data->coded_width;
		pix_mp->height = q_data->coded_height;
		/*
		 * Set pixelformat to the format in which mt vcodec
		 * outputs the decoded frame
		 */
		pix_mp->num_planes = q_data->fmt->num_planes;
		pix_mp->pixelformat = q_data->fmt->fourcc;
		pix_mp->field = q_data->field;

		for (i = 0; i < pix_mp->num_planes; i++) {
			pix_mp->plane_fmt[i].bytesperline =
				q_data->bytesperline[i];
			pix_mp->plane_fmt[i].sizeimage =
				q_data->sizeimage[i];
		}

		mtk_v4l2_debug(1, "fourcc:(%d %d),field:%d,bytesperline:%d,sizeimage:%d,%d,%d\n",
			ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc,
			q_data->fmt->fourcc,
			pix_mp->field,
			pix_mp->plane_fmt[0].bytesperline,
			pix_mp->plane_fmt[0].sizeimage,
			pix_mp->plane_fmt[1].bytesperline,
			pix_mp->plane_fmt[1].sizeimage);

	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		/*
		 * This is run on OUTPUT
		 * The buffer contains compressed image
		 * so width and height have no meaning.
		 * Assign value here to pass v4l2-compliance test
		 */
		pix_mp->width = q_data->visible_width;
		pix_mp->height = q_data->visible_height;
		pix_mp->plane_fmt[0].bytesperline = q_data->bytesperline[0];
		pix_mp->plane_fmt[0].sizeimage = q_data->sizeimage[0];
		pix_mp->pixelformat = q_data->fmt->fourcc;
		pix_mp->num_planes = q_data->fmt->num_planes;
	} else {
		pix_mp->num_planes = q_data->fmt->num_planes;
		pix_mp->pixelformat = q_data->fmt->fourcc;
		fourcc = ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc;

		pix_mp->width = q_data->coded_width;
		pix_mp->height = q_data->coded_height;
		for (i = 0; i < pix_mp->num_planes; i++) {
			pix_mp->plane_fmt[i].bytesperline =
				q_data->bytesperline[i];
			pix_mp->plane_fmt[i].sizeimage =
				q_data->sizeimage[i];
		}

		mtk_v4l2_debug(1,
					   " [%d] type=%d state=%d Format information could not be read, not ready yet!",
					   ctx->id, f->type, ctx->state);
	}

	return 0;
}

static int vb2ops_vdec_queue_setup(struct vb2_queue *vq,
	unsigned int *nbuffers,
	unsigned int *nplanes,
	unsigned int sizes[],
	struct device *alloc_devs[])
{
	struct mtk_vcodec_ctx *ctx;
	struct mtk_q_data *q_data;
	unsigned int i;

	if (IS_ERR_OR_NULL(vq) || IS_ERR_OR_NULL(nbuffers) ||
	    IS_ERR_OR_NULL(nplanes) || IS_ERR_OR_NULL(alloc_devs)) {
		mtk_v4l2_err("vq %p, nbuffers %p, nplanes %p, alloc_devs %p",
			vq, nbuffers, nplanes, alloc_devs);
		return -EINVAL;
	}

	ctx = vb2_get_drv_priv(vq);
	q_data = mtk_vdec_get_q_data(ctx, vq->type);
	if (q_data == NULL || (*nplanes) > MTK_VCODEC_MAX_PLANES ||
	    (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && q_data->fmt == NULL)) {
		mtk_v4l2_err("vq->type=%d nplanes %d err", vq->type, *nplanes);
		return -EINVAL;
	}

	if (*nplanes) {
		for (i = 0; i < *nplanes; i++) {
			if (sizes[i] < q_data->sizeimage[i])
				return -EINVAL;
		}
	} else {
		if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			*nplanes = q_data->fmt->num_planes;
		else
			*nplanes = 1;

		for (i = 0; i < *nplanes; i++)
			sizes[i] = q_data->sizeimage[i];
	}

	mtk_v4l2_debug(1,
				   "[%d]\t type = %d, get %d plane(s), %d buffer(s) of size 0x%x 0x%x ",
				   ctx->id, vq->type, *nplanes, *nbuffers,
				   sizes[0], sizes[1]);

	return 0;
}

static int vb2ops_vdec_buf_prepare(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_q_data *q_data;
	unsigned int plane = 0;
	unsigned int i;
	struct mtk_video_dec_buf *mtkbuf;
	struct vb2_v4l2_buffer *vb2_v4l2;

	mtk_v4l2_debug(4, "[%d] (%d) id=%d",
				   ctx->id, vb->vb2_queue->type, vb->index);

	q_data = mtk_vdec_get_q_data(ctx, vb->vb2_queue->type);

	for (i = 0; i < q_data->fmt->num_planes; i++) {
		if (vb2_plane_size(vb, i) < q_data->sizeimage[i]) {
			mtk_v4l2_err("data will not fit into plane %d (%lu < %d)",
						 i, vb2_plane_size(vb, i),
						 q_data->sizeimage[i]);
		}
	}

	// Check if need to proceed cache operations
	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	mtkbuf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
	if (vb->vb2_queue->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (mtkbuf->general_user_fd > -1) {
			mtkbuf->frame_buffer.dma_general_buf =
				dma_buf_get(mtkbuf->general_user_fd);
			if (IS_ERR(mtkbuf->frame_buffer.dma_general_buf))
				mtkbuf->frame_buffer.dma_general_buf = 0;
			else {
				mtkbuf->frame_buffer.dma_general_addr =
					get_general_buffer_dma_addr(ctx, mtkbuf->general_user_fd);
			}
		} else
			mtkbuf->frame_buffer.dma_general_buf = 0;

		mtk_v4l2_debug(4,
			"general_buf fd=%d, dma_buf=%p, DMA=%pad",
			mtkbuf->general_user_fd,
			mtkbuf->frame_buffer.dma_general_buf,
			&mtkbuf->frame_buffer.dma_general_addr);

		if (mtkbuf->meta_user_fd > 0) {
			mtkbuf->frame_buffer.dma_meta_buf =
				dma_buf_get(mtkbuf->meta_user_fd);
			if (IS_ERR(mtkbuf->frame_buffer.dma_meta_buf))
				mtkbuf->frame_buffer.dma_meta_buf = 0;
			else {
				mtkbuf->frame_buffer.dma_meta_addr =
					get_meta_buffer_dma_addr(ctx, mtkbuf->meta_user_fd);
			}
		} else
			mtkbuf->frame_buffer.dma_meta_buf = 0;
		mtk_v4l2_debug(4,
			"meta_buf fd=%d, dma_buf=%p, DMA=%pad",
			mtkbuf->meta_user_fd,
			mtkbuf->frame_buffer.dma_meta_buf,
			&mtkbuf->frame_buffer.dma_meta_addr);
	}
	if (vb->vb2_queue->memory == VB2_MEMORY_DMABUF &&
		!(mtkbuf->flags & NO_CAHCE_CLEAN) &&
		!(ctx->dec_params.svp_mode)) {
		if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			struct mtk_vcodec_mem src_mem;
			struct vb2_dc_buf *dc_buf = vb->planes[0].mem_priv;
			mtk_v4l2_debug(4, "[%d] Cache sync+", ctx->id);

			mtk_dma_sync_sg_range(dc_buf->dma_sgt, &ctx->dev->plat_dev->dev,
				vb->planes[0].bytesused, DMA_TO_DEVICE);

			src_mem.dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);
			src_mem.size = (size_t)vb->planes[0].bytesused;

			mtk_v4l2_debug(4,
			   "[%d] Cache sync- TD for %pad sz=%d dev %p",
			   ctx->id,
			   &src_mem.dma_addr,
			   (unsigned int)src_mem.size,
			   &ctx->dev->plat_dev->dev);
		} else {
			for (plane = 0; plane < vb->num_planes; plane++) {
				struct vdec_fb dst_mem;
				struct vb2_dc_buf *dc_buf = vb->planes[plane].mem_priv;
				mtk_v4l2_debug(4, "[%d] Cache sync+", ctx->id);

				dma_sync_sg_for_device(&ctx->dev->plat_dev->dev,
					dc_buf->dma_sgt->sgl,
					dc_buf->dma_sgt->orig_nents,
					DMA_TO_DEVICE);
				dst_mem.fb_base[plane].dma_addr =
					vb2_dma_contig_plane_dma_addr(vb,
					plane);
				dst_mem.fb_base[plane].size =
					ctx->picinfo.fb_sz[plane];
				mtk_v4l2_debug(4,
				 "[%d] Cache sync- TD for %pad sz=%d dev %p",
				 ctx->id,
				 &dst_mem.fb_base[plane].dma_addr,
				 (unsigned int)dst_mem.fb_base[plane].size,
				 &ctx->dev->plat_dev->dev);
			}
		}
	}
	return 0;
}

static void vb2ops_vdec_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_buffer *src_buf;
	struct vb2_v4l2_buffer *src_vb2_v4l2;
	struct mtk_vcodec_mem *src_mem;
	unsigned int src_chg = 0;
	bool res_chg = false;
	bool mtk_vcodec_unsupport = false;
	bool need_seq_header = false;
	bool need_log = false;
	int ret = 0;
	unsigned long frame_size[2];
	unsigned int i = 0;
	unsigned int dpbsize = 1;
	unsigned int bs_fourcc, fm_fourcc;
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2 = NULL;
	struct mtk_video_dec_buf *buf = NULL;
	struct mtk_q_data *dst_q_data;
	u32 fourcc;
	int last_frame_type = 0;
	struct mtk_color_desc color_desc;
	struct vb2_queue *dst_vq;
	dma_addr_t new_dma_addr;
	bool new_dma = false;
	char debug_bs[50] = "";
	int pair_cnt, limit_cnt;
#ifdef VDEC_CHECK_ALIVE
	struct vdec_check_alive_work_struct *retrigger_ctx_work;
#endif
	mtk_v4l2_debug(4, "[%d] (%d) id=%d, vb=%p",
				   ctx->id, vb->vb2_queue->type,
				   vb->index, vb);

	if (ctx->state == MTK_STATE_FREE) {
		struct mtk_q_data *q_data;

		q_data = mtk_vdec_get_q_data(ctx, vb->vb2_queue->type);

		ret = vdec_if_init(ctx, q_data->fmt->fourcc);
		mtk_vdec_init_set_frame_wq(ctx);
		v4l2_m2m_set_dst_buffered(ctx->m2m_ctx,
			ctx->input_driven != NON_INPUT_DRIVEN);
		if (ctx->input_driven == INPUT_DRIVEN_CB_FRM)
			init_waitqueue_head(&ctx->fm_wq);
		if (ret) {
			mtk_v4l2_err("[%d]: vdec_if_init() fail ret=%d",
						 ctx->id, ret);
			if (ret == -EIO)
				mtk_vdec_error_handle(ctx, "init");
			else {
				ctx->state = MTK_STATE_ABORT;
				mtk_vdec_queue_error_event(ctx);
			}
			return;
		}
		ctx->state = MTK_STATE_INIT;
	}

	mtk_vdec_check_vcp_active(ctx, false, true, "start"); // cnt+1 before state change

#ifdef VDEC_CHECK_ALIVE
	/* ctx resume, queue work for check alive timer */
	if (mtk_vdec_dvfs_params_change(ctx)) {
		retrigger_ctx_work = kzalloc(sizeof(*retrigger_ctx_work), GFP_KERNEL);
		INIT_WORK(&retrigger_ctx_work->work, mtk_vdec_check_alive_work);
		retrigger_ctx_work->ctx = ctx;
		retrigger_ctx_work->dev = ctx->dev;
		queue_work(ctx->dev->check_alive_workqueue, &retrigger_ctx_work->work);
		mtk_v4l2_debug(0, "%s [VDVFS] retrigger ctx work: %d", __func__, ctx->id);
		ctx->is_active = 1;
	}
#endif
	/*
	 * check if this buffer is ready to be used after decode
	 */
	vb2_v4l2 = to_vb2_v4l2_buffer(vb);
	buf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
	if (vb->vb2_queue->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		mutex_lock(&ctx->buf_lock);
		if (buf->used == false) {
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, vb2_v4l2);
			buf->queued_in_vb2 = true;
			buf->queued_in_v4l2 = true;
			buf->ready_to_display = false;
		} else {
			buf->queued_in_vb2 = false;
			buf->queued_in_v4l2 = true;
			buf->ready_to_display = false;
		}
		if (ctx->input_driven) {
			buf->flags &= ~CROP_CHANGED;
			buf->flags &= ~REF_FREED;
		}

		for (i = 0; i < vb->num_planes; i++) {
			new_dma_addr =
				vb2_dma_contig_plane_dma_addr(vb, i);
			// real buffer changed in this slot
			if (buf->frame_buffer.fb_base[i].dmabuf != vb->planes[i].dbuf) {
				new_dma = true;
				mtk_v4l2_debug(1, "[%d] id=%d get new buffer: old dma_addr[%d] = %llx %p, new dma_addr[%d] = %llx %p",
					ctx->id, vb->index, i,
					(unsigned long)buf->frame_buffer.fb_base[i].dma_addr,
					buf->frame_buffer.fb_base[i].dmabuf,
					i, (unsigned long)new_dma_addr, vb->planes[i].dbuf);
			}
		}
		// only allow legacy buffers in this slot still referenced put to driver
		if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM &&
			!new_dma && buf->used == true) {
			buf->queued_in_vb2 = true;
			v4l2_m2m_buf_queue_check(ctx->m2m_ctx, vb2_v4l2);
		}
		update_dst_cnt(ctx);
		mutex_unlock(&ctx->buf_lock);

		if (ctx->input_driven == INPUT_DRIVEN_CB_FRM)
			wake_up(&ctx->fm_wq);

		mtk_vdec_set_frame(ctx, buf);

		if (ctx->input_driven == NON_INPUT_DRIVEN && ctx->align_mode) {
			pair_cnt = MIN((*ctx->src_cnt), (*ctx->dst_cnt));
			limit_cnt = mtk_vdec_get_align_limit(ctx);

			if (pair_cnt >= limit_cnt) {
				mtk_v4l2_debug(2, "[%d] (%d) pair cnt %d(%d,%d) >= %d when align mode, need to set align_type(%d)",
					ctx->id, vb->vb2_queue->type, pair_cnt,
					(*ctx->src_cnt), (*ctx->dst_cnt),
					limit_cnt, atomic_read(&ctx->align_type));
				atomic_cmpxchg(&ctx->align_type,
					VDEC_ALIGN_WAIT, VDEC_ALIGN_FULL); // 0->1
			} else
				mtk_v4l2_debug(8, "[%d] (%d) pair cnt %d(%d,%d) < %d when align mode, no need to set align_type(%d)",
					ctx->id, vb->vb2_queue->type, pair_cnt,
					(*ctx->src_cnt), (*ctx->dst_cnt),
					limit_cnt, atomic_read(&ctx->align_type));
		}

		return;
	}

	buf->used = false;
	v4l2_m2m_buf_queue_check(ctx->m2m_ctx, to_vb2_v4l2_buffer(vb));
	update_src_cnt(ctx);

	if (ctx->input_driven == NON_INPUT_DRIVEN && ctx->align_mode) {
		pair_cnt = MIN((*ctx->src_cnt), (*ctx->dst_cnt));
		limit_cnt = mtk_vdec_get_align_limit(ctx);

		if (pair_cnt >= limit_cnt || buf->lastframe != NON_EOS)
			mtk_v4l2_debug(2, "[%d] (%d) pair cnt %d(%d,%d) >= %d or is EOS (%d) when align mode, need to set align_type(%d)",
				ctx->id, vb->vb2_queue->type, pair_cnt,
				(*ctx->src_cnt), (*ctx->dst_cnt),
				limit_cnt, buf->lastframe, atomic_read(&ctx->align_type));
		else
			mtk_v4l2_debug(8, "[%d] (%d) pair cnt %d(%d,%d) < %d, no need to set align_type(%d)",
				ctx->id, vb->vb2_queue->type, pair_cnt,
				(*ctx->src_cnt), (*ctx->dst_cnt),
				limit_cnt, atomic_read(&ctx->align_type));

		if (buf->lastframe != NON_EOS)
			atomic_set(&ctx->align_type, VDEC_ALIGN_RESET);
		else if (pair_cnt >= limit_cnt)
			atomic_cmpxchg(&ctx->align_type, VDEC_ALIGN_WAIT, VDEC_ALIGN_FULL); // 0->1
	}

	if (ctx->state != MTK_STATE_INIT) {
		mtk_v4l2_debug(4, "[%d] already init driver %d",
					   ctx->id, ctx->state);
		return;
	}

	src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);

	if (!src_vb2_v4l2 ||
		src_vb2_v4l2 == &ctx->dec_flush_buf->vb) {
		mtk_v4l2_err("No src buffer 0x%p", src_vb2_v4l2);
		return;
	}
	src_buf = &src_vb2_v4l2->vb2_buf;

	vb2_v4l2 = to_vb2_v4l2_buffer(vb);
	buf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
	src_mem = &buf->bs_buffer;
	if (mtk_v4l2_dbg_level > 0)
		src_mem->va = vb2_plane_vaddr(src_buf, 0);
	src_mem->dma_addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	src_mem->size = (size_t)src_buf->planes[0].bytesused;
	src_mem->length = (size_t)src_buf->planes[0].length;
	src_mem->data_offset = (size_t)src_buf->planes[0].data_offset;
	src_mem->dmabuf = src_buf->planes[0].dbuf;
	src_mem->flags = vb2_v4l2->flags;
	src_mem->index = vb->index;
	src_mem->hdr10plus_buf = &buf->hdr10plus_buf;
	ctx->dec_params.timestamp = src_vb2_v4l2->vb2_buf.timestamp;
	ctx->bs_list[src_mem->index + 1] = (uintptr_t)src_mem;

	mtk_v4l2_debug(2,
		"[%d] buf id=%d va=%p DMA=%lx size=%zx length=%zu dmabuf=%p pts %llu",
		ctx->id, src_buf->index,
		src_mem->va, (unsigned long)src_mem->dma_addr,
		src_mem->size, src_mem->length,
		src_mem->dmabuf, ctx->dec_params.timestamp);

	if (src_mem->va != NULL) {
		sprintf(debug_bs, "%02x %02x %02x %02x %02x %02x %02x %02x %02x",
		  ((char *)src_mem->va)[0], ((char *)src_mem->va)[1], ((char *)src_mem->va)[2],
		  ((char *)src_mem->va)[3], ((char *)src_mem->va)[4], ((char *)src_mem->va)[5],
		  ((char *)src_mem->va)[6], ((char *)src_mem->va)[7], ((char *)src_mem->va)[8]);
	}

	frame_size[0] = ctx->dec_params.frame_size_width;
	frame_size[1] = ctx->dec_params.frame_size_height;

	vdec_if_set_param(ctx, SET_PARAM_FRAME_SIZE, frame_size);

	if (ctx->dec_param_change & MTK_DEC_PARAM_DECODE_MODE)
		vdec_if_set_param(ctx, SET_PARAM_DECODE_MODE, &ctx->dec_params.decode_mode);

	ret = vdec_if_decode(ctx, src_mem, NULL, &src_chg);
	mtk_vdec_set_param(ctx);

	/* src_chg bit0 for res change flag, bit1 for realloc mv buf flag,
	 * bit2 for not support flag, other bits are reserved
	 */
	res_chg = ((src_chg & VDEC_RES_CHANGE) != 0U) ? true : false;
	mtk_vcodec_unsupport = ((src_chg & VDEC_HW_NOT_SUPPORT) != 0) ?
						   true : false;
	need_seq_header = ((src_chg & VDEC_NEED_SEQ_HEADER) != 0U) ?
					  true : false;
	if (ret || !res_chg || mtk_vcodec_unsupport
		|| need_seq_header) {
		/*
		 * fb == NULL menas to parse SPS/PPS header or
		 * resolution info in src_mem. Decode can fail
		 * if there is no SPS header or picture info
		 * in bs
		 */
		vb2_v4l2 = to_vb2_v4l2_buffer(vb);
		buf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
		last_frame_type = buf->lastframe;

		if (need_seq_header)
			vb2_v4l2->flags |= V4L2_BUF_FLAG_OUTPUT_NOT_GENERATED;

		src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		if (!src_vb2_v4l2) {
			mtk_v4l2_err("[%d]Error!!src_buf is NULL!");
			return;
		}
		src_buf = &src_vb2_v4l2->vb2_buf;
		clean_free_bs_buffer(ctx, NULL);
		update_src_cnt(ctx);

		need_log = ret || mtk_vcodec_unsupport || (need_seq_header && ctx->init_cnt < 5);
		mtk_v4l2_debug((need_log ? 0 : 1),
			"[%d] vdec_if_decode() src_buf=%d, size=%zu, fail=%d, res_chg=%d, mtk_vcodec_unsupport=%d, need_seq_header=%d, init_cnt=%d, BS %s",
			ctx->id, src_buf->index,
			src_mem->size, ret, res_chg,
			mtk_vcodec_unsupport, need_seq_header, ctx->init_cnt, debug_bs);

		/* If not support the source, eg: w/h,
		 * bitdepth, level, we need to stop to play it
		 */
		if (need_seq_header) {
			mtk_v4l2_debug(3, "[%d]Error!! Need seq header! (cnt %d)",
						 ctx->id, ctx->init_cnt);
			mtk_vdec_queue_noseqheader_event(ctx);
		} else if (mtk_vcodec_unsupport || last_frame_type != NON_EOS) {
			mtk_v4l2_err("[%d]Error!! Codec driver not support the file!",
						 ctx->id);
			ctx->state = MTK_STATE_ABORT;
			mtk_vdec_queue_error_event(ctx);
		} else if (ret == -EIO) {
			/* ipi timeout / VPUD crashed ctx abort */
			mtk_vdec_error_handle(ctx, "dec init");
		}
		ctx->init_cnt++;
		return;
	}

	if (res_chg) {
		mtk_v4l2_debug(3, "[%d] vdec_if_decode() res_chg: %d\n",
					   ctx->id, res_chg);
		clean_free_bs_buffer(ctx, src_mem);
		mtk_vdec_queue_res_chg_event(ctx);

		/* remove all framebuffer.
		 * framebuffer with old byteused cannot use.
		 */
		while (v4l2_m2m_dst_buf_remove(ctx->m2m_ctx) != NULL)
			mtk_v4l2_debug(3, "[%d] v4l2_m2m_dst_buf_remove()", ctx->id);
		update_dst_cnt(ctx);
	}

	ret = vdec_if_get_param(ctx, GET_PARAM_PIC_INFO,
		&ctx->last_decoded_picinfo);
	if (ret) {
		mtk_v4l2_err("[%d]Error!! Cannot get param : GET_PARAM_PICTURE_INFO ERR",
					 ctx->id);
		return;
	}

	dst_q_data = &ctx->q_data[MTK_Q_DATA_DST];
	fourcc = ctx->last_decoded_picinfo.fourcc;
	dst_q_data->fmt = mtk_find_fmt_by_pixel(fourcc);

	for (i = 0; i < dst_q_data->fmt->num_planes; i++) {
		dst_q_data->sizeimage[i] = ctx->last_decoded_picinfo.fb_sz[i];
		dst_q_data->bytesperline[i] = ctx->last_decoded_picinfo.buf_w;
	}

	bs_fourcc = ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc;
	fm_fourcc = ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc;
	mtk_v4l2_debug(0,
				   "[%d] Init Vdec OK wxh=%dx%d pic wxh=%dx%d bitdepth:%d lo:%d sz[0]=0x%x sz[1]=0x%x",
				   ctx->id,
				   ctx->last_decoded_picinfo.buf_w,
				   ctx->last_decoded_picinfo.buf_h,
				   ctx->last_decoded_picinfo.pic_w,
				   ctx->last_decoded_picinfo.pic_h,
				   ctx->last_decoded_picinfo.bitdepth,
				   ctx->last_decoded_picinfo.layout_mode,
				   dst_q_data->sizeimage[0],
				   dst_q_data->sizeimage[1]);

	mtk_v4l2_debug(0, "[%d] bs %c%c%c%c fm %c%c%c%c, num_planes %d, fb_sz[0] %d, fb_sz[1] %d, BS %s",
				   ctx->id,
				   bs_fourcc & 0xFF, (bs_fourcc >> 8) & 0xFF,
				   (bs_fourcc >> 16) & 0xFF,
				   (bs_fourcc >> 24) & 0xFF,
				   fm_fourcc & 0xFF, (fm_fourcc >> 8) & 0xFF,
				   (fm_fourcc >> 16) & 0xFF,
				   (fm_fourcc >> 24) & 0xFF,
				   dst_q_data->fmt->num_planes,
				   ctx->last_decoded_picinfo.fb_sz[0],
				   ctx->last_decoded_picinfo.fb_sz[1], debug_bs);

	ret = vdec_if_get_param(ctx, GET_PARAM_DPB_SIZE, &dpbsize);
	if (dpbsize == 0)
		mtk_v4l2_err("[%d] GET_PARAM_DPB_SIZE fail=%d", ctx->id, ret);

	ctx->last_dpb_size = dpbsize;

	ret = vdec_if_get_param(ctx, GET_PARAM_COLOR_DESC, &color_desc);
	if (ret == 0) {
		ctx->last_is_hdr = color_desc.is_hdr;
	} else {
		mtk_v4l2_err("[%d] GET_PARAM_COLOR_DESC fail=%d",
		ctx->id, ret);
	}

	dst_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	if (!vb2_is_streaming(dst_vq)) {
		ctx->picinfo = ctx->last_decoded_picinfo;
		ctx->dpb_size = dpbsize;
		ctx->is_hdr = color_desc.is_hdr;
	}

	mtk_vdec_check_vcp_active(ctx, false, true, "init done"); // cnt+1 before state change
	ctx->state = MTK_STATE_HEADER;
	mtk_v4l2_debug(1, "[%d] dpbsize=%d", ctx->id, ctx->last_dpb_size);

}

static void vb2ops_vdec_buf_finish(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2;
	struct mtk_video_dec_buf *buf;
	unsigned int plane = 0;
	struct mtk_video_dec_buf *mtkbuf;

	if (vb->vb2_queue->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return;

	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	buf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);
	mutex_lock(&ctx->buf_lock);
	buf->queued_in_v4l2 = false;
	buf->queued_in_vb2 = false;
	mutex_unlock(&ctx->buf_lock);

	// Check if need to proceed cache operations for Capture Queue
	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	mtkbuf = container_of(vb2_v4l2, struct mtk_video_dec_buf, vb);

	if (mtkbuf->frame_buffer.dma_general_buf != 0) {
		dma_buf_put(mtkbuf->frame_buffer.dma_general_buf);
		mtkbuf->frame_buffer.dma_general_buf = 0;
		mtk_v4l2_debug(4,
		"dma_buf_put general_buf fd=%d, dma_buf=%p, DMA=%pad",
		mtkbuf->general_user_fd,
		mtkbuf->frame_buffer.dma_general_buf,
		&mtkbuf->frame_buffer.dma_general_addr);
	}
	if (mtkbuf->frame_buffer.dma_meta_buf != 0) {
		dma_buf_put(mtkbuf->frame_buffer.dma_meta_buf);
		mtkbuf->frame_buffer.dma_meta_buf = 0;
		mtk_v4l2_debug(4,
		"dma_buf_put meta_buf fd=%d, dma_buf=%p, DMA=%pad",
		mtkbuf->meta_user_fd,
		mtkbuf->frame_buffer.dma_meta_buf,
		&mtkbuf->frame_buffer.dma_meta_addr);
	}

	if (vb->vb2_queue->memory == VB2_MEMORY_DMABUF &&
		!(mtkbuf->flags & NO_CAHCE_INVALIDATE) &&
		!(ctx->dec_params.svp_mode)) {
		for (plane = 0; plane < buf->frame_buffer.num_planes; plane++) {
			struct vdec_fb dst_mem;
			struct vb2_dc_buf *dc_buf = vb->planes[plane].mem_priv;

			mtk_v4l2_debug(4, "[%d] Cache sync+", ctx->id);

			dma_sync_sg_for_cpu(&ctx->dev->plat_dev->dev, dc_buf->dma_sgt->sgl,
				dc_buf->dma_sgt->orig_nents, DMA_FROM_DEVICE);
			dst_mem.fb_base[plane].dma_addr =
				vb2_dma_contig_plane_dma_addr(vb, plane);
			dst_mem.fb_base[plane].size = ctx->picinfo.fb_sz[plane];

			mtk_v4l2_debug(4,
				"[%d] Cache sync- FD for %pad sz=%d dev %p pfb %p",
				ctx->id,
				&dst_mem.fb_base[plane].dma_addr,
				(unsigned int)dst_mem.fb_base[plane].size,
				&ctx->dev->plat_dev->dev,
				&buf->frame_buffer);
		}
	}
}

static void vb2ops_vdec_buf_cleanup(struct vb2_buffer *vb)
{
	int i;
	struct mtk_vcodec_ctx *ctx;
	struct vb2_v4l2_buffer *vb2_v4l2 = container_of(vb,
		struct vb2_v4l2_buffer, vb2_buf);
	struct mtk_video_dec_buf *buf = container_of(vb2_v4l2,
		struct mtk_video_dec_buf, vb);

	ctx = vb2_get_drv_priv(vb->vb2_queue);
	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
		!vb2_is_streaming(vb->vb2_queue)) {
		mutex_lock(&ctx->buf_lock);
		if (buf->used == true) {
			for (i = 0; i < buf->frame_buffer.num_planes; i++) {
				if (buf->frame_buffer.fb_base[i].dmabuf)
					fput(buf->frame_buffer.fb_base[i].dmabuf->file);
				mtk_v4l2_debug(4, "[Ref cnt] id=%d Ref put dma %p",
				    buf->frame_buffer.index, buf->frame_buffer.fb_base[i].dmabuf);
			}
			buf->used = false;
		}
		mutex_unlock(&ctx->buf_lock);
	}
}

static int vb2ops_vdec_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vb2_v4l2 = container_of(vb,
		struct vb2_v4l2_buffer, vb2_buf);
	struct mtk_video_dec_buf *buf = container_of(vb2_v4l2,
		struct mtk_video_dec_buf, vb);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		mtk_v4l2_debug(4, "[%d] pfb=%p used %d ready_to_display %d queued_in_v4l2 %d",
			vb->vb2_queue->type, &buf->frame_buffer,
			buf->used, buf->ready_to_display, buf->queued_in_v4l2);
		/* User could use different struct dma_buf*
		 * with the same index & enter this buf_init.
		 * once this buffer buf->used == true will reset in mistake
		 * VB2 use kzalloc for struct mtk_video_dec_buf,
		 * so init could be no need
		 */
		if (buf->used == false) {
			buf->ready_to_display = false;
			buf->queued_in_v4l2 = false;
		}
	} else if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		/* Do not reset EOS for 1st buffer with Early EOS*/
		/* buf->lastframe = NON_EOS; */
	} else {
		mtk_v4l2_err("vb2ops_vdec_buf_init: unknown queue type");
		return -EINVAL;
	}

	return 0;
}

static int vb2ops_vdec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	unsigned long total_frame_bufq_count;
	unsigned long vcp_dvfs_data[1] = {0};

	mtk_v4l2_debug(4, "[%d] (%d) state=(%x)", ctx->id, q->type, ctx->state);

	if (ctx->state == MTK_STATE_FLUSH) {
		mtk_vdec_check_vcp_active(ctx, false, true, "start"); // cnt+1 before state change
		ctx->state = MTK_STATE_HEADER;
	}

	//SET_PARAM_TOTAL_FRAME_BUFQ_COUNT for SW DEC
	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (ctx->ipi_blocked != NULL)
			*(ctx->ipi_blocked) = false;
		if (ctx->dst_cnt != NULL)
			*(ctx->dst_cnt) = 0;
		atomic_set(&ctx->align_type, VDEC_ALIGN_FULL);
		ctx->align_start_cnt = ctx->dpb_size;

		total_frame_bufq_count = q->num_buffers;
		if (vdec_if_set_param(ctx,
			SET_PARAM_TOTAL_FRAME_BUFQ_COUNT,
			&total_frame_bufq_count)) {
			mtk_v4l2_err("[%d] Error!! Cannot set param",
				ctx->id);
		}

		mutex_lock(&ctx->dev->dec_dvfs_mutex);
		if (ctx->dev->vdec_reg == 0 && ctx->dev->vdec_mmdvfs_clk == 0) {
			if (!mtk_vdec_dvfs_is_pw_always_on(ctx->dev))
				mtk_vcodec_dec_pw_on(&ctx->dev->pm);
			mtk_vdec_prepare_vcp_dvfs_data(ctx, vcp_dvfs_data);
			if (vdec_if_set_param(ctx, SET_PARAM_MMDVFS, vcp_dvfs_data) != 0) {
				mtk_v4l2_err("[VDVFS][%d] streamOn ipi fail", ctx->id);
				ctx->state = MTK_STATE_ABORT;
				mtk_vdec_dvfs_reset_AO_flag(ctx->dev);
			}
			mtk_vdec_dvfs_sync_vsi_data(ctx);
			mtk_v4l2_debug(0, "[VDVFS][%d(%d)] start DVFS(UP): freq:%d, h_l:%d, op:%d",
				ctx->id, ctx->state, ctx->dev->vdec_dvfs_params.target_freq,
				ctx->dev->vdec_dvfs_params.high_loading_scenario,
				ctx->dec_params.operating_rate);
			if (!mtk_vdec_dvfs_is_pw_always_on(ctx->dev))
				mtk_vcodec_dec_pw_off(&ctx->dev->pm);
		} else {
			mtk_v4l2_debug(0, "[%d][VDVFS][VDEC] start ctrl DVFS in AP", ctx->id);
			mtk_vdec_dvfs_begin_inst(ctx);
		}
		mtk_vdec_pmqos_begin_inst(ctx);
		mutex_unlock(&ctx->dev->dec_dvfs_mutex);

	} else if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (ctx->src_cnt != NULL)
			*(ctx->src_cnt) = 0;

		if (!mtk_vcodec_is_vcp(MTK_INST_DECODER)) {
			// set SET_PARAM_TOTAL_BITSTREAM_BUFQ_COUNT for
			// error handling when framing
			total_frame_bufq_count = q->num_buffers;
			if (vdec_if_set_param(ctx,
				SET_PARAM_TOTAL_BITSTREAM_BUFQ_COUNT,
				&total_frame_bufq_count)) {
				mtk_v4l2_err("[%d] Error!! Cannot set param",
					ctx->id);
			}
		}
		mtk_vdec_ts_reset(ctx);
	}

	mtk_vdec_set_param(ctx);
	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		v4l2_m2m_set_dst_buffered(ctx->m2m_ctx, ctx->input_driven != NON_INPUT_DRIVEN);

	return 0;
}

static void vb2ops_vdec_stop_streaming(struct vb2_queue *q)
{

	struct vb2_buffer *dst_buf = NULL;
	struct vb2_v4l2_buffer *src_vb2_v4l2, *dst_vb2_v4l2;
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	struct mtk_video_dec_buf *src_buf_info = NULL;
	unsigned int i = 0;
	struct mtk_video_dec_buf *srcbuf, *dstbuf;
	unsigned long vcp_dvfs_data[1] = {0};

	mtk_v4l2_debug(4, "[%d] (%d) state=(%x) ctx->decoded_frame_cnt=%d",
		ctx->id, q->type, ctx->state, ctx->decoded_frame_cnt);

	ctx->input_max_ts = 0;

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (ctx->state >= MTK_STATE_HEADER) {
			src_vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
			if (src_vb2_v4l2 != NULL) {
				src_buf_info =
					container_of(src_vb2_v4l2, struct mtk_video_dec_buf, vb);
				/* for bs buffer reuse case & avoid put to done twice*/
				mtk_vdec_reset_decoder(ctx, 0, &src_buf_info->bs_buffer, q->type);
			} else {
				mtk_vdec_reset_decoder(ctx, 0, NULL, q->type);
			}
		}

		mutex_lock(&ctx->buf_lock);
		while ((src_vb2_v4l2 = v4l2_m2m_src_buf_remove(ctx->m2m_ctx)))
			if (src_vb2_v4l2 != &ctx->dec_flush_buf->vb &&
				src_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
				v4l2_m2m_buf_done(src_vb2_v4l2, VB2_BUF_STATE_ERROR);

		for (i = 0; i < q->num_buffers; i++)
			if (q->bufs[i]->state == VB2_BUF_STATE_ACTIVE) {
				src_vb2_v4l2 = container_of(
					q->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
				srcbuf = container_of(
					src_vb2_v4l2, struct mtk_video_dec_buf, vb);
				mtk_v4l2_err("[%d] src num_buffers %d q_cnt %d queue id=%d state %d %d %d %d",
					ctx->id, q->num_buffers,
					atomic_read(&q->owned_by_drv_count),
					srcbuf->vb.vb2_buf.index,
					q->bufs[i]->state, srcbuf->queued_in_vb2,
					srcbuf->queued_in_v4l2, srcbuf->used);
				vb2_buffer_done(q->bufs[i], VB2_BUF_STATE_ERROR);
			}
		mutex_unlock(&ctx->buf_lock);
		ctx->dec_flush_buf->lastframe = NON_EOS;
		update_src_cnt(ctx);
		return;
	}

	mtk_vdec_flush_set_frame_wq(ctx);

	if (ctx->state >= MTK_STATE_HEADER) {

		/* Until STREAMOFF is called on the CAPTURE queue
		 * (acknowledging the event), the driver operates
		 * as if the resolution hasn't changed yet, i.e.
		 * VIDIOC_G_FMT< etc. return previous resolution.
		 * So we update picinfo here
		 */
		ctx->picinfo = ctx->last_decoded_picinfo;
		ctx->dpb_size = ctx->last_dpb_size;
		ctx->is_hdr = ctx->last_is_hdr;

		mtk_v4l2_debug(2,
			"[%d]-> new(%d,%d), old(%d,%d), real(%d,%d) bit:%d\n",
			ctx->id, ctx->last_decoded_picinfo.pic_w,
			ctx->last_decoded_picinfo.pic_h,
			ctx->picinfo.pic_w, ctx->picinfo.pic_h,
			ctx->last_decoded_picinfo.buf_w,
			ctx->last_decoded_picinfo.buf_h,
			ctx->picinfo.bitdepth);

		mtk_vdec_reset_decoder(ctx, 0, NULL, q->type);
	}

	mutex_lock(&ctx->buf_lock);
	while ((dst_vb2_v4l2 = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))) {
		dst_buf = &dst_vb2_v4l2->vb2_buf;

		for (i = 0; i < dst_buf->num_planes; i++)
			vb2_set_plane_payload(dst_buf, i, 0);

		if (dst_vb2_v4l2->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(dst_vb2_v4l2, VB2_BUF_STATE_ERROR);
	}
	mutex_unlock(&ctx->buf_lock);
	update_dst_cnt(ctx);

	/* check buffer status */
	mutex_lock(&ctx->buf_lock);
	for (i = 0; i < q->num_buffers; i++) {
		dst_vb2_v4l2 = container_of(
			q->bufs[i], struct vb2_v4l2_buffer, vb2_buf);
		dstbuf = container_of(
			dst_vb2_v4l2, struct mtk_video_dec_buf, vb);
		mtk_v4l2_debug((q->bufs[i]->state == VB2_BUF_STATE_ACTIVE) ? 0 : 4,
			"[%d] dst num_buffers %d q_cnt %d status=%x queue id=%d %p %llx state %d %d %d %d",
			ctx->id, q->num_buffers, atomic_read(&q->owned_by_drv_count),
			dstbuf->frame_buffer.status,
			dstbuf->vb.vb2_buf.index, &dstbuf->frame_buffer,
			(unsigned long)(&dstbuf->frame_buffer),
			q->bufs[i]->state, dstbuf->queued_in_vb2,
			dstbuf->queued_in_v4l2, dstbuf->used);
		if (q->bufs[i]->state == VB2_BUF_STATE_ACTIVE)
			vb2_buffer_done(q->bufs[i], VB2_BUF_STATE_ERROR);
	}
	mutex_unlock(&ctx->buf_lock);
	mutex_lock(&ctx->dev->dec_dvfs_mutex);
	ctx->is_active = 0;
	if (ctx->dev->vdec_reg == 0 && ctx->dev->vdec_mmdvfs_clk == 0) {
		if (!mtk_vdec_dvfs_is_pw_always_on(ctx->dev))
			mtk_vcodec_dec_pw_on(&ctx->dev->pm);
		mtk_vdec_unprepare_vcp_dvfs_data(ctx, vcp_dvfs_data);
		if (vdec_if_set_param(ctx, SET_PARAM_MMDVFS, vcp_dvfs_data) != 0) {
			mtk_v4l2_err("[VDVFS][%d] streamOff ipi fail", ctx->id);
			ctx->state = MTK_STATE_ABORT;
			mtk_vdec_dvfs_reset_AO_flag(ctx->dev);
		}
		mtk_vdec_dvfs_sync_vsi_data(ctx);
		mtk_v4l2_debug(0, "[VDVFS][%d(%d)] stop DVFS (UP): freq: %d, h_l: %d, op: %d",
			ctx->id, ctx->state, ctx->dev->vdec_dvfs_params.target_freq,
			ctx->dev->vdec_dvfs_params.high_loading_scenario,
			ctx->dec_params.operating_rate);
		if (!mtk_vdec_dvfs_is_pw_always_on(ctx->dev))
			mtk_vcodec_dec_pw_off(&ctx->dev->pm);
	} else {
		mtk_v4l2_debug(0, "[%d][VDVFS][VDEC] stop ctrl DVFS in AP", ctx->id);
		mtk_vdec_dvfs_end_inst(ctx);
	}
	mtk_vdec_pmqos_end_inst(ctx);
	mutex_unlock(&ctx->dev->dec_dvfs_mutex);

}

static void m2mops_vdec_device_run(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;
	struct mtk_vcodec_dev *dev = ctx->dev;

	queue_work(dev->decode_workqueue, &ctx->decode_work);
}

static int m2mops_vdec_job_ready(void *m2m_priv)
{
	struct mtk_vcodec_ctx *ctx = m2m_priv;
	int ret = 1;

	if (ctx->state == MTK_STATE_ABORT)
		ret = 0;

	if ((ctx->last_decoded_picinfo.pic_w != ctx->picinfo.pic_w) ||
		(ctx->last_decoded_picinfo.pic_h != ctx->picinfo.pic_h) ||
		(ctx->last_dpb_size != ctx->dpb_size) ||
		(ctx->last_is_hdr != ctx->is_hdr))
		ret = 0;

	if (ctx->state != MTK_STATE_HEADER)
		ret = 0;

	if (ctx->input_driven != NON_INPUT_DRIVEN && (*ctx->ipi_blocked))
		ret = 0;

	if (ctx->input_driven == NON_INPUT_DRIVEN && ctx->align_mode &&
		atomic_read(&ctx->align_type) == VDEC_ALIGN_WAIT && (*ctx->wait_align))
		ret = 0;

	mtk_v4l2_debug(4, "[%d] ret %d", ctx->id, ret);

	return ret;
}

static void m2mops_vdec_job_abort(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;

	if (ctx->input_driven == INPUT_DRIVEN_PUT_FRM)
		vdec_if_set_param(ctx, SET_PARAM_FRAME_BUFFER, NULL);
	else if (ctx->input_driven == INPUT_DRIVEN_CB_FRM)
		wake_up(&ctx->fm_wq);

	mtk_v4l2_debug(4, "[%d]", ctx->id);
	ctx->state = MTK_STATE_STOP;
}

static int vdec_set_hdr10plus_data(struct mtk_vcodec_ctx *ctx,
				   struct v4l2_vdec_hdr10plus_data *hdr10plus_data)
{
	struct __u8 __user *buffer = u64_to_user_ptr(hdr10plus_data->addr);
	struct hdr10plus_info *hdr10plus_buf = &ctx->dec_params.hdr10plus_buf;

	mtk_v4l2_debug(4, "hdr10plus_data size %d", hdr10plus_data->size);
	hdr10plus_buf->size = hdr10plus_data->size;
	if (hdr10plus_buf->size > HDR10_PLUS_MAX_SIZE)
		hdr10plus_buf->size = HDR10_PLUS_MAX_SIZE;

	if (hdr10plus_buf->size == 0)
		return 0;

	if (buffer == NULL) {
		mtk_v4l2_err("invalid null pointer");
		return -EINVAL;
	}

	if (copy_from_user(&hdr10plus_buf->data, buffer, hdr10plus_buf->size)) {
		mtk_v4l2_err("copy hdr10plus from user fails");
		return -EFAULT;
	}

	return 0;
}

static enum vdec_set_param_type CID2SetParam(u32 id)
{
	switch (id) {
	case V4L2_CID_VDEC_TRICK_MODE:
		return SET_PARAM_TRICK_MODE;
	case V4L2_CID_VDEC_HDR10_INFO:
		return SET_PARAM_HDR10_INFO;
	case V4L2_CID_VDEC_NO_REORDER:
		return SET_PARAM_NO_REORDER;
	}

	return SET_PARAM_MAX;
}

static int mtk_vdec_g_v_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mtk_vcodec_ctx *ctx = ctrl_to_ctx(ctrl);
	int ret = 0;
	static unsigned int value;
	struct mtk_color_desc *color_desc;

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		if (ctx->state >= MTK_STATE_HEADER)
			ctrl->val = ctx->dpb_size;
		else {
			mtk_v4l2_debug(1, "Seqinfo not ready");
			ctrl->val = 0;
		}
		break;
	case V4L2_CID_MPEG_MTK_FRAME_INTERVAL:
		if (vdec_if_get_param(ctx,
			GET_PARAM_FRAME_INTERVAL, &value) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		ctrl->p_new.p_u32 = &value;
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_MTK_FRAME_INTERVAL val = %u",
					   *(ctrl->p_new.p_u32));
		break;
	case V4L2_CID_MPEG_MTK_COLOR_DESC:
		color_desc = (struct mtk_color_desc *)ctrl->p_new.p_u32;
		if (vdec_if_get_param(ctx, GET_PARAM_COLOR_DESC, color_desc)
		    != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_MTK_ASPECT_RATIO:
		if (vdec_if_get_param(ctx, GET_PARAM_ASPECT_RATIO, &ctrl->val)
		    != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_MTK_FIX_BUFFERS:
		if (vdec_if_get_param(ctx,
		    GET_PARAM_PLATFORM_SUPPORTED_FIX_BUFFERS, &ctrl->val)
		    != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_MTK_FIX_BUFFERS_SVP:
		if (vdec_if_get_param(ctx,
		    GET_PARAM_PLATFORM_SUPPORTED_FIX_BUFFERS_SVP, &ctrl->val)
		    != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_MTK_INTERLACING:
		if (vdec_if_get_param(ctx, GET_PARAM_INTERLACING, &ctrl->val)
		    != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_MTK_INTERLACING_FIELD_SEQ:
		if (vdec_if_get_param(ctx,
			GET_PARAM_INTERLACING_FIELD_SEQ, &ctrl->val) != 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_MTK_CODEC_TYPE:
		if (vdec_if_get_param(ctx, GET_PARAM_CODEC_TYPE, &ctrl->val)
			!= 0) {
			mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_MTK_GET_LOG:
		mtk_vcodec_get_log(
			ctx, ctx->dev, ctrl->p_new.p_char,
			MTK_VCODEC_LOG_INDEX_LOG, get_vcu_vpud_log);
		break;
	case V4L2_CID_MPEG_MTK_GET_VCP_PROP:
		mtk_vcodec_get_log(
			ctx, ctx->dev, ctrl->p_new.p_char, MTK_VCODEC_LOG_INDEX_PROP, NULL);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int mtk_vdec_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mtk_vcodec_ctx *ctx = ctrl_to_ctx(ctrl);

	mtk_v4l2_debug(4, "[%d] id 0x%x val %d array[0] %d array[1] %d",
				   ctx->id, ctrl->id, ctrl->val,
				   ctrl->p_new.p_u32[0], ctrl->p_new.p_u32[1]);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_MTK_DECODE_MODE:
		ctx->dec_params.decode_mode = ctrl->val;
		ctx->dec_param_change |= MTK_DEC_PARAM_DECODE_MODE;
		break;
	case V4L2_CID_MPEG_MTK_SEC_DECODE: {
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
		struct vb2_queue *src_vq;

		if (ctrl->val) {
			if (vcp_get_io_device(VCP_IOMMU_SEC)) {
				src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
					V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
				src_vq->dev = vcp_get_io_device(VCP_IOMMU_SEC);
				mtk_v4l2_debug(4, "use VCP_IOMMU_SEC domain");
			}

		}
#endif
		ctx->dec_params.svp_mode = ctrl->val;
		ctx->dec_param_change |= MTK_DEC_PARAM_SEC_DECODE;
		mtk_v4l2_debug(ctrl->val ? 0 : 1, "[%d] V4L2_CID_MPEG_MTK_SEC_DECODE id %d val %d",
			ctx->id, ctrl->id, ctrl->val);
		break;
	}
	case V4L2_CID_MPEG_MTK_FRAME_SIZE:
		if (ctx->dec_params.frame_size_width == 0)
			ctx->dec_params.frame_size_width = ctrl->val;
		else if (ctx->dec_params.frame_size_height == 0)
			ctx->dec_params.frame_size_height = ctrl->val;
		else if (ctx->dec_params.fixed_max_frame_buffer_mode == 0)
			ctx->dec_params.fixed_max_frame_buffer_mode = ctrl->val;
		ctx->dec_param_change |= MTK_DEC_PARAM_FRAME_SIZE;
		break;
	case V4L2_CID_MPEG_MTK_FIXED_MAX_FRAME_BUFFER:
		if (ctx->dec_params.fixed_max_frame_size_width == 0)
			ctx->dec_params.fixed_max_frame_size_width = ctrl->val;
		else if (ctx->dec_params.fixed_max_frame_size_height == 0)
			ctx->dec_params.fixed_max_frame_size_height = ctrl->val;
		ctx->dec_param_change |= MTK_DEC_PARAM_FIXED_MAX_FRAME_SIZE;
		break;
	case V4L2_CID_MPEG_MTK_CRC_PATH:
		ctx->dec_params.crc_path = ctrl->p_new.p_char;
		ctx->dec_param_change |= MTK_DEC_PARAM_CRC_PATH;
		break;
	case V4L2_CID_MPEG_MTK_GOLDEN_PATH:
		ctx->dec_params.golden_path = ctrl->p_new.p_char;
		ctx->dec_param_change |= MTK_DEC_PARAM_GOLDEN_PATH;
		break;
	case V4L2_CID_MPEG_MTK_SET_WAIT_KEY_FRAME:
		ctx->dec_params.wait_key_frame = ctrl->val;
		ctx->dec_param_change |= MTK_DEC_PARAM_WAIT_KEY_FRAME;
		break;
	case V4L2_CID_MPEG_MTK_SET_DECODE_ERROR_HANDLE_MODE:
		ctx->dec_params.decode_error_handle_mode = ctrl->val;
		ctx->dec_param_change |= MTK_DEC_PARAM_DECODE_ERROR_HANDLE_MODE;
		break;
	case V4L2_CID_MPEG_MTK_SET_NAL_SIZE_LENGTH:
		ctx->dec_params.nal_size_length = ctrl->val;
		ctx->dec_param_change |= MTK_DEC_PARAM_NAL_SIZE_LENGTH;
		break;
	case V4L2_CID_MPEG_MTK_OPERATING_RATE:
		ctx->dec_params.operating_rate = ctrl->val;
		ctx->dec_param_change |= MTK_DEC_PARAM_OPERATING_RATE;
		break;
	case V4L2_CID_MPEG_MTK_REAL_TIME_PRIORITY:
		ctx->dec_params.priority = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_QUEUED_FRAMEBUF_COUNT:
		ctx->dec_params.queued_frame_buf_count = ctrl->val;
		break;
	case V4L2_CID_MPEG_MTK_LOG:
		mtk_vcodec_set_log(
			ctx, ctx->dev, ctrl->p_new.p_char,
			MTK_VCODEC_LOG_INDEX_LOG, set_vcu_vpud_log);
		break;
	case V4L2_CID_MPEG_MTK_VCP_PROP:
		mtk_vcodec_set_log(
			ctx, ctx->dev, ctrl->p_new.p_char, MTK_VCODEC_LOG_INDEX_PROP, NULL);
		break;
	case V4L2_CID_VDEC_DETECT_TIMESTAMP:
		ctx->dec_params.enable_detect_ts = ctrl->val;
		break;
	case V4L2_CID_VDEC_SLICE_COUNT:
		ctx->dec_params.slice_count = ctrl->val;
		break;
	case V4L2_CID_VDEC_TRICK_MODE:
	case V4L2_CID_VDEC_NO_REORDER:
	case V4L2_CID_VDEC_HDR10_INFO: {
		int ret;
		enum vdec_set_param_type type = SET_PARAM_MAX;

		if (!ctx->drv_handle)
			return 0;

		type = CID2SetParam(ctrl->id);
		if (ctrl->is_ptr)
			ret = vdec_if_set_param(ctx, type, ctrl->p_new.p);
		else {
			unsigned long in = ctrl->val;

			ret = vdec_if_set_param(ctx, type, &in);
		}

		if (ret < 0) {
			mtk_v4l2_debug(5, "ctrl-id=0x%x fails! ret %d",
					ctrl->id, ret);
			return ret;
		}
		break;
	}
	case V4L2_CID_VDEC_HDR10PLUS_DATA:
		return vdec_set_hdr10plus_data(ctx, ctrl->p_new.p);
	default:
		mtk_v4l2_debug(4, "ctrl-id=%x not support!", ctrl->id);
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops mtk_vcodec_dec_ctrl_ops = {
	.g_volatile_ctrl = mtk_vdec_g_v_ctrl,
	.s_ctrl = mtk_vdec_s_ctrl,
};


void mtk_vcodec_dec_custom_ctrls_check(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_config *cfg, void *priv)
{
	v4l2_ctrl_new_custom(hdl, cfg, NULL);

	if (hdl->error) {
		mtk_v4l2_debug(0, "Adding control failed %s %x %d",
			cfg->name, cfg->id, hdl->error);
	} else {
		mtk_v4l2_debug(4, "Adding control %s %x %d",
			cfg->name, cfg->id, hdl->error);
	}
}

int mtk_vcodec_dec_ctrls_setup(struct mtk_vcodec_ctx *ctx)
{
	struct v4l2_ctrl *ctrl;
	const struct v4l2_ctrl_ops *ops = &mtk_vcodec_dec_ctrl_ops;
	struct v4l2_ctrl_handler *handler = &ctx->ctrl_hdl;
	struct v4l2_ctrl_config cfg;

	v4l2_ctrl_handler_init(&ctx->ctrl_hdl, MTK_MAX_CTRLS_HINT);

	/* g_volatile_ctrl */
	ctrl = v4l2_ctrl_new_std(&ctx->ctrl_hdl,
		&mtk_vcodec_dec_ctrl_ops,
		V4L2_CID_MIN_BUFFERS_FOR_CAPTURE,
		0, 32, 1, 1);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_FRAME_INTERVAL;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Video frame interval";
	cfg.min = 16666;
	cfg.max = 41719;
	cfg.step = 1;
	cfg.def = 33333;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_ASPECT_RATIO;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Video aspect ratio";
	cfg.min = 0;
	cfg.max = 0xF000F;
	cfg.step = 1;
	cfg.def = 0x10001;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_FIX_BUFFERS;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Video fix buffers";
	cfg.min = 0;
	cfg.max = 0xF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_FIX_BUFFERS_SVP;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Video fix buffers for svp";
	cfg.min = 0;
	cfg.max = 0xF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_INTERLACING;
	cfg.type = V4L2_CTRL_TYPE_BOOLEAN;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "MTK Query Interlacing";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_CODEC_TYPE;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "MTK Query HW/SW Codec Type";
	cfg.min = 0;
	cfg.max = 10;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_INTERLACING_FIELD_SEQ;
	cfg.type = V4L2_CTRL_TYPE_BOOLEAN;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "MTK Query Interlacing FieldSeq";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_COLOR_DESC;
	cfg.type = V4L2_CTRL_TYPE_U32;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "MTK vdec Color Description for HDR";
	cfg.min = 0;
	cfg.max = 0xffffffff;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct mtk_color_desc)/sizeof(u32));
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	/* s_ctrl */
	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_DECODE_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video decode mode";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_SEC_DECODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video Sec Decode path";
	cfg.min = 0;
	cfg.max = 32;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_FRAME_SIZE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video frame size";
	cfg.min = 0;
	cfg.max = 65535;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_FIXED_MAX_FRAME_BUFFER;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video fixed maximum frame size";
	cfg.min = 0;
	cfg.max = 65535;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_CRC_PATH;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video crc path";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_GOLDEN_PATH;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video golden path";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_SET_WAIT_KEY_FRAME;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Wait key frame";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_SET_DECODE_ERROR_HANDLE_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Decode Error Handle Mode";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_OPERATING_RATE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Vdec Operating Rate";
	cfg.min = 0;
	cfg.max = 4096;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_REAL_TIME_PRIORITY;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Vdec Real Time Priority";
	cfg.min = -1;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = -1;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_QUEUED_FRAMEBUF_COUNT;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video queued frame buf count";
	cfg.min = 0;
	cfg.max = 64;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_LOG;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video Log";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_VCP_PROP;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "Video VCP Property";
	cfg.min = 0;
	cfg.max = 255;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_GET_LOG;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Get Video Log";
	cfg.min = 0;
	cfg.max = LOG_PROPERTY_SIZE;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_MPEG_MTK_GET_VCP_PROP;
	cfg.type = V4L2_CTRL_TYPE_STRING;
	cfg.flags = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE;
	cfg.name = "Get Video VCP Property";
	cfg.min = 0;
	cfg.max = LOG_PROPERTY_SIZE;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_DETECT_TIMESTAMP;
	cfg.type = V4L2_CTRL_TYPE_BOOLEAN;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "VDEC detect timestamp mode";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_SLICE_COUNT;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_WRITE_ONLY;
	cfg.name = "VDEC set slice count";
	cfg.min = 0;
	cfg.max = 256, // max 256 slice in a frame according to HW DE
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_HDR10_INFO;
	cfg.type = V4L2_CTRL_TYPE_U8;
	cfg.flags = V4L2_CTRL_FLAG_HAS_PAYLOAD | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set HDR10 information";
	cfg.min = 0;
	cfg.max = 0xFF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct v4l2_vdec_hdr10_info));
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_HDR10PLUS_DATA;
	cfg.type = V4L2_CTRL_TYPE_U8;
	cfg.flags = V4L2_CTRL_FLAG_HAS_PAYLOAD | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set HDR10PLUS data";
	cfg.min = 0;
	cfg.max = 0xFF;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	cfg.dims[0] = (sizeof(struct v4l2_vdec_hdr10plus_data));
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_TRICK_MODE;
	cfg.type = V4L2_CTRL_TYPE_INTEGER;
	cfg.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC set/get trick mode";
	cfg.min = V4L2_VDEC_TRICK_MODE_ALL;
	cfg.max = V4L2_VDEC_TRICK_MODE_I;
	cfg.step = 1;
	cfg.def = V4L2_VDEC_TRICK_MODE_ALL;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	memset(&cfg, 0, sizeof(cfg));
	cfg.id = V4L2_CID_VDEC_NO_REORDER;
	cfg.type = V4L2_CTRL_TYPE_BOOLEAN;
	cfg.flags = V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	cfg.name = "VDEC show frame without reorder";
	cfg.min = 0;
	cfg.max = 1;
	cfg.step = 1;
	cfg.def = 0;
	cfg.ops = ops;
	mtk_vcodec_dec_custom_ctrls_check(handler, &cfg, NULL);

	if (ctx->ctrl_hdl.error) {
		mtk_v4l2_err("Adding control failed %d",
					 ctx->ctrl_hdl.error);
		return ctx->ctrl_hdl.error;
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);
	return 0;
}

const struct v4l2_m2m_ops mtk_vdec_m2m_ops = {
	.device_run     = m2mops_vdec_device_run,
	.job_ready      = m2mops_vdec_job_ready,
	.job_abort      = m2mops_vdec_job_abort,
};

static const struct vb2_ops mtk_vdec_vb2_ops = {
	.queue_setup    = vb2ops_vdec_queue_setup,
	.buf_prepare    = vb2ops_vdec_buf_prepare,
	.buf_queue      = vb2ops_vdec_buf_queue,
	.wait_prepare   = vb2_ops_wait_prepare,
	.wait_finish    = vb2_ops_wait_finish,
	.buf_init       = vb2ops_vdec_buf_init,
	.buf_finish     = vb2ops_vdec_buf_finish,
	.buf_cleanup = vb2ops_vdec_buf_cleanup,
	.start_streaming        = vb2ops_vdec_start_streaming,
	.stop_streaming = vb2ops_vdec_stop_streaming,
};

const struct v4l2_ioctl_ops mtk_vdec_ioctl_ops = {
	.vidioc_streamon        = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff       = v4l2_m2m_ioctl_streamoff,
	.vidioc_reqbufs         = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf        = v4l2_m2m_ioctl_querybuf,
	.vidioc_expbuf          = v4l2_m2m_ioctl_expbuf,

	.vidioc_qbuf            = vidioc_vdec_qbuf,
	.vidioc_dqbuf           = vidioc_vdec_dqbuf,

	.vidioc_try_fmt_vid_cap_mplane  = vidioc_try_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_out_mplane  = vidioc_try_fmt_vid_out_mplane,

	.vidioc_s_fmt_vid_cap_mplane    = vidioc_vdec_s_fmt,
	.vidioc_s_fmt_vid_out_mplane    = vidioc_vdec_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane    = vidioc_vdec_g_fmt,
	.vidioc_g_fmt_vid_out_mplane    = vidioc_vdec_g_fmt,

	.vidioc_create_bufs             = v4l2_m2m_ioctl_create_bufs,

	.vidioc_enum_fmt_vid_cap = vidioc_vdec_enum_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_out = vidioc_vdec_enum_fmt_vid_out_mplane,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
#ifdef TV_INTEGRATION
	.vidioc_enum_frameintervals     = vidioc_enum_frameintervals,
#endif

	.vidioc_querycap                = vidioc_vdec_querycap,
	.vidioc_subscribe_event         = vidioc_vdec_subscribe_evt,
	.vidioc_unsubscribe_event       = v4l2_event_unsubscribe,
	.vidioc_g_selection             = vidioc_vdec_g_selection,
	.vidioc_s_selection             = vidioc_vdec_s_selection,
#ifdef TV_INTEGRATION
	.vidioc_g_parm                  = vidioc_g_parm,
#endif
	.vidioc_decoder_cmd     = vidioc_decoder_cmd,
	.vidioc_try_decoder_cmd = vidioc_try_decoder_cmd,
};

static void *mtk_vdec_dc_attach_dmabuf(struct vb2_buffer *vb, struct device *dev,
				  struct dma_buf *dbuf, unsigned long size)
{
	struct vb2_dc_buf *buf;
	struct dma_buf_attachment *dba;

	if (dbuf->size < size)
		return ERR_PTR(-EFAULT);

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dev = dev;
	buf->vb = vb;

	/* create attachment for the dmabuf with the user device */
	dba = dma_buf_attach(dbuf, buf->dev);
	if (IS_ERR(dba)) {
		pr_info("failed to attach dmabuf\n");
		kfree(buf);
		return dba;
	}

	/* always skip cache operations, we handle it manually */
	dba->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	buf->dma_dir = vb->vb2_queue->dma_dir;
	buf->size = size;
	buf->db_attach = dba;

	return buf;
}

int mtk_vcodec_dec_queue_init(void *priv, struct vb2_queue *src_vq,
	struct vb2_queue *dst_vq)
{
	struct mtk_vcodec_ctx *ctx = priv;
	int ret = 0;

	mtk_v4l2_debug(4, "[%d]", ctx->id);

	src_vq->type            = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes        = VB2_DMABUF | VB2_MMAP;
	src_vq->drv_priv        = ctx;
	src_vq->buf_struct_size = sizeof(struct mtk_video_dec_buf);
	src_vq->ops             = &mtk_vdec_vb2_ops;
	vdec_dma_contig_memops = vb2_dma_contig_memops;
	vdec_dma_contig_memops.attach_dmabuf = mtk_vdec_dc_attach_dmabuf;
	src_vq->mem_ops         = &vdec_dma_contig_memops;
	mtk_v4l2_debug(4, "src_vq use vdec_dma_contig_memops");
	src_vq->bidirectional = 1;

	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock            = &ctx->q_mutex;
#if IS_ENABLED(CONFIG_MTK_TINYSYS_VCP_SUPPORT)
	if (ctx->id & 1) {
		src_vq->dev		= vcp_get_io_device(VCP_IOMMU_VENC_512MB2);
		mtk_v4l2_debug(4, "use VCP_IOMMU_VENC_512MB2 domain");
	} else {
		src_vq->dev		= vcp_get_io_device(VCP_IOMMU_VDEC_512MB1);
		mtk_v4l2_debug(4, "use VCP_IOMMU_VDEC_512MB1 domain");
	}
#if IS_ENABLED(CONFIG_VIDEO_MEDIATEK_VCU)
	if (!src_vq->dev) {
		src_vq->dev = &ctx->dev->plat_dev->dev;
		mtk_v4l2_debug(4, "vcp_get_io_device NULL use plat_dev domain");
	}
#endif
#else
	src_vq->dev		= &ctx->dev->plat_dev->dev;
#endif
	src_vq->allow_zero_bytesused = 1;

	ret = vb2_queue_init(src_vq);
	if (ret) {
		mtk_v4l2_err("Failed to initialize videobuf2 queue(output)");
		return ret;
	}
	dst_vq->type            = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes        = VB2_DMABUF | VB2_MMAP;
	dst_vq->drv_priv        = ctx;
	dst_vq->buf_struct_size = sizeof(struct mtk_video_dec_buf);
	dst_vq->ops             = &mtk_vdec_vb2_ops;
	dst_vq->mem_ops         = &vdec_dma_contig_memops;
	mtk_v4l2_debug(4, "dst_vq use vdec_dma_contig_memops");
	dst_vq->bidirectional = 1;

	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock            = &ctx->q_mutex;
	dst_vq->dev             = &ctx->dev->plat_dev->dev;
	dst_vq->allow_zero_bytesused = 1;

	ret = vb2_queue_init(dst_vq);
	if (ret) {
		vb2_queue_release(src_vq);
		mtk_v4l2_err("Failed to initialize videobuf2 queue(capture)");
	}

	return ret;
}

MODULE_LICENSE("GPL v2");

