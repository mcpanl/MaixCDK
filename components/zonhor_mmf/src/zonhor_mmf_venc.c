#include "zonhor_mmf.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* C facade over raw CVI VENC */
#define ZLOGI(fmt, ...) printf("[zonhor_venc] " fmt "\n", ##__VA_ARGS__)
#define ZLOGE(fmt, ...) printf("[zonhor_venc] ERROR: " fmt "\n", ##__VA_ARGS__)

typedef struct {
	CVI_BOOL inited;
	CVI_BOOL started;
	CVI_BOOL bound;

	VENC_CHN chn;
	PAYLOAD_TYPE_E payload;

	CVI_U32 width;
	CVI_U32 height;
	CVI_U32 fps;
	CVI_U32 gop;
	CVI_U32 bitrate_kbps;
	CVI_BOOL cbr;

	VB_POOL vb_pool;

	/* Remember last bound input for clean unbind on destroy. */
	z_camera_output_id_t bound_input_id;
	VPSS_GRP bound_grp;
	VPSS_CHN bound_chn;
} zonhor_venc_state_t;

static zonhor_venc_state_t g_states[ZONHOR_MMF_MAX_VENC_CHN];

static zonhor_venc_state_t *get_state(VENC_CHN chn)
{
	if (chn >= ZONHOR_MMF_MAX_VENC_CHN)
		return NULL;
	return &g_states[chn];
}

static CVI_S32 vpss_set_depth(VPSS_GRP grp, VPSS_CHN chn, CVI_U32 depth)
{
	VPSS_CHN_ATTR_S attr;
	CVI_S32 ret;

	memset(&attr, 0, sizeof(attr));
	ret = CVI_VPSS_GetChnAttr(grp, chn, &attr);
	if (ret != CVI_SUCCESS)
		return ret;
	if (attr.u32Depth == depth)
		return CVI_SUCCESS;

	/* Must disable before changing depth on a live channel. */
	ret = CVI_VPSS_DisableChn(grp, chn);
	if (ret != CVI_SUCCESS)
		return ret;

	attr.u32Depth = depth;
	/* Our reserved half-YUV input profile uses NV21. */
	attr.enPixelFormat = PIXEL_FORMAT_NV21;

	ret = CVI_VPSS_SetChnAttr(grp, chn, &attr);
	if (ret != CVI_SUCCESS)
		return ret;

	ret = CVI_VPSS_EnableChn(grp, chn);
	if (ret != CVI_SUCCESS)
		return ret;

	return CVI_SUCCESS;
}

static CVI_S32 vpss_get_bind_pos(z_camera_output_id_t id, VPSS_GRP *grp, VPSS_CHN *chn)
{
	z_camera_output_desc_t desc;

	if (!grp || !chn)
		return CVI_FAILURE;

	memset(&desc, 0, sizeof(desc));
	return ZONHOR_MMF_GetVencBindInfo(id, grp, chn, &desc);
}

