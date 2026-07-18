/**
 * MaixCDK — shared CVI MMF runtime singleton.
 *
 * Replicates the z_lib pipeline topology using the x_lib X_MMF APIs:
 *   VPSS group 0  (VpssDev 0): display path  — receives user RGB888, outputs NV21 → VO
 *   VPSS group 1  (VpssDev 1): camera path   — receives VI frames, outputs RGB888 to user
 *
 * Both Camera::add_channel (VPSS grp 1, chn 1) and NN SYS/VB init share
 * this context, so any construction order is safe.
 *
 * PLATFORM_ZONHOR: display uses framebuffer (/dev/fb0), so VO is disabled;
 * camera still uses VI → VPSS. Sensor mode is selected via set_sensor_ini_path()
 * (IMX678 1080p binning vs 5MP).
 */

#include "maix_cvi_media_runtime.hpp"
#include "maix_util.hpp"
#include "global_config.h"
#include <cstring>
#include <cstdio>

// CVI middleware headers pulled in transitively via x_mmf.h → sample_comm.h
extern "C" {
#include "sample_comm.h"
}

namespace maix::cvi {

// ──────────────────────────────────────────────────────────────────────────────
// Static members
// ──────────────────────────────────────────────────────────────────────────────
std::mutex  MediaRuntime::s_mutex;
std::mutex  MediaRuntime::s_disp_vpss0_send_mutex;
int         MediaRuntime::s_refcount = 0;
X_MMF_CTX_S MediaRuntime::s_ctx;
std::string MediaRuntime::s_sensor_ini_pending;
std::string MediaRuntime::s_sensor_ini_active;

std::mutex &MediaRuntime::display_vpss0_send_mutex()
{
    return s_disp_vpss0_send_mutex;
}

bool MediaRuntime::is_inited()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_ctx.inited;
}

void MediaRuntime::set_sensor_ini_path(const char *path)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!path || path[0] == '\0') {
        s_sensor_ini_pending.clear();
        X_MMF_SetSensorIniPath(nullptr);
        return;
    }
    s_sensor_ini_pending = path;
    X_MMF_SetSensorIniPath(path);
}

const char *MediaRuntime::sensor_ini_path()
{
    /* Prefer active (what VI was inited with), else pending. */
    if (!s_sensor_ini_active.empty())
        return s_sensor_ini_active.c_str();
    if (!s_sensor_ini_pending.empty())
        return s_sensor_ini_pending.c_str();
    const char *p = X_MMF_GetSensorIniPath();
    return p ? p : "";
}

CVI_S32 MediaRuntime::reconfigure_sensor_if_needed()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_sensor_ini_pending.empty())
        return CVI_SUCCESS;
    if (!s_ctx.inited) {
        X_MMF_SetSensorIniPath(s_sensor_ini_pending.c_str());
        return CVI_SUCCESS;
    }
    if (s_sensor_ini_pending == s_sensor_ini_active)
        return CVI_SUCCESS;
    if (s_refcount > 0) {
        printf("[MediaRuntime] cannot reconfigure sensor ini while refcount=%d "
               "(pending=%s active=%s)\n",
               s_refcount, s_sensor_ini_pending.c_str(), s_sensor_ini_active.c_str());
        return CVI_FAILURE;
    }
    printf("[MediaRuntime] reconfigure sensor: %s -> %s\n",
           s_sensor_ini_active.c_str(), s_sensor_ini_pending.c_str());
    X_MMF_SetSensorIniPath(s_sensor_ini_pending.c_str());
    _deinit();
    _init();
    return s_ctx.inited ? CVI_SUCCESS : CVI_FAILURE;
}

// ──────────────────────────────────────────────────────────────────────────────
// Display VPSS group 0 constants
// ──────────────────────────────────────────────────────────────────────────────
static constexpr VPSS_GRP  DISP_VPSS_GRP = 0;
static constexpr VPSS_CHN  DISP_VPSS_CHN = VPSS_CHN0;
static constexpr VO_LAYER   VO_LAYER_ID   = 0;
static constexpr VO_CHN     VO_CHN_ID     = 0;

// Camera VPSS group 1 constants
static constexpr VPSS_GRP  CAM_VPSS_GRP  = 1;

// Default camera VPSS channel resolution (resizable later via CVI_VPSS_SetChnAttr)
static constexpr CVI_U32   CAM_DEF_W     = 640;
static constexpr CVI_U32   CAM_DEF_H     = 480;

