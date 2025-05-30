/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef _UAPI_MEDIATEK_DRM_H
#define _UAPI_MEDIATEK_DRM_H

#include <drm/drm.h>

#ifndef BIT
#define BIT(nr)                 ((unsigned long)(1) << (nr))
#endif

#define MTK_SUBMIT_NO_IMPLICIT   0x0 /* disable implicit sync */
#define MTK_SUBMIT_IN_FENCE   0x1 /* enable input fence */
#define MTK_SUBMIT_OUT_FENCE  0x2  /* enable output fence */

#define MTK_DRM_PROP_OVERLAP_LAYER_NUM  "OVERLAP_LAYER_NUM"
#define MTK_DRM_PROP_NEXT_BUFF_IDX  "NEXT_BUFF_IDX"
#define MTK_DRM_PROP_PRESENT_FENCE  "PRESENT_FENCE"

struct mml_frame_info;

/**
 * User-desired buffer creation information structure.
 *
 * @size: user-desired memory allocation size.
 *	- this size value would be page-aligned internally.
 * @flags: user request for setting memory type or cache attributes.
 * @handle: returned a handle to created gem object.
 *	- this handle will be set by gem module of kernel side.
 */
struct drm_mtk_gem_create {
	__u64 size;
	__u32 flags;
	__u32 handle;
};

/**
 * A structure for getting buffer offset.
 *
 * @handle: a pointer to gem object created.
 * @pad: just padding to be 64-bit aligned.
 * @offset: relatived offset value of the memory region allocated.
 *     - this value should be set by user.
 */
struct drm_mtk_gem_map_off {
	__u32 handle;
	__u32 pad;
	__u64 offset;
};

/**
 * A structure for buffer submit.
 *
 * @type:
 * @session_id:
 * @layer_id:
 * @layer_en:
 * @fb_id:
 * @index:
 * @fence_fd:
 * @interface_index:
 * @interface_fence_fd:
 */
struct drm_mtk_gem_submit {
	__u32 type;
	/* session */
	__u32 session_id;
	/* layer */
	__u32 layer_id;
	__u32 layer_en;
	/* buffer */
	__u32 fb_id;
	/* output */
	__u32 index;
	__s32 fence_fd;
	__u32 interface_index;
	__s32 interface_fence_fd;
	__s32 ion_fd;
};

/**
 * A structure for secure/gem hnd transform.
 *
 * @sec_hnd: handle of secure memory
 * @gem_hnd: handle of gem
 */
struct drm_mtk_sec_gem_hnd {
	__u32 sec_hnd;
	__u32 gem_hnd;
};

/**
 * A structure for session create.
 *
 * @type:
 * @device_id:
 * @mode:
 * @session_id:
 */
struct drm_mtk_session {
	__u32 type;
	/* device */
	__u32 device_id;
	/* mode */
	__u32 mode;
	/* output */
	__u32 session_id;
};

struct msync_level_table {
	unsigned int level_id;
	unsigned int level_fps;
	unsigned int max_fps;
	unsigned int min_fps;
};

struct msync_parameter_table {
	unsigned int msync_max_fps;
	unsigned int msync_min_fps;
	unsigned int msync_level_num;
	struct msync_level_table *level_tb;
};
/* PQ */
#define C_TUN_IDX 19 /* COLOR_TUNING_INDEX */
#define COLOR_TUNING_INDEX 19
#define THSHP_TUNING_INDEX 24
#define THSHP_PARAM_MAX 146 /* TDSHP_3_0 */
#define PARTIAL_Y_INDEX 22
#define GLOBAL_SAT_SIZE 22
#define CONTRAST_SIZE 22
#define BRIGHTNESS_SIZE 22
#define PARTIAL_Y_SIZE 16
#define PQ_HUE_ADJ_PHASE_CNT 4
#define PQ_SAT_ADJ_PHASE_CNT 4
#define PQ_PARTIALS_CONTROL 5
#define PURP_TONE_SIZE 3
#define SKIN_TONE_SIZE 8  /* (-6) */
#define GRASS_TONE_SIZE 6 /* (-2) */
#define SKY_TONE_SIZE 3
#define CCORR_COEF_CNT 4 /* ccorr feature */
#define S_GAIN_BY_Y_CONTROL_CNT 5
#define S_GAIN_BY_Y_HUE_PHASE_CNT 20
#define LSP_CONTROL_CNT 8
#define COLOR_3D_CNT 4 /* color 3D feature */
#define COLOR_3D_WINDOW_CNT 3
#define COLOR_3D_WINDOW_SIZE 45
#define C_3D_CNT 4 /* COLOR_3D_CNT */
#define C_3D_WINDOW_CNT 3
#define C_3D_WINDOW_SIZE 45

enum TONE_ENUM { PURP_TONE = 0, SKIN_TONE = 1, GRASS_TONE = 2, SKY_TONE = 3 };

struct DISP_PQ_WIN_PARAM {
	int split_en;
	int start_x;
	int start_y;
	int end_x;
	int end_y;
};
#define DISP_PQ_WIN_PARAM_T struct DISP_PQ_WIN_PARAM

struct DISP_PQ_MAPPING_PARAM {
	int image;
	int video;
	int camera;
};

#define DISP_PQ_MAPPING_PARAM_T struct DISP_PQ_MAPPING_PARAM

struct MDP_COLOR_CAP {
	unsigned int en;
	unsigned int pos_x;
	unsigned int pos_y;
};

struct MDP_TDSHP_REG {
	unsigned int TDS_GAIN_MID;
	unsigned int TDS_GAIN_HIGH;
	unsigned int TDS_COR_GAIN;
	unsigned int TDS_COR_THR;
	unsigned int TDS_COR_ZERO;
	unsigned int TDS_GAIN;
	unsigned int TDS_COR_VALUE;
};
struct DISPLAY_PQ_T {

	unsigned int GLOBAL_SAT[GLOBAL_SAT_SIZE];
	unsigned int CONTRAST[CONTRAST_SIZE];
	unsigned int BRIGHTNESS[BRIGHTNESS_SIZE];
	unsigned int PARTIAL_Y[PARTIAL_Y_INDEX][PARTIAL_Y_SIZE];
	unsigned int PURP_TONE_S[COLOR_TUNING_INDEX]
				[PQ_PARTIALS_CONTROL][PURP_TONE_SIZE];
	unsigned int SKIN_TONE_S[COLOR_TUNING_INDEX]
				[PQ_PARTIALS_CONTROL][SKIN_TONE_SIZE];
	unsigned int GRASS_TONE_S[COLOR_TUNING_INDEX]
				[PQ_PARTIALS_CONTROL][GRASS_TONE_SIZE];
	unsigned int SKY_TONE_S[COLOR_TUNING_INDEX]
			       [PQ_PARTIALS_CONTROL][SKY_TONE_SIZE];
	unsigned int PURP_TONE_H[COLOR_TUNING_INDEX][PURP_TONE_SIZE];
	unsigned int SKIN_TONE_H[COLOR_TUNING_INDEX][SKIN_TONE_SIZE];
	unsigned int GRASS_TONE_H[COLOR_TUNING_INDEX][GRASS_TONE_SIZE];
	unsigned int SKY_TONE_H[COLOR_TUNING_INDEX][SKY_TONE_SIZE];
	unsigned int CCORR_COEF[CCORR_COEF_CNT][3][3];
	unsigned int S_GAIN_BY_Y[5][S_GAIN_BY_Y_HUE_PHASE_CNT];
	unsigned int S_GAIN_BY_Y_EN;
	unsigned int LSP_EN;
	unsigned int LSP[LSP_CONTROL_CNT];
	unsigned int COLOR_3D[4][COLOR_3D_WINDOW_CNT][COLOR_3D_WINDOW_SIZE];
};
#define DISPLAY_PQ struct DISPLAY_PQ_T

struct DISPLAY_COLOR_REG {
	unsigned int GLOBAL_SAT;
	unsigned int CONTRAST;
	unsigned int BRIGHTNESS;
	unsigned int PARTIAL_Y[PARTIAL_Y_SIZE];
	unsigned int PURP_TONE_S[PQ_PARTIALS_CONTROL][PURP_TONE_SIZE];
	unsigned int SKIN_TONE_S[PQ_PARTIALS_CONTROL][SKIN_TONE_SIZE];
	unsigned int GRASS_TONE_S[PQ_PARTIALS_CONTROL][GRASS_TONE_SIZE];
	unsigned int SKY_TONE_S[PQ_PARTIALS_CONTROL][SKY_TONE_SIZE];
	unsigned int PURP_TONE_H[PURP_TONE_SIZE];
	unsigned int SKIN_TONE_H[SKIN_TONE_SIZE];
	unsigned int GRASS_TONE_H[GRASS_TONE_SIZE];
	unsigned int SKY_TONE_H[SKY_TONE_SIZE];
	unsigned int S_GAIN_BY_Y[S_GAIN_BY_Y_CONTROL_CNT]
				[S_GAIN_BY_Y_HUE_PHASE_CNT];
	unsigned int S_GAIN_BY_Y_EN;
	unsigned int LSP_EN;
	unsigned int COLOR_3D[COLOR_3D_WINDOW_CNT][COLOR_3D_WINDOW_SIZE];
};
#define DISPLAY_COLOR_REG_T struct DISPLAY_COLOR_REG


