#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/prctl.h>

#include "cvi_buffer.h"
#include "cvi_ae_comm.h"
#include "cvi_awb_comm.h"
#include "cvi_comm_isp.h"
#include <inttypes.h>
#include "z_mmf.h"
#include <time.h>  // 新增：用于计时

#ifndef ALIGN_UP
#define ALIGN_UP(x, a)    (((x) + ((a) - 1)) & ~((a) - 1))
#endif

#ifndef ALIGN
#define ALIGN(x, a)  (((x) + ((a) - 1)) & ~((a) - 1))
#endif

#ifndef LOGE
#define LOGE(fmt, ...)  SAMPLE_PRT("[ERR] " fmt, ##__VA_ARGS__)
#endif
#ifndef LOGI
#define LOGI(fmt, ...)  SAMPLE_PRT("[INF] " fmt, ##__VA_ARGS__)
#endif
#ifndef LOGW
#define LOGW(fmt, ...)  SAMPLE_PRT("[WRN] " fmt, ##__VA_ARGS__)
#endif

// 把像素格式打印成人类可读
static const char* _pf2s(PIXEL_FORMAT_E pf) {
    switch (pf) {
        case PIXEL_FORMAT_RGB_888: return "RGB888";
        case PIXEL_FORMAT_RGB_888_PLANAR: return "RGB888_PLANAR";
        case PIXEL_FORMAT_YUV_PLANAR_420: return "420";
        case PIXEL_FORMAT_YUV_PLANAR_422: return "422";
            // 按需再补
        default: return "UNKNOWN";
    }
}

typedef struct {
    CVI_U64 phyAddr;
    CVI_VOID *virAddr;
    CVI_U32 frameSize;
} Z_RGB_FRAME_PRIV;

static void dump_rgb_edge(const CVI_U8 *p, CVI_U32 w, CVI_U32 h) {
    // 打印前后 4 像素，避免刷屏
    printf("[DBG] RGB head/tail dump (w=%u,h=%u):\n", w, h);
    for (int i = 0; i < 4; ++i) {
        int idx = i * 3;
        printf("  head px[%d]: R=%u G=%u B=%u\n", i, p[idx], p[idx+1], p[idx+2]);
    }
    CVI_U32 totalPx = w * h;
    for (int i = (int)totalPx - 4; i < (int)totalPx; ++i) {
        int idx = i * 3;
        printf("  tail px[%d]: R=%u G=%u B=%u\n", i, p[idx], p[idx+1], p[idx+2]);
    }
}


