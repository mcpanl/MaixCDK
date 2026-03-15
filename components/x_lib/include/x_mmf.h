#ifndef __X_MMF_H__
#define __X_MMF_H__


#include "sample_comm.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

typedef struct _X_VI_CTX_S {
    VI_DEV ViDev;
    VI_PIPE ViPipe;
    VI_CHN ViChn;
    SAMPLE_VI_CONFIG_S stViConfig;
    VB_CONFIG_S stVbConf;
    SIZE_S stSize;
} X_VI_CTX_S;

/* 函数声明 */
CVI_S32 X_VI_INIT(X_VI_CTX_S *pstViCtx);
CVI_S32 X_VI_TAKE_FRAME(X_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo, CVI_S32 s32MilliSec);
CVI_S32 X_VI_RELEASE_FRAME(X_VI_CTX_S *pstViCtx, VIDEO_FRAME_INFO_S *pstFrameInfo);
CVI_S32 X_VI_DEINIT(X_VI_CTX_S *pstViCtx);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif /* End of #ifndef __X_MMF_H__ */