#include "x_mmf_priv.h"

#if X_MMF_ENABLE_REGION

CVI_S32 X_MMF_REGION_Create(RGN_HANDLE handle, const RGN_ATTR_S *attr)
{
    if (!attr) {
        return CVI_FAILURE;
    }
    return CVI_RGN_Create(handle, attr);
}

CVI_S32 X_MMF_REGION_Destroy(RGN_HANDLE handle)
{
    return CVI_RGN_Destroy(handle);
}

CVI_S32 X_MMF_REGION_Attach(RGN_HANDLE handle, const MMF_CHN_S *mmf_chn, const RGN_CHN_ATTR_S *chn_attr)
{
    if (!mmf_chn || !chn_attr) {
        return CVI_FAILURE;
    }
    return CVI_RGN_AttachToChn(handle, mmf_chn, chn_attr);
}

CVI_S32 X_MMF_REGION_Detach(RGN_HANDLE handle, const MMF_CHN_S *mmf_chn)
{
    if (!mmf_chn) {
        return CVI_FAILURE;
    }
    return CVI_RGN_DetachFromChn(handle, mmf_chn);
}

CVI_S32 X_MMF_REGION_SetBitmap(RGN_HANDLE handle, const BITMAP_S *bitmap)
{
    if (!bitmap) {
        return CVI_FAILURE;
    }
    return CVI_RGN_SetBitMap(handle, bitmap);
}

#endif