#define DISP_COLOR_TM_MAX 4
struct DISP_COLOR_TRANSFORM {
	int matrix[DISP_COLOR_TM_MAX][DISP_COLOR_TM_MAX];
};

struct DISPLAY_TDSHP_T {

	unsigned int entry[THSHP_TUNING_INDEX][THSHP_PARAM_MAX];

};
#define DISPLAY_TDSHP struct DISPLAY_TDSHP_T

enum PQ_DS_index_t {
	DS_en = 0,
	iUpSlope,
	iUpThreshold,
	iDownSlope,
	iDownThreshold,
	iISO_en,
	iISO_thr1,
	iISO_thr0,
	iISO_thr3,
	iISO_thr2,
	iISO_IIR_alpha,
	iCorZero_clip2,
	iCorZero_clip1,
	iCorZero_clip0,
	iCorThr_clip2,
	iCorThr_clip1,
	iCorThr_clip0,
	iCorGain_clip2,
	iCorGain_clip1,
	iCorGain_clip0,
	iGain_clip2,
	iGain_clip1,
	iGain_clip0,
	PQ_DS_INDEX_MAX
};

struct DISP_PQ_DS_PARAM {
	int param[PQ_DS_INDEX_MAX];
};
#define DISP_PQ_DS_PARAM_T struct DISP_PQ_DS_PARAM

enum PQ_DC_index_t {
	BlackEffectEnable = 0,
	WhiteEffectEnable,
	StrongBlackEffect,
	StrongWhiteEffect,
	AdaptiveBlackEffect,
	AdaptiveWhiteEffect,
	ScenceChangeOnceEn,
	ScenceChangeControlEn,
	ScenceChangeControl,
	ScenceChangeTh1,
	ScenceChangeTh2,
	ScenceChangeTh3,
	ContentSmooth1,
	ContentSmooth2,
	ContentSmooth3,
	MiddleRegionGain1,
	MiddleRegionGain2,
	BlackRegionGain1,
	BlackRegionGain2,
	BlackRegionRange,
	BlackEffectLevel,
	BlackEffectParam1,
	BlackEffectParam2,
	BlackEffectParam3,
	BlackEffectParam4,
	WhiteRegionGain1,
	WhiteRegionGain2,
	WhiteRegionRange,
	WhiteEffectLevel,
	WhiteEffectParam1,
	WhiteEffectParam2,
	WhiteEffectParam3,
	WhiteEffectParam4,
	ContrastAdjust1,
	ContrastAdjust2,
	DCChangeSpeedLevel,
	ProtectRegionEffect,
	DCChangeSpeedLevel2,
	ProtectRegionWeight,
	DCEnable,
	DarkSceneTh,
	DarkSceneSlope,
	DarkDCGain,
	DarkACGain,
	BinomialTh,
	BinomialSlope,
	BinomialDCGain,
	BinomialACGain,
	BinomialTarRange,
	bIIRCurveDiffSumTh,
	bIIRCurveDiffMaxTh,
	bGlobalPQEn,
	bHistAvoidFlatBgEn,
	PQDC_INDEX_MAX
};
#define PQ_DC_index enum PQ_DC_index_t

struct DISP_PQ_DC_PARAM {
	int param[PQDC_INDEX_MAX];
};

/* OD */
struct DISP_OD_CMD {
	unsigned int size;
	unsigned int type;
	unsigned int ret;
	unsigned long param0;
	unsigned long param1;
	unsigned long param2;
	unsigned long param3;
};


struct DISP_WRITE_REG {
	unsigned int reg;
	unsigned int val;
	unsigned int mask;
};

struct DISP_READ_REG {
	unsigned int reg;
	unsigned int val;
	unsigned int mask;
};

enum disp_ccorr_id_t {
	DISP_CCORR0 = 0,
	DISP_CCORR1,
	DISP_CCORR2,
	DISP_CCORR3,
	DISP_CCORR_TOTAL
};

struct DISP_CCORR_COEF_T {
	enum disp_ccorr_id_t hw_id;
	unsigned int coef[3][3];
};

#define DISP_GAMMA_LUT_SIZE 512
#define DISP_GAMMA_12BIT_LUT_SIZE 1024

enum disp_gamma_id_t {
	DISP_GAMMA0 = 0,
	DISP_GAMMA1,
	DISP_GAMMA_TOTAL
};


struct DISP_GAMMA_LUT_T {
	enum disp_gamma_id_t hw_id;
	unsigned int lut[DISP_GAMMA_LUT_SIZE];
};

struct DISP_GAMMA_12BIT_LUT_T {
	enum disp_gamma_id_t hw_id;
	unsigned int lut_0[DISP_GAMMA_12BIT_LUT_SIZE];
	unsigned int lut_1[DISP_GAMMA_12BIT_LUT_SIZE];
};

struct DISP_PQ_PARAM {
	unsigned int u4SHPGain;  /* 0 : min , 9 : max. */
	unsigned int u4SatGain;  /* 0 : min , 9 : max. */
	unsigned int u4PartialY; /* 0 : min , 9 : max. */
	unsigned int u4HueAdj[PQ_HUE_ADJ_PHASE_CNT];
	unsigned int u4SatAdj[PQ_SAT_ADJ_PHASE_CNT];
	unsigned int u4Contrast;   /* 0 : min , 9 : max. */
	unsigned int u4Brightness; /* 0 : min , 9 : max. */
	unsigned int u4Ccorr;      /* 0 : min , 3 : max. ccorr feature */
	unsigned int u4ColorLUT; /* 0 : min , 3 : max.  ccorr feature */
};
#define DISP_PQ_PARAM_T struct DISP_PQ_PARAM

struct DISP_DITHER_PARAM {
	_Bool relay;
	__u32 mode;
};

struct DISP_AAL_TRIG_STATE {
	int curAli;
	int aliThreshold;
	unsigned int dre_frm_trigger;
	unsigned int dre3_en_state;
	unsigned int dre3_krn_flag;
};


#define DRM_MTK_GEM_CREATE		0x00
#define DRM_MTK_GEM_MAP_OFFSET		0x01
#define DRM_MTK_GEM_SUBMIT		0x02
#define DRM_MTK_SESSION_CREATE		0x03
#define DRM_MTK_SESSION_DESTROY		0x04
#define DRM_MTK_LAYERING_RULE           0x05
#define DRM_MTK_CRTC_GETFENCE           0x06
#define DRM_MTK_WAIT_REPAINT            0x07
#define DRM_MTK_GET_DISPLAY_CAPS	0x08
#define DRM_MTK_SET_DDP_MODE   0x09
#define DRM_MTK_GET_SESSION_INFO	0x0A
#define DRM_MTK_SEC_HND_TO_GEM_HND	0x0B
#define DRM_MTK_GET_MASTER_INFO		0x0C
#define DRM_MTK_CRTC_GETSFFENCE         0x0D
#define DRM_MTK_MML_GEM_SUBMIT         0x0E
#define DRM_MTK_SET_MSYNC_PARAMS         0x0F
#define DRM_MTK_GET_MSYNC_PARAMS         0x10
#define DRM_MTK_FACTORY_LCM_AUTO_TEST    0x11
#define DRM_MTK_DRM_SET_LEASE_INFO    0x12
#define DRM_MTK_DRM_GET_LEASE_INFO    0x13
#define DRM_MTK_CRTC_FENCE_REL           0x14

/* PQ */
#define DRM_MTK_SET_12BIT_GAMMALUT	0x1D
#define DRM_MTK_PQ_PERSIST_PROPERTY	0x1F
#define DRM_MTK_SET_CCORR		0x20
#define DRM_MTK_CCORR_EVENTCTL   0x21
#define DRM_MTK_CCORR_GET_IRQ    0x22
#define DRM_MTK_SET_GAMMALUT    0x23
#define DRM_MTK_SET_PQPARAM    0x24
#define DRM_MTK_SET_PQINDEX    0x25
#define DRM_MTK_SET_COLOR_REG    0x26
#define DRM_MTK_MUTEX_CONTROL    0x27
#define DRM_MTK_READ_REG    0x28
#define DRM_MTK_WRITE_REG    0x29
#define DRM_MTK_BYPASS_COLOR   0x2A
#define DRM_MTK_PQ_SET_WINDOW   0x2B
#define DRM_MTK_GET_LCM_INDEX   0x2C
#define DRM_MTK_SUPPORT_COLOR_TRANSFORM    0x2D
#define DRM_MTK_READ_SW_REG   0x2E
#define DRM_MTK_WRITE_SW_REG   0x2F

/* AAL */
#define DRM_MTK_AAL_INIT_REG	0x30
#define DRM_MTK_AAL_GET_HIST	0x31
#define DRM_MTK_AAL_SET_PARAM	0x32
#define DRM_MTK_AAL_EVENTCTL	0x33
#define DRM_MTK_AAL_INIT_DRE30	0x34
#define DRM_MTK_AAL_GET_SIZE	0x35

#define DRM_MTK_AAL_SET_TRIGGER_STATE 0x5F
#define DRM_MTK_AAL_SET_ESS20_SPECT_PARAM	0x36
#define DRM_MTK_HDMI_GET_DEV_INFO	0x3A
#define DRM_MTK_HDMI_AUDIO_ENABLE	0x3B
#define DRM_MTK_HDMI_AUDIO_CONFIG	0x3C
#define DRM_MTK_HDMI_GET_CAPABILITY	0x3D