CVI_S32 Z_VI_INIT(Z_VI_CTX_S *pstViCtx)
{
    SAMPLE_COMM_SYS_Exit();

    CVI_S32 s32Ret = CVI_SUCCESS;
    COMPRESS_MODE_E enCompressMode = COMPRESS_MODE_NONE;
    PIC_SIZE_E enPicSize;
    CVI_U32 u32BlkSize;

    SAMPLE_INI_CFG_S stIniCfg = {0};
    stIniCfg = (SAMPLE_INI_CFG_S) {
            .enSource  = VI_PIPE_FRAME_SOURCE_DEV,
            .devNum    = 1,
            .enSnsType[0] = SONY_IMX327_2L_MIPI_2M_30FPS_12BIT,
            .enWDRMode[0] = WDR_MODE_NONE,
            .s32BusId[0]  = 3,
            .MipiDev[0]   = 0xff,
    };

    /* 1. 解析ini */
    s32Ret = SAMPLE_COMM_VI_ParseIni(&stIniCfg);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("Parse ini fail\n");
        return s32Ret;
    }

    CVI_VI_SetDevNum(stIniCfg.devNum);
    s32Ret = SAMPLE_COMM_VI_IniToViCfg(&stIniCfg, &pstViCtx->stViConfig);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    /* 2. 获取图像尺寸 */
    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(
            pstViCtx->stViConfig.astViInfo[0].stSnsInfo.enSnsType, &enPicSize);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &pstViCtx->stSize);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    /* 3. 初始化系统 VB */
    memset(&pstViCtx->stVbConf, 0, sizeof(VB_CONFIG_S));
    pstViCtx->stVbConf.u32MaxPoolCnt = 1;

    u32BlkSize = COMMON_GetPicBufferSize(
            pstViCtx->stSize.u32Width, pstViCtx->stSize.u32Height,
            SAMPLE_PIXEL_FORMAT, DATA_BITWIDTH_8, enCompressMode, DEFAULT_ALIGN
    );

    pstViCtx->stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    pstViCtx->stVbConf.astCommPool[0].u32BlkCnt  = 8;

    // pstViCtx->stVbConf.astCommPool[1].u32BlkSize = u32BlkSize;
    // pstViCtx->stVbConf.astCommPool[1].u32BlkCnt  = 8;

    printf("<<< init system\n");


    s32Ret = SAMPLE_COMM_SYS_Init(&pstViCtx->stVbConf);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    printf("<<< start vi\n");

    /* 4. 启动 VI */
    s32Ret = SAMPLE_COMM_VI_StartSensor(&pstViCtx->stViConfig);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    s32Ret = SAMPLE_COMM_VI_StartDev(&pstViCtx->stViConfig.astViInfo[0]);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    s32Ret = SAMPLE_COMM_VI_StartMIPI(&pstViCtx->stViConfig);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    /* 创建 pipe */
    VI_PIPE_ATTR_S stPipeAttr;
    stPipeAttr.bYuvSkip = CVI_FALSE;
    stPipeAttr.u32MaxW = pstViCtx->stSize.u32Width;
    stPipeAttr.u32MaxH = pstViCtx->stSize.u32Height;
    stPipeAttr.enPixFmt = PIXEL_FORMAT_RGB_BAYER_12BPP;
    stPipeAttr.enBitWidth = DATA_BITWIDTH_12;
    stPipeAttr.stFrameRate.s32SrcFrameRate = -1;
    stPipeAttr.stFrameRate.s32DstFrameRate = -1;
    stPipeAttr.bNrEn = CVI_TRUE;
    stPipeAttr.bYuvBypassPath = CVI_FALSE;
    stPipeAttr.enCompressMode = pstViCtx->stViConfig.astViInfo[0].stChnInfo.enCompressMode;

    pstViCtx->ViPipe = 0;
    pstViCtx->ViChn = 0;
    pstViCtx->ViDev = 0;

    s32Ret = CVI_VI_CreatePipe(pstViCtx->ViPipe, &stPipeAttr);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    s32Ret = CVI_VI_StartPipe(pstViCtx->ViPipe);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    s32Ret = SAMPLE_COMM_VI_CreateIsp(&pstViCtx->stViConfig);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    s32Ret = SAMPLE_COMM_VI_StartViChn(&pstViCtx->stViConfig);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    CVI_SYS_SetVPSSMode(VPSS_MODE_DUAL);

    VI_VPSS_MODE_S stViVpssMode;
    stViVpssMode.aenMode[0] = VI_OFFLINE_VPSS_OFFLINE;
    stViVpssMode.aenMode[1] = VI_ONLINE_VPSS_OFFLINE;

    CVI_SYS_SetVIVPSSMode(&stViVpssMode);

    printf("<<< init vpss group 1\n");

    /************************************************
     * step5.1:  Init VPSS 1
     ************************************************/
    VPSS_GRP	   VpssGrp	  = 1;
    VPSS_GRP_ATTR_S    stVpssGrpAttr;
    VPSS_CHN           VpssChn        = VPSS_CHN0;
    VPSS_CHN           VpssChn_1        = VPSS_CHN1;
    CVI_BOOL           abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {0, 0};
    VPSS_CHN_ATTR_S    astVpssChnAttr[VPSS_MAX_PHY_CHN_NUM] = {0, 0};

    stVpssGrpAttr.stFrameRate.s32SrcFrameRate    = -1;
    stVpssGrpAttr.stFrameRate.s32DstFrameRate    = -1;
    stVpssGrpAttr.enPixelFormat                  = SAMPLE_PIXEL_FORMAT;
    stVpssGrpAttr.u32MaxW                        = pstViCtx->stSize.u32Width;
    stVpssGrpAttr.u32MaxH                        = pstViCtx->stSize.u32Height;
    stVpssGrpAttr.u8VpssDev                      = 1;

    // 通道0提供给Camera模块read使用
    astVpssChnAttr[VpssChn].u32Width                    = 552;
    astVpssChnAttr[VpssChn].u32Height                   = 368;
    astVpssChnAttr[VpssChn].enVideoFormat               = VIDEO_FORMAT_LINEAR;
    astVpssChnAttr[VpssChn].enPixelFormat               = PIXEL_FORMAT_RGB_888;
    astVpssChnAttr[VpssChn].stFrameRate.s32SrcFrameRate = 30;
    astVpssChnAttr[VpssChn].stFrameRate.s32DstFrameRate = 30;
    astVpssChnAttr[VpssChn].u32Depth                    = 0;
    astVpssChnAttr[VpssChn].bMirror                     = CVI_FALSE;
    astVpssChnAttr[VpssChn].bFlip                       = CVI_FALSE;
    astVpssChnAttr[VpssChn].stAspectRatio.enMode        = ASPECT_RATIO_AUTO;
    astVpssChnAttr[VpssChn].stAspectRatio.bEnableBgColor = CVI_TRUE;
    astVpssChnAttr[VpssChn].stAspectRatio.u32BgColor    = COLOR_RGB_YELLOW;
    astVpssChnAttr[VpssChn].stNormalize.bEnable         = CVI_FALSE;

    // 通道1提供给NN模块作为输入
    astVpssChnAttr[VpssChn_1].u32Width                    = 552;
    astVpssChnAttr[VpssChn_1].u32Height                   = 368;
    astVpssChnAttr[VpssChn_1].enVideoFormat               = VIDEO_FORMAT_LINEAR;
    astVpssChnAttr[VpssChn_1].enPixelFormat               = SAMPLE_PIXEL_FORMAT;
    astVpssChnAttr[VpssChn_1].stFrameRate.s32SrcFrameRate = 30;
    astVpssChnAttr[VpssChn_1].stFrameRate.s32DstFrameRate = 30;
    astVpssChnAttr[VpssChn_1].u32Depth                    = 0;
    astVpssChnAttr[VpssChn_1].bMirror                     = CVI_FALSE;
    astVpssChnAttr[VpssChn_1].bFlip                       = CVI_FALSE;
    astVpssChnAttr[VpssChn_1].stAspectRatio.enMode        = ASPECT_RATIO_AUTO;
    astVpssChnAttr[VpssChn_1].stAspectRatio.bEnableBgColor = CVI_TRUE;
    astVpssChnAttr[VpssChn_1].stAspectRatio.u32BgColor    = COLOR_RGB_BLACK;
    astVpssChnAttr[VpssChn_1].stNormalize.bEnable         = CVI_FALSE;

    /*start vpss*/
    abChnEnable[0] = CVI_TRUE;
    abChnEnable[1] = CVI_TRUE;
    s32Ret = SAMPLE_COMM_VPSS_Init(VpssGrp, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("init vpss group failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    printf("<<< start vpss group 1\n");

    s32Ret = SAMPLE_COMM_VPSS_Start(VpssGrp, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("start vpss group failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    printf("<<< vi bind vpss group 1\n");

    s32Ret = SAMPLE_COMM_VI_Bind_VPSS(pstViCtx->ViPipe, pstViCtx->ViChn, VpssGrp);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("vi bind vpss failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    printf("<<< init vpss group 0\n");


    /************************************************
     * step5.2:  Init VPSS 0
     ************************************************/
    VPSS_GRP	   VpssGrp1	  = 0;
    VPSS_GRP_ATTR_S    stVpssGrpAttr1;
    VPSS_CHN           VpssChn1        = VPSS_CHN0;
    CVI_BOOL           abChnEnable1[VPSS_MAX_PHY_CHN_NUM] = {0};
    VPSS_CHN_ATTR_S    astVpssChnAttr1[VPSS_MAX_PHY_CHN_NUM] = {0};
    memset(&stVpssGrpAttr1, 0, sizeof(stVpssGrpAttr1));


    stVpssGrpAttr1.stFrameRate.s32SrcFrameRate    = -1;
    stVpssGrpAttr1.stFrameRate.s32DstFrameRate    = -1;
    stVpssGrpAttr1.enPixelFormat                  = PIXEL_FORMAT_RGB_888;
    stVpssGrpAttr1.u32MaxW                        = 552;
    stVpssGrpAttr1.u32MaxH                        = 368;
    stVpssGrpAttr1.u8VpssDev                      = 0;


    astVpssChnAttr1[VpssChn1].u32Width                    = 552;
    astVpssChnAttr1[VpssChn1].u32Height                   = 368;
    astVpssChnAttr1[VpssChn1].enVideoFormat               = VIDEO_FORMAT_LINEAR;
    astVpssChnAttr1[VpssChn1].enPixelFormat               = PIXEL_FORMAT_NV21;
    astVpssChnAttr1[VpssChn1].stFrameRate.s32SrcFrameRate = 30;
    astVpssChnAttr1[VpssChn1].stFrameRate.s32DstFrameRate = 30;
    astVpssChnAttr1[VpssChn1].u32Depth                    = 1;
    astVpssChnAttr1[VpssChn1].bMirror                     = CVI_FALSE;
    astVpssChnAttr1[VpssChn1].bFlip                       = CVI_FALSE;
    astVpssChnAttr1[VpssChn1].stAspectRatio.enMode        = ASPECT_RATIO_AUTO;
    astVpssChnAttr1[VpssChn1].stAspectRatio.bEnableBgColor = CVI_TRUE;
    astVpssChnAttr1[VpssChn1].stAspectRatio.u32BgColor    = COLOR_RGB_BLACK;
    astVpssChnAttr1[VpssChn1].stNormalize.bEnable         = CVI_FALSE;

    /*start vpss*/
    abChnEnable1[0] = CVI_TRUE;
    s32Ret = SAMPLE_COMM_VPSS_Init(VpssGrp1, abChnEnable1, &stVpssGrpAttr1, astVpssChnAttr1);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("init vpss group 1 failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    printf("<<< start vpss group 0\n");

    s32Ret = SAMPLE_COMM_VPSS_Start(VpssGrp1, abChnEnable1, &stVpssGrpAttr1, astVpssChnAttr1);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("start vpss group 1 failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    printf("<<< init vo\n");

    /************************************************
     * step6:  Init VO
     ************************************************/
    SAMPLE_VO_CONFIG_S stVoConfig;
    RECT_S stDefDispRect  = {0, 0, 368, 552};
    SIZE_S stDefImageSize = {368, 552};
    VO_CHN VoChn = 0;

    s32Ret = SAMPLE_COMM_VO_GetDefConfig(&stVoConfig);
    if (s32Ret != CVI_SUCCESS) {
        CVI_TRACE_LOG(CVI_DBG_ERR, "SAMPLE_COMM_VO_GetDefConfig failed with %#x\n", s32Ret);
        return s32Ret;
    }

    stVoConfig.VoDev	 = VoChn;
    stVoConfig.stVoPubAttr.enIntfType  = VO_INTF_MIPI;
    stVoConfig.stVoPubAttr.enIntfSync  = VO_OUTPUT_720P60;
    stVoConfig.stDispRect	 = stDefDispRect;
    stVoConfig.stImageSize	 = stDefImageSize;
    stVoConfig.enPixFormat	 = SAMPLE_PIXEL_FORMAT;
    stVoConfig.enVoMode	 = VO_MODE_1MUX;

    s32Ret = SAMPLE_COMM_VO_StartVO(&stVoConfig);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("SAMPLE_COMM_VO_StartVO failed with %#x\n", s32Ret);
        return s32Ret;
    }

    CVI_VO_SetChnRotation(VoChn, VoChn, ROTATION_90);

    SAMPLE_COMM_VPSS_Bind_VO(0, VPSS_CHN0, 0, VoChn);

    CVI_VO_EnableChn(VoChn, VoChn);
    CVI_VO_ShowChn(VoChn, VoChn);


    SAMPLE_PRT("[Z_VI_INIT] success. width=%d height=%d\n",
               pstViCtx->stSize.u32Width, pstViCtx->stSize.u32Height);

    return CVI_SUCCESS;
}

CVI_S32 Z_VI_TAKE_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec)
{
    CVI_S32 s32Ret;
    s32Ret = CVI_VI_GetChnFrame(pstViCtx->ViPipe, pstViCtx->ViChn, pstFrameInfo, s32MilliSec);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("[Z_VI_TAKE_FRAME] failed: 0x%x\n", s32Ret);
    }
    return s32Ret;
}

CVI_S32 Z_VI_RELEASE_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo)
{
    CVI_S32 s32Ret;
    s32Ret = CVI_VI_ReleaseChnFrame(pstViCtx->ViPipe, pstViCtx->ViChn, pstFrameInfo);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("[Z_VI_RELEASE_FRAME] failed: 0x%x\n", s32Ret);
    }
    return s32Ret;
}

CVI_S32 Z_VPSS_TAKE_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec)
{
    CVI_S32 s32GetRet = CVI_SUCCESS;
    VPSS_GRP VpssGrp = 0;      /* 与上面创建的保持一致 */
    VPSS_CHN VpssChn = VPSS_CHN0;

    s32GetRet = CVI_VPSS_GetChnFrame(VpssGrp, VpssChn, pstFrameInfo, s32MilliSec);
    if (s32GetRet != CVI_SUCCESS) {
        /* 超时或失败：打印并继续（可根据需要做重试/断开处理） */
        SAMPLE_PRT("CVI_VPSS_GetChnFrame failed: 0x%x\n", s32GetRet);
        return s32GetRet;
    }

    return s32GetRet;
}