/* ── Public API ────────────────────────────────────────────────────────── */
CVI_S32 ZONHOR_MMF_VencCreate(const ZONHOR_MMF_VENC_CFG_S *cfg)
{
	zonhor_venc_state_t *st;
	VENC_CHN_ATTR_S attr;
	VENC_RECV_PIC_PARAM_S recv;
	VENC_PARAM_MOD_S mod;
	VB_POOL_CONFIG_S pool_cfg;
	VENC_CHN_POOL_S venc_pool;
	CVI_S32 ret;
	CVI_U32 w, h;
	CVI_U32 blk_size;
	VB_POOL vb_pool = VB_INVALID_POOLID;

	if (!cfg)
		return CVI_FAILURE;
	if (cfg->cbr != CVI_TRUE)
		return CVI_FAILURE;
	if (cfg->payload != PT_H264)
		return CVI_FAILURE;

	st = get_state(cfg->chn);
	if (!st)
		return CVI_FAILURE;

	/* Support safe re-create after previous failure. */
	if (st->inited) {
		(void)ZONHOR_MMF_VencDestroy(cfg->chn);
		st = get_state(cfg->chn);
		if (!st)
			return CVI_FAILURE;
	}

	w = cfg->width ? cfg->width : 540;
	h = cfg->height ? cfg->height : 960;
	/* Even dimensions for H.264. */
	w &= ~1u;
	h &= ~1u;
	/* Allow bind input whose storage width is 64-aligned (e.g. 540 -> 576). */
	{
		CVI_U32 max_w = ZONHOR_MMF_VPSS_ALIGN_UP(w);
		CVI_U32 max_h = h;

		blk_size = COMMON_GetVencFrameBufferSize(cfg->payload, max_w, max_h);
		if (blk_size == 0) {
			ZLOGE("COMMON_GetVencFrameBufferSize returned 0 for %ux%u", max_w, max_h);
			return CVI_FAILURE;
		}

		memset(&pool_cfg, 0, sizeof(pool_cfg));
		pool_cfg.u32BlkSize = blk_size;
		pool_cfg.u32BlkCnt = 3;
		pool_cfg.enRemapMode = VB_REMAP_MODE_NONE;

		st->vb_pool = CVI_VB_CreatePool(&pool_cfg);
		if (st->vb_pool == VB_INVALID_POOLID) {
			ZLOGE("CVI_VB_CreatePool failed size=%u cnt=3", blk_size);
			return CVI_FAILURE;
		}
		vb_pool = st->vb_pool;

		memset(&attr, 0, sizeof(attr));
		attr.stVencAttr.enType = cfg->payload; /* PT_H264 */
		attr.stVencAttr.u32MaxPicWidth = max_w;
		attr.stVencAttr.u32MaxPicHeight = max_h;
		attr.stVencAttr.u32PicWidth = w;
		attr.stVencAttr.u32PicHeight = h;
	}

	/* ~0.5MB bitstream ring; enough for 540x960 substream. */
	attr.stVencAttr.u32BufSize = 512 * 1024;
	attr.stVencAttr.u32Profile = 0; /* baseline */
	attr.stVencAttr.bByFrame = CVI_TRUE;
	attr.stVencAttr.bEsBufQueueEn = CVI_TRUE;
	attr.stVencAttr.stAttrH264e.bRcnRefShareBuf = CVI_TRUE;

	/* CBR H.264e */
	attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
	attr.stRcAttr.stH264Cbr.u32Gop = (cfg->gop ? cfg->gop : 30);
	attr.stRcAttr.stH264Cbr.u32StatTime = 2;
	attr.stRcAttr.stH264Cbr.u32SrcFrameRate = (cfg->fps ? cfg->fps : 30);
	attr.stRcAttr.stH264Cbr.fr32DstFrameRate = (cfg->fps ? cfg->fps : 30);
	attr.stRcAttr.stH264Cbr.bVariFpsEn = CVI_FALSE;
	attr.stRcAttr.stH264Cbr.u32BitRate = (cfg->bitrate_kbps ? cfg->bitrate_kbps : 2000);

	attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	attr.stGopAttr.stNormalP.s32IPQpDelta = 2;

	/* Must set ModParam before CreateChn. */
	memset(&mod, 0, sizeof(mod));
	mod.enVencModType = MODTYPE_H264E;
	ret = CVI_VENC_GetModParam(&mod);
	if (ret != CVI_SUCCESS) {
		ZLOGE("CVI_VENC_GetModParam failed: 0x%x", ret);
		goto fail;
	}
	mod.stH264eModParam.enH264eVBSource = VB_SOURCE_USER;
	ret = CVI_VENC_SetModParam(&mod);
	if (ret != CVI_SUCCESS) {
		ZLOGE("CVI_VENC_SetModParam(USER) failed: 0x%x", ret);
		goto fail;
	}

	/* Stale chn from unclean exit makes CreateChn hang — clear first. */
	{
		VENC_CHN_ATTR_S existing;
		memset(&existing, 0, sizeof(existing));
		if (CVI_VENC_GetChnAttr(cfg->chn, &existing) == CVI_SUCCESS) {
			ZLOGI("VENC chn %d already exists — clearing", cfg->chn);
			(void)CVI_VENC_StopRecvFrame(cfg->chn);
			(void)CVI_VENC_DetachVbPool(cfg->chn);
			(void)CVI_VENC_DestroyChn(cfg->chn);
		}
	}

	ret = CVI_VENC_CreateChn(cfg->chn, &attr);
	if (ret != CVI_SUCCESS) {
		ZLOGE("CVI_VENC_CreateChn(%d) failed: 0x%x", cfg->chn, ret);
		goto fail;
	}

	memset(&venc_pool, 0, sizeof(venc_pool));
	venc_pool.hPicVbPool = st->vb_pool;
	venc_pool.hPicInfoVbPool = VB_INVALID_POOLID;
	ret = CVI_VENC_AttachVbPool(cfg->chn, &venc_pool);
	if (ret != CVI_SUCCESS) {
		ZLOGE("CVI_VENC_AttachVbPool(%d) failed: 0x%x", cfg->chn, ret);
		(void)CVI_VENC_DestroyChn(cfg->chn);
		goto fail;
	}

	memset(&recv, 0, sizeof(recv));
	recv.s32RecvPicNum = -1; /* unlimited until StopRecvFrame */
	ret = CVI_VENC_StartRecvFrame(cfg->chn, &recv);
	if (ret != CVI_SUCCESS) {
		ZLOGE("CVI_VENC_StartRecvFrame(%d) failed: 0x%x", cfg->chn, ret);
		(void)CVI_VENC_DetachVbPool(cfg->chn);
		(void)CVI_VENC_DestroyChn(cfg->chn);
		goto fail;
	}

	st->inited = CVI_TRUE;
	st->started = CVI_TRUE;
	st->bound = CVI_FALSE;
	st->chn = cfg->chn;
	st->payload = cfg->payload;
	st->width = w;
	st->height = h;
	st->fps = (cfg->fps ? cfg->fps : 30);
	st->gop = (cfg->gop ? cfg->gop : 30);
	st->bitrate_kbps = (cfg->bitrate_kbps ? cfg->bitrate_kbps : 2000);
	st->cbr = CVI_TRUE;
	st->vb_pool = vb_pool;

	ZLOGI("VENC(%d) created H264 Pic=%ux%u Max=%ux%u CBR %ukbps",
	      cfg->chn, w, h, attr.stVencAttr.u32MaxPicWidth,
	      attr.stVencAttr.u32MaxPicHeight, st->bitrate_kbps);
	return CVI_SUCCESS;

fail:
	if (st->vb_pool != VB_INVALID_POOLID) {
		(void)CVI_VB_DestroyPool(st->vb_pool);
		st->vb_pool = VB_INVALID_POOLID;
	}
	/* Best-effort clean stale channel. */
	(void)CVI_VENC_StopRecvFrame(cfg->chn);
	(void)CVI_VENC_DetachVbPool(cfg->chn);
	(void)CVI_VENC_DestroyChn(cfg->chn);
	memset(st, 0, sizeof(*st));
	return ret != CVI_SUCCESS ? ret : CVI_FAILURE;
}

