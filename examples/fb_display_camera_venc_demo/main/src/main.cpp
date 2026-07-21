/**
 * fb_display_camera_venc_demo
 *
 * Step 0: FB preview + Enable G3-Ch0 + GetChnFrame probe (bind / MEM)
 * Step 1: Create/Start VENC only
 * Step 2: GetChnFrame(G3) → SendFrame (+ silent stream drain)
 * Step 3: GetStream + print pack meta
 * Step 4: VPSS G3-Ch0 → VENC Bind (no user Send); GetStream only
 *
 * Later steps gated by --step N. See 000_zonhor_fb_display_camera_venc_demo_plan.md
 */

#include "maix_basic.hpp"
#include "z_display.hpp"
#include "maix_camera.hpp"
#include "z_image.hpp"
#include "main.h"

extern "C" {
#include "zonhor_mmf.h"
#include "zonhor_graph_profile.h"
#include "cvi_vpss.h"
#include "cvi_sys.h"
#include "cvi_venc.h"
#include "cvi_vb.h"
#include "cvi_buffer.h"
#include "sample_comm.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

using namespace maix;

static constexpr int kPanelWidth = 172;
static constexpr int kPanelHeight = 320;
static constexpr const char *kDefaultFbDevice = "/dev/fb0";
static constexpr int kCamWidth = 720;
static constexpr int kCamHeight = 720;

static constexpr VPSS_GRP kG2 = 2;
static constexpr VPSS_CHN kG2ChC = 2; /* half YUV → G3 */
static constexpr VPSS_GRP kG3 = 3;
static constexpr VPSS_CHN kG3Ch0 = 0;

static constexpr VENC_CHN kVencChn = 0;
static constexpr int kDefaultStep = 4;
static constexpr int kStep2SendCount = 10;
static constexpr int kStep4GetCount = 15;
static constexpr int kStep4PerfMs = 3000;
static constexpr CVI_S32 kVencSendTimeoutMs = 2000;
static constexpr CVI_S32 kVencGetStreamTimeoutMs = 200;

/* Track bind state for logging; actual state lives in zonhor_mmf. */
static bool g_venc_bound = false;

static ZONHOR_MMF_VENC_CFG_S make_venc_cfg(VENC_CHN chn, CVI_U32 width, CVI_U32 height)
{
	ZONHOR_MMF_VENC_CFG_S cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.chn = chn;
	cfg.payload = PT_H264;
	cfg.width = width;
	cfg.height = height;
	cfg.fps = 30;
	cfg.gop = 30;
	cfg.bitrate_kbps = 2000;
	cfg.cbr = CVI_TRUE;
	return cfg;
}

/** Step 1: Create + StartRecvFrame via zonhor_mmf wrapper. */
static CVI_S32 venc_create_start(VENC_CHN chn, CVI_U32 width, CVI_U32 height)
{
	ZONHOR_MMF_VENC_CFG_S cfg = make_venc_cfg(chn, width, height);
	CVI_S32 ret = ZONHOR_MMF_VencCreate(&cfg);
	if (ret != CVI_SUCCESS) {
		log::error("ZONHOR_MMF_VencCreate(%d) failed: 0x%x\n", chn, ret);
		return ret;
	}
	log::info("ZONHOR_MMF_VencCreate(%d) ok H264 %ux%u\n", chn, width, height);
	return CVI_SUCCESS;
}

static void venc_destroy(VENC_CHN chn)
{
	if (g_venc_bound) {
		CVI_S32 ret = ZONHOR_MMF_VencUnbindInput(chn, Z_CAMERA_OUTPUT_SUB_VENC);
		if (ret != CVI_SUCCESS)
			log::warn("ZONHOR_MMF_VencUnbindInput(%d): 0x%x\n", chn, ret);
		else
			log::info("ZONHOR_MMF_VencUnbindInput(%d) ok\n", chn);
		g_venc_bound = false;
	}

	CVI_S32 ret = ZONHOR_MMF_VencDestroy(chn);
	if (ret != CVI_SUCCESS)
		log::warn("ZONHOR_MMF_VencDestroy(%d): 0x%x\n", chn, ret);
	else
		log::info("ZONHOR_MMF_VencDestroy(%d) ok\n", chn);
}

static void print_usage(const char *prog)
{
	printf("Usage: %s [--step N] [fb_device]\n", prog);
	printf("  --step 0  G3-Ch0 GetFrame probe only\n");
	printf("  --step 1  + Create/Start VENC (no SendFrame)\n");
	printf("  --step 2  + GetFrame(G3)→SendFrame (+silent drain)\n");
	printf("  --step 3  + GetStream pack dump\n");
	printf("  --step 4  + VPSS→VENC Bind (no Send)  [default]\n");
	printf("  Later steps reserved (file)\n");
}

static void dump_proc_file(const char *path, const char *tag)
{
	log::info("===== %s (%s) =====\n", path, tag);
	FILE *fp = fopen(path, "r");
	if (!fp) {
		log::warn("cannot open %s\n", path);
		return;
	}
	char line[512];
	while (fgets(line, sizeof(line), fp))
		fputs(line, stdout);
	fclose(fp);
	fflush(stdout);
	log::info("===== end %s (%s) =====\n", path, tag);
}

static void dump_vpss_proc(const char *tag)
{
	dump_proc_file("/proc/cvitek/vpss", tag);
}

static void dump_venc_proc(const char *tag)
{
	dump_proc_file("/proc/cvitek/venc", tag);
}

static void log_video_frame(const char *tag, const VIDEO_FRAME_S *vf)
{
	if (!vf)
		return;
	log::info("%s fmt=%d wh=%ux%u stride=%u/%u/%u len=%u/%u/%u "
		  "phy=0x%llx/0x%llx/0x%llx pts=%llu "
		  "offset L/R/T/B=%d/%d/%d/%d\n",
		  tag,
		  (int)vf->enPixelFormat,
		  vf->u32Width, vf->u32Height,
		  vf->u32Stride[0], vf->u32Stride[1], vf->u32Stride[2],
		  vf->u32Length[0], vf->u32Length[1], vf->u32Length[2],
		  (unsigned long long)vf->u64PhyAddr[0],
		  (unsigned long long)vf->u64PhyAddr[1],
		  (unsigned long long)vf->u64PhyAddr[2],
		  (unsigned long long)vf->u64PTS,
		  (int)vf->s16OffsetLeft, (int)vf->s16OffsetRight,
		  (int)vf->s16OffsetTop, (int)vf->s16OffsetBottom);
}

/** Enable G3-Ch0 with NV21 + depth>=1 (create-only by default). */
static CVI_S32 enable_group3_ch0(CVI_U32 *out_w, CVI_U32 *out_h)
{
	z_camera_output_desc_t desc;
	VPSS_CHN_ATTR_S attr;
	CVI_S32 ret;
	CVI_BOOL need_set = CVI_FALSE;

	memset(&desc, 0, sizeof(desc));
	memset(&attr, 0, sizeof(attr));

	if (ZONHOR_MMF_GetOutputDesc(Z_CAMERA_OUTPUT_SUB_VENC, &desc) == CVI_SUCCESS) {
		log::info("SUB_VENC profile: grp=%u chn=%u fmt=%u "
			  "logical=%ux%u buffer=%ux%u create_only=%d\n",
			  desc.group_id, desc.channel_id, (unsigned)desc.pixel_format,
			  desc.extent.logical_width, desc.extent.logical_height,
			  desc.extent.buffer_width, desc.extent.buffer_height,
			  (int)desc.create_only);
	}

	ret = CVI_VPSS_GetChnAttr(kG3, kG3Ch0, &attr);
	if (ret != CVI_SUCCESS) {
		log::error("CVI_VPSS_GetChnAttr(G3,0) failed: 0x%x\n", ret);
		return ret;
	}

	if (attr.enPixelFormat != PIXEL_FORMAT_NV21) {
		attr.enPixelFormat = PIXEL_FORMAT_NV21;
		need_set = CVI_TRUE;
	}
	attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
	if (attr.u32Depth < 1) {
		attr.u32Depth = 1;
		need_set = CVI_TRUE;
	}
	if (attr.u32Width == 0 || attr.u32Height == 0) {
		attr.u32Width = desc.extent.logical_width ?
				desc.extent.logical_width : 540;
		attr.u32Height = desc.extent.logical_height ?
				 desc.extent.logical_height : 960;
		need_set = CVI_TRUE;
	}

	if (need_set) {
		ret = CVI_VPSS_SetChnAttr(kG3, kG3Ch0, &attr);
		if (ret != CVI_SUCCESS) {
			log::error("CVI_VPSS_SetChnAttr(G3,0) failed: 0x%x\n", ret);
			return ret;
		}
	}
	log::info("G3-Ch0 attr: %ux%u depth=%u fmt=%d\n",
		  attr.u32Width, attr.u32Height, attr.u32Depth,
		  (int)attr.enPixelFormat);

	ret = CVI_VPSS_EnableChn(kG3, kG3Ch0);
	if (ret != CVI_SUCCESS) {
		log::error("CVI_VPSS_EnableChn(G3,0) failed: 0x%x\n", ret);
		return ret;
	}
	log::info("CVI_VPSS_EnableChn(G3,0) ok\n");

	if (out_w)
		*out_w = attr.u32Width;
	if (out_h)
		*out_h = attr.u32Height;
	return CVI_SUCCESS;
}

/**
 * For MEM path only: raise G2-ChC depth so GetChnFrame can dequeue.
 * Avoid unnecessary SetChnAttr on the live bind path.
 */
static CVI_S32 set_g2_chc_depth(CVI_U32 depth)
{
	VPSS_CHN_ATTR_S attr;
	CVI_S32 ret;

	memset(&attr, 0, sizeof(attr));
	ret = CVI_VPSS_GetChnAttr(kG2, kG2ChC, &attr);
	if (ret != CVI_SUCCESS) {
		log::error("CVI_VPSS_GetChnAttr(G2,2) failed: 0x%x\n", ret);
		return ret;
	}
	if (attr.u32Depth == depth)
		return CVI_SUCCESS;

	/* Must disable before changing depth on a live channel. */
	CVI_VPSS_DisableChn(kG2, kG2ChC);
	attr.u32Depth = depth;
	attr.enPixelFormat = PIXEL_FORMAT_NV21;
	ret = CVI_VPSS_SetChnAttr(kG2, kG2ChC, &attr);
	if (ret != CVI_SUCCESS) {
		log::error("CVI_VPSS_SetChnAttr(G2,2) depth=%u failed: 0x%x\n", depth, ret);
		return ret;
	}
	ret = CVI_VPSS_EnableChn(kG2, kG2ChC);
	if (ret != CVI_SUCCESS) {
		log::error("CVI_VPSS_EnableChn(G2,2) failed: 0x%x\n", ret);
		return ret;
	}
	log::info("G2-ChC depth set to %u\n", depth);
	return CVI_SUCCESS;
}

static bool try_get_g3_frame(const char *tag)
{
	VIDEO_FRAME_INFO_S frame;
	memset(&frame, 0, sizeof(frame));

	CVI_S32 ret = CVI_VPSS_GetChnFrame(kG3, kG3Ch0, &frame, 2000);
	if (ret != CVI_SUCCESS) {
		log::error("[%s] CVI_VPSS_GetChnFrame(3,0) failed: 0x%x (BUF_EMPTY=0xc006800e)\n",
			   tag, ret);
		return false;
	}
	log::info("[%s] CVI_VPSS_GetChnFrame(3,0) ok\n", tag);
	log_video_frame("G3-Ch0", &frame.stVFrame);
	CVI_VPSS_ReleaseChnFrame(kG3, kG3Ch0, &frame);
	return true;
}

/**
 * Bind path may look "alive" (G3 RecvCnt++) while Ch0 disabled → CostTime=0.
 * After enable, if GetFrame still fails, fall back to MEM:
 *   unbind → GetFrame(G2-ChC) → SendFrame(G3) → GetFrame(G3-Ch0)
 */
static bool probe_g3_bind_then_mem(CVI_U32 *g3_w, CVI_U32 *g3_h)
{
	log::info("=== probe path A: bind G2-ChC → G3 (only EnableChn G3-Ch0) ===\n");
	if (enable_group3_ch0(g3_w, g3_h) != CVI_SUCCESS)
		return false;

	dump_vpss_proc("after EnableChn G3-Ch0 (bind path)");
	time::sleep_ms(500);

	for (int i = 0; i < 5; ++i) {
		if (try_get_g3_frame("bind")) {
			log::info("G3-Ch0 bind-path SUCCESS\n");
			return true;
		}
	}

	log::warn("bind path failed — switch to MEM: unbind + Get/SendFrame\n");
	dump_vpss_proc("before unbind");

	CVI_S32 ret = SAMPLE_COMM_VPSS_UnBind_VPSS(kG2, kG2ChC, kG3);
	if (ret != CVI_SUCCESS)
		log::warn("UnBind G2-ChC→G3: 0x%x (continue MEM attempt)\n", ret);
	else
		log::info("UnBind G2-ChC→G3 ok\n");

	if (set_g2_chc_depth(1) != CVI_SUCCESS)
		return false;
	if (enable_group3_ch0(g3_w, g3_h) != CVI_SUCCESS)
		return false;

	dump_vpss_proc("after unbind + depth=1 (MEM path)");

	for (int i = 0; i < 8; ++i) {
		VIDEO_FRAME_INFO_S in_frame;
		memset(&in_frame, 0, sizeof(in_frame));

		ret = CVI_VPSS_GetChnFrame(kG2, kG2ChC, &in_frame, 2000);
		if (ret != CVI_SUCCESS) {
			log::error("MEM: GetChnFrame(G2,2) failed: 0x%x\n", ret);
			continue;
		}
		log::info("MEM: got G2-ChC frame\n");
		log_video_frame("G2-ChC", &in_frame.stVFrame);

		ret = CVI_VPSS_SendFrame(kG3, &in_frame, 2000);
		if (ret != CVI_SUCCESS) {
			log::error("MEM: SendFrame(G3) failed: 0x%x\n", ret);
			CVI_VPSS_ReleaseChnFrame(kG2, kG2ChC, &in_frame);
			continue;
		}
		log::info("MEM: SendFrame(G3) ok\n");

		bool ok = try_get_g3_frame("mem");
		CVI_VPSS_ReleaseChnFrame(kG2, kG2ChC, &in_frame);
		if (ok) {
			log::info("G3-Ch0 MEM-path SUCCESS\n");
			dump_vpss_proc("after MEM success");
			return true;
		}
	}

	dump_vpss_proc("MEM path exhausted");
	return false;
}

/**
 * Step 2/3: feed N frames from G3-Ch0 into VENC.
 * Still Release VPSS frame after Send.
 *
 * verbose_stream=false (Step2): silent GetStream drain (anti-BUSY)
 * verbose_stream=true  (Step3): print pack meta + startcode bytes
 */
static const char *h264_nal_name(H264E_NALU_TYPE_E t)
{
	switch (t) {
	case H264E_NALU_BSLICE: return "B";
	case H264E_NALU_PSLICE: return "P";
	case H264E_NALU_ISLICE: return "I";
	case H264E_NALU_IDRSLICE: return "IDR";
	case H264E_NALU_SEI: return "SEI";
	case H264E_NALU_SPS: return "SPS";
	case H264E_NALU_PPS: return "PPS";
	default: return "?";
	}
}

struct VencStreamStats {
	int get_ok;
	int get_timeout;
	int packs;
	int saw_sps;
	int saw_pps;
	int saw_idr;
	int saw_i;
	int saw_p;
};

static void venc_log_stream(const char *tag, const VENC_STREAM_S *stream,
			    VencStreamStats *st)
{
	if (!stream || !stream->pstPack)
		return;

	log::info("%s GetStream ok packs=%u seq=%u\n",
		  tag, stream->u32PackCount, stream->u32Seq);

	for (CVI_U32 i = 0; i < stream->u32PackCount; ++i) {
		const VENC_PACK_S *pk = &stream->pstPack[i];
		H264E_NALU_TYPE_E nalu = pk->DataType.enH264EType;
		const CVI_U8 *p = pk->pu8Addr ? (pk->pu8Addr + pk->u32Offset) : nullptr;
		CVI_U32 len = pk->u32Len;

		if (st) {
			++st->packs;
			if (nalu == H264E_NALU_SPS) ++st->saw_sps;
			else if (nalu == H264E_NALU_PPS) ++st->saw_pps;
			else if (nalu == H264E_NALU_IDRSLICE) ++st->saw_idr;
			else if (nalu == H264E_NALU_ISLICE) ++st->saw_i;
			else if (nalu == H264E_NALU_PSLICE) ++st->saw_p;
		}

		char hex[48];
		hex[0] = '\0';
		if (p && len > 0) {
			unsigned n = len < 8 ? len : 8;
			char *h = hex;
			for (unsigned b = 0; b < n; ++b)
				h += sprintf(h, "%02x%s", p[b], (b + 1 < n) ? " " : "");
		}

		log::info("  pack[%u] type=%s(%d) len=%u pts=%llu end=%d "
			  "off=%u head=[%s]%s\n",
			  i, h264_nal_name(nalu), (int)nalu, len,
			  (unsigned long long)pk->u64PTS,
			  (int)pk->bFrameEnd, pk->u32Offset, hex,
			  (p && len >= 4 && p[0] == 0 && p[1] == 0 &&
			   ((p[2] == 0 && p[3] == 1) || p[2] == 1))
				  ? " startcode=Y" : "");
	}
}

static int venc_drain_stream(VENC_CHN chn, CVI_S32 timeout_ms, bool verbose,
			     VencStreamStats *st, int max_gets)
{
	int drained = 0;
	CVI_S32 deadline_slice = timeout_ms > 0 ? timeout_ms : 200;

	for (int n = 0; n < max_gets; ++n) {
		ZONHOR_MMF_VENC_STREAM_S mmf_stream;
		CVI_S32 ret;

		memset(&mmf_stream, 0, sizeof(mmf_stream));
		ret = ZONHOR_MMF_VencGetStream(chn, &mmf_stream, deadline_slice);
		if (ret != CVI_SUCCESS) {
			if (ret == CVI_ERR_VENC_BUSY && drained > 0)
				break;
			if (verbose && ret != CVI_ERR_VENC_BUSY)
				log::warn("ZONHOR_MMF_VencGetStream failed: 0x%x\n", ret);
			if (st)
				++st->get_timeout;
			break;
		}

		++drained;
		if (st)
			++st->get_ok;
		if (verbose) {
			venc_log_stream("Step3", &mmf_stream.stream, st);
		} else if (st) {
			for (CVI_U32 i = 0; i < mmf_stream.stream.u32PackCount; ++i) {
				H264E_NALU_TYPE_E nalu =
					mmf_stream.stream.pstPack[i].DataType.enH264EType;
				++st->packs;
				if (nalu == H264E_NALU_SPS) ++st->saw_sps;
				else if (nalu == H264E_NALU_PPS) ++st->saw_pps;
				else if (nalu == H264E_NALU_IDRSLICE) ++st->saw_idr;
				else if (nalu == H264E_NALU_ISLICE) ++st->saw_i;
				else if (nalu == H264E_NALU_PSLICE) ++st->saw_p;
			}
		}

		ret = ZONHOR_MMF_VencReleaseStream(chn, &mmf_stream);
		if (ret != CVI_SUCCESS)
			log::warn("ZONHOR_MMF_VencReleaseStream: 0x%x\n", ret);
	}
	return drained;
}

static bool venc_send_from_g3(VENC_CHN chn, int send_count, bool verbose_stream)
{
	int ok = 0;
	int fail_get = 0;
	int fail_send = 0;
	int drained = 0;
	VencStreamStats st = {};

	log::info("=== Step%d: SendFrame from G3-Ch0 × %d (%s) ===\n",
		  verbose_stream ? 3 : 2, send_count,
		  verbose_stream ? "GetStream pack dump" : "silent stream drain");

	for (int i = 0; i < send_count; ++i) {
		VIDEO_FRAME_INFO_S frame;
		memset(&frame, 0, sizeof(frame));

		CVI_S32 ret = CVI_VPSS_GetChnFrame(kG3, kG3Ch0, &frame, 2000);
		if (ret != CVI_SUCCESS) {
			++fail_get;
			log::error("Step2[%d] GetChnFrame(G3,0) failed: 0x%x\n", i, ret);
			continue;
		}

		if (i == 0)
			log_video_frame("Step2 first G3", &frame.stVFrame);

		ret = ZONHOR_MMF_VencSendFrame(chn, &frame, kVencSendTimeoutMs);
		if (ret == CVI_ERR_VENC_BUSY) {
			drained += venc_drain_stream(chn, 200, verbose_stream, &st, 4);
			ret = ZONHOR_MMF_VencSendFrame(chn, &frame, kVencSendTimeoutMs);
		}

		if (ret != CVI_SUCCESS) {
			++fail_send;
			log::error("Step2[%d] ZONHOR_MMF_VencSendFrame(%d) failed: 0x%x "
				   "wh=%ux%u fmt=%d (BUSY=0xc0078012)\n",
				   i, chn, ret,
				   frame.stVFrame.u32Width, frame.stVFrame.u32Height,
				   (int)frame.stVFrame.enPixelFormat);
		} else {
			++ok;
			log::info("Step2[%d] ZONHOR_MMF_VencSendFrame(%d) ok\n", i, chn);
		}

		CVI_S32 rret = CVI_VPSS_ReleaseChnFrame(kG3, kG3Ch0, &frame);
		if (rret != CVI_SUCCESS)
			log::warn("Step2[%d] ReleaseChnFrame(G3,0): 0x%x\n", i, rret);

		CVI_S32 to = verbose_stream ? kVencGetStreamTimeoutMs : 100;
		drained += venc_drain_stream(chn, to, verbose_stream, &st, 4);
	}

	/* Final drain — longer wait so late packs surface. */
	drained += venc_drain_stream(chn, verbose_stream ? 2000 : 500,
				     verbose_stream, &st, 16);

	log::info("Step2 SendFrame stats: ok=%d fail_get=%d fail_send=%d "
		  "stream_gets≈%d / %d\n",
		  ok, fail_get, fail_send, drained, send_count);

	if (verbose_stream) {
		log::info("Step3 GetStream stats: ok=%d timeout≈%d packs=%d "
			  "SPS=%d PPS=%d IDR=%d I=%d P=%d\n",
			  st.get_ok, st.get_timeout, st.packs,
			  st.saw_sps, st.saw_pps, st.saw_idr, st.saw_i, st.saw_p);
	}

	dump_venc_proc(verbose_stream ? "after Step3 GetStream" : "after Step2 SendFrame");
	dump_vpss_proc(verbose_stream ? "after Step3 GetStream" : "after Step2 SendFrame");

	bool send_ok = ok >= (send_count * 8 / 10) && fail_send == 0;
	if (!verbose_stream)
		return send_ok;

	/* Need non-empty streams + SPS/PPS or IDR/I. */
	bool stream_ok = st.get_ok >= 1 && st.packs >= 1 &&
			 (st.saw_sps > 0 || st.saw_pps > 0 ||
			  st.saw_idr > 0 || st.saw_i > 0);
	return send_ok && stream_ok;
}

/**
 * Step 4: Bind G3-Ch0 → VENC, then GetStream only (no SendFrame / no GetChnFrame).
 *
 * Sophgo sample order: Create → Bind → StartRecvFrame.
 * We already Start'd in Step1, so: StopRecv → Bind → StartRecv.
 *
 * Perf: keep draining Camera RGB (G2-Ch1 depth) or VPSS back-pressure
 * throttles G3→VENC (~15fps). Quiet timed loop measures wall_fps.
 */
static void drain_camera_rgb(camera::Camera *cam)
{
	if (!cam)
		return;
	image::Image *img = cam->read();
	if (img)
		delete img;
}

static bool venc_bind_g3_and_get_stream(VENC_CHN chn, int get_count,
					camera::Camera *cam)
{
	VencStreamStats st = {};

	log::info("=== Step4: ZONHOR_MMF_VencBindInput(SUB_VENC) + perf GetStream ===\n");

	CVI_S32 ret = ZONHOR_MMF_VencBindInput(chn, Z_CAMERA_OUTPUT_SUB_VENC);
	if (ret != CVI_SUCCESS) {
		log::error("ZONHOR_MMF_VencBindInput(%d, SUB_VENC) failed: 0x%x\n", chn, ret);
		return false;
	}
	g_venc_bound = true;
	log::info("ZONHOR_MMF_VencBindInput(%d, SUB_VENC) ok\n", chn);

	dump_vpss_proc("after Bind+StartRecv G3→VENC");

	for (int i = 0; i < 5; ++i) {
		drain_camera_rgb(cam);
		time::sleep_ms(20);
	}
	for (int i = 0; i < get_count && st.saw_sps == 0; ++i) {
		drain_camera_rgb(cam);
		venc_drain_stream(chn, kVencGetStreamTimeoutMs, true, &st, 4);
	}
	log::info("Step4 warmup: SPS=%d PPS=%d IDR=%d get_ok=%d\n",
		  st.saw_sps, st.saw_pps, st.saw_idr, st.get_ok);

	VencStreamStats perf = {};
	auto t0 = std::chrono::steady_clock::now();
	int quiet_gets = 0;
	int idle_spins = 0;
	while (true) {
		auto now = std::chrono::steady_clock::now();
		auto elapsed_ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - t0)
				.count();
		if (elapsed_ms >= kStep4PerfMs)
			break;

		drain_camera_rgb(cam);
		int got = venc_drain_stream(chn, 50, false, &perf, 8);
		quiet_gets += got;
		if (got == 0) {
			++idle_spins;
			time::sleep_ms(1);
		}
	}
	auto t1 = std::chrono::steady_clock::now();
	double secs = std::chrono::duration<double>(t1 - t0).count();
	double wall_fps = secs > 0.0 ? (quiet_gets / secs) : 0.0;

	log::info("Step4 PERF: quiet_gets=%d over %.3fs → wall_fps=%.2f "
		  "(idle_spins=%d) P=%d\n",
		  quiet_gets, secs, wall_fps, idle_spins, perf.saw_p);
	log::info("Step4 totals: get_ok=%d packs=%d SPS=%d PPS=%d IDR=%d P=%d\n",
		  st.get_ok + perf.get_ok, st.packs + perf.packs,
		  st.saw_sps + perf.saw_sps, st.saw_pps + perf.saw_pps,
		  st.saw_idr + perf.saw_idr, st.saw_p + perf.saw_p);
	dump_venc_proc("after Step4 GetStream");
	dump_vpss_proc("after Step4 GetStream");

	bool stream_ok = (st.get_ok + perf.get_ok) >= 3 &&
			 (st.saw_sps + perf.saw_sps > 0 ||
			  st.saw_pps + perf.saw_pps > 0 ||
			  st.saw_idr + perf.saw_idr > 0);
	if (wall_fps < 20.0)
		log::warn("Step4 wall_fps=%.2f looks low (expect ~30 on 1080p30)\n",
			  wall_fps);
	else
		log::info("Step4 wall_fps=%.2f looks healthy\n", wall_fps);

	return stream_ok;
}