#ifdef PLATFORM_ZONHOR
// IMX678 5MP mode max (board reports up to 2848x1602; leave headroom for 2880x1620 ISP)
static constexpr CVI_U32   CAM_VI_MAX_W  = 2880;
static constexpr CVI_U32   CAM_VI_MAX_H  = 1620;
#else
// VI 典型输出（GC4653 等）；grp_attr 须 ≥ VI 输出，否则 VI→VPSS 异常（对齐 z_lib stSize）
static constexpr CVI_U32   CAM_VI_MAX_W  = 2560;
static constexpr CVI_U32   CAM_VI_MAX_H  = 1440;
#endif

// Display panel resolution on MaixCam（与 z_lib Z_WIDTH/Z_HEIGHT 一致；VO 使用 90° 旋转）
static constexpr CVI_U32   DISP_W        = 640;
static constexpr CVI_U32   DISP_H        = 480;

/**
 * One black RGB888 frame into VPSS grp 0 — matches DisplayCviXmmf stride / ION layout.
 * Caller must hold MediaRuntime::display_vpss0_send_mutex().
 */
static CVI_S32 _vpss_send_display_black_prime(void)
{
    const CVI_U32 w         = DISP_W;
    const CVI_U32 h         = DISP_H;
    const CVI_U32 stride  = (w * 3u + 63u) & ~63u;
    const CVI_U32 frameSize = stride * h;

    CVI_U64   phyAddr = 0;
    CVI_VOID *virAddr = nullptr;
    CVI_S32 ret = CVI_SYS_IonAlloc_Cached(&phyAddr, &virAddr, "rt_prime_blk", frameSize);
    if (ret != CVI_SUCCESS) {
        return ret;
    }

    memset(virAddr, 0, (size_t)frameSize);
    CVI_SYS_IonFlushCache(phyAddr, virAddr, frameSize);

    VIDEO_FRAME_INFO_S srcFrame;
    memset(&srcFrame, 0, sizeof(srcFrame));
    VIDEO_FRAME_S *vf = &srcFrame.stVFrame;
    vf->u32Width      = w;
    vf->u32Height     = h;
    vf->enPixelFormat = PIXEL_FORMAT_RGB_888;
    vf->enVideoFormat = VIDEO_FORMAT_LINEAR;
    vf->u32Stride[0]  = stride;
    vf->u32Length[0]  = frameSize;
    vf->u64PhyAddr[0] = phyAddr;
    vf->pu8VirAddr[0] = (CVI_U8 *)virAddr;

    ret = CVI_VPSS_SendFrame(DISP_VPSS_GRP, &srcFrame, -1);
    CVI_SYS_IonFree(phyAddr, virAddr);
    return ret;
}