CVI_S32 ZONHOR_MMF_VencDestroy(VENC_CHN chn)
{
	zonhor_venc_state_t *st;
	CVI_S32 ret;

	st = get_state(chn);
	if (!st)
		return CVI_FAILURE;
	if (!st->inited)
		return CVI_SUCCESS;

	/* Exit cleanup order: UnBind -> StopRecvFrame -> DetachVbPool -> DestroyChn -> DestroyPool */
	if (st->bound) {
		(void)SAMPLE_COMM_VPSS_UnBind_VENC(st->bound_grp, st->bound_chn, chn);
		st->bound = CVI_FALSE;
	}

	ret = CVI_VENC_StopRecvFrame(chn);
	if (ret != CVI_SUCCESS)
		ZLOGE("CVI_VENC_StopRecvFrame(%d) failed: 0x%x", chn, ret);

	ret = CVI_VENC_DetachVbPool(chn);
	if (ret != CVI_SUCCESS)
		ZLOGE("CVI_VENC_DetachVbPool(%d) failed: 0x%x", chn, ret);

	ret = CVI_VENC_DestroyChn(chn);
	if (ret != CVI_SUCCESS)
		ZLOGE("CVI_VENC_DestroyChn(%d) failed: 0x%x", chn, ret);

	if (st->vb_pool != VB_INVALID_POOLID) {
		ret = CVI_VB_DestroyPool(st->vb_pool);
		if (ret != CVI_SUCCESS)
			ZLOGE("CVI_VB_DestroyPool(%u) failed: 0x%x", st->vb_pool, ret);
		st->vb_pool = VB_INVALID_POOLID;
	}

	memset(st, 0, sizeof(*st));
	return CVI_SUCCESS;
}

