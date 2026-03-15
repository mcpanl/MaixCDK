#include "z_mmf_priv.h"

typedef struct {
    CVI_U64 phyAddr;
    CVI_VOID *virAddr;
    CVI_U32 frameSize;
} Z_RGB_FRAME_PRIV;

static const char* _pf2s(PIXEL_FORMAT_E pf)
{
    switch (pf) {
        case PIXEL_FORMAT_RGB_888: return "RGB888";
        case PIXEL_FORMAT_RGB_888_PLANAR: return "RGB888_PLANAR";
        case PIXEL_FORMAT_YUV_PLANAR_420: return "420";
        case PIXEL_FORMAT_YUV_PLANAR_422: return "422";
        default: return "UNKNOWN";
    }
}

CVI_S32 Z_VI_TAKE_FRAME_AS_RGB888(
        uint8_t **ppRGB,
        uint32_t *pWidth,
        uint32_t *pHeight,
        uint32_t *pStride,
        uint64_t *pPhyAddr,
        void **pVirAddr,
        uint32_t *pFrameSize)
{
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

    VIDEO_FRAME_S *vf = &stFrm.stVFrame;

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

    uint8_t *srcVir = vf->pu8VirAddr[0];
    CVI_BOOL need_unmap = CVI_FALSE;

    if (!srcVir) {
        CVI_VOID *tmp_vir = CVI_SYS_Mmap(vf->u64PhyAddr[0], vf->u32Length[0]);
        if (tmp_vir == NULL) {
            printf("[ERR] CVI_SYS_Mmap failed! phy=0x%llx len=%u\n",
                   vf->u64PhyAddr[0], vf->u32Length[0]);
            CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
            return -6;
        }
        srcVir = (uint8_t *)tmp_vir;
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
    const size_t dstSize = dstStride * (size_t)srcH;
    const size_t srcMax = (size_t)vf->u32Length[0];

    CVI_U64 dstPhy = 0;
    CVI_VOID *dstVir = NULL;
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

        tdma2d.paddr_src = vf->u64PhyAddr[0];
        tdma2d.paddr_dst = dstPhy;
        tdma2d.w_bytes = srcW * 3;
        tdma2d.h = srcH;
        tdma2d.stride_bytes_src = srcStride;
        tdma2d.stride_bytes_dst = srcW * 3;

        CVI_SYS_IonFlushCache(vf->u64PhyAddr[0], srcVir, vf->u32Length[0]);
        CVI_S32 ret_dma2d = CVI_SYS_TDMACopy2D(&tdma2d);
        if (ret_dma2d != CVI_SUCCESS) {
            printf("[WARN] CVI_SYS_TDMACopy2D failed (ret=0x%x), fallback to CPU loop\n", ret_dma2d);
            for (uint32_t y = 0; y < srcH; ++y) {
                const uint8_t *s = srcVir + (size_t)y * (size_t)srcStride;
                uint8_t *d = (uint8_t *)dstVir + (size_t)y * (size_t)(srcW * 3);
                memcpy(d, s, (size_t)srcW * 3);
            }
        } else {
            CVI_SYS_IonInvalidateCache(dstPhy, dstVir, (CVI_U32)dstSize);
        }
    }

    CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, &stFrm);
    if (need_unmap) {
        CVI_SYS_Munmap(srcVir, vf->u32Length[0]);
    }

    *ppRGB = (uint8_t *)dstVir;
    *pWidth = srcW;
    *pHeight = srcH;
    *pStride = (uint32_t)dstStride;
    *pPhyAddr = dstPhy;
    *pVirAddr = dstVir;
    *pFrameSize = (uint32_t)dstSize;

    return 0;
}

void Z_VPSS_FreeRGB888(uint64_t phyAddr, void *virAddr)
{
    if (virAddr && phyAddr) {
        CVI_SYS_IonFree(phyAddr, virAddr);
    }
}