// ──────────────────────────────────────────────────────────────────────────────
// _init — called once on first acquire()
// ──────────────────────────────────────────────────────────────────────────────
void MediaRuntime::_init()
{
    static std::once_flag s_exit_register_once;
    std::call_once(s_exit_register_once, []() {
        maix::util::register_exit_function(MediaRuntime::_at_exit_handler);
        maix::log::info("MediaRuntime: registered util exit handler for MMF cleanup\n");
    });

    if (!s_sensor_ini_pending.empty()) {
        X_MMF_SetSensorIniPath(s_sensor_ini_pending.c_str());
    }

    X_MMF_CONFIG_S cfg;
    X_MMF_DefaultConfig(&cfg);

    // ── VI ──────────────────────────────────────────────────────────────────
    cfg.vi.enable        = CVI_TRUE;
    cfg.vi.use_default_ini = CVI_TRUE;

    // ── VPSS group 0: display (user RGB888 → VPSS → NV21 → VO) ─────────────
    cfg.vpss_grp_count = 2;

    cfg.vpss[0].grp_enable                 = CVI_TRUE;
    cfg.vpss[0].grp                         = DISP_VPSS_GRP;
    cfg.vpss[0].grp_attr.stFrameRate.s32SrcFrameRate = -1;
    cfg.vpss[0].grp_attr.stFrameRate.s32DstFrameRate = -1;
    /* Bootstrap defaults only: DisplayCviXmmf calls configure_display_vpss_input() so
     * u32MaxW/H / enPixelFormat match the real user SendFrame size & format (else 0xc0068003). */
    cfg.vpss[0].grp_attr.enPixelFormat      = PIXEL_FORMAT_RGB_888;
    cfg.vpss[0].grp_attr.u32MaxW            = DISP_W;
    cfg.vpss[0].grp_attr.u32MaxH            = DISP_H;
    cfg.vpss[0].grp_attr.u8VpssDev          = 0;

    cfg.vpss[0].chn_enable[0]               = CVI_TRUE;
    cfg.vpss[0].chn_attr[0].u32Width        = DISP_W;
    cfg.vpss[0].chn_attr[0].u32Height       = DISP_H;
    cfg.vpss[0].chn_attr[0].enVideoFormat   = VIDEO_FORMAT_LINEAR;
    cfg.vpss[0].chn_attr[0].enPixelFormat   = PIXEL_FORMAT_NV21;       // VO input
    cfg.vpss[0].chn_attr[0].stFrameRate.s32SrcFrameRate = 30;
    cfg.vpss[0].chn_attr[0].stFrameRate.s32DstFrameRate = 30;
    cfg.vpss[0].chn_attr[0].u32Depth        = 1;
    cfg.vpss[0].chn_attr[0].stAspectRatio.enMode         = ASPECT_RATIO_AUTO;
    cfg.vpss[0].chn_attr[0].stAspectRatio.bEnableBgColor = CVI_TRUE;
    cfg.vpss[0].chn_attr[0].stAspectRatio.u32BgColor     = COLOR_RGB_BLACK;

    // ── VPSS group 1: camera (VI → VPSS → RGB888 → user) ───────────────────
    cfg.vpss[1].grp_enable                 = CVI_TRUE;
    cfg.vpss[1].grp                         = CAM_VPSS_GRP;
    cfg.vpss[1].grp_attr.stFrameRate.s32SrcFrameRate = -1;
    cfg.vpss[1].grp_attr.stFrameRate.s32DstFrameRate = -1;
    cfg.vpss[1].grp_attr.enPixelFormat      = SAMPLE_PIXEL_FORMAT;     // NV21 from VI
    cfg.vpss[1].grp_attr.u32MaxW            = CAM_VI_MAX_W;
    cfg.vpss[1].grp_attr.u32MaxH            = CAM_VI_MAX_H;
    cfg.vpss[1].grp_attr.u8VpssDev          = 1;                        // DUAL mode dev 1

    // channel 0 — primary camera frame
    cfg.vpss[1].chn_enable[0]               = CVI_TRUE;
    cfg.vpss[1].chn_attr[0].u32Width        = CAM_DEF_W;
    cfg.vpss[1].chn_attr[0].u32Height       = CAM_DEF_H;
    cfg.vpss[1].chn_attr[0].enVideoFormat   = VIDEO_FORMAT_LINEAR;
    cfg.vpss[1].chn_attr[0].enPixelFormat   = PIXEL_FORMAT_RGB_888;
    cfg.vpss[1].chn_attr[0].stFrameRate.s32SrcFrameRate = 30;
    cfg.vpss[1].chn_attr[0].stFrameRate.s32DstFrameRate = 30;
    cfg.vpss[1].chn_attr[0].u32Depth        = 0;
    cfg.vpss[1].chn_attr[0].stAspectRatio.enMode         = ASPECT_RATIO_AUTO;
    cfg.vpss[1].chn_attr[0].stAspectRatio.bEnableBgColor = CVI_TRUE;
    cfg.vpss[1].chn_attr[0].stAspectRatio.u32BgColor     = COLOR_RGB_BLACK;

    // channel 1 — add_channel secondary frame (disabled until requested)
    cfg.vpss[1].chn_enable[1]               = CVI_FALSE;
    cfg.vpss[1].chn_attr[1].u32Width        = CAM_DEF_W;
    cfg.vpss[1].chn_attr[1].u32Height       = CAM_DEF_H;
    cfg.vpss[1].chn_attr[1].enVideoFormat   = VIDEO_FORMAT_LINEAR;
    cfg.vpss[1].chn_attr[1].enPixelFormat   = PIXEL_FORMAT_RGB_888;
    cfg.vpss[1].chn_attr[1].stFrameRate.s32SrcFrameRate = 30;
    cfg.vpss[1].chn_attr[1].stFrameRate.s32DstFrameRate = 30;
    cfg.vpss[1].chn_attr[1].u32Depth        = 0;
    cfg.vpss[1].chn_attr[1].stAspectRatio.enMode         = ASPECT_RATIO_AUTO;
    cfg.vpss[1].chn_attr[1].stAspectRatio.bEnableBgColor = CVI_TRUE;
    cfg.vpss[1].chn_attr[1].stAspectRatio.u32BgColor     = COLOR_RGB_BLACK;

    // ── VO ──────────────────────────────────────────────────────────────────
#ifdef PLATFORM_ZONHOR
    /* Zonhor uses framebuffer display (/dev/fb0); skip MIPI VO bring-up. */
    cfg.vo.enable = CVI_FALSE;
    printf("[MediaRuntime] zonhor: VO disabled (FB display)\n");
#else
    cfg.vo.enable = CVI_TRUE;
    cfg.vo.vo_layer = VO_LAYER_ID;
    cfg.vo.vo_chn   = VO_CHN_ID;
    cfg.vo.set_rotation = CVI_TRUE;
    cfg.vo.rotation     = ROTATION_90;

    SAMPLE_VO_CONFIG_S vo_cfg;
    if (SAMPLE_COMM_VO_GetDefConfig(&vo_cfg) != CVI_SUCCESS) {
        printf("[MediaRuntime] SAMPLE_COMM_VO_GetDefConfig failed — VO disabled\n");
        cfg.vo.enable = CVI_FALSE;
    } else {
        vo_cfg.VoDev                        = VO_CHN_ID;
        vo_cfg.stVoPubAttr.enIntfType       = VO_INTF_MIPI;
        vo_cfg.stVoPubAttr.enIntfSync       = VO_OUTPUT_720P60;
        vo_cfg.stDispRect.s32X              = 0;
        vo_cfg.stDispRect.s32Y              = 0;
        vo_cfg.stDispRect.u32Width          = DISP_H;   // swapped for 90° panel
        vo_cfg.stDispRect.u32Height         = DISP_W;
        vo_cfg.stImageSize.u32Width         = DISP_H;
        vo_cfg.stImageSize.u32Height        = DISP_W;
        vo_cfg.enPixFormat                  = SAMPLE_PIXEL_FORMAT;
        vo_cfg.enVoMode                     = VO_MODE_1MUX;
        cfg.vo.vo_cfg = vo_cfg;
    }
#endif

    CVI_S32 ret = X_MMF_Init(&s_ctx, &cfg);
    if (ret != CVI_SUCCESS) {
        printf("[MediaRuntime] X_MMF_Init failed: 0x%x\n", ret);
        s_sensor_ini_active.clear();
        return;
    }

    s_sensor_ini_active = s_sensor_ini_pending;
    if (s_sensor_ini_active.empty()) {
        const char *p = X_MMF_GetSensorIniPath();
        if (p && p[0])
            s_sensor_ini_active = p;
    }

    // Bind VI to camera VPSS group 1
    ret = X_MMF_BindVIToVPSS(&s_ctx, CAM_VPSS_GRP);
    if (ret != CVI_SUCCESS) {
        printf("[MediaRuntime] X_MMF_BindVIToVPSS failed: 0x%x\n", ret);
    }

    if (s_ctx.cfg.vo.enable) {
        // Bind display VPSS group 0, chn 0 → VO
        ret = X_MMF_BindVPSSToVO(DISP_VPSS_GRP, DISP_VPSS_CHN, VO_LAYER_ID, VO_CHN_ID);
        if (ret != CVI_SUCCESS) {
            printf("[MediaRuntime] X_MMF_BindVPSSToVO failed: 0x%x\n", ret);
        }

        CVI_VO_EnableChn(VO_LAYER_ID, VO_CHN_ID);

        /* Prime VPSS→VO with black before ShowChn to avoid a brief garbage / green flash
         * from uninitialized scanout buffers (VO was enabled with no valid frame yet). */
        {
            std::lock_guard<std::mutex> disp_lk(s_disp_vpss0_send_mutex);
            for (int i = 0; i < 2; ++i) {
                ret = _vpss_send_display_black_prime();
                if (ret != CVI_SUCCESS) {
                    printf("[MediaRuntime] display black prime frame %d failed: 0x%x\n", i, ret);
                    break;
                }
            }
        }

        CVI_VO_ShowChn(VO_LAYER_ID, VO_CHN_ID);
    }

    printf("[MediaRuntime] init ok (sensor_ini=%s vi=%ux%u vo=%d)\n",
           s_sensor_ini_active.empty() ? "(default)" : s_sensor_ini_active.c_str(),
           s_ctx.vi_size.u32Width, s_ctx.vi_size.u32Height,
           (int)s_ctx.cfg.vo.enable);
}

