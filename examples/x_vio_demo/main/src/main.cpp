#include "maix_basic.hpp"
#include "main.h"

#include <stdio.h>
#include <string.h>

#include "z_mmf.h"
#include "z_camera.hpp"
#include "z_display.hpp"
#include "z_image.hpp"

extern "C" {
#include "cvi_vpss.h"
#include "x_mmf.h"
}

using namespace maix;

/* 与 z_lib / vision 中 MIPI 预览分辨率一致（z_mmf.h: Z_WIDTH x Z_HEIGHT） */
static const int kDispW = Z_WIDTH;
static const int kDispH = Z_HEIGHT;

/**
 * 前段：与 z_vio_demo 相同走 vision（maix::display / camera；Display 构造里会 z_lib_init，析构 z_lib_deinit）。
 * 必须在本阶段结束后再启动 X_MMF，避免两套 SYS/VI/VPSS/VO 同时占用。
 */
static void run_vision_phase_z_lib()
{
    log::info("phase1: screen solid colors (vision + z_lib)");

    display::Display disp(kDispW, kDispH);
    image::Image img(kDispW, kDispH);

    const image::Color palette[] = {
        image::COLOR_RED,
        image::COLOR_GREEN,
        image::COLOR_BLUE,
    };

    for (int i = 0; i < 6; ++i) {
        img.clear();
        img.draw_rect(0, 0, kDispW, kDispH, palette[i % 3], -1);
        disp.show(img);
        time::sleep(1);
    }

    log::info("phase2: camera one frame + hex (vision)");
    camera::Camera cam(kDispW, kDispH);

    cam.read();

    time::sleep(1);

    image::Image *cam_img = cam.read();
    if (!cam_img) {
        log::error("camera read failed");
    } else {
        printf("[CAM] w=%d h=%d fmt=%d\n",
               cam_img->width(), cam_img->height(), (int)cam_img->format());
        Bytes *b = cam_img->to_bytes();
        if (b && b->data && b->data_len > 0) {
            unsigned n = (unsigned)b->data_len < 32U ? (unsigned)b->data_len : 32U;
            printf("[CAM] first %u bytes:", n);
            for (unsigned j = 0; j < n; ++j) {
                printf(" %02X", (unsigned char)b->data[j]);
            }
            printf("\n");
        }
        delete cam_img;
    }

    log::info("phase3: read camera -> show on display x6 (CPU path)");
    for (int k = 0; k < 120; ++k) {
        cam_img = cam.read();
        if (cam_img) {
            disp.show(*cam_img);
            delete cam_img;
        }
        time::sleep_ms(33);
    }

    /* disp 后析构：z_lib_deinit，释放 VI/VPSS/VO，供 X_MMF 重新 init */
}

static CVI_S32 setup_vo_cfg(X_MMF_CONFIG_S *cfg)
{
#if X_MMF_ENABLE_VO
    CVI_S32 r;
    RECT_S rect = {0, 0, (CVI_U32)kDispH, (CVI_U32)kDispW};
    SIZE_S size = {(CVI_U32)kDispH, (CVI_U32)kDispW};

    r = SAMPLE_COMM_VO_GetDefConfig(&cfg->vo.vo_cfg);
    if (r != CVI_SUCCESS) {
        return r;
    }
    cfg->vo.enable = CVI_TRUE;
    cfg->vo.vo_cfg.VoDev = 0;
    cfg->vo.vo_cfg.stVoPubAttr.enIntfType = VO_INTF_MIPI;
    cfg->vo.vo_cfg.stVoPubAttr.enIntfSync = VO_OUTPUT_720P60;
    cfg->vo.vo_cfg.stDispRect = rect;
    cfg->vo.vo_cfg.stImageSize = size;
    cfg->vo.vo_cfg.enPixFormat = SAMPLE_PIXEL_FORMAT;
    cfg->vo.vo_cfg.enVoMode = VO_MODE_1MUX;
    cfg->vo.vo_layer = 0;
    cfg->vo.vo_chn = 0;
    cfg->vo.set_rotation = CVI_FALSE;
    return CVI_SUCCESS;
#else
    (void)cfg;
    return CVI_FAILURE;
#endif
}