CVI_S32 Z_VPSS_RELEASE_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    VPSS_GRP VpssGrp = 0;      /* 与上面创建的保持一致 */
    VPSS_CHN VpssChn = VPSS_CHN0;

    s32Ret = CVI_VPSS_ReleaseChnFrame(VpssGrp, VpssChn, pstFrameInfo);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("CVI_VPSS_ReleaseChnFrame failed: 0x%x\n", s32Ret);
    }

    return s32Ret;
}

CVI_S32 Z_VPSS_TAKE_FRAME1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec)
{
    CVI_S32 s32GetRet = CVI_SUCCESS;
    VPSS_GRP VpssGrp = 1;      /* 与上面创建的保持一致 */
    VPSS_CHN VpssChn = VPSS_CHN0;

    s32GetRet = CVI_VPSS_GetChnFrame(VpssGrp, VpssChn, pstFrameInfo, s32MilliSec);
    if (s32GetRet != CVI_SUCCESS) {
        /* 超时或失败：打印并继续（可根据需要做重试/断开处理） */
        SAMPLE_PRT("CVI_VPSS_GetChnFrame failed: 0x%x\n", s32GetRet);
        return s32GetRet;
    }

    return s32GetRet;
}

CVI_S32 Z_VPSS_RELEASE_FRAME1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    VPSS_GRP VpssGrp = 1;      /* 与上面创建的保持一致 */
    VPSS_CHN VpssChn = VPSS_CHN0;

    s32Ret = CVI_VPSS_ReleaseChnFrame(VpssGrp, VpssChn, pstFrameInfo);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("CVI_VPSS_ReleaseChnFrame failed: 0x%x\n", s32Ret);
    }

    return s32Ret;
}