/* C3D */
#define DRM_MTK_C3D_GET_BIN_NUM     0x40
#define DRM_MTK_C3D_GET_IRQ         0x41
#define DRM_MTK_C3D_EVENTCTL        0x42
#define DRM_MTK_C3D_SET_LUT         0x44
#define DRM_MTK_SET_BYPASS_C3D      0x45
/* CHIST */
#define DRM_MTK_GET_CHIST           0x46
#define DRM_MTK_GET_CHIST_CAPS      0x47
#define DRM_MTK_SET_CHIST_CONFIG    0x48

#define DRM_MTK_SET_DITHER_PARAM 0x43
#define DRM_MTK_BYPASS_DISP_GAMMA 0x49

/* DISP TDSHP */
#define DRM_MTK_SET_DISP_TDSHP_REG 0x50
#define DRM_MTK_DISP_TDSHP_GET_SIZE 0x51

/* DISP_CLARITY */
#define DRM_MTK_DISP_CLARITY_SET_REG 0x5E

#define DRM_MTK_GET_PQ_CAPS 0x54
#define DRM_MTK_SET_PQ_CAPS 0x55

#define DRM_MTK_AIBLD_CV_MODE 0x56

#define DRM_MTK_ODDMR_LOAD_PARAM 0x57
#define DRM_MTK_ODDMR_CTL 0x58
#define DRM_MTK_SET_GAMMA_MUL_DISABLE 0x59
#define DRM_MTK_GET_PANELS_INFO 0x5a

#define DRM_MTK_KICK_IDLE 0x5b
#define DRM_MTK_GET_MODE_EXT_INFO 0x5c


/* C3D */
#define DISP_C3D_1DLUT_SIZE 32

struct DISP_C3D_LUT {
	unsigned int lut1d[DISP_C3D_1DLUT_SIZE];
	unsigned long long lut3d;
};

enum MTKFB_DISPIF_TYPE {
	DISPIF_TYPE_DBI = 0,
	DISPIF_TYPE_DPI,
	DISPIF_TYPE_DSI,
	DISPIF_TYPE_DPI0,
	DISPIF_TYPE_DPI1,
	DISPIF_TYPE_DSI0,
	DISPIF_TYPE_DSI1,
	HDMI = 7,
	HDMI_SMARTBOOK,
	MHL,
	DISPIF_TYPE_EPD,
	DISPLAYPORT,
	SLIMPORT
};

enum MTKFB_DISPIF_MODE {
	DISPIF_MODE_VIDEO = 0,
	DISPIF_MODE_COMMAND
};

struct mtk_dispif_info {
	unsigned int display_id;
	unsigned int isHwVsyncAvailable;
	enum MTKFB_DISPIF_TYPE displayType;
	unsigned int displayWidth;
	unsigned int displayHeight;
	unsigned int displayFormat;
	enum MTKFB_DISPIF_MODE displayMode;
	unsigned int vsyncFPS;
	unsigned int physicalWidth;
	unsigned int physicalHeight;
	unsigned int isConnected;
	unsigned int lcmOriginalWidth;
	unsigned int lcmOriginalHeight;
};

#define DRM_IOCTL_MTK_SET_DDP_MODE	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SET_DDP_MODE, unsigned int)

enum MTK_DRM_SESSION_MODE {
	MTK_DRM_SESSION_INVALID = 0,
	/* single output */
	MTK_DRM_SESSION_DL,

	/* two ouputs */
	MTK_DRM_SESSION_DOUBLE_DL,
	MTK_DRM_SESSION_DC_MIRROR,

	/* three session at same time */
	MTK_DRM_SESSION_TRIPLE_DL,
	MTK_DRM_SESSION_NUM,
};

#define MAX_FRAME_RATIO_NUMBER (10)
#define MAX_LAYER_RATIO_NUMBER (12)

enum MTK_LAYERING_CAPS {
	MTK_LAYERING_OVL_ONLY = 0x00000001,
	MTK_MDP_RSZ_LAYER =		0x00000002,
	MTK_DISP_RSZ_LAYER =	0x00000004,
	MTK_MDP_ROT_LAYER =		0x00000008,
	MTK_MDP_HDR_LAYER =		0x00000010,
	MTK_NO_FBDC =			0x00000020,
	MTK_CLIENT_CLEAR_LAYER =	0x00000040,
	MTK_DISP_CLIENT_CLEAR_LAYER =	0x00000080,
	MTK_DMDP_RSZ_LAYER =		0x00000100,
	MTK_MML_OVL_LAYER =	0x00000200,
	MTK_MML_DISP_DIRECT_LINK_LAYER =	0x00000400,
	MTK_MML_DISP_DIRECT_DECOUPLE_LAYER =	0x00000800,
	MTK_MML_DISP_DECOUPLE_LAYER =	0x00001000,
	MTK_MML_DISP_MDP_LAYER =	0x00002000,
	MTK_MML_DISP_NOT_SUPPORT =	0x00004000,
	MTK_HWC_UNCHANGED_LAYER = 0x00008000,
	MTK_HWC_INACTIVE_LAYER = 0x00010000,
	MTK_HWC_UNCHANGED_FBT_LAYER = 0x00020000,
	MTK_DISP_UNCHANGED_RATIO_VALID = 0x00040000,
	MTK_DISP_FBT_RATIO_VALID = 0x00080000,
	MTK_DISP_CLIENT_LAYER = 0x00100000,
	MTK_DISP_SRC_YUV_LAYER = 0x00200000,
};

enum MTK_DISP_CAPS {
	MTK_GLES_FBT_GET_RATIO = 0x00000001,
	MTK_GLES_FBT_UNCHANGED = 0x00000002,
};

struct drm_mtk_layer_config {
	__u32 ovl_id;
	__u32 src_fmt;
	int dataspace;
	__u32 dst_offset_x, dst_offset_y;
	__u32 dst_width, dst_height;
	int ext_sel_layer;
	__u32 src_offset_x, src_offset_y;
	__u32 src_width, src_height;
	__u32 layer_caps;
	__u32 clip; /* drv internal use */
	__u64 buffer_alloc_id;
	__u8 compress;
	__u8 secure;
};

struct wb_frame_info {
	__u32 fmt;
	__u32 src_width, src_height;
	__u32 dst_width, dst_height;
};
#define LYE_CRTC 4
struct drm_mtk_layering_info {
	struct drm_mtk_layer_config *input_config[LYE_CRTC];
	int disp_mode[LYE_CRTC];
	/* index of crtc display mode including resolution, fps... */
	int disp_mode_idx[LYE_CRTC];
	int layer_num[LYE_CRTC];
	int gles_head[LYE_CRTC];
	int gles_tail[LYE_CRTC];
	__u32 disp_caps[LYE_CRTC];
	__u32 frame_idx[LYE_CRTC];
	int hrt_num;
	__u32 disp_idx;
	__u32 disp_list;
	/* res_idx: SF/HWC selects which resolution to use */
	int res_idx;
	__u32 hrt_weight;
	__u32 hrt_idx;
	struct mml_frame_info *mml_cfg[LYE_CRTC];
	struct wb_frame_info wb_cfg[LYE_CRTC];
};

/**
 * A structure for fence retrival.
 *
 * @crtc_id:
 * @fence_fd:
 * @fence_idx:
 */
struct drm_mtk_fence {
	/* input */
	__u32 crtc_id; /**< Id */

	/* output */
	__s32 fence_fd;
	/* device */
	__u32 fence_idx;
};

enum DRM_REPAINT_TYPE {
	DRM_WAIT_FOR_REPAINT,
	DRM_REPAINT_FOR_ANTI_LATENCY,
	DRM_REPAINT_FOR_SWITCH_DECOUPLE,
	DRM_REPAINT_FOR_SWITCH_DECOUPLE_MIRROR,
	DRM_REPAINT_FOR_IDLE,
	DRM_REPAINT_TYPE_NUM,
};

enum MTK_DRM_DISP_FEATURE {
	DRM_DISP_FEATURE_HRT = 0x00000001,
	DRM_DISP_FEATURE_DISP_SELF_REFRESH = 0x00000002,
	DRM_DISP_FEATURE_RPO = 0x00000004,
	DRM_DISP_FEATURE_FORCE_DISABLE_AOD = 0x00000008,
	DRM_DISP_FEATURE_OUTPUT_ROTATED = 0x00000010,
	DRM_DISP_FEATURE_THREE_SESSION = 0x00000020,
	DRM_DISP_FEATURE_FBDC = 0x00000040,
	DRM_DISP_FEATURE_SF_PRESENT_FENCE = 0x00000080,
	DRM_DISP_FEATURE_PQ_34_COLOR_MATRIX = 0x00000100,
	/*Msync*/
	DRM_DISP_FEATURE_MSYNC2_0 = 0x00000200,
	DRM_DISP_FEATURE_MML_PRIMARY = 0x00000400,
	DRM_DISP_FEATURE_VIRUTAL_DISPLAY = 0x00000800,
	DRM_DISP_FEATURE_IOMMU = 0x00001000,
	DRM_DISP_FEATURE_OVL_BW_MONITOR = 0x00002000,
	DRM_DISP_FEATURE_GPU_CACHE = 0x00004000,
	DRM_DISP_FEATURE_SPHRT = 0x00008000,
};