int _main(int argc, char *argv[])
{
	const char *fb_device = kDefaultFbDevice;
	int step = kDefaultStep;
	bool venc_started = false;
	CVI_U32 g3_w = 540;
	CVI_U32 g3_h = 960;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return 0;
		}
		if (strcmp(argv[i], "--step") == 0 && i + 1 < argc) {
			step = atoi(argv[++i]);
			continue;
		}
		if (strncmp(argv[i], "--step=", 7) == 0) {
			step = atoi(argv[i] + 7);
			continue;
		}
		if (argv[i][0] != '-')
			fb_device = argv[i];
	}

	log::info("fb_display_camera_venc_demo: step=%d fb=%s panel=%dx%d cam=%dx%d\n",
		  step, fb_device, kPanelWidth, kPanelHeight, kCamWidth, kCamHeight);

	display::Display screen(kPanelWidth, kPanelHeight, image::FMT_RGB888, fb_device);
	log::info("screen opened: %dx%d\n", screen.width(), screen.height());

	camera::Camera cam(kCamWidth, kCamHeight, image::FMT_RGB888);
	log::info("camera opened: %dx%d\n", cam.width(), cam.height());

	dump_vpss_proc("after camera open (before G3 enable)");

	bool g3_ok = probe_g3_bind_then_mem(&g3_w, &g3_h);
	if (!g3_ok) {
		log::error("G3-Ch0 probe FAILED on both bind and MEM paths\n");
		return -1;
	}

	if (step >= 1) {
		CVI_S32 vret = venc_create_start(kVencChn, g3_w, g3_h);
		if (vret != CVI_SUCCESS) {
			log::error("Step1 VENC Create/Start FAILED: 0x%x\n", vret);
			return -1;
		}
		venc_started = true;
		dump_venc_proc("after VENC StartRecvFrame");
		log::info("Step1 SUCCESS: ZONHOR_MMF_VencCreate chn=%d ok\n", kVencChn);
	}

	bool step_ok = true;
	if (step >= 4) {
		step_ok = venc_bind_g3_and_get_stream(kVencChn, kStep4GetCount, &cam);
		if (step_ok)
			log::info("Step4 SUCCESS: Bind path GetStream ok\n");
		else
			log::error("Step4 FAILED: Bind/GetStream below threshold\n");
	} else if (step >= 2) {
		const bool verbose = (step >= 3);
		step_ok = venc_send_from_g3(kVencChn, kStep2SendCount, verbose);
		if (verbose) {
			if (step_ok)
				log::info("Step3 SUCCESS: GetStream packs ok\n");
			else
				log::error("Step3 FAILED: Send or GetStream below threshold\n");
		} else {
			if (step_ok)
				log::info("Step2 SUCCESS: SendFrame from G3 ok\n");
			else
				log::error("Step2 FAILED: SendFrame stats below threshold\n");
		}
	}

	image::Image canvas(screen.width(), screen.height(), image::FMT_RGB888);
	const int preview_frames = (step >= 2) ? 30 : 60;
	int frames = 0;

	while (!app::need_exit() && frames < preview_frames) {
		canvas.draw_rect(0, 0, canvas.width(), canvas.height(), image::COLOR_BLACK, -1);

		image::Image *cam_img = cam.read();
		if (cam_img) {
			image::Image *thumb = cam_img->resize(86, 152);
			if (thumb) {
				canvas.draw_image(0, 0, *thumb);
				delete thumb;
			}
			delete cam_img;
		}

		err::Err e = screen.show(canvas);
		if (e != err::ERR_NONE) {
			log::error("screen.show failed: %d\n", (int)e);
			if (venc_started)
				venc_destroy(kVencChn);
			return -1;
		}
		++frames;
		time::sleep_ms(50);
	}

	dump_vpss_proc("before exit");
	if (venc_started) {
		dump_venc_proc("before destroy");
		venc_destroy(kVencChn);
	}
	return step_ok ? 0 : -1;
}

int main(int argc, char *argv[])
{
	sys::register_default_signal_handle();
	CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