CVI_S32 Z_VPSS_TAKE_FRAME1_1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec)
{
    CVI_S32 s32GetRet = CVI_SUCCESS;
    VPSS_GRP VpssGrp = 1;      /* 与上面创建的保持一致 */
    VPSS_CHN VpssChn = VPSS_CHN1;

    s32GetRet = CVI_VPSS_GetChnFrame(VpssGrp, VpssChn, pstFrameInfo, s32MilliSec);
    if (s32GetRet != CVI_SUCCESS) {
        /* 超时或失败：打印并继续（可根据需要做重试/断开处理） */
        SAMPLE_PRT("CVI_VPSS_GetChnFrame failed: 0x%x\n", s32GetRet);
        return s32GetRet;
    }

    return s32GetRet;
}

CVI_S32 Z_VPSS_RELEASE_FRAME1_1(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    VPSS_GRP VpssGrp = 1;      /* 与上面创建的保持一致 */
    VPSS_CHN VpssChn = VPSS_CHN1;

    s32Ret = CVI_VPSS_ReleaseChnFrame(VpssGrp, VpssChn, pstFrameInfo);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("CVI_VPSS_ReleaseChnFrame failed: 0x%x\n", s32Ret);
    }

    return s32Ret;
}


CVI_S32 Z_VO_PUSH_FRAME(Z_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo)
{
    CVI_S32 s32Ret;

    VO_CHN VoChn = 0;

    s32Ret = CVI_VO_SendFrame(VoChn, VoChn, pstFrameInfo, 0);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("CVI_VO_SendFrame failed: 0x%x\n", s32Ret);
        /* 即便发送失败，也应 Release 帧以防内存泄露 */
    }

    return s32Ret;
}

#if 0
CVI_S32 Z_VI_TAKE_FRAME_AS_RGB888(
        uint8_t** ppRGB,
        uint32_t* pWidth,
        uint32_t* pHeight,
        uint32_t* pStride,
        uint64_t* pPhyAddr,
        void**    pVirAddr,
        uint32_t* pFrameSize
) {
//    printf("==== Z_VI_TAKE_FRAME_AS_RGB888: begin ====\n");

    // 1) 参数校验
    if (!ppRGB || !pWidth || !pHeight || !pStride || !pPhyAddr || !pVirAddr || !pFrameSize) {
        printf("[ERR] invalid output pointers\n");
        return -1;
    }

    VIDEO_FRAME_INFO_S stFrm;
    memset(&stFrm, 0, sizeof(stFrm));

    // NOTE: 如需指定 grp/chn，可扩展参数；此处假设 0/0
    int vpssGrp = 1, vpssChn = 0;
    CVI_S32 ret = CVI_VPSS_GetChnFrame(vpssGrp, vpssChn, &stFrm, 2000);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] CVI_VPSS_GetChnFrame fail ret=0x%x\n", ret);
        return ret;
    }

    VIDEO_FRAME_S* vf = &stFrm.stVFrame;