enum MTK_DRM_DUMP_POINT {
	MTK_DRM_BEFORE_PQ,
	MTK_DRM_AFTER_PQ,
	MTK_DRM_DUMP_POINT_NUM,
};

enum mtk_mmsys_id {
	MMSYS_MT2701 = 0x2701,
	MMSYS_MT2712 = 0x2712,
	MMSYS_MT8173 = 0x8173,
	MMSYS_MT6779 = 0x6779,
	MMSYS_MT6885 = 0x6885,
	MMSYS_MT6983 = 0x6983,
	MMSYS_MT6985 = 0x6985,
	MMSYS_MT6873 = 0x6873,
	MMSYS_MT6853 = 0x6853,
	MMSYS_MT6833 = 0x6833,
	MMSYS_MT6877 = 0x6877,
	MMSYS_MT6879 = 0x6879,
	MMSYS_MT6895 = 0x6895,
	MMSYS_MT6886 = 0x6886,
	MMSYS_MT6855 = 0x6855,
	MMSYS_MT6835 = 0x6835,
	MMSYS_MAX,
};

#define MTK_DRM_COLOR_FORMAT_A_BIT (1 << MTK_DRM_COLOR_FORMAT_A)
#define MTK_DRM_COLOR_FORMAT_R_BIT (1 << MTK_DRM_COLOR_FORMAT_R)
#define MTK_DRM_COLOR_FORMAT_G_BIT (1 << MTK_DRM_COLOR_FORMAT_G)
#define MTK_DRM_COLOR_FORMAT_B_BIT (1 << MTK_DRM_COLOR_FORMAT_B)
#define MTK_DRM_COLOR_FORMAT_Y_BIT (1 << MTK_DRM_COLOR_FORMAT_Y)
#define MTK_DRM_COLOR_FORMAT_U_BIT (1 << MTK_DRM_COLOR_FORMAT_U)
#define MTK_DRM_COLOR_FORMAT_V_BIT (1 << MTK_DRM_COLOR_FORMAT_V)
#define MTK_DRM_COLOR_FORMAT_S_BIT (1 << MTK_DRM_COLOR_FORMAT_S)
#define MTK_DRM_COLOR_FORMAT_H_BIT (1 << MTK_DRM_COLOR_FORMAT_H)
#define MTK_DRM_COLOR_FORMAT_M_BIT (1 << MTK_DRM_COLOR_FORMAT_M)


#define MTK_DRM_DISP_CHIST_CHANNEL_COUNT 7

enum MTK_DRM_CHIST_COLOR_FORMT {
	MTK_DRM_COLOR_FORMAT_A,
	MTK_DRM_COLOR_FORMAT_R,
	MTK_DRM_COLOR_FORMAT_G,
	MTK_DRM_COLOR_FORMAT_B,
	MTK_DRM_COLOR_FORMAT_Y,
	MTK_DRM_COLOR_FORMAT_U,
	MTK_DRM_COLOR_FORMAT_V,
	MTK_DRM_COLOR_FORMAT_S,
	MTK_DRM_COLOR_FORMAT_H,
	MTK_DRM_COLOR_FORMAT_M,
	MTK_DRM_COLOR_FORMAT_MAX
};

enum MTK_DRM_CHIST_CALLER {
	MTK_DRM_CHIST_CALLER_PQ,
	MTK_DRM_CHIST_CALLER_HWC,
	MTK_DRM_CHIST_CALLER_UNKONW
};

struct mtk_drm_disp_caps_info {
	unsigned int hw_ver;
	unsigned int disp_feature_flag;
	int lcm_degree; /* for rotate180 */
	unsigned int rsz_in_max[2]; /* for RPO { width, height } */

	/* for WCG */
	int lcm_color_mode;
	unsigned int max_luminance;
	unsigned int average_luminance;
	unsigned int min_luminance;

	/* for color histogram */
	unsigned int color_format;
	unsigned int max_bin;
	unsigned int max_channel;

	/* Msync2.0 */
	unsigned int msync_level_num;
};

enum MTK_CRTC_ABILITY {
	ABILITY_FBDC = BIT(0),
	ABILITY_EXT_LAYER = BIT(1),
	ABILITY_IDLEMGR = BIT(2),
	ABILITY_ESD_CHECK = BIT(3),
	ABILITY_PQ = BIT(4),
	ABILITY_RSZ = BIT(5),
	ABILITY_MML = BIT(6),
	ABILITY_WCG = BIT(7),
	ABILITY_PRE_TE = BIT(8),
	ABILITY_BW_MONITOR = BIT(9),
	ABILITY_CWB = BIT(10),
	ABILITY_MSYNC20 = BIT(11),
};

struct mtk_drm_wb_caps {
	_Bool support;
	_Bool rsz;
	_Bool rsz_crop;
	unsigned int rsz_out_min_w;
	unsigned int rsz_out_min_h;
	unsigned int rsz_out_max_w;
	unsigned int rsz_out_max_h;
};

struct mtk_drm_conn_caps {
	unsigned int lcm_degree;
};

#define MAX_MODES 30

struct mtk_drm_connector_caps {
	struct mtk_drm_conn_caps conn_caps;
	unsigned int width_after_pq[MAX_MODES];
	unsigned int height_after_pq[MAX_MODES];
};

struct mtk_drm_mode_ext_info {
	unsigned int crtc_id;
	unsigned int mode_num;
	unsigned int *total_offset;
};

struct mtk_drm_crtc_caps {
	struct mtk_drm_wb_caps wb_caps[MTK_DRM_DUMP_POINT_NUM];
	unsigned int crtc_ability;
	unsigned int ovl_csc_bit_number;
};

struct drm_mtk_session_info {
	unsigned int session_id;
	unsigned int vsyncFPS;
	unsigned int physicalWidthUm;
	unsigned int physicalHeightUm;
};

enum drm_disp_ccorr_id_t {
	DRM_DISP_CCORR0 = 0,
	DRM_DISP_CCORR1,
	DRM_DISP_CCORR_TOTAL
};

struct DRM_DISP_CCORR_COEF_T {
	enum drm_disp_ccorr_id_t hw_id;
	unsigned int coef[3][3];
	unsigned int offset[3];
	int FinalBacklight;
	int silky_bright_flag;
};

enum drm_disp_gamma_id_t {
	DRM_DISP_GAMMA0 = 0,
	DRM_DISP_GAMMA1,
	DRM_DISP_GAMMA_TOTAL
};

#define DRM_DISP_GAMMA_LUT_SIZE 512

struct DRM_DISP_GAMMA_LUT_T {
	enum drm_disp_gamma_id_t hw_id;
	unsigned int lut[DRM_DISP_GAMMA_LUT_SIZE];
};

struct DRM_DISP_READ_REG {
	unsigned int reg;
	unsigned int val;
	unsigned int mask;
};

struct DRM_DISP_WRITE_REG {
	unsigned int reg;
	unsigned int val;
	unsigned int mask;
};

