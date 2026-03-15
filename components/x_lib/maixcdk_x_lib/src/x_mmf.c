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
#include "x_mmf.h"
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

// 打印解析后的 ini 配置，便于确认实际生效的参数
static void _dump_ini_cfg(const SAMPLE_INI_CFG_S *cfg) {
    if (!cfg) {
        LOGW("[INI] cfg is NULL, skip dump");
        return;
    }

    LOGI("[INI] enSource=%d devNum=%d", cfg->enSource, cfg->devNum);

    int num = cfg->devNum;
    if (num <= 0) {
        LOGW("[INI] devNum <= 0");
        return;
    }

    // 为安全起见，限制最多打印前 4 个通道/设备
    int maxPrint = (num > 4) ? 4 : num;
    for (int i = 0; i < maxPrint; ++i) {
        LOGI("[INI] idx=%d snsType=%d wdrMode=%d busId=%d mipiDev=0x%x",
             i,
             cfg->enSnsType[i],
             cfg->enWDRMode[i],
             cfg->s32BusId[i],
             (unsigned int)cfg->MipiDev[i]);
    }
    if (num > maxPrint) {
        LOGI("[INI] ... (total %d, printed %d)", num, maxPrint);
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


CVI_S32 X_VI_INIT(X_VI_CTX_S *pstViCtx)
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

    // 详细打印解析后的 ini 结果
    _dump_ini_cfg(&stIniCfg);

    CVI_VI_SetDevNum(stIniCfg.devNum);
    s32Ret = SAMPLE_COMM_VI_IniToViCfg(&stIniCfg, &pstViCtx->stViConfig);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    // 打印转换后的 VI 配置关键字段（以 0 号为例）
    LOGI("[VI CFG] snsType=%d compressMode=%d",
         pstViCtx->stViConfig.astViInfo[0].stSnsInfo.enSnsType,
         pstViCtx->stViConfig.astViInfo[0].stChnInfo.enCompressMode);

    /* 2. 获取图像尺寸 */
    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(
            pstViCtx->stViConfig.astViInfo[0].stSnsInfo.enSnsType, &enPicSize);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &pstViCtx->stSize);
    if (s32Ret != CVI_SUCCESS) return s32Ret;

        LOGI("[SIZE] parsed image size: %ux%u",
         pstViCtx->stSize.u32Width, pstViCtx->stSize.u32Height);

    /* 3. 初始化系统 VB */
    memset(&pstViCtx->stVbConf, 0, sizeof(VB_CONFIG_S));
    pstViCtx->stVbConf.u32MaxPoolCnt = 1;

    u32BlkSize = COMMON_GetPicBufferSize(
            pstViCtx->stSize.u32Width, pstViCtx->stSize.u32Height,
            SAMPLE_PIXEL_FORMAT, DATA_BITWIDTH_8, enCompressMode, DEFAULT_ALIGN
    );

        LOGI("[VB] blockSize=%u (fmt=%d bitwidth=%d compress=%d align=%d)",
         u32BlkSize,
         SAMPLE_PIXEL_FORMAT,
         DATA_BITWIDTH_8,
         enCompressMode,
         DEFAULT_ALIGN);

    pstViCtx->stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
    pstViCtx->stVbConf.astCommPool[0].u32BlkCnt  = 4;

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

    SAMPLE_PRT("[X_VI_INIT] success. width=%d height=%d\n",
               pstViCtx->stSize.u32Width, pstViCtx->stSize.u32Height);

    return CVI_SUCCESS;
}

CVI_S32 X_VI_TAKE_FRAME(X_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec)
{
    CVI_S32 s32Ret;
    s32Ret = CVI_VI_GetChnFrame(pstViCtx->ViPipe, pstViCtx->ViChn, pstFrameInfo, s32MilliSec);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("[X_VI_TAKE_FRAME] failed: 0x%x\n", s32Ret);
    }
    return s32Ret;
}

CVI_S32 X_VI_RELEASE_FRAME(X_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo)
{
    CVI_S32 s32Ret;
    s32Ret = CVI_VI_ReleaseChnFrame(pstViCtx->ViPipe, pstViCtx->ViChn, pstFrameInfo);
    if (s32Ret != CVI_SUCCESS) {
        SAMPLE_PRT("[X_VI_RELEASE_FRAME] failed: 0x%x\n", s32Ret);
    }
    return s32Ret;
}


CVI_S32 X_VI_DEINIT(X_VI_CTX_S *pstViCtx)
{
    VO_CHN VoChn = 0;
    VPSS_GRP	   VpssGrp	  = 1;
    VPSS_GRP	   VpssGrp1	  = 0;
    CVI_BOOL           abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {1, 1};
    CVI_BOOL           abChnEnable1[VPSS_MAX_PHY_CHN_NUM] = {1};
    CVI_S32            ret = CVI_SUCCESS;
;
    SAMPLE_COMM_VI_DestroyIsp(&pstViCtx->stViConfig);
    SAMPLE_COMM_VI_DestroyVi(&pstViCtx->stViConfig);

    SAMPLE_COMM_SYS_Exit();
    SAMPLE_PRT("[X_VI_DEINIT] done\n");
    return CVI_SUCCESS;
}