//    printf("[DBG] VPSS frame:\n");
//    printf("      size=%ux%u pf=%d(%s)\n", vf->u32Width, vf->u32Height, vf->enPixelFormat, _pf2s(vf->enPixelFormat));
//    printf("      stride0=%u length0=%u\n", vf->u32Stride[0], vf->u32Length[0]);
//    printf("      phy0=0x%016" PRIx64 " vir0=%p\n", vf->u64PhyAddr[0], vf->pu8VirAddr[0]);

    if (vf->enPixelFormat != PIXEL_FORMAT_RGB_888) {
        printf("[ERR] not RGB888, pf=%d(%s)\n", vf->enPixelFormat, _pf2s(vf->enPixelFormat));
        CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
        return -2;
    }

    // 2) 基本字段校验
    if (vf->u32Width == 0 || vf->u32Height == 0) {
        printf("[ERR] invalid size: %ux%u\n", vf->u32Width, vf->u32Height);
        CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
        return -3;
    }
    if (vf->u32Stride[0] < vf->u32Width * 3) {
        printf("[ERR] invalid stride: stride=%u, expect >= %u\n",
               vf->u32Stride[0], vf->u32Width * 3);
        CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
        return -4;
    }
    if (vf->u32Length[0] == 0) {
        printf("[ERR] invalid length0=0\n");
        CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
        return -5;
    }

    // 3) 源虚拟地址：有些平台 VirAddr 可能为 NULL，此时必须 mmap 物理地址
    uint8_t* srcVir = vf->pu8VirAddr[0];
    CVI_BOOL need_unmap = CVI_FALSE;

    if (!srcVir) {
//        printf("[WARN] vf->pu8VirAddr[0] == NULL, try CVI_SYS_Mmap...\n");
        // 注意：某些 SoC 的 SYS_Mmap 接口签名可能略不同，按你实际 SDK 调整
        CVI_VOID* tmp_vir = CVI_SYS_Mmap(vf->u64PhyAddr[0], vf->u32Length[0]);
        if (tmp_vir == NULL) {
            printf("[ERR] CVI_SYS_Mmap failed! phy=0x%llx len=%u\n",
                   vf->u64PhyAddr[0], vf->u32Length[0]);
            CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
            return -6;
        }
        srcVir = (uint8_t*)tmp_vir;
        need_unmap = CVI_TRUE;
//        printf("[DBG] mmap ok: srcVir=%p\n", srcVir);
    }

    // 4) cache：硬件->CPU 读前 Invalidate（即便 mmap 也一样）
    ret = CVI_SYS_IonInvalidateCache(vf->u64PhyAddr[0], srcVir, vf->u32Length[0]);
    if (ret != CVI_SUCCESS) {
        printf("[WARN] InvalidateCache fail ret=0x%x (continue)\n", ret);
    }

    // 5) 计算目标 stride/size（使用 size_t 防溢出）
    const uint32_t srcW = vf->u32Width;
    const uint32_t srcH = vf->u32Height;
    const uint32_t srcStride = vf->u32Stride[0];
//    const size_t   dstStride = (size_t)ALIGN((size_t)srcW * 3, 64);
//    const size_t   dstSize   = dstStride * (size_t)srcH;

    const size_t dstStride = (size_t)srcW * 3;
    const size_t dstSize   = dstStride * (size_t)srcH;

    // 源可用最大字节数（按长度裁剪）
    const size_t   srcMax = (size_t)vf->u32Length[0];
    // 理论上需要拷贝的总字节数（按行拷贝）
    const size_t   idealCopy = (size_t)srcH * (size_t)srcW * 3;

//    printf("[DBG] copy plan: srcW=%u srcH=%u srcStride=%u\n", srcW, srcH, srcStride);
//    printf("[DBG] dstStride=%zu dstSize=%zu (%.2f KB)\n", dstStride, dstSize, dstSize/1024.0);
//    printf("[DBG] srcMax=%zu idealCopy=%zu\n", srcMax, idealCopy);

    // 6) 目标 ION 分配
    CVI_U64 dstPhy = 0;
    CVI_VOID* dstVir = NULL;
    ret = CVI_SYS_IonAlloc_Cached(&dstPhy, &dstVir, "RGB888_Out", (CVI_U32)dstSize);
    if (ret != CVI_SUCCESS || !dstVir) {
        printf("[ERR] IonAlloc fail ret=0x%x size=%zu\n", ret, dstSize);
        if (need_unmap) CVI_SYS_Munmap(srcVir, vf->u32Length[0]);
        CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
        return -7;
    }
//    printf("[DBG] dst alloc ok: phy=0x%016" PRIx64 " vir=%p\n", dstPhy, dstVir);

    // 7) 行拷贝前的边界检查
    //  - 每一行最多能拷贝 srcW*3 字节
    //  - 但整块源缓冲最多 srcMax 字节
    //  - 计算出最后一行的起始偏移必须 < srcMax
    {
        size_t last_line_off = (size_t)(srcH - 1) * (size_t)srcStride;
        if (last_line_off >= srcMax) {
            printf("[ERR] source overflow risk: last_line_off=%zu >= srcMax=%zu\n",
                   last_line_off, srcMax);
            CVI_SYS_IonFree(dstPhy, dstVir);
            if (need_unmap) CVI_SYS_Munmap(srcVir, vf->u32Length[0]);
            CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
            return -8;
        }
        size_t last_line_need = (size_t)srcW * 3;
        size_t last_line_avail = srcMax - last_line_off;
        if (last_line_need > last_line_avail) {
            printf("[ERR] last line need=%zu > avail=%zu (srcMax=%zu, off=%zu)\n",
                   last_line_need, last_line_avail, srcMax, last_line_off);
            CVI_SYS_IonFree(dstPhy, dstVir);
            if (need_unmap) CVI_SYS_Munmap(srcVir, vf->u32Length[0]);
            CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
            return -9;
        }
    }

    // 8) 行拷贝