struct DISP_TDSHP_REG {
	__u32 tdshp_softcoring_gain;
	__u32 tdshp_gain_high;
	__u32 tdshp_gain_mid;
	__u32 tdshp_ink_sel;
	__u32 tdshp_bypass_high;
	__u32 tdshp_bypass_mid;
	__u32 tdshp_en;
	__u32 tdshp_limit_ratio;
	__u32 tdshp_gain;
	__u32 tdshp_coring_zero;
	__u32 tdshp_coring_thr;
	__u32 tdshp_coring_value;
	__u32 tdshp_bound;
	__u32 tdshp_limit;
	__u32 tdshp_sat_proc;
	__u32 tdshp_ac_lpf_coe;
	__u32 tdshp_clip_thr;
	__u32 tdshp_clip_ratio;
	__u32 tdshp_clip_en;
	__u32 tdshp_ylev_p048;
	__u32 tdshp_ylev_p032;
	__u32 tdshp_ylev_p016;
	__u32 tdshp_ylev_p000;
	__u32 tdshp_ylev_p112;
	__u32 tdshp_ylev_p096;
	__u32 tdshp_ylev_p080;
	__u32 tdshp_ylev_p064;
	__u32 tdshp_ylev_p176;
	__u32 tdshp_ylev_p160;
	__u32 tdshp_ylev_p144;
	__u32 tdshp_ylev_p128;
	__u32 tdshp_ylev_p240;
	__u32 tdshp_ylev_p224;
	__u32 tdshp_ylev_p208;
	__u32 tdshp_ylev_p192;
	__u32 tdshp_ylev_en;
	__u32 tdshp_ylev_alpha;
	__u32 tdshp_ylev_256;
	__u32 pbc1_radius_r;
	__u32 pbc1_theta_r;
	__u32 pbc1_rslope_1;
	__u32 pbc1_gain;
	__u32 pbc1_lpf_en;
	__u32 pbc1_en;
	__u32 pbc1_lpf_gain;
	__u32 pbc1_tslope;
	__u32 pbc1_radius_c;
	__u32 pbc1_theta_c;
	__u32 pbc1_edge_slope;
	__u32 pbc1_edge_thr;
	__u32 pbc1_edge_en;
	__u32 pbc1_conf_gain;
	__u32 pbc1_rslope;
	__u32 pbc2_radius_r;
	__u32 pbc2_theta_r;
	__u32 pbc2_rslope_1;
	__u32 pbc2_gain;
	__u32 pbc2_lpf_en;
	__u32 pbc2_en;
	__u32 pbc2_lpf_gain;
	__u32 pbc2_tslope;
	__u32 pbc2_radius_c;
	__u32 pbc2_theta_c;
	__u32 pbc2_edge_slope;
	__u32 pbc2_edge_thr;
	__u32 pbc2_edge_en;
	__u32 pbc2_conf_gain;
	__u32 pbc2_rslope;
	__u32 pbc3_radius_r;
	__u32 pbc3_theta_r;
	__u32 pbc3_rslope_1;
	__u32 pbc3_gain;
	__u32 pbc3_lpf_en;
	__u32 pbc3_en;
	__u32 pbc3_lpf_gain;
	__u32 pbc3_tslope;
	__u32 pbc3_radius_c;
	__u32 pbc3_theta_c;
	__u32 pbc3_edge_slope;
	__u32 pbc3_edge_thr;
	__u32 pbc3_edge_en;
	__u32 pbc3_conf_gain;
	__u32 pbc3_rslope;
	__u32 tdshp_mid_softlimit_ratio;
	__u32 tdshp_mid_coring_zero;
	__u32 tdshp_mid_coring_thr;
	__u32 tdshp_mid_softcoring_gain;
	__u32 tdshp_mid_coring_value;
	__u32 tdshp_mid_bound;
	__u32 tdshp_mid_limit;
	__u32 tdshp_high_softlimit_ratio;
	__u32 tdshp_high_coring_zero;
	__u32 tdshp_high_coring_thr;
	__u32 tdshp_high_softcoring_gain;
	__u32 tdshp_high_coring_value;
	__u32 tdshp_high_bound;
	__u32 tdshp_high_limit;
	__u32 edf_clip_ratio_inc;
	__u32 edf_edge_gain;
	__u32 edf_detail_gain;
	__u32 edf_flat_gain;
	__u32 edf_gain_en;
	__u32 edf_edge_th;
	__u32 edf_detail_fall_th;
	__u32 edf_detail_rise_th;
	__u32 edf_flat_th;
	__u32 edf_edge_slope;
	__u32 edf_detail_fall_slope;
	__u32 edf_detail_rise_slope;
	__u32 edf_flat_slope;
	__u32 edf_edge_mono_slope;
	__u32 edf_edge_mono_th;
	__u32 edf_edge_mag_slope;
	__u32 edf_edge_mag_th;
	__u32 edf_edge_trend_flat_mag;
	__u32 edf_edge_trend_slope;
	__u32 edf_edge_trend_th;
	__u32 edf_bld_wgt_mag;
	__u32 edf_bld_wgt_mono;
	__u32 edf_bld_wgt_trend;
	__u32 tdshp_cboost_lmt_u;
	__u32 tdshp_cboost_lmt_l;
	__u32 tdshp_cboost_en;
	__u32 tdshp_cboost_gain;
	__u32 tdshp_cboost_yconst;
	__u32 tdshp_cboost_yoffset_sel;
	__u32 tdshp_cboost_yoffset;
	__u32 tdshp_post_ylev_p048;
	__u32 tdshp_post_ylev_p032;
	__u32 tdshp_post_ylev_p016;
	__u32 tdshp_post_ylev_p000;
	__u32 tdshp_post_ylev_p112;
	__u32 tdshp_post_ylev_p096;
	__u32 tdshp_post_ylev_p080;
	__u32 tdshp_post_ylev_p064;
	__u32 tdshp_post_ylev_p176;
	__u32 tdshp_post_ylev_p160;
	__u32 tdshp_post_ylev_p144;
	__u32 tdshp_post_ylev_p128;
	__u32 tdshp_post_ylev_p240;
	__u32 tdshp_post_ylev_p224;
	__u32 tdshp_post_ylev_p208;
	__u32 tdshp_post_ylev_p192;
	__u32 tdshp_post_ylev_en;
	__u32 tdshp_post_ylev_alpha;
	__u32 tdshp_post_ylev_256;
};

struct DISP_TDSHP_DISPLAY_SIZE {
	int width;
	int height;
	int lcm_width;
	int lcm_height;
};

struct DISP_MDP_AAL_CLARITY_REG {
	// Bilateral
	__u32 bilateral_impulse_noise_en;
	__u32 dre_bilateral_detect_en;
	__u32 bilateral_range_flt_slope;
	__u32 bilateral_flt_en;
	__u32 have_bilateral_filter;
	__u32 dre_output_mode;

	// Bilateral Blending
	__u32 dre_bilateral_activate_blending_A;
	__u32 dre_bilateral_activate_blending_B;
	__u32 dre_bilateral_activate_blending_C;
	__u32 dre_bilateral_activate_blending_D;
	__u32 dre_bilateral_activate_blending_wgt_gain;
	__u32 dre_bilateral_blending_en;
	__u32 dre_bilateral_blending_wgt;
	__u32 dre_bilateral_blending_wgt_mode;
	__u32 dre_bilateral_size_blending_wgt;

	// Filter 1
	__u32 bilateral_custom_range_flt1_0_0;
	__u32 bilateral_custom_range_flt1_0_1;
	__u32 bilateral_custom_range_flt1_0_2;
	__u32 bilateral_custom_range_flt1_0_3;
	__u32 bilateral_custom_range_flt1_0_4;

	__u32 bilateral_custom_range_flt1_1_0;
	__u32 bilateral_custom_range_flt1_1_1;
	__u32 bilateral_custom_range_flt1_1_2;
	__u32 bilateral_custom_range_flt1_1_3;
	__u32 bilateral_custom_range_flt1_1_4;

	__u32 bilateral_custom_range_flt1_2_0;
	__u32 bilateral_custom_range_flt1_2_1;
	__u32 bilateral_custom_range_flt1_2_2;
	__u32 bilateral_custom_range_flt1_2_3;
	__u32 bilateral_custom_range_flt1_2_4;

	// Filter 2
	__u32 bilateral_custom_range_flt2_0_0;
	__u32 bilateral_custom_range_flt2_0_1;
	__u32 bilateral_custom_range_flt2_0_2;
	__u32 bilateral_custom_range_flt2_0_3;
	__u32 bilateral_custom_range_flt2_0_4;

	__u32 bilateral_custom_range_flt2_1_0;
	__u32 bilateral_custom_range_flt2_1_1;
	__u32 bilateral_custom_range_flt2_1_2;
	__u32 bilateral_custom_range_flt2_1_3;
	__u32 bilateral_custom_range_flt2_1_4;

	__u32 bilateral_custom_range_flt2_2_0;
	__u32 bilateral_custom_range_flt2_2_1;
	__u32 bilateral_custom_range_flt2_2_2;
	__u32 bilateral_custom_range_flt2_2_3;
	__u32 bilateral_custom_range_flt2_2_4;

	// Bilateral Flt Config
	__u32 bilateral_contrary_blending_wgt;
	__u32 bilateral_custom_range_flt_gain;
	__u32 bilateral_custom_range_flt_slope;
	__u32 bilateral_range_flt_gain;
	__u32 bilateral_size_blending_wgt;

	// Bilateral Frequency Blending
	__u32 bilateral_contrary_blending_out_wgt;
	__u32 bilateral_custom_range_flt1_out_wgt;
	__u32 bilateral_custom_range_flt2_out_wgt;
	__u32 bilateral_range_flt_out_wgt;
	__u32 bilateral_size_blending_out_wgt;

	// Bilateral Region Protection
	__u32 dre_bilateral_blending_region_protection_en;
	__u32 dre_bilateral_region_protection_activate_A;
	__u32 dre_bilateral_region_protection_activate_B;
	__u32 dre_bilateral_region_protection_activate_C;
	__u32 dre_bilateral_region_protection_activate_D;
	__u32 dre_bilateral_region_protection_input_shift_bit;
};

struct DISP_TDSHP_CLARITY_REG {
	// High & Mid Gain
	__u32 tdshp_gain_high;
	__u32 tdshp_gain_mid;

	// Mid-Band Vertical Filter
	__u32 mid_coef_v_custom_range_flt_0_0;
	__u32 mid_coef_v_custom_range_flt_0_1;
	__u32 mid_coef_v_custom_range_flt_0_2;
	__u32 mid_coef_v_custom_range_flt_0_3;
	__u32 mid_coef_v_custom_range_flt_0_4;

	__u32 mid_coef_v_custom_range_flt_1_0;
	__u32 mid_coef_v_custom_range_flt_1_1;
	__u32 mid_coef_v_custom_range_flt_1_2;
	__u32 mid_coef_v_custom_range_flt_1_3;
	__u32 mid_coef_v_custom_range_flt_1_4;

	__u32 mid_coef_v_custom_range_flt_2_0;
	__u32 mid_coef_v_custom_range_flt_2_1;
	__u32 mid_coef_v_custom_range_flt_2_2;
	__u32 mid_coef_v_custom_range_flt_2_3;
	__u32 mid_coef_v_custom_range_flt_2_4;