CVI_S32 ZONHOR_MMF_VencSetCropFromOutput(VENC_CHN chn, z_camera_output_id_t id)
{
	zonhor_venc_state_t *st;
	z_camera_output_desc_t desc;
	VENC_CHN_PARAM_S param;
	VENC_CHN_ATTR_S attr;
	CVI_U32 crop_w, crop_h;
	CVI_S32 crop_x, crop_y;
	CVI_S32 ret;

	st = get_state(chn);
	if (!st || !st->inited)
		return CVI_FAILURE;

	memset(&desc, 0, sizeof(desc));
	ret = ZONHOR_MMF_GetOutputDesc(id, &desc);
	if (ret != CVI_SUCCESS)
		return ret;

	crop_w = desc.extent.valid_width ? desc.extent.valid_width
					: desc.extent.logical_width;
	crop_h = desc.extent.valid_height ? desc.extent.valid_height
					: desc.extent.logical_height;
	crop_x = (CVI_S32)desc.extent.valid_x;
	crop_y = (CVI_S32)desc.extent.valid_y;
	/* VENC crop X must be a multiple of 16. */
	crop_x &= ~15;

	ZLOGI("VencSetCropFromOutput chn=%d id=%d logical=%ux%u buffer=%ux%u "
	      "valid=(%u,%u %ux%u) -> crop=(%d,%d %ux%u) venc_pic=%ux%u",
	      chn, (int)id,
	      desc.extent.logical_width, desc.extent.logical_height,
	      desc.extent.buffer_width, desc.extent.buffer_height,
	      desc.extent.valid_x, desc.extent.valid_y,
	      desc.extent.valid_width, desc.extent.valid_height,
	      crop_x, crop_y, crop_w, crop_h, st->width, st->height);

	if (crop_w == 0 || crop_h == 0)
		return CVI_FAILURE;

	/*
	 * No right/bottom padding in storage → crop unnecessary.
	 * left-aligned valid inside wider buffer still needs crop when
	 * buffer_width > valid_width (classic 540 vs align64=576 case).
	 */
	if (!z_extent_needs_crop(&desc.extent) &&
	    desc.extent.buffer_width <= crop_w &&
	    desc.extent.buffer_height <= crop_h) {
		ZLOGI("VencSetCropFromOutput: no padding, skip crop");
		return CVI_SUCCESS;
	}

	memset(&attr, 0, sizeof(attr));
	ret = CVI_VENC_GetChnAttr(chn, &attr);
	if (ret == CVI_SUCCESS) {
		ZLOGI("VENC attr before crop: Pic=%ux%u Max=%ux%u",
		      attr.stVencAttr.u32PicWidth, attr.stVencAttr.u32PicHeight,
		      attr.stVencAttr.u32MaxPicWidth, attr.stVencAttr.u32MaxPicHeight);
	}

	memset(&param, 0, sizeof(param));
	ret = CVI_VENC_GetChnParam(chn, &param);
	if (ret != CVI_SUCCESS) {
		ZLOGE("CVI_VENC_GetChnParam(%d) failed: 0x%x", chn, ret);
		return ret;
	}

	param.stCropCfg.bEnable = CVI_TRUE;
	param.stCropCfg.stRect.s32X = crop_x;
	param.stCropCfg.stRect.s32Y = crop_y;
	param.stCropCfg.stRect.u32Width = crop_w;
	param.stCropCfg.stRect.u32Height = crop_h;

	ret = CVI_VENC_SetChnParam(chn, &param);
	if (ret != CVI_SUCCESS) {
		ZLOGE("CVI_VENC_SetChnParam crop(%d,%d %ux%u) failed: 0x%x",
		      crop_x, crop_y, crop_w, crop_h, ret);
		return ret;
	}

	ZLOGI("VENC(%d) crop enabled (%d,%d %ux%u)", chn, crop_x, crop_y, crop_w, crop_h);
	return CVI_SUCCESS;
}