int _main(int argc, char *argv[])
{
    X_MMF_CTX_S ctx;
    X_MMF_CONFIG_S cfg;
    CVI_S32 ret;
    uint64_t now_s;

    (void)argc;
    (void)argv;

    log::info("x_vio_demo start");

    run_vision_phase_z_lib();

    log::info("phase4: X_MMF hardware VI->VPSS(rot90)->VO");

    X_MMF_DefaultConfig(&cfg);
    cfg.log_level = X_MMF_LOG_INFO;

#if X_MMF_ENABLE_VI
    cfg.vi.enable = CVI_TRUE;
#endif
#if X_MMF_ENABLE_VPSS
    cfg.vpss_grp_count = 1;
    cfg.vpss[0].grp_enable = CVI_TRUE;
    cfg.vpss[0].grp = 0;
    cfg.vpss[0].grp_attr.enPixelFormat = SAMPLE_PIXEL_FORMAT;
    cfg.vpss[0].grp_attr.u32MaxW = 2560;
    cfg.vpss[0].grp_attr.u32MaxH = 1440;
    cfg.vpss[0].grp_attr.u8VpssDev = 0;
    cfg.vpss[0].chn_enable[0] = CVI_TRUE;
    cfg.vpss[0].chn_attr[0].u32Width = (CVI_U32)kDispW;
    cfg.vpss[0].chn_attr[0].u32Height = (CVI_U32)kDispH;
    cfg.vpss[0].chn_attr[0].enPixelFormat = SAMPLE_PIXEL_FORMAT;
    cfg.vpss[0].chn_attr[0].enVideoFormat = VIDEO_FORMAT_LINEAR;
    cfg.vpss[0].chn_attr[0].u32Depth = 2;
#endif

    ret = setup_vo_cfg(&cfg);
    if (ret != CVI_SUCCESS) {
        log::error("setup_vo_cfg failed: 0x%x", ret);
        return -1;
    }

    ret = X_MMF_Init(&ctx, &cfg);
    if (ret != CVI_SUCCESS) {
        log::error("X_MMF_Init failed: 0x%x", ret);
        return -1;
    }

#if X_MMF_ENABLE_VI && X_MMF_ENABLE_VPSS
    CVI_SYS_SetVPSSMode(VPSS_MODE_DUAL);
    {
        VI_VPSS_MODE_S mode;
        memset(&mode, 0, sizeof(mode));
        mode.aenMode[0] = VI_OFFLINE_VPSS_OFFLINE;
        mode.aenMode[1] = VI_ONLINE_VPSS_OFFLINE;
        CVI_SYS_SetVIVPSSMode(&mode);
    }
    ret = X_MMF_BindVIToVPSS(&ctx, 0);
    if (ret != CVI_SUCCESS) {
        log::warn("Bind VI->VPSS failed: 0x%x", ret);
    }
#endif

#if X_MMF_ENABLE_VI && X_MMF_ENABLE_VPSS && X_MMF_ENABLE_VO
    CVI_VPSS_SetChnRotation(0, VPSS_CHN0, ROTATION_90);
    ret = X_MMF_BindVPSSToVO(0, VPSS_CHN0, cfg.vo.vo_layer, cfg.vo.vo_chn);
    if (ret != CVI_SUCCESS) {
        log::warn("Bind VPSS->VO failed: 0x%x", ret);
    } else {
        log::info("Hardware chain: VI->VPSS(rot90)->VO");
    }
#endif

    while (!app::need_exit()) {
        now_s = time::time_s();
        log::info("time_s=%llu", (unsigned long long)now_s);
        time::sleep(1);
    }

#if X_MMF_ENABLE_VI && X_MMF_ENABLE_VPSS && X_MMF_ENABLE_VO
    X_MMF_UnbindVPSSToVO(0, VPSS_CHN0, cfg.vo.vo_layer, cfg.vo.vo_chn);
#endif
#if X_MMF_ENABLE_VI && X_MMF_ENABLE_VPSS
    X_MMF_UnbindVIFromVPSS(&ctx, 0);
#endif

    X_MMF_Deinit(&ctx);
    log::info("x_vio_demo exit");
    return 0;
}

int main(int argc, char *argv[])
{
    sys::register_default_signal_handle();
    CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