//    printf("[DBG] memcpy line-by-line ...\n");
    for (uint32_t y = 0; y < srcH; ++y) {
        const uint8_t* s = srcVir + (size_t)y * (size_t)srcStride;
        uint8_t*       d = (uint8_t*)dstVir + (size_t)y * (size_t)dstStride;

        // 每行边界检查（保护）
        size_t remain = srcMax - ((size_t)y * (size_t)srcStride);
        size_t need   = (size_t)srcW * 3;
        if (remain < need) {
            printf("[ERR] line %u: remain=%zu < need=%zu, abort\n", y, remain, need);
            CVI_SYS_IonFree(dstPhy, dstVir);
            if (need_unmap) CVI_SYS_Munmap(srcVir, vf->u32Length[0]);
            CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
            return -10;
        }

        memcpy(d, s, need);
    }

    // 9) 释放 VPSS 帧 + 取消可能的 mmap
    CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
    if (need_unmap) {
        CVI_SYS_Munmap(srcVir, vf->u32Length[0]);
//        printf("[DBG] src munmap done\n");
    }

    // 10) 输出
    *ppRGB     = (uint8_t*)dstVir;
    *pWidth    = srcW;
    *pHeight   = srcH;
    *pStride   = (uint32_t)dstStride;
    *pPhyAddr  = dstPhy;
    *pVirAddr  = dstVir;
    *pFrameSize= (uint32_t)dstSize;

//    printf("[OK ] done: %ux%u stride=%u size=%u outVir=%p outPhy=0x%016" PRIx64 "\n",
//            srcW, srcH, (uint32_t)dstStride, (uint32_t)dstSize, dstVir, dstPhy);
//    printf("==== Z_VI_TAKE_FRAME_AS_RGB888: end ====\n");
    return 0;
}
#endif


CVI_S32 Z_VI_TAKE_FRAME_AS_RGB888(
        uint8_t** ppRGB,
        uint32_t* pWidth,
        uint32_t* pHeight,
        uint32_t* pStride,
        uint64_t* pPhyAddr,
        void**    pVirAddr,
        uint32_t* pFrameSize
) {
    //    printf("==== Z_VI_TAKE_FRAME_AS_RGB888: begin ====\n");

    if (!ppRGB || !pWidth || !pHeight || !pStride || !pPhyAddr || !pVirAddr || !pFrameSize) {
        printf("[ERR] invalid output pointers\n");
        return -1;
    }

    VIDEO_FRAME_INFO_S stFrm;
    memset(&stFrm, 0, sizeof(stFrm));

    int vpssGrp = 1, vpssChn = 0;
    CVI_S32 ret = CVI_VPSS_GetChnFrame(vpssGrp, vpssChn, &stFrm, 2000);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] CVI_VPSS_GetChnFrame fail ret=0x%x\n", ret);
        return ret;
    }

    VIDEO_FRAME_S* vf = &stFrm.stVFrame;

    if (vf->enPixelFormat != PIXEL_FORMAT_RGB_888) {
        printf("[ERR] not RGB888, pf=%d(%s)\n", vf->enPixelFormat, _pf2s(vf->enPixelFormat));
        CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
        return -2;
    }

    if (vf->u32Width == 0 || vf->u32Height == 0) {
        printf("[ERR] invalid size: %ux%u\n", vf->u32Width, vf->u32Height);
        CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
        return -3;
    }
    if (vf->u32Stride[0] < vf->u32Width * 3) {
        printf("[ERR] invalid stride: stride=%u, expect >= %u\n",
               vf->u32Stride[0], vf->u32Width * 3);
        CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
        return -4;
    }
    if (vf->u32Length[0] == 0) {
        printf("[ERR] invalid length0=0\n");
        CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
        return -5;
    }

    uint8_t* srcVir = vf->pu8VirAddr[0];
    CVI_BOOL need_unmap = CVI_FALSE;

    if (!srcVir) {
        CVI_VOID* tmp_vir = CVI_SYS_Mmap(vf->u64PhyAddr[0], vf->u32Length[0]);
        if (tmp_vir == NULL) {
            printf("[ERR] CVI_SYS_Mmap failed! phy=0x%llx len=%u\n",
                   vf->u64PhyAddr[0], vf->u32Length[0]);
            CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
            return -6;
        }
        srcVir = (uint8_t*)tmp_vir;
        need_unmap = CVI_TRUE;
    }

    ret = CVI_SYS_IonInvalidateCache(vf->u64PhyAddr[0], srcVir, vf->u32Length[0]);
    if (ret != CVI_SUCCESS) {
        printf("[WARN] InvalidateCache fail ret=0x%x (continue)\n", ret);
    }

    const uint32_t srcW = vf->u32Width;
    const uint32_t srcH = vf->u32Height;
    const uint32_t srcStride = vf->u32Stride[0];
    const size_t dstStride = (size_t)srcW * 3;
    const size_t dstSize   = dstStride * (size_t)srcH;
    const size_t srcMax = (size_t)vf->u32Length[0];
    const size_t idealCopy = (size_t)srcH * (size_t)srcW * 3;

    CVI_U64 dstPhy = 0;
    CVI_VOID* dstVir = NULL;
    ret = CVI_SYS_IonAlloc_Cached(&dstPhy, &dstVir, "RGB888_Out", (CVI_U32)dstSize);
    if (ret != CVI_SUCCESS || !dstVir) {
        printf("[ERR] IonAlloc fail ret=0x%x size=%zu\n", ret, dstSize);
        if (need_unmap) CVI_SYS_Munmap(srcVir, vf->u32Length[0]);
        CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
        return -7;
    }

    {
        size_t last_line_off = (size_t)(srcH - 1) * (size_t)srcStride;
        if (last_line_off >= srcMax) {
            printf("[ERR] source overflow risk: last_line_off=%zu >= srcMax=%zu\n",
                   last_line_off, srcMax);
            CVI_SYS_IonFree(dstPhy, dstVir);
            if (need_unmap) CVI_SYS_Munmap(srcVir, vf->u32Length[0]);
            CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
            return -8;
        }
        size_t last_line_need = (size_t)srcW * 3;
        size_t last_line_avail = srcMax - last_line_off;
        if (last_line_need > last_line_avail) {
            printf("[ERR] last line need=%zu > avail=%zu\n", last_line_need, last_line_avail);
            CVI_SYS_IonFree(dstPhy, dstVir);
            if (need_unmap) CVI_SYS_Munmap(srcVir, vf->u32Length[0]);
            CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
            return -9;
        }
    }

    if (srcStride == srcW * 3) {
        CVI_SYS_IonFlushCache(vf->u64PhyAddr[0], srcVir, vf->u32Length[0]);
        CVI_S32 ret_dma = CVI_SYS_TDMACopy(dstPhy, vf->u64PhyAddr[0], (CVI_U32)dstSize);
        if (ret_dma != CVI_SUCCESS) {
            printf("[WARN] CVI_SYS_TDMACopy failed (ret=0x%x), fallback to memcpy\n", ret_dma);
            memcpy(dstVir, srcVir, dstSize);
        } else {
            CVI_SYS_IonInvalidateCache(dstPhy, dstVir, (CVI_U32)dstSize);
        }
    } else {
        CVI_TDMA_2D_S tdma2d;
        memset(&tdma2d, 0, sizeof(tdma2d));

        tdma2d.paddr_src        = vf->u64PhyAddr[0];
        tdma2d.paddr_dst        = dstPhy;
        tdma2d.w_bytes          = srcW * 3;     // 每行有效拷贝的字节数
        tdma2d.h                = srcH;         // 总行数
        tdma2d.stride_bytes_src = srcStride;    // 源行跨度（带对齐）
        tdma2d.stride_bytes_dst = srcW * 3;     // 目标行跨度（无对齐）

        CVI_SYS_IonFlushCache(vf->u64PhyAddr[0], srcVir, vf->u32Length[0]);
        CVI_S32 ret_dma2d = CVI_SYS_TDMACopy2D(&tdma2d);
        if (ret_dma2d != CVI_SUCCESS) {
            printf("[WARN] CVI_SYS_TDMACopy2D failed (ret=0x%x), fallback to CPU loop\n", ret_dma2d);
            for (uint32_t y = 0; y < srcH; ++y) {
                const uint8_t* s = srcVir + (size_t)y * (size_t)srcStride;
                uint8_t*       d = (uint8_t*)dstVir + (size_t)y * (size_t)(srcW * 3);
                memcpy(d, s, (size_t)srcW * 3);
            }
        } else {
            CVI_SYS_IonInvalidateCache(dstPhy, dstVir, (CVI_U32)dstSize);
        }
    }

    // 9) 释放 VPSS 帧 + 取消 mmap
    CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
    if (need_unmap) {
        CVI_SYS_Munmap(srcVir, vf->u32Length[0]);
    }

    // 10) 输出
    *ppRGB     = (uint8_t*)dstVir;
    *pWidth    = srcW;
    *pHeight   = srcH;
    *pStride   = (uint32_t)dstStride;
    *pPhyAddr  = dstPhy;
    *pVirAddr  = dstVir;
    *pFrameSize= (uint32_t)dstSize;

    //    printf("==== Z_VI_TAKE_FRAME_AS_RGB888: end ====\n");
    return 0;
}