CVI_S32 ZONHOR_MMF_VencBindInput(VENC_CHN chn, z_camera_output_id_t id)
{
	zonhor_venc_state_t *st;
	VPSS_GRP grp;
	VPSS_CHN vchn;
	z_camera_output_desc_t desc;
	VPSS_CHN_ATTR_S chn_attr;
	VENC_CHN_ATTR_S venc_attr;
	CVI_S32 ret;
	VENC_RECV_PIC_PARAM_S recv;

	st = get_state(chn);
	if (!st || !st->inited)
		return CVI_FAILURE;

	memset(&desc, 0, sizeof(desc));
	ret = vpss_get_bind_pos(id, &grp, &vchn);
	if (ret != CVI_SUCCESS)
		return ret;

	(void)ZONHOR_MMF_GetOutputDesc(id, &desc);
	ZLOGI("VencBindInput chn=%d <- grp=%d chn=%d name=%s "
	      "logical=%ux%u buffer=%ux%u valid=(%u,%u %ux%u) venc_pic=%ux%u",
	      chn, (int)grp, (int)vchn, desc.name ? desc.name : "?",
	      desc.extent.logical_width, desc.extent.logical_height,
	      desc.extent.buffer_width, desc.extent.buffer_height,
	      desc.extent.valid_x, desc.extent.valid_y,
	      desc.extent.valid_width, desc.extent.valid_height,
	      st->width, st->height);

	memset(&chn_attr, 0, sizeof(chn_attr));
	if (CVI_VPSS_GetChnAttr(grp, vchn, &chn_attr) == CVI_SUCCESS) {
		ZLOGI("VPSS G%u-Ch%u attr: %ux%u depth=%u fmt=%d",
		      (unsigned)grp, (unsigned)vchn,
		      chn_attr.u32Width, chn_attr.u32Height,
		      chn_attr.u32Depth, (int)chn_attr.enPixelFormat);
	}

	/* One-shot probe of bind input frame metadata (width vs stride). */
	{
		VIDEO_FRAME_INFO_S probe;
		CVI_U32 old_depth = 0;

		memset(&probe, 0, sizeof(probe));
		memset(&chn_attr, 0, sizeof(chn_attr));
		if (CVI_VPSS_GetChnAttr(grp, vchn, &chn_attr) == CVI_SUCCESS)
			old_depth = chn_attr.u32Depth;
		if (old_depth == 0)
			(void)vpss_set_depth(grp, vchn, 1);
		if (CVI_VPSS_GetChnFrame(grp, vchn, &probe, 200) == CVI_SUCCESS) {
			ZLOGI("bind-input probe: wh=%ux%u stride=%u/%u/%u len=%u/%u/%u fmt=%d",
			      probe.stVFrame.u32Width, probe.stVFrame.u32Height,
			      probe.stVFrame.u32Stride[0], probe.stVFrame.u32Stride[1],
			      probe.stVFrame.u32Stride[2],
			      probe.stVFrame.u32Length[0], probe.stVFrame.u32Length[1],
			      probe.stVFrame.u32Length[2],
			      (int)probe.stVFrame.enPixelFormat);
			(void)CVI_VPSS_ReleaseChnFrame(grp, vchn, &probe);
		} else {
			ZLOGI("bind-input probe: GetChnFrame timeout/fail (ok if just enabled)");
		}
	}

	memset(&venc_attr, 0, sizeof(venc_attr));
	if (CVI_VENC_GetChnAttr(chn, &venc_attr) == CVI_SUCCESS) {
		ZLOGI("VENC(%d) attr: Pic=%ux%u Max=%ux%u",
		      chn,
		      venc_attr.stVencAttr.u32PicWidth,
		      venc_attr.stVencAttr.u32PicHeight,
		      venc_attr.stVencAttr.u32MaxPicWidth,
		      venc_attr.stVencAttr.u32MaxPicHeight);
	}

	/* Bind-path expects VPSS depth=0. */
	ret = vpss_set_depth(grp, vchn, 0);
	if (ret != CVI_SUCCESS)
		return ret;

	ret = CVI_VENC_StopRecvFrame(chn);
	if (ret != CVI_SUCCESS)
		return ret;

	/* Best-effort crop; primary fix is padding-free half extent. */
	ret = ZONHOR_MMF_VencSetCropFromOutput(chn, id);
	if (ret != CVI_SUCCESS)
		ZLOGE("VencSetCropFromOutput before bind failed: 0x%x (continue)", ret);

	ret = SAMPLE_COMM_VPSS_Bind_VENC(grp, vchn, chn);
	if (ret != CVI_SUCCESS) {
		/* Try to keep VENC usable. */
		memset(&recv, 0, sizeof(recv));
		recv.s32RecvPicNum = -1;
		(void)CVI_VENC_StartRecvFrame(chn, &recv);
		return ret;
	}

	st->bound = CVI_TRUE;
	st->bound_input_id = id;
	st->bound_grp = grp;
	st->bound_chn = vchn;

	memset(&recv, 0, sizeof(recv));
	recv.s32RecvPicNum = -1;
	ret = CVI_VENC_StartRecvFrame(chn, &recv);
	if (ret != CVI_SUCCESS) {
		(void)SAMPLE_COMM_VPSS_UnBind_VENC(grp, vchn, chn);
		st->bound = CVI_FALSE;
		return ret;
	}

	ZLOGI("VencBindInput chn=%d ok (G%u-Ch%u -> VENC)", chn,
	      (unsigned)grp, (unsigned)vchn);
	return CVI_SUCCESS;
}