// ──────────────────────────────────────────────────────────────────────────────
// _deinit — called once when last reference is released
// ──────────────────────────────────────────────────────────────────────────────
void MediaRuntime::_deinit()
{
    /* Hold for entire teardown so no concurrent Display::show can SendFrame while we
     * flush black and destroy VPSS/VO. release() already holds s_mutex — lock order: s_mutex, then this. */
    std::lock_guard<std::mutex> disp_lk(s_disp_vpss0_send_mutex);

    /* Last frames through a still-bound VPSS→VO: all-zero RGB888 → black scanout, so the next
     * process init does not inherit our last image (hardware may still show until re-init). */
    if (s_ctx.inited && s_ctx.cfg.vo.enable) {
        for (int i = 0; i < 2; ++i) {
            CVI_S32 r = _vpss_send_display_black_prime();
            if (r != CVI_SUCCESS) {
                printf("[MediaRuntime] deinit black flush frame %d failed: 0x%x\n", i, r);
                break;
            }
        }
    }

    if (s_ctx.cfg.vo.enable) {
        CVI_VO_HideChn(VO_LAYER_ID, VO_CHN_ID);
        CVI_VO_DisableChn(VO_LAYER_ID, VO_CHN_ID);
        X_MMF_UnbindVPSSToVO(DISP_VPSS_GRP, DISP_VPSS_CHN, VO_LAYER_ID, VO_CHN_ID);
    }
    X_MMF_UnbindVIFromVPSS(&s_ctx, CAM_VPSS_GRP);

    X_MMF_Deinit(&s_ctx);
    s_sensor_ini_active.clear();
    printf("[MediaRuntime] deinit ok\n");
}