void Z_VPSS_FreeRGB888(uint64_t phyAddr, void* virAddr)
{
    if (virAddr && phyAddr) {
        CVI_SYS_IonFree(phyAddr, virAddr);
    }
}


CVI_S32 Z_VO_PUSH_FRAME_WITH_RGB888(
    Z_VI_CTX_S *pstViCtx,
    const CVI_U8 *pRGB,
    CVI_U32 inW, CVI_U32 inH,
    VIDEO_FRAME_INFO_S *pstOutFrame
)
{
    if (!pRGB) {
        printf("[ERR] invalid input\n");
        return CVI_FAILURE;
    }

//    printf("[Z_VO_PUSH_FRAME] width=%d height=%d\n", inW, inH);

    const CVI_S32 vpssGrp = 0;
    const CVI_S32 vpssChn = VPSS_CHN0;

    CVI_U32 stride    = ALIGN(inW * 3, 64);
    CVI_U32 frameSize = stride * inH;
    CVI_U32 dataSize  = inW * inH * 3;

    CVI_U64 phyAddr = 0;
    CVI_VOID *pVirAddr = NULL;

    CVI_S32 ret = CVI_SYS_IonAlloc_Cached(&phyAddr, &pVirAddr, "RGB_Frame", frameSize);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] ION alloc failed\n");
        return ret;
    }

    memset(pVirAddr, 0, frameSize);
    for (CVI_U32 y = 0; y < inH; y++) {
        memcpy((CVI_U8*)pVirAddr + y * stride ,
               pRGB + y * inW * 3,
               inW * 3);
    }
    ret = CVI_SYS_IonFlushCache(phyAddr, pVirAddr, frameSize);

    if(ret != CVI_SUCCESS) {
        printf("[ERR] ION CVI_SYS_IonFlushCache failed %x\n", ret);
    }

    VIDEO_FRAME_INFO_S stFrameInfo;
    memset(&stFrameInfo, 0, sizeof(stFrameInfo));
    VIDEO_FRAME_S *vf = &stFrameInfo.stVFrame;

    vf->u32Width      = inW;
    vf->u32Height     = inH;
    vf->enPixelFormat = PIXEL_FORMAT_RGB_888;
    vf->u32Stride[0]  = stride;
    vf->u32Length[0]  = frameSize;

    vf->u64PhyAddr[0] = phyAddr;
    vf->pu8VirAddr[0] = (CVI_U8*)pVirAddr;

//    printf(">>> begin send\n");

    ret = CVI_VPSS_SendFrame(vpssGrp, &stFrameInfo, -1);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] Vpss SendFrame failed %x\n", ret);
        CVI_SYS_IonFree(phyAddr, pVirAddr);
        return ret;
    }

//    printf(">>> send success\n");

    // 获取输出
    ret = CVI_VPSS_GetChnFrame(vpssGrp, vpssChn, pstOutFrame, 2000);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] GetChnFrame failed\n");
        CVI_SYS_IonFree(phyAddr, pVirAddr);
        return ret;
    }