CVI_S32 ZONHOR_MMF_VencUnbindInput(VENC_CHN chn, z_camera_output_id_t id)
{
	zonhor_venc_state_t *st;
	VPSS_GRP grp;
	VPSS_CHN vchn;
	CVI_S32 ret;

	st = get_state(chn);
	if (!st || !st->inited)
		return CVI_FAILURE;

	ret = vpss_get_bind_pos(id, &grp, &vchn);
	if (ret != CVI_SUCCESS)
		return ret;

	if (!st->bound)
		return CVI_SUCCESS;

	(void)SAMPLE_COMM_VPSS_UnBind_VENC(grp, vchn, chn);
	st->bound = CVI_FALSE;

	/* Unbind-path best-effort restore depth>=1 for user GetChnFrame. */
	ret = vpss_set_depth(grp, vchn, 1);
	return ret;
}

CVI_S32 ZONHOR_MMF_VencSendFrame(VENC_CHN chn, const VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
	if (!frame)
		return CVI_FAILURE;
	return CVI_VENC_SendFrame(chn, frame, timeout_ms);
}

CVI_S32 ZONHOR_MMF_VencSendNv21UserData(VENC_CHN chn, const CVI_U8 *data,
					 CVI_U32 width, CVI_U32 height,
					 CVI_S32 timeout_ms)
{
	zonhor_venc_state_t *st;
	VB_BLK blk;
	VIDEO_FRAME_INFO_S frame;
	CVI_U32 w, h, y_size, uv_size, blk_size;
	void *vir = NULL;
	CVI_U64 phy;
	CVI_S32 ret;

	if (!data)
		return CVI_FAILURE;

	st = get_state(chn);
	if (!st || !st->inited || st->vb_pool == VB_INVALID_POOLID)
		return CVI_FAILURE;

	w = width ? width : st->width;
	h = height ? height : st->height;
	w &= ~1u;
	h &= ~1u;

	blk_size = COMMON_GetVencFrameBufferSize(st->payload, w, h);
	if (blk_size == 0)
		return CVI_FAILURE;

	blk = CVI_VB_GetBlock(st->vb_pool, blk_size);
	if (blk == VB_INVALID_HANDLE)
		return CVI_FAILURE;

	phy = CVI_VB_Handle2PhysAddr(blk);
	ret = CVI_VB_GetBlockVirAddr(st->vb_pool, blk, &vir);
	if (ret != CVI_SUCCESS || !vir) {
		CVI_VB_ReleaseBlock(blk);
		return ret != CVI_SUCCESS ? ret : CVI_FAILURE;
	}

	y_size = w * h;
	uv_size = y_size / 2;
	memcpy(vir, data, y_size + uv_size);

	memset(&frame, 0, sizeof(frame));
	frame.stVFrame.u32Width = w;
	frame.stVFrame.u32Height = h;
	frame.stVFrame.enPixelFormat = PIXEL_FORMAT_NV21;
	frame.stVFrame.u32Stride[0] = w;
	frame.stVFrame.u32Stride[1] = w;
	frame.stVFrame.u64PhyAddr[0] = phy;
	frame.stVFrame.u64PhyAddr[1] = phy + y_size;
	frame.stVFrame.pu8VirAddr[0] = (CVI_U8 *)vir;
	frame.stVFrame.pu8VirAddr[1] = (CVI_U8 *)vir + y_size;
	frame.stVFrame.u32Length[0] = y_size;
	frame.stVFrame.u32Length[1] = uv_size;

	ret = CVI_VENC_SendFrame(chn, &frame, timeout_ms);
	CVI_VB_ReleaseBlock(blk);
	return ret;
}