CVI_S32 Z_VO_PUSH_FRAME_WITH_RGB888(
    Z_VI_CTX_S *pstViCtx,
    const CVI_U8 *pRGB,
    CVI_U32 inW, CVI_U32 inH,
    VIDEO_FRAME_INFO_S *pstOutFrame)
{
    if (!pRGB) {
        printf("[ERR] invalid input\n");
        return CVI_FAILURE;
    }

    const CVI_S32 vpssGrp = 0;
    const CVI_S32 vpssChn = VPSS_CHN0;

    CVI_U32 stride = ALIGN(inW * 3, 64);
    CVI_U32 frameSize = stride * inH;

    CVI_U64 phyAddr = 0;
    CVI_VOID *pVirAddr = NULL;

    (void)pstViCtx;

    CVI_S32 ret = CVI_SYS_IonAlloc_Cached(&phyAddr, &pVirAddr, "RGB_Frame", frameSize);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] ION alloc failed\n");
        return ret;
    }

    memset(pVirAddr, 0, frameSize);
    for (CVI_U32 y = 0; y < inH; y++) {
        memcpy((CVI_U8 *)pVirAddr + y * stride,
               pRGB + y * inW * 3,
               inW * 3);
    }
    ret = CVI_SYS_IonFlushCache(phyAddr, pVirAddr, frameSize);

    if (ret != CVI_SUCCESS) {
        printf("[ERR] ION CVI_SYS_IonFlushCache failed %x\n", ret);
    }

    VIDEO_FRAME_INFO_S stFrameInfo;
    memset(&stFrameInfo, 0, sizeof(stFrameInfo));
    VIDEO_FRAME_S *vf = &stFrameInfo.stVFrame;

    vf->u32Width = inW;
    vf->u32Height = inH;
    vf->enPixelFormat = PIXEL_FORMAT_RGB_888;
    vf->u32Stride[0] = stride;
    vf->u32Length[0] = frameSize;

    vf->u64PhyAddr[0] = phyAddr;
    vf->pu8VirAddr[0] = (CVI_U8 *)pVirAddr;

    ret = CVI_VPSS_SendFrame(vpssGrp, &stFrameInfo, -1);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] Vpss SendFrame failed %x\n", ret);
        CVI_SYS_IonFree(phyAddr, pVirAddr);
        return ret;
    }

    ret = CVI_VPSS_GetChnFrame(vpssGrp, vpssChn, pstOutFrame, 2000);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] GetChnFrame failed\n");
        CVI_SYS_IonFree(phyAddr, pVirAddr);
        return ret;
    }

    Z_RGB_FRAME_PRIV *priv = malloc(sizeof(Z_RGB_FRAME_PRIV));
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
        VIDEO_FRAME_INFO_S *pstOutFrame)
{
    if (!pRGB || !pstOutFrame) {
        printf("[ERR] invalid input\n");
        return CVI_FAILURE;
    }

    const CVI_S32 vpssGrp = 1;
    const CVI_S32 vpssChn = VPSS_CHN0;

    CVI_U32 stride = ALIGN(inW * 3, 64);
    CVI_U32 frameSize = stride * inH;

    CVI_U64 phyAddr = 0;
    CVI_VOID *pVirAddr = NULL;

    CVI_S32 ret = CVI_SYS_IonAlloc_Cached(&phyAddr, &pVirAddr, "RGB_Frame", frameSize);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] ION alloc failed\n");
        return ret;
    }

    memset(pVirAddr, 0, frameSize);
    for (CVI_U32 y = 0; y < inH; y++) {
        memcpy((CVI_U8 *)pVirAddr + y * stride,
               pRGB + y * inW * 3,
               inW * 3);
    }
    CVI_SYS_IonFlushCache(phyAddr, pVirAddr, frameSize);

    VIDEO_FRAME_INFO_S stFrameInfo;
    memset(&stFrameInfo, 0, sizeof(stFrameInfo));
    VIDEO_FRAME_S *vf = &stFrameInfo.stVFrame;

    vf->u32Width = inW;
    vf->u32Height = inH;
    vf->enPixelFormat = PIXEL_FORMAT_RGB_888;
    vf->u32Stride[0] = stride;
    vf->u32Length[0] = frameSize;

    vf->u64PhyAddr[0] = phyAddr;
    vf->pu8VirAddr[0] = (CVI_U8 *)pVirAddr;

    ret = CVI_VPSS_SendFrame(vpssGrp, &stFrameInfo, -1);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] SendFrame failed\n");
        CVI_SYS_IonFree(phyAddr, pVirAddr);
        return ret;
    }

    ret = CVI_VPSS_GetChnFrame(vpssGrp, vpssChn, pstOutFrame, 2000);
    if (ret != CVI_SUCCESS) {
        printf("[ERR] GetChnFrame failed\n");
        CVI_SYS_IonFree(phyAddr, pVirAddr);
        return ret;
    }

    Z_RGB_FRAME_PRIV *priv = malloc(sizeof(Z_RGB_FRAME_PRIV));
    priv->phyAddr = phyAddr;
    priv->virAddr = pVirAddr;
    priv->frameSize = frameSize;
    pstOutFrame->stVFrame.pPrivateData = priv;

    return CVI_SUCCESS;
}

CVI_S32 Z_SIMPLE_VPSS_FreeConvertedFrame(VIDEO_FRAME_INFO_S *pFrame)
{
    if (!pFrame) return CVI_FAILURE;

    const CVI_S32 vpssGrp = 0;
    const CVI_S32 vpssChn = VPSS_CHN0;

    CVI_VPSS_ReleaseChnFrame(vpssGrp, vpssChn, pFrame);

    Z_RGB_FRAME_PRIV *priv = (Z_RGB_FRAME_PRIV *)pFrame->stVFrame.pPrivateData;
    if (priv) {
        CVI_SYS_IonFree(priv->phyAddr, priv->virAddr);
        free(priv);
    }

    return CVI_SUCCESS;
}