//     记录输入ION块，由调用者释放
    Z_RGB_FRAME_PRIV* priv = malloc(sizeof(Z_RGB_FRAME_PRIV));
    priv->phyAddr = phyAddr;
    priv->virAddr = pVirAddr;
    priv->frameSize = frameSize;
    pstOutFrame->stVFrame.pPrivateData = priv;

    Z_SIMPLE_VPSS_FreeConvertedFrame(pstOutFrame);
    return CVI_SUCCESS;
}

CVI_S32 Z_SIMPLE_VPSS_ConvertRGB888(
        const CVI_U8 *pRGB,
        CVI_U32 inW, CVI_U32 inH,
        VIDEO_FRAME_INFO_S *pstOutFrame
) {
    if (!pRGB || !pstOutFrame) {
        printf("[ERR] invalid input\n");
        return CVI_FAILURE;
    }

    const CVI_S32 vpssGrp = 1;
    const CVI_S32 vpssChn = VPSS_CHN0;

    CVI_U32 stride    = ALIGN(inW * 3, 64);
    CVI_U32 frameSize = stride * inH;
    CVI_U32 dataSize  = inW * inH * 3;

    CVI_U64 phyAddr = 0;
    CVI_VOID *pVirAddr = NULL;

    CVI_S32 ret = CVI_SYS_IonAlloc_Cached(&phyAddr, &pVirAddr, "RGB_Frame", frameSize);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] ION alloc failed\n");
        return ret;
    }

    memset(pVirAddr, 0, frameSize);
    for (CVI_U32 y = 0; y < inH; y++) {
        memcpy((CVI_U8*)pVirAddr + y * stride ,
               pRGB + y * inW * 3,
               inW * 3);
    }
    CVI_SYS_IonFlushCache(phyAddr, pVirAddr, frameSize);

    VIDEO_FRAME_INFO_S stFrameInfo;
    memset(&stFrameInfo, 0, sizeof(stFrameInfo));
    VIDEO_FRAME_S *vf = &stFrameInfo.stVFrame;

    vf->u32Width      = inW;
    vf->u32Height     = inH;
    vf->enPixelFormat = PIXEL_FORMAT_RGB_888;
    vf->u32Stride[0]  = stride;
    vf->u32Length[0]  = frameSize;

    vf->u64PhyAddr[0] = phyAddr;
    vf->pu8VirAddr[0] = (CVI_U8*)pVirAddr;

    ret = CVI_VPSS_SendFrame(vpssGrp, &stFrameInfo, -1);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] SendFrame failed\n");
        CVI_SYS_IonFree(phyAddr, pVirAddr);
        return ret;
    }

    // 获取输出
    ret = CVI_VPSS_GetChnFrame(vpssGrp, vpssChn, pstOutFrame, 2000);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] GetChnFrame failed\n");
        CVI_SYS_IonFree(phyAddr, pVirAddr);
        return ret;
    }

    // 记录输入ION块，由调用者释放
    Z_RGB_FRAME_PRIV* priv = malloc(sizeof(Z_RGB_FRAME_PRIV));
    priv->phyAddr = phyAddr;
    priv->virAddr = pVirAddr;
    priv->frameSize = frameSize;
    pstOutFrame->stVFrame.pPrivateData = priv;

    return CVI_SUCCESS;
}


// 释放输出帧
CVI_S32 Z_SIMPLE_VPSS_FreeConvertedFrame(VIDEO_FRAME_INFO_S *pFrame) {
    if (!pFrame) return CVI_FAILURE;

    const CVI_S32 vpssGrp = 0;
    const CVI_S32 vpssChn = VPSS_CHN0;

    // 释放 VPSS 输出缓存
    CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, pFrame);

    // 释放输入 ION
    Z_RGB_FRAME_PRIV* priv = (Z_RGB_FRAME_PRIV*)pFrame->stVFrame.pPrivateData;
    if (priv) {
        CVI_SYS_IonFree(priv->phyAddr, priv->virAddr);
        free(priv);
    }

    return CVI_SUCCESS;
}



CVI_S32 Z_VI_DEINIT(Z_VI_CTX_S *pstViCtx)
{
    VO_CHN VoChn = 0;
    VPSS_GRP	   VpssGrp	  = 1;
    VPSS_GRP	   VpssGrp1	  = 0;
    VPSS_GRP	   VpssGrp3	  = 3;
    CVI_BOOL           abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {1, 1};
    CVI_BOOL           abChnEnable1[VPSS_MAX_PHY_CHN_NUM] = {1};
    CVI_BOOL           abChnEnable3[VPSS_MAX_PHY_CHN_NUM] = {1};

    SAMPLE_COMM_VPSS_UnBind_VO(0, 0, 0, VoChn);
    SAMPLE_COMM_VI_UnBind_VPSS(pstViCtx->ViPipe, pstViCtx->ViChn, VpssGrp);
    SAMPLE_COMM_VPSS_Stop(VpssGrp, abChnEnable);
    SAMPLE_COMM_VPSS_Stop(VpssGrp1, abChnEnable1);
    SAMPLE_COMM_VPSS_Stop(VpssGrp3, abChnEnable3);

    SAMPLE_COMM_VI_DestroyIsp(&pstViCtx->stViConfig);
    SAMPLE_COMM_VI_DestroyVi(&pstViCtx->stViConfig);


    CVI_VO_HideChn(VoChn, VoChn);
    CVI_VO_DisableChn(VoChn, VoChn);

//    SAMPLE_COMM_VO_StopVO(&stVoConfig);

    SAMPLE_COMM_SYS_Exit();
    SAMPLE_PRT("[Z_VI_DEINIT] done\n");
    return CVI_SUCCESS;
}
