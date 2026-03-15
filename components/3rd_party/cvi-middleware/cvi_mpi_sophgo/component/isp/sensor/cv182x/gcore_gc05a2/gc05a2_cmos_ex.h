#ifndef __GC05A2_CMOS_EX_H_
#define __GC05A2_CMOS_EX_H_

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#include <linux/cvi_type.h>
#include "cvi_sns_ctrl.h"

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

enum gc05a2_linear_regs_e {
	LINEAR_EXP_H,       //0x0202 bit[13:8]
	LINEAR_EXP_L,       //0x0203
	LINEAR_GAIN_H,      //0x0204 bit[15:8]
	LINEAR_GAIN_L,      //0x0205 bit[7:0]
	LINEAR_VTS_H,       //0x0340 bit[13:8]
	LINEAR_VTS_L,       //0x0341
	LINEAR_FLIP_MIRROR, //0x0101 bit[1:0]
	LINEAR_REGS_NUM
};

typedef enum _GC05A2_MODE_E {
	GC05A2_MODE_720P60 = 0,
	GC05A2_MODE_1280x960P30,
	GC05A2_MODE_2560x1920P30,
	GC05A2_MODE_LINEAR_NUM,
	GC05A2_MODE_NUM
} GC05A2_MODE_E;

typedef struct _GC05A2_STATE_S {
	CVI_U32		u32Sexp_MAX;
} GC05A2_STATE_S;

typedef struct _GC05A2_MODE_S {
	ISP_WDR_SIZE_S astImg[2];
	CVI_FLOAT f32MaxFps;
	CVI_FLOAT f32MinFps;
	CVI_U32 u32HtsDef;
	CVI_U32 u32VtsDef;
	SNS_ATTR_S stExp[2];
	SNS_ATTR_LARGE_S stAgain[2];
	SNS_ATTR_LARGE_S stDgain[2];
	char name[64];
} GC05A2_MODE_S;

/****************************************************************************
 * external variables and functions                                         *
 ****************************************************************************/

extern ISP_SNS_STATE_S *g_pastGc05a2[VI_MAX_PIPE_NUM];
extern ISP_SNS_COMMBUS_U g_aunGc05a2_BusInfo[];
extern ISP_SNS_MIRRORFLIP_TYPE_E g_aeGc05a2_MirrorFip[VI_MAX_PIPE_NUM];
extern CVI_U8 gc05a2_i2c_addr;
extern const CVI_U32 gc05a2_addr_byte;
extern const CVI_U32 gc05a2_data_byte;
extern void gc05a2_init(VI_PIPE ViPipe);
extern void gc05a2_exit(VI_PIPE ViPipe);
extern void gc05a2_standby(VI_PIPE ViPipe);
extern void gc05a2_restart(VI_PIPE ViPipe);
extern int  gc05a2_write_register(VI_PIPE ViPipe, int addr, int data);
extern int  gc05a2_read_register(VI_PIPE ViPipe, int addr);
extern int  gc05a2_probe(VI_PIPE ViPipe);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */


#endif /* __GC05A2_CMOS_EX_H_ */