	// Mid-Band Horizontal Filter
	__u32 mid_coef_h_custom_range_flt_0_0;
	__u32 mid_coef_h_custom_range_flt_0_1;
	__u32 mid_coef_h_custom_range_flt_0_2;
	__u32 mid_coef_h_custom_range_flt_0_3;
	__u32 mid_coef_h_custom_range_flt_0_4;

	__u32 mid_coef_h_custom_range_flt_1_0;
	__u32 mid_coef_h_custom_range_flt_1_1;
	__u32 mid_coef_h_custom_range_flt_1_2;
	__u32 mid_coef_h_custom_range_flt_1_3;
	__u32 mid_coef_h_custom_range_flt_1_4;

	__u32 mid_coef_h_custom_range_flt_2_0;
	__u32 mid_coef_h_custom_range_flt_2_1;
	__u32 mid_coef_h_custom_range_flt_2_2;
	__u32 mid_coef_h_custom_range_flt_2_3;
	__u32 mid_coef_h_custom_range_flt_2_4;

	// High-Band Vertical Filter
	__u32 high_coef_v_custom_range_flt_0_0;
	__u32 high_coef_v_custom_range_flt_0_1;
	__u32 high_coef_v_custom_range_flt_0_2;
	__u32 high_coef_v_custom_range_flt_0_3;
	__u32 high_coef_v_custom_range_flt_0_4;

	__u32 high_coef_v_custom_range_flt_1_0;
	__u32 high_coef_v_custom_range_flt_1_1;
	__u32 high_coef_v_custom_range_flt_1_2;
	__u32 high_coef_v_custom_range_flt_1_3;
	__u32 high_coef_v_custom_range_flt_1_4;

	__u32 high_coef_v_custom_range_flt_2_0;
	__u32 high_coef_v_custom_range_flt_2_1;
	__u32 high_coef_v_custom_range_flt_2_2;
	__u32 high_coef_v_custom_range_flt_2_3;
	__u32 high_coef_v_custom_range_flt_2_4;

	// High-Band Horizontal Filter
	__u32 high_coef_h_custom_range_flt_0_0;
	__u32 high_coef_h_custom_range_flt_0_1;
	__u32 high_coef_h_custom_range_flt_0_2;
	__u32 high_coef_h_custom_range_flt_0_3;
	__u32 high_coef_h_custom_range_flt_0_4;

	__u32 high_coef_h_custom_range_flt_1_0;
	__u32 high_coef_h_custom_range_flt_1_1;
	__u32 high_coef_h_custom_range_flt_1_2;
	__u32 high_coef_h_custom_range_flt_1_3;
	__u32 high_coef_h_custom_range_flt_1_4;

	__u32 high_coef_h_custom_range_flt_2_0;
	__u32 high_coef_h_custom_range_flt_2_1;
	__u32 high_coef_h_custom_range_flt_2_2;
	__u32 high_coef_h_custom_range_flt_2_3;
	__u32 high_coef_h_custom_range_flt_2_4;

	// High-Band Right-Diagonal Filter
	__u32 high_coef_rd_custom_range_flt_0_0;
	__u32 high_coef_rd_custom_range_flt_0_1;
	__u32 high_coef_rd_custom_range_flt_0_2;
	__u32 high_coef_rd_custom_range_flt_0_3;
	__u32 high_coef_rd_custom_range_flt_0_4;

	__u32 high_coef_rd_custom_range_flt_1_0;
	__u32 high_coef_rd_custom_range_flt_1_1;
	__u32 high_coef_rd_custom_range_flt_1_2;
	__u32 high_coef_rd_custom_range_flt_1_3;
	__u32 high_coef_rd_custom_range_flt_1_4;

	__u32 high_coef_rd_custom_range_flt_2_0;
	__u32 high_coef_rd_custom_range_flt_2_1;
	__u32 high_coef_rd_custom_range_flt_2_2;
	__u32 high_coef_rd_custom_range_flt_2_3;
	__u32 high_coef_rd_custom_range_flt_2_4;

	// High-Band Left-Diagonal Filter
	__u32 high_coef_ld_custom_range_flt_0_0;
	__u32 high_coef_ld_custom_range_flt_0_1;
	__u32 high_coef_ld_custom_range_flt_0_2;
	__u32 high_coef_ld_custom_range_flt_0_3;
	__u32 high_coef_ld_custom_range_flt_0_4;

	__u32 high_coef_ld_custom_range_flt_1_0;
	__u32 high_coef_ld_custom_range_flt_1_1;
	__u32 high_coef_ld_custom_range_flt_1_2;
	__u32 high_coef_ld_custom_range_flt_1_3;
	__u32 high_coef_ld_custom_range_flt_1_4;

	__u32 high_coef_ld_custom_range_flt_2_0;
	__u32 high_coef_ld_custom_range_flt_2_1;
	__u32 high_coef_ld_custom_range_flt_2_2;
	__u32 high_coef_ld_custom_range_flt_2_3;
	__u32 high_coef_ld_custom_range_flt_2_4;

	// Active Parameters
	__u32 mid_negative_offset;
	__u32 mid_positive_offset;
	__u32 high_negative_offset;
	__u32 high_positive_offset;

	// Active Parameters Frequency D
	__u32 D_active_parameter_N_gain;
	__u32 D_active_parameter_N_offset;
	__u32 D_active_parameter_P_gain;
	__u32 D_active_parameter_P_offset;

	// Active Parameters Frequency H
	__u32 High_active_parameter_N_gain;
	__u32 High_active_parameter_N_offset;
	__u32 High_active_parameter_P_gain;
	__u32 High_active_parameter_P_offset;

	// Active Parameters Frequency L
	__u32 L_active_parameter_N_gain;
	__u32 L_active_parameter_N_offset;
	__u32 L_active_parameter_P_gain;
	__u32 L_active_parameter_P_offset;

	// Active Parameters Frequency M
	__u32 Mid_active_parameter_N_gain;
	__u32 Mid_active_parameter_N_offset;
	__u32 Mid_active_parameter_P_gain;
	__u32 Mid_active_parameter_P_offset;

	// Size Parameters
	__u32 SIZE_PARA_BIG_HUGE;
	__u32 SIZE_PARA_MEDIUM_BIG;
	__u32 SIZE_PARA_SMALL_MEDIUM;

	// Final Size Adaptive Weight Huge
	__u32 high_auto_adaptive_weight_HUGE;
	__u32 high_size_adaptive_weight_HUGE;
	__u32 Mid_auto_adaptive_weight_HUGE;
	__u32 Mid_size_adaptive_weight_HUGE;

	// Final Size Adaptive Weight Big
	__u32 high_auto_adaptive_weight_BIG;
	__u32 high_size_adaptive_weight_BIG;
	__u32 Mid_auto_adaptive_weight_BIG;
	__u32 Mid_size_adaptive_weight_BIG;

	// Final Size Adaptive Weight Medium
	__u32 high_auto_adaptive_weight_MEDIUM;
	__u32 high_size_adaptive_weight_MEDIUM;
	__u32 Mid_auto_adaptive_weight_MEDIUM;
	__u32 Mid_size_adaptive_weight_MEDIUM;

	// Final Size Adaptive Weight Small
	__u32 high_auto_adaptive_weight_SMALL;
	__u32 high_size_adaptive_weight_SMALL;
	__u32 Mid_auto_adaptive_weight_SMALL;
	__u32 Mid_size_adaptive_weight_SMALL;

	// Config
	__u32 FILTER_HIST_EN;
	__u32 FREQ_EXTRACT_ENHANCE;

	// Frequency Weighting
	__u32 freq_D_weighting;
	__u32 freq_H_weighting;
	__u32 freq_L_weighting;
	__u32 freq_M_weighting;

	// Frequency Weighting Final
	__u32 freq_D_final_weighting;
	__u32 freq_L_final_weighting;
	__u32 freq_M_final_weighting;
	__u32 freq_WH_final_weighting;
	__u32 SIZE_PARAMETER;

	// Luma Chroma Parameters
	__u32 chroma_high_gain;
	__u32 chroma_high_index;
	__u32 chroma_low_gain;
	__u32 chroma_low_index;
	__u32 luma_high_gain;
	__u32 luma_high_index;
	__u32 luma_low_gain;
	__u32 luma_low_index;
	__u32 Chroma_adaptive_mode;
	__u32 Chroma_shift;
	__u32 Luma_adaptive_mode;
	__u32 Luma_shift;

	// Base Black & White edges
	__u32 class_0_positive_gain;
	__u32 class_0_negative_gain;
};

struct DISP_CLARITY_REG {
	struct DISP_MDP_AAL_CLARITY_REG mdp_aal_clarity_regs;
	struct DISP_TDSHP_CLARITY_REG disp_tdshp_clarity_regs;
};

struct drm_mtk_channel_hist {
	unsigned int channel_id;
	enum MTK_DRM_CHIST_COLOR_FORMT color_format;
	unsigned int hist[256];
	unsigned int bin_count;
};

struct drm_mtk_chist_info {
	unsigned int present_fence;
	unsigned int device_id;
	enum MTK_DRM_CHIST_CALLER caller;
	unsigned int get_channel_count;
	struct drm_mtk_channel_hist channel_hist[MTK_DRM_DISP_CHIST_CHANNEL_COUNT];
	unsigned int lcm_width;
	unsigned int lcm_height;
};

