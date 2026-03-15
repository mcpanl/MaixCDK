#ifndef __SC231HAI_CMOS_EX_H_
#define __SC231HAI_CMOS_EX_H_

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif


#include <linux/cvi_type.h>
#include "cvi_sns_ctrl.h"


enum sc231hai_linear_regs_e {
	LINEAR_SHS1_0_ADDR,
	LINEAR_SHS1_1_ADDR,
	LINEAR_SHS1_2_ADDR,
	LINEAR_AGAIN_0_ADDR,
	LINEAR_AGAIN_1_ADDR,
	LINEAR_DGAIN_0_ADDR,
	LINEAR_DGAIN_1_ADDR,
	LINEAR_VMAX_0_ADDR,
	LINEAR_VMAX_1_ADDR,
	LINEAR_FLIP_MIRROR,
	LINEAR_REGS_NUM
};

enum sc231hai_dol2_regs_e {
	WDR2_SHS1_0_ADDR,
	WDR2_SHS1_1_ADDR,
	WDR2_SHS1_2_ADDR,
	WDR2_SHS2_0_ADDR,
	WDR2_SHS2_1_ADDR,
	WDR2_SHS2_2_ADDR,
	WDR2_AGAIN1_0_ADDR,
	WDR2_AGAIN1_1_ADDR,
	WDR2_DGAIN1_0_ADDR,
	WDR2_DGAIN1_1_ADDR,
	WDR2_AGAIN2_0_ADDR,
	WDR2_AGAIN2_1_ADDR,
	WDR2_DGAIN2_0_ADDR,
	WDR2_DGAIN2_1_ADDR,
	WDR2_VMAX_0_ADDR,
	WDR2_VMAX_1_ADDR,
	WDR2_MAXSEXP_0_ADDR,
	WDR2_MAXSEXP_1_ADDR,
	WDR2_FLIP_MIRROR,
	WDR2_REGS_NUM
};

typedef enum _SC231HAI_MODE_E {
	SC231HAI_MODE_480P60 = 0,
	SC231HAI_MODE_1080P60,
	SC231HAI_MODE_LINEAR_NUM,
	SC231HAI_MODE_480P60_WDR = SC231HAI_MODE_LINEAR_NUM,
	SC231HAI_MODE_NUM
} SC231HAI_MODE_E;

typedef struct _SC231HAI_STATE_S {
	CVI_U32		u32Sexp_MAX;	/* (2*{16’h3e23,16’h3e24} – 'd21)/2 */
} SC231HAI_STATE_S;

typedef struct _SC231HAI_MODE_S {
	ISP_WDR_SIZE_S astImg[2];
	CVI_FLOAT f32MaxFps;
	CVI_FLOAT f32MinFps;
	CVI_U32 u32HtsDef;
	CVI_U32 u32VtsDef;
	SNS_ATTR_S stExp[2];
	SNS_ATTR_S stAgain[2];
	SNS_ATTR_S stDgain[2];
	CVI_U16 u16SexpMaxReg;		/* {16’h3e23,16’h3e24} */
	char name[64];
} SC231HAI_MODE_S;

/****************************************************************************
 * external variables and functions                                         *
 ****************************************************************************/

extern ISP_SNS_STATE_S *g_pastSC231HAI[VI_MAX_PIPE_NUM];
extern ISP_SNS_COMMBUS_U g_aunSC231HAI_BusInfo[];
extern CVI_U16 g_au16SC231HAI_GainMode[];
extern CVI_U16 g_au16SC231HAI_L2SMode[];
extern const CVI_U8 sc231hai_i2c_addr;
extern const CVI_U32 sc231hai_addr_byte;
extern const CVI_U32 sc231hai_data_byte;
extern void sc231hai_init(VI_PIPE ViPipe);
extern void sc231hai_exit(VI_PIPE ViPipe);
extern void sc231hai_standby(VI_PIPE ViPipe);
extern void sc231hai_restart(VI_PIPE ViPipe);
extern int  sc231hai_write_register(VI_PIPE ViPipe, int addr, int data);
extern int  sc231hai_read_register(VI_PIPE ViPipe, int addr);
extern int  sc231hai_probe(VI_PIPE ViPipe);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */


#endif /* __SC231HAI_CMOS_EX_H_ */