void MediaRuntime::_at_exit_handler()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_ctx.inited) {
        return;
    }
    maix::log::info("MediaRuntime: process exit, deinit CVI MMF\n");
    _deinit();
    s_refcount = 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────
void MediaRuntime::acquire()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_refcount++ == 0) {
        _init();
        if (!s_ctx.inited) {
            /* Init failed: do not leave a dangling holder ref. */
            s_refcount = 0;
        }
    }
}

void MediaRuntime::release()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_refcount > 0 && --s_refcount == 0) {
        _deinit();
    }
}

X_MMF_CTX_S *MediaRuntime::ctx()
{
    return &s_ctx;
}

CVI_S32 MediaRuntime::configure_display_vpss_input(CVI_U32 max_w, CVI_U32 max_h,
                                                   PIXEL_FORMAT_E en_pixel_format)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_refcount <= 0 || !s_ctx.inited) {
        return CVI_FAILURE;
    }
    if (max_w == 0) {
        max_w = DISP_W;
    }
    if (max_h == 0) {
        max_h = DISP_H;
    }

    VPSS_GRP_ATTR_S attr;
    CVI_S32 ret = CVI_VPSS_GetGrpAttr(DISP_VPSS_GRP, &attr);
    if (ret != CVI_SUCCESS) {
        printf("[MediaRuntime] CVI_VPSS_GetGrpAttr(vpss grp %d) failed: 0x%x\n", DISP_VPSS_GRP, ret);
        return ret;
    }

    attr.u32MaxW         = max_w;
    attr.u32MaxH         = max_h;
    attr.enPixelFormat   = en_pixel_format;

    ret = CVI_VPSS_SetGrpAttr(DISP_VPSS_GRP, &attr);
    if (ret != CVI_SUCCESS) {
        printf("[MediaRuntime] CVI_VPSS_SetGrpAttr(vpss grp %d) failed: 0x%x\n", DISP_VPSS_GRP, ret);
        return ret;
    }

    /* Keep cached config in sync for diagnostics / future X_MMF paths (slot 0 = display grp) */
    if (s_ctx.cfg.vpss_grp_count > 0) {
        s_ctx.cfg.vpss[0].grp_attr.u32MaxW       = max_w;
        s_ctx.cfg.vpss[0].grp_attr.u32MaxH       = max_h;
        s_ctx.cfg.vpss[0].grp_attr.enPixelFormat = en_pixel_format;
    }

    return CVI_SUCCESS;
}

} // namespace maix::cvi