struct drm_mtk_channel_config {
	_Bool enabled;
	enum MTK_DRM_CHIST_COLOR_FORMT color_format;
	unsigned int bin_count;
	unsigned int channel_id;
	unsigned int blk_width;
	unsigned int blk_height;
	unsigned int roi_start_x;
	unsigned int roi_start_y;
	unsigned int roi_end_x;
	unsigned int roi_end_y;
};

struct drm_mtk_chist_caps {
	unsigned int device_id;
	unsigned int support_color;
	unsigned int lcm_width;
	unsigned int lcm_height;
	struct drm_mtk_channel_config chist_config[MTK_DRM_DISP_CHIST_CHANNEL_COUNT];
};

struct drm_mtk_chist_config {
	unsigned int device_id;
	unsigned int lcm_color_mode;
	unsigned int config_channel_count;
	enum MTK_DRM_CHIST_CALLER caller;
	struct drm_mtk_channel_config chist_config[MTK_DRM_DISP_CHIST_CHANNEL_COUNT];
};


struct drm_mtk_ccorr_caps {
	unsigned int ccorr_bit;
	unsigned int ccorr_number;
	unsigned int ccorr_linear;//1st byte:high 4 bit:CCORR1,low 4 bit:CCORR0
};

struct mtk_drm_pq_caps_info {
	struct drm_mtk_ccorr_caps ccorr_caps;
};

enum MTK_DRM_ODDMR_CTL_CMD {
	MTK_DRM_ODDMR_OD_INIT = 0,
	MTK_DRM_ODDMR_OD_ENABLE = 1,
	MTK_DRM_ODDMR_OD_DISABLE = 2,
	MTK_DRM_ODDMR_DMR_INIT = 3,
	MTK_DRM_ODDMR_DMR_ENABLE = 4,
	MTK_DRM_ODDMR_DMR_DISABLE = 5,
	MTK_DRM_ODDMR_LOAD_PARAM = 6,
	MTK_DRM_ODDMR_OD_READ_SW_REG = 7,
	MTK_DRM_ODDMR_OD_WRITE_SW_REG = 8,
	MTK_DRM_ODDMR_OD_USER_GAIN = 9,
};

struct mtk_drm_oddmr_ctl {
	enum MTK_DRM_ODDMR_CTL_CMD cmd;
	__u32 size;
	__u8 *data;
};

struct mtk_drm_oddmr_param {
	__u32 head_id;
	__u32 size;
	__u8 *data;
	__u32 checksum;
};

#define GET_PANELS_STR_LEN 64
struct mtk_drm_panels_info {
	int connector_cnt;
	int default_connector_id;
	unsigned int *connector_obj_id;
	char **panel_name;
	unsigned int *panel_id;
};

#define DRM_IOCTL_MTK_GEM_CREATE	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_GEM_CREATE, struct drm_mtk_gem_create)

#define DRM_IOCTL_MTK_GEM_MAP_OFFSET	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_GEM_MAP_OFFSET, struct drm_mtk_gem_map_off)

#define DRM_IOCTL_MTK_GEM_SUBMIT	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_GEM_SUBMIT, struct drm_mtk_gem_submit)

#define DRM_IOCTL_MTK_SESSION_CREATE	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SESSION_CREATE, struct drm_mtk_session)

#define DRM_IOCTL_MTK_SESSION_DESTROY	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SESSION_DESTROY, struct drm_mtk_session)

#define DRM_IOCTL_MTK_LAYERING_RULE	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_LAYERING_RULE, struct drm_mtk_layering_info)

#define DRM_IOCTL_MTK_CRTC_GETFENCE	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_CRTC_GETFENCE, struct drm_mtk_fence)

#define DRM_IOCTL_MTK_CRTC_FENCE_REL	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_CRTC_FENCE_REL, struct drm_mtk_fence)

#define DRM_IOCTL_MTK_CRTC_GETSFFENCE	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_CRTC_GETSFFENCE, struct drm_mtk_fence)

#define DRM_IOCTL_MTK_MML_GEM_SUBMIT	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_MML_GEM_SUBMIT, struct mml_submit)

#define DRM_IOCTL_MTK_SET_MSYNC_PARAMS    DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SET_MSYNC_PARAMS, struct msync_parameter_table)

#define DRM_IOCTL_MTK_GET_MSYNC_PARAMS    DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_GET_MSYNC_PARAMS, struct msync_parameter_table)

#define DRM_IOCTL_MTK_FACTORY_LCM_AUTO_TEST	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_FACTORY_LCM_AUTO_TEST, int)

#define DRM_IOCTL_MTK_DRM_SET_LEASE_INFO	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_DRM_SET_LEASE_INFO, int)

#define DRM_IOCTL_MTK_DRM_GET_LEASE_INFO	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_DRM_GET_LEASE_INFO, int)


#define DRM_IOCTL_MTK_WAIT_REPAINT	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_WAIT_REPAINT, unsigned int)

#define DRM_IOCTL_MTK_GET_DISPLAY_CAPS	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_GET_DISPLAY_CAPS, struct mtk_drm_disp_caps_info)

#define DRM_IOCTL_MTK_SET_DDP_MODE     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SET_DDP_MODE, unsigned int)

#define DRM_IOCTL_MTK_GET_SESSION_INFO     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_GET_SESSION_INFO, struct drm_mtk_session_info)

#define DRM_IOCTL_MTK_GET_MASTER_INFO     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_GET_MASTER_INFO, int)

#define DRM_IOCTL_MTK_SEC_HND_TO_GEM_HND     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SEC_HND_TO_GEM_HND, struct drm_mtk_sec_gem_hnd)

#define DRM_IOCTL_MTK_PQ_PERSIST_PROPERTY    DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_PQ_PERSIST_PROPERTY, unsigned int [32])

#define DRM_IOCTL_MTK_SET_CCORR     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SET_CCORR, struct DRM_DISP_CCORR_COEF_T)

#define DRM_IOCTL_MTK_CCORR_EVENTCTL     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_CCORR_EVENTCTL, unsigned int)

#define DRM_IOCTL_MTK_CCORR_GET_IRQ     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_CCORR_GET_IRQ, unsigned int)

#define DRM_IOCTL_MTK_AIBLD_CV_MODE     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_AIBLD_CV_MODE, _Bool)

#define DRM_IOCTL_MTK_SET_GAMMALUT     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SET_GAMMALUT, struct DISP_GAMMA_LUT_T)

#define DRM_IOCTL_MTK_SET_12BIT_GAMMALUT     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SET_12BIT_GAMMALUT, struct DISP_GAMMA_12BIT_LUT_T)

#define DRM_IOCTL_MTK_SET_PQPARAM     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SET_PQPARAM, struct DISP_PQ_PARAM)

#define DRM_IOCTL_MTK_SET_PQINDEX     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SET_PQINDEX, struct DISPLAY_PQ_T)

#define DRM_IOCTL_MTK_SET_COLOR_REG     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SET_COLOR_REG,  struct DISPLAY_COLOR_REG)

#define DRM_IOCTL_MTK_MUTEX_CONTROL     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_MUTEX_CONTROL, unsigned int)

#define DRM_IOCTL_MTK_READ_REG     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_READ_REG, struct DISP_READ_REG)

#define DRM_IOCTL_MTK_WRITE_REG     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_WRITE_REG, struct DISP_WRITE_REG)

#define DRM_IOCTL_MTK_BYPASS_COLOR    DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_BYPASS_COLOR, unsigned int)

#define DRM_IOCTL_MTK_PQ_SET_WINDOW    DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_PQ_SET_WINDOW, struct DISP_PQ_WIN_PARAM)

#define DRM_IOCTL_MTK_GET_LCM_INDEX    DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_GET_LCM_INDEX, unsigned int)

#define DRM_IOCTL_MTK_GET_PANELS_INFO   DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_GET_PANELS_INFO, struct mtk_drm_panels_info)

#define DRM_IOCTL_MTK_SUPPORT_COLOR_TRANSFORM     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SUPPORT_COLOR_TRANSFORM, struct DISP_COLOR_TRANSFORM)

#define DRM_IOCTL_MTK_READ_SW_REG     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_READ_SW_REG, struct DISP_READ_REG)

#define DRM_IOCTL_MTK_WRITE_SW_REG     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_WRITE_SW_REG, struct DISP_WRITE_REG)

#define DRM_IOCTL_MTK_SUPPORT_COLOR_TRANSFORM    DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_SUPPORT_COLOR_TRANSFORM, \
			struct DISP_COLOR_TRANSFORM)

// for Display TDSHP
#define DRM_IOCTL_MTK_SET_DISP_TDSHP_REG      DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_SET_DISP_TDSHP_REG, struct DISP_TDSHP_REG)

#define DRM_IOCTL_MTK_DISP_TDSHP_GET_SIZE      DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_DISP_TDSHP_GET_SIZE, struct DISP_TDSHP_DISPLAY_SIZE)

// for Display Clarity
#define DRM_IOCTL_MTK_DISP_CLARITY_SET_REG      DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_DISP_CLARITY_SET_REG, struct DISP_CLARITY_REG)

#define DRM_IOCTL_MTK_C3D_GET_BIN_NUM       DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_C3D_GET_BIN_NUM, unsigned int)

#define DRM_IOCTL_MTK_C3D_GET_IRQ       DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_C3D_GET_IRQ, unsigned int)

