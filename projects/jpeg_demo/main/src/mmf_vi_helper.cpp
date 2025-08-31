#include "mmf_vi_helper.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int _mmf_vi_frame_pop(int ch, void **frame_info,  mmf_frame_info_t *frame_info_mmap, int block_ms) {
    if (frame_info == NULL || frame_info_mmap == NULL) {
        printf("invalid param\n");
        return -1;
    }

    int ret = -1;
    VIDEO_FRAME_INFO_S *frame = (VIDEO_FRAME_INFO_S *)malloc(sizeof(VIDEO_FRAME_INFO_S));
    memset(frame, 0, sizeof(VIDEO_FRAME_INFO_S));
    if ((ret = CVI_VPSS_GetChnFrame(0, ch, frame, (CVI_S32)block_ms)) == 0) {
        int image_size = frame->stVFrame.u32Length[0]
            + frame->stVFrame.u32Length[1]
            + frame->stVFrame.u32Length[2];
        CVI_VOID *vir_addr;
        vir_addr = CVI_SYS_MmapCache(frame->stVFrame.u64PhyAddr[0], image_size);
        CVI_SYS_IonInvalidateCache(frame->stVFrame.u64PhyAddr[0], vir_addr, image_size);

        frame->stVFrame.pu8VirAddr[0] = (CVI_U8 *)vir_addr;		// save virtual address for munmap
        frame_info_mmap->data = vir_addr;
        frame_info_mmap->len = image_size;
        frame_info_mmap->w = frame->stVFrame.u32Width;
        frame_info_mmap->h = frame->stVFrame.u32Height;
        frame_info_mmap->fmt = frame->stVFrame.enPixelFormat;
    } else {
        free(frame);
        frame = NULL;
    }

    if (frame_info) {
        *frame_info = frame;
    }
    return ret;
}

void _mmf_vi_frame_free(int ch, void **frame_info)
{
    if (!frame_info || !*frame_info) {
        return;
    }

    VIDEO_FRAME_INFO_S *frame = (VIDEO_FRAME_INFO_S *)*frame_info;
    int image_size = frame->stVFrame.u32Length[0]
        + frame->stVFrame.u32Length[1]
        + frame->stVFrame.u32Length[2];
    CVI_SYS_Munmap(frame->stVFrame.pu8VirAddr[0], image_size);
    if (CVI_VPSS_ReleaseChnFrame(0, ch, frame) != 0) {
        SAMPLE_PRT("CVI_VI_ReleaseChnFrame NG\n");
    }

    free(*frame_info);
    *frame_info = NULL;
}