CVI_S32 ZONHOR_MMF_VencGetStream(VENC_CHN chn, ZONHOR_MMF_VENC_STREAM_S *out, CVI_S32 timeout_ms)
{
	zonhor_venc_state_t *st;
	VENC_CHN_STATUS_S status;
	CVI_S32 ret;
	CVI_U32 pack_n;
	CVI_S32 waited = 0;
	CVI_S32 deadline = timeout_ms > 0 ? timeout_ms : 200;

	if (!out)
		return CVI_FAILURE;

	st = get_state(chn);
	if (!st || !st->inited)
		return CVI_FAILURE;

	memset(out, 0, sizeof(*out));
	memset(&status, 0, sizeof(status));

	/*
	 * Sophgo MPI: QueryStatus → pstPack slots → GetStream.
	 * Poll until stream data is ready or timeout (matches fb_display demo).
	 */
	while (waited <= deadline) {
		ret = CVI_VENC_QueryStatus(chn, &status);
		if (ret != CVI_SUCCESS)
			return ret;
		if (status.u32LeftStreamFrames > 0 ||
		    (status.u32CurPacks > 0 && status.u32CurPacks < 64 &&
		     status.u32LeftStreamBytes > 0))
			break;
		if (deadline == 0)
			break;
		usleep(1000);
		waited += 1;
	}

	if (!(status.u32LeftStreamFrames > 0 ||
	      (status.u32CurPacks > 0 && status.u32CurPacks < 64 &&
	       status.u32LeftStreamBytes > 0)))
		return CVI_ERR_VENC_BUSY;

	pack_n = status.u32CurPacks ? status.u32CurPacks : 8;
	if (pack_n > ZONHOR_MMF_VENC_MAX_PACKS)
		pack_n = ZONHOR_MMF_VENC_MAX_PACKS;

	memset(out->packs, 0, sizeof(VENC_PACK_S) * pack_n);
	out->stream.pstPack = out->packs;
	out->stream.u32Seq = 0;

	ret = CVI_VENC_GetStream(chn, &out->stream, deadline);
	return ret;
}

CVI_S32 ZONHOR_MMF_VencReleaseStream(VENC_CHN chn, ZONHOR_MMF_VENC_STREAM_S *stream)
{
	if (!stream)
		return CVI_FAILURE;
	return CVI_VENC_ReleaseStream(chn, &stream->stream);
}

