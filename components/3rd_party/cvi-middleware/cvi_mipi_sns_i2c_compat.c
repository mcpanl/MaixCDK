/*
 * Compat shim: CVI_MIPI_Set_SnsI2cInfo is present in sample/common and headers,
 * but prebuilt libisp.so (riscv64 & arm64) does not export it yet.
 * Reuse fd_mipi / mipi_open_dev from libisp.so.
 */
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>

#include "cvi_sns_ctrl.h"
#include <linux/cvi_defines.h>
#include <linux/cvi_common.h>
#include "cvi_isp.h"
#include "linux/cif_uapi.h"

extern CVI_S32 fd_mipi;
extern CVI_S32 mipi_open_dev(CVI_VOID);

CVI_S32 CVI_MIPI_Set_SnsI2cInfo(CVI_S32 ViPipe, const CVI_VOID *stSnsI2cInfo)
{
	CVI_S32 s32Ret = 0;
	SNS_I2C_INFO *snsi2cinfo;

	if ((ViPipe < 0) || (ViPipe >= VI_MAX_PIPE_NUM)) {
		return -ENODEV;
	}

	if (fd_mipi < 0) {
		s32Ret = mipi_open_dev();
		if (s32Ret != CVI_SUCCESS)
			return s32Ret;
	}

	snsi2cinfo = (SNS_I2C_INFO *)stSnsI2cInfo;
	if (ioctl(fd_mipi, CVI_MIPI_SET_SNS_I2C_INFO, (void *)(uintptr_t)snsi2cinfo) < 0) {
		return errno;
	}
	return s32Ret;
}