#define DRM_IOCTL_MTK_C3D_EVENTCTL       DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_C3D_EVENTCTL, unsigned int)

#define DRM_IOCTL_MTK_C3D_SET_LUT   DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_C3D_SET_LUT, struct DISP_C3D_LUT)

#define DRM_IOCTL_MTK_SET_BYPASS_C3D   DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_SET_BYPASS_C3D, unsigned int)

#define DRM_IOCTL_MTK_GET_CHIST     DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_GET_CHIST, struct drm_mtk_chist_info)

#define DRM_IOCTL_MTK_GET_CHIST_CAPS     DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_GET_CHIST_CAPS, struct drm_mtk_chist_caps)

#define DRM_IOCTL_MTK_SET_CHIST_CONFIG     DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_SET_CHIST_CONFIG, struct drm_mtk_chist_config)

#define DRM_IOCTL_MTK_SET_DITHER_PARAM    DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_SET_DITHER_PARAM, struct DISP_DITHER_PARAM)
#define DRM_IOCTL_MTK_BYPASS_DISP_GAMMA    DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_BYPASS_DISP_GAMMA, unsigned int)

#define DRM_IOCTL_MTK_GET_PQ_CAPS DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_GET_PQ_CAPS, struct mtk_drm_pq_caps_info)
#define DRM_IOCTL_MTK_SET_PQ_CAPS    DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_SET_PQ_CAPS, struct mtk_drm_pq_caps_info)

#define DRM_IOCTL_MTK_ODDMR_LOAD_PARAM    DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_ODDMR_LOAD_PARAM, struct mtk_drm_oddmr_param)
#define DRM_IOCTL_MTK_ODDMR_CTL    DRM_IOWR(DRM_COMMAND_BASE + \
				DRM_MTK_ODDMR_CTL, struct mtk_drm_oddmr_ctl)

#define DRM_IOCTL_MTK_KICK_IDLE    DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_KICK_IDLE, unsigned int)

#define DRM_IOCTL_MTK_GET_MODE_EXT_INFO DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_GET_MODE_EXT_INFO, struct mtk_drm_mode_ext_info)


/* AAL IOCTL */
#define AAL_HIST_BIN            33	/* [0..32] */
#define AAL_DRE_POINT_NUM       29
#define AAL_DRE_BLK_NUM			(16)

/* Display Clarity */
#define MDP_AAL_CLARITY_READBACK_NUM (7)
#define DISP_TDSHP_CLARITY_READBACK_NUM (12)

struct DISP_AAL_INITREG {
	/* DRE */
	int dre_map_bypass;
	/* ESS */
	int cabc_gainlmt[33];
	/* DRE 3.0 Reg. */
	int dre_s_lower;
	int dre_s_upper;
	int dre_y_lower;
	int dre_y_upper;
	int dre_h_lower;
	int dre_h_upper;
	int dre_h_slope;
	int dre_s_slope;
	int dre_y_slope;
	int dre_x_alpha_base;
	int dre_x_alpha_shift_bit;
	int dre_y_alpha_base;
	int dre_y_alpha_shift_bit;
	int act_win_x_end;
	int dre_blk_x_num;
	int dre_blk_y_num;
	int dre_blk_height;
	int dre_blk_width;
	int dre_blk_area;
	int dre_blk_area_min;
	int hist_bin_type;
	int dre_flat_length_slope;
	int dre_flat_length_th;
	int act_win_y_start;
	int act_win_y_end;
	int blk_num_x_start;
	int blk_num_x_end;
	int dre0_blk_num_x_start;
	int dre0_blk_num_x_end;
	int dre1_blk_num_x_start;
	int dre1_blk_num_x_end;
	int blk_cnt_x_start;
	int blk_cnt_x_end;
	int blk_num_y_start;
	int blk_num_y_end;
	int blk_cnt_y_start;
	int blk_cnt_y_end;
	int last_tile_x_flag;
	int last_tile_y_flag;
	int act_win_x_start;
	int dre0_blk_cnt_x_start;
	int dre0_blk_cnt_x_end;
	int dre1_blk_cnt_x_start;
	int dre1_blk_cnt_x_end;
	int dre0_act_win_x_start;
	int dre0_act_win_x_end;
	int dre1_act_win_x_start;
	int dre1_act_win_x_end;
	_Bool isdual;
	int width;
	int height;
};

enum rgbSeq {
	gain_r,
	gain_g,
	gain_b,
};

struct DISP_AAL_PARAM {
	int DREGainFltStatus[AAL_DRE_POINT_NUM];
	int cabc_fltgain_force;	/* 10-bit ; [0,1023] */
	int cabc_gainlmt[33];
	int FinalBacklight;	/* 10-bit ; [0,1023] */
	int silky_bright_flag;
	int allowPartial;
	int refreshLatency;	/* DISP_AAL_REFRESH_LATENCY */
	unsigned int silky_bright_gain[3];    /* 13-bit ; [1,8192] */
	unsigned long long dre30_gain;
};

struct DISP_DRE30_INIT {
	/* DRE 3.0 SW */
	unsigned long long dre30_hist_addr;
};

struct DISP_AAL_DISPLAY_SIZE {
	int width;
	int height;
	_Bool isdualpipe;
	int aaloverhead;
};

struct DISP_AAL_HIST {
	unsigned int serviceFlags;
	int backlight;
	int aal0_colorHist;
	int aal1_colorHist;
	unsigned int aal0_maxHist[AAL_HIST_BIN];
	unsigned int aal1_maxHist[AAL_HIST_BIN];
	int requestPartial;
	unsigned long long dre30_hist;
	unsigned int panel_type;
	int essStrengthIndex;
	int ess_enable;
	int dre_enable;
	unsigned int aal0_yHist[AAL_HIST_BIN];
	unsigned int aal1_yHist[AAL_HIST_BIN];
	unsigned int MaxHis_denominator_pipe0[AAL_DRE_BLK_NUM];
	unsigned int MaxHis_denominator_pipe1[AAL_DRE_BLK_NUM];
	int srcWidth;
	int srcHeight;
	unsigned int aal0_clarity[MDP_AAL_CLARITY_READBACK_NUM];
	unsigned int aal1_clarity[MDP_AAL_CLARITY_READBACK_NUM];
	unsigned int tdshp0_clarity[DISP_TDSHP_CLARITY_READBACK_NUM];
	unsigned int tdshp1_clarity[DISP_TDSHP_CLARITY_READBACK_NUM];
	int pipeLineNum;
	_Bool need_config;
#ifdef OPLUS_FEATURE_DISPLAY_APOLLO
	unsigned int apollo_backlight;
#endif
};

struct DISP_AAL_ESS20_SPECT_PARAM {
	unsigned int ELVSSPN;
	unsigned int ClarityGain; /* 10-bit ; [0,1023] */
	unsigned int flag;//
};

enum SET_BL_EXT_TYPE {
	SET_BACKLIGHT_LEVEL,
	SET_ELVSS_PN,
	ENABLE_DYN_ELVSS,
	DISABLE_DYN_ELVSS,
};

#define DRM_IOCTL_MTK_AAL_INIT_REG	DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_AAL_INIT_REG, struct DISP_AAL_INITREG)

#define DRM_IOCTL_MTK_AAL_GET_HIST	DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_AAL_GET_HIST, struct DISP_AAL_HIST)

#define DRM_IOCTL_MTK_AAL_SET_PARAM	DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_AAL_SET_PARAM, struct DISP_AAL_PARAM)

#define DRM_IOCTL_MTK_AAL_EVENTCTL	DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_AAL_EVENTCTL, unsigned int)

#define DRM_IOCTL_MTK_AAL_INIT_DRE30	DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_AAL_INIT_DRE30, struct DISP_DRE30_INIT)

#define DRM_IOCTL_MTK_AAL_GET_SIZE	DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_AAL_GET_SIZE, struct DISP_AAL_DISPLAY_SIZE)

#define DRM_IOCTL_MTK_AAL_SET_TRIGGER_STATE	DRM_IOWR(DRM_COMMAND_BASE + \
			DRM_MTK_AAL_SET_TRIGGER_STATE, struct DISP_AAL_TRIG_STATE)

#define DRM_IOCTL_MTK_HDMI_GET_DEV_INFO     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_HDMI_GET_DEV_INFO, struct mtk_dispif_info)
#define DRM_IOCTL_MTK_HDMI_AUDIO_ENABLE     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_HDMI_AUDIO_ENABLE, unsigned int)

#define DRM_IOCTL_MTK_HDMI_AUDIO_CONFIG     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_HDMI_AUDIO_CONFIG, unsigned int)

#define DRM_IOCTL_MTK_HDMI_GET_CAPABILITY     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_HDMI_GET_CAPABILITY, unsigned int)

#define DRM_IOCTL_MTK_AAL_SET_ESS20_SPECT_PARAM     DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_AAL_SET_ESS20_SPECT_PARAM, struct DISP_AAL_ESS20_SPECT_PARAM)

#define DRM_IOCTL_MTK_SET_GAMMA_MUL_DISABLE DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_MTK_SET_GAMMA_MUL_DISABLE, _Bool)

#define MTK_DRM_ADVANCE
#define MTK_DRM_FORMAT_DIM		fourcc_code('D', ' ', '0', '0')
#endif /* _UAPI_MEDIATEK_DRM_H */

