#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <linux/cvi_comm_video.h>
#include "cvi_sns_ctrl.h"
#include "sc231hai_cmos_ex.h"

static void sc231hai_wdr_480p60_2to1_init(VI_PIPE ViPipe);
static void sc231hai_linear_480p60_init(VI_PIPE ViPipe);
static void sc231hai_linear_1080p60_init(VI_PIPE ViPipe);

const CVI_U8 sc231hai_i2c_addr = 0x30;        /* I2C Address of SC231HAI */
const CVI_U32 sc231hai_addr_byte = 2;
const CVI_U32 sc231hai_data_byte = 1;
static int g_fd[VI_MAX_PIPE_NUM] = {[0 ... (VI_MAX_PIPE_NUM - 1)] = -1};

int sc231hai_i2c_init(VI_PIPE ViPipe)
{
	char acDevFile[16] = {0};
	CVI_U8 u8DevNum;

	if (g_fd[ViPipe] >= 0)
		return CVI_SUCCESS;
	int ret;

	u8DevNum = g_aunSC231HAI_BusInfo[ViPipe].s8I2cDev;
	snprintf(acDevFile, sizeof(acDevFile),  "/dev/i2c-%u", u8DevNum);

	g_fd[ViPipe] = open(acDevFile, O_RDWR, 0600);

	if (g_fd[ViPipe] < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "Open /dev/cvi_i2c_drv-%u error!\n", u8DevNum);
		return CVI_FAILURE;
	}

	ret = ioctl(g_fd[ViPipe], I2C_SLAVE_FORCE, sc231hai_i2c_addr);
	if (ret < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_SLAVE_FORCE error!\n");
		close(g_fd[ViPipe]);
		g_fd[ViPipe] = -1;
		return ret;
	}

	return CVI_SUCCESS;
}

int sc231hai_i2c_exit(VI_PIPE ViPipe)
{
	if (g_fd[ViPipe] >= 0) {
		close(g_fd[ViPipe]);
		g_fd[ViPipe] = -1;
		return CVI_SUCCESS;
	}
	return CVI_FAILURE;
}

int sc231hai_read_register(VI_PIPE ViPipe, int addr)
{
	int ret, data;
	CVI_U8 buf[8];
	CVI_U8 idx = 0;

	if (g_fd[ViPipe] < 0)
		return CVI_FAILURE;

	if (sc231hai_addr_byte == 2)
		buf[idx++] = (addr >> 8) & 0xff;

	// add address byte 0
	buf[idx++] = addr & 0xff;

	ret = write(g_fd[ViPipe], buf, sc231hai_addr_byte);
	if (ret < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_WRITE error!\n");
		return ret;
	}

	buf[0] = 0;
	buf[1] = 0;
	ret = read(g_fd[ViPipe], buf, sc231hai_data_byte);
	if (ret < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_READ error!\n");
		return ret;
	}

	// pack read back data
	data = 0;
	if (sc231hai_data_byte == 2) {
		data = buf[0] << 8;
		data += buf[1];
	} else {
		data = buf[0];
	}

	syslog(LOG_DEBUG, "i2c r 0x%x = 0x%x\n", addr, data);
	return data;
}

int sc231hai_write_register(VI_PIPE ViPipe, int addr, int data)
{
	CVI_U8 idx = 0;
	int ret;
	CVI_U8 buf[8];

	if (g_fd[ViPipe] < 0)
		return CVI_SUCCESS;

	if (sc231hai_addr_byte == 2) {
		buf[idx] = (addr >> 8) & 0xff;
		idx++;
		buf[idx] = addr & 0xff;
		idx++;
	}

	if (sc231hai_data_byte == 1) {
		buf[idx] = data & 0xff;
		idx++;
	}

	ret = write(g_fd[ViPipe], buf, sc231hai_addr_byte + sc231hai_data_byte);
	if (ret < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_WRITE error!\n");
		return CVI_FAILURE;
	}
	syslog(LOG_DEBUG, "i2c w 0x%x 0x%x\n", addr, data);
	return CVI_SUCCESS;
}

static void delay_ms(int ms)
{
	usleep(ms * 1000);
}

void sc231hai_standby(VI_PIPE ViPipe)
{
	sc231hai_write_register(ViPipe, 0x0100, 0x00);
}

void sc231hai_restart(VI_PIPE ViPipe)
{
	sc231hai_write_register(ViPipe, 0x0100, 0x00);
	delay_ms(20);
	sc231hai_write_register(ViPipe, 0x0100, 0x01);
}

void sc231hai_default_reg_init(VI_PIPE ViPipe)
{
	CVI_U32 i;

	for (i = 0; i < g_pastSC231HAI[ViPipe]->astSyncInfo[0].snsCfg.u32RegNum; i++) {
		sc231hai_write_register(ViPipe,
				g_pastSC231HAI[ViPipe]->astSyncInfo[0].snsCfg.astI2cData[i].u32RegAddr,
				g_pastSC231HAI[ViPipe]->astSyncInfo[0].snsCfg.astI2cData[i].u32Data);
	}
}

#define SC231HAI_CHIP_ID_HI_ADDR		0x3107
#define SC231HAI_CHIP_ID_LO_ADDR		0x3108
#define SC231HAI_CHIP_ID			0xcb6a

// void sc231hai_mirror_flip(VI_PIPE ViPipe, ISP_SNS_MIRRORFLIP_TYPE_E eSnsMirrorFlip)
// {
// 	CVI_U8 val = 0;

// 	switch (eSnsMirrorFlip) {
// 	case ISP_SNS_NORMAL:
// 		break;
// 	case ISP_SNS_MIRROR:
// 		val |= 0x6;
// 		break;
// 	case ISP_SNS_FLIP:
// 		val |= 0x60;
// 		break;
// 	case ISP_SNS_MIRROR_FLIP:
// 		val |= 0x66;
// 		break;
// 	default:
// 		return;
// 	}

// 	sc231hai_write_register(ViPipe, 0x3221, val);
// }

int sc231hai_probe(VI_PIPE ViPipe)
{
	int nVal;
	CVI_U16 chip_id;

	delay_ms(4);
	if (sc231hai_i2c_init(ViPipe) != CVI_SUCCESS)
		return CVI_FAILURE;

	nVal = sc231hai_read_register(ViPipe, SC231HAI_CHIP_ID_HI_ADDR);
	if (nVal < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "read sensor id error.\n");
		return nVal;
	}
	chip_id = (nVal & 0xFF) << 8;
	nVal = sc231hai_read_register(ViPipe, SC231HAI_CHIP_ID_LO_ADDR);
	if (nVal < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "read sensor id error.\n");
		return nVal;
	}
	chip_id |= (nVal & 0xFF);

	if (chip_id != SC231HAI_CHIP_ID) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "Sensor ID Mismatch! Use the wrong sensor??\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}


void sc231hai_init(VI_PIPE ViPipe)
{
	WDR_MODE_E       enWDRMode;
	CVI_BOOL          bInit;
	CVI_U8            u8ImgMode;

	bInit       = g_pastSC231HAI[ViPipe]->bInit;
	enWDRMode   = g_pastSC231HAI[ViPipe]->enWDRMode;
	u8ImgMode   = g_pastSC231HAI[ViPipe]->u8ImgMode;

	sc231hai_i2c_init(ViPipe);

	/* When sensor first init, config all registers */
	if (bInit == CVI_FALSE) {
		if (enWDRMode == WDR_MODE_2To1_LINE) {
			if (u8ImgMode == SC231HAI_MODE_480P60_WDR) {
				/* SC231HAI_MODE_480P60_WDR */
				sc231hai_wdr_480p60_2to1_init(ViPipe);
			} else {
			}
		} else {
			if (u8ImgMode == SC231HAI_MODE_480P60)
				/* SC231HAI_MODE_480P60 */
				sc231hai_linear_480p60_init(ViPipe);
			else if (u8ImgMode == SC231HAI_MODE_1080P60) {
				/* SC231HAI_MODE_1080P60 */
				sc231hai_linear_1080p60_init(ViPipe);
			}
		}
	}
	/* When sensor switch mode(linear<->WDR or resolution), config different registers(if possible) */
	else {
		if (enWDRMode == WDR_MODE_2To1_LINE) {
			if (u8ImgMode == SC231HAI_MODE_480P60_WDR) {
				/* SC231HAI_MODE_480P60_WDR */
				sc231hai_wdr_480p60_2to1_init(ViPipe);
			} else {
			}
		} else {
			if (u8ImgMode == SC231HAI_MODE_480P60)
				/* SC231HAI_MODE_480P60 */
				sc231hai_linear_480p60_init(ViPipe);
			else if (u8ImgMode == SC231HAI_MODE_1080P60) {
				/* SC231HAI_MODE_1080P60 */
				sc231hai_linear_1080p60_init(ViPipe);
			}
		}
	}
	g_pastSC231HAI[ViPipe]->bInit = CVI_TRUE;
}

void sc231hai_exit(VI_PIPE ViPipe)
{
	sc231hai_i2c_exit(ViPipe);
}


static void sc231hai_linear_480p60_init(VI_PIPE ViPipe)
{
	sc231hai_write_register(ViPipe, 0x0103,0x01);
	sc231hai_write_register(ViPipe, 0x36e9,0x80);
	sc231hai_write_register(ViPipe, 0x37f9,0x80);
	sc231hai_write_register(ViPipe, 0x301f,0x50);
	sc231hai_write_register(ViPipe, 0x3058,0x21);
	sc231hai_write_register(ViPipe, 0x3059,0x53);
	sc231hai_write_register(ViPipe, 0x305a,0x40);
	sc231hai_write_register(ViPipe, 0x3200,0x01);
	sc231hai_write_register(ViPipe, 0x3201,0x40);
	sc231hai_write_register(ViPipe, 0x3202,0x00);
	sc231hai_write_register(ViPipe, 0x3203,0x3c);
	sc231hai_write_register(ViPipe, 0x3204,0x06);
	sc231hai_write_register(ViPipe, 0x3205,0x47);
	sc231hai_write_register(ViPipe, 0x3206,0x04);
	sc231hai_write_register(ViPipe, 0x3207,0x03);
	sc231hai_write_register(ViPipe, 0x3208,0x02);
	sc231hai_write_register(ViPipe, 0x3209,0x80);
	sc231hai_write_register(ViPipe, 0x320a,0x01);
	sc231hai_write_register(ViPipe, 0x320b,0xe0);
	sc231hai_write_register(ViPipe, 0x320c,0x08);
	sc231hai_write_register(ViPipe, 0x320d,0xca);
	sc231hai_write_register(ViPipe, 0x320e,0x02);
	sc231hai_write_register(ViPipe, 0x320f,0x26);
	sc231hai_write_register(ViPipe, 0x3210,0x00);
	sc231hai_write_register(ViPipe, 0x3211,0x02);
	sc231hai_write_register(ViPipe, 0x3212,0x00);
	sc231hai_write_register(ViPipe, 0x3213,0x02);
	sc231hai_write_register(ViPipe, 0x3215,0x31);
	sc231hai_write_register(ViPipe, 0x3220,0x01);
	sc231hai_write_register(ViPipe, 0x3250,0x00);
	sc231hai_write_register(ViPipe, 0x3301,0x0a);
	sc231hai_write_register(ViPipe, 0x3302,0x20);
	sc231hai_write_register(ViPipe, 0x3304,0x90);
	sc231hai_write_register(ViPipe, 0x3305,0x00);
	sc231hai_write_register(ViPipe, 0x3306,0x68);
	sc231hai_write_register(ViPipe, 0x3309,0xd0);
	sc231hai_write_register(ViPipe, 0x330b,0xd8);
	sc231hai_write_register(ViPipe, 0x330d,0x08);
	sc231hai_write_register(ViPipe, 0x331c,0x04);
	sc231hai_write_register(ViPipe, 0x331e,0x81);
	sc231hai_write_register(ViPipe, 0x331f,0xc1);
	sc231hai_write_register(ViPipe, 0x3323,0x06);
	sc231hai_write_register(ViPipe, 0x3333,0x10);
	sc231hai_write_register(ViPipe, 0x3334,0x40);
	sc231hai_write_register(ViPipe, 0x3364,0x5e);
	sc231hai_write_register(ViPipe, 0x336c,0x8e);
	sc231hai_write_register(ViPipe, 0x337f,0x13);
	sc231hai_write_register(ViPipe, 0x338f,0x80);
	sc231hai_write_register(ViPipe, 0x3390,0x08);
	sc231hai_write_register(ViPipe, 0x3391,0x18);
	sc231hai_write_register(ViPipe, 0x3392,0xb8);
	sc231hai_write_register(ViPipe, 0x3393,0x0e);
	sc231hai_write_register(ViPipe, 0x3394,0x14);
	sc231hai_write_register(ViPipe, 0x3395,0x10);
	sc231hai_write_register(ViPipe, 0x3396,0x88);
	sc231hai_write_register(ViPipe, 0x3397,0x98);
	sc231hai_write_register(ViPipe, 0x3398,0xf8);
	sc231hai_write_register(ViPipe, 0x3399,0x0a);
	sc231hai_write_register(ViPipe, 0x339a,0x0e);
	sc231hai_write_register(ViPipe, 0x339b,0x10);
	sc231hai_write_register(ViPipe, 0x339c,0x16);
	sc231hai_write_register(ViPipe, 0x33ae,0x80);
	sc231hai_write_register(ViPipe, 0x33af,0xc0);
	sc231hai_write_register(ViPipe, 0x33b2,0x50);
	sc231hai_write_register(ViPipe, 0x33b3,0x14);
	sc231hai_write_register(ViPipe, 0x33f8,0x00);
	sc231hai_write_register(ViPipe, 0x33f9,0x68);
	sc231hai_write_register(ViPipe, 0x33fa,0x00);
	sc231hai_write_register(ViPipe, 0x33fb,0x68);
	sc231hai_write_register(ViPipe, 0x33fc,0x48);
	sc231hai_write_register(ViPipe, 0x33fd,0x78);
	sc231hai_write_register(ViPipe, 0x349f,0x03);
	sc231hai_write_register(ViPipe, 0x34a6,0x40);
	sc231hai_write_register(ViPipe, 0x34a7,0x58);
	sc231hai_write_register(ViPipe, 0x34a8,0x10);
	sc231hai_write_register(ViPipe, 0x34a9,0x10);
	sc231hai_write_register(ViPipe, 0x34f8,0x78);
	sc231hai_write_register(ViPipe, 0x34f9,0x10);
	sc231hai_write_register(ViPipe, 0x3619,0x20);
	sc231hai_write_register(ViPipe, 0x361a,0x90);
	sc231hai_write_register(ViPipe, 0x3633,0x44);
	sc231hai_write_register(ViPipe, 0x3637,0x5c);
	sc231hai_write_register(ViPipe, 0x363c,0xc0);
	sc231hai_write_register(ViPipe, 0x363d,0x02);
	sc231hai_write_register(ViPipe, 0x3660,0x80);
	sc231hai_write_register(ViPipe, 0x3661,0x81);
	sc231hai_write_register(ViPipe, 0x3662,0x8f);
	sc231hai_write_register(ViPipe, 0x3663,0x81);
	sc231hai_write_register(ViPipe, 0x3664,0x81);
	sc231hai_write_register(ViPipe, 0x3665,0x82);
	sc231hai_write_register(ViPipe, 0x3666,0x8f);
	sc231hai_write_register(ViPipe, 0x3667,0x08);
	sc231hai_write_register(ViPipe, 0x3668,0x80);
	sc231hai_write_register(ViPipe, 0x3669,0x88);
	sc231hai_write_register(ViPipe, 0x366a,0x98);
	sc231hai_write_register(ViPipe, 0x366b,0xb8);
	sc231hai_write_register(ViPipe, 0x366c,0xf8);
	sc231hai_write_register(ViPipe, 0x3670,0xb2);
	sc231hai_write_register(ViPipe, 0x3671,0xa2);
	sc231hai_write_register(ViPipe, 0x3672,0x88);
	sc231hai_write_register(ViPipe, 0x3680,0x33);
	sc231hai_write_register(ViPipe, 0x3681,0x33);
	sc231hai_write_register(ViPipe, 0x3682,0x43);
	sc231hai_write_register(ViPipe, 0x36c0,0x80);
	sc231hai_write_register(ViPipe, 0x36c1,0x88);
	sc231hai_write_register(ViPipe, 0x36c8,0x88);
	sc231hai_write_register(ViPipe, 0x36c9,0xb8);
	sc231hai_write_register(ViPipe, 0x36ea,0x0b);
	sc231hai_write_register(ViPipe, 0x36eb,0x0c);
	sc231hai_write_register(ViPipe, 0x36ec,0x6c);
	sc231hai_write_register(ViPipe, 0x36ed,0x24);
	sc231hai_write_register(ViPipe, 0x3718,0x04);
	sc231hai_write_register(ViPipe, 0x3722,0x8b);
	sc231hai_write_register(ViPipe, 0x3724,0xd1);
	sc231hai_write_register(ViPipe, 0x3741,0x08);
	sc231hai_write_register(ViPipe, 0x3770,0x17);
	sc231hai_write_register(ViPipe, 0x3771,0x9b);
	sc231hai_write_register(ViPipe, 0x3772,0x9b);
	sc231hai_write_register(ViPipe, 0x37c0,0x88);
	sc231hai_write_register(ViPipe, 0x37c1,0xb8);
	sc231hai_write_register(ViPipe, 0x37fa,0x0b);
	sc231hai_write_register(ViPipe, 0x37fc,0x10);
	sc231hai_write_register(ViPipe, 0x37fd,0x24);
	sc231hai_write_register(ViPipe, 0x3902,0xc0);
	sc231hai_write_register(ViPipe, 0x3903,0x40);
	sc231hai_write_register(ViPipe, 0x3909,0x00);
	sc231hai_write_register(ViPipe, 0x391f,0x41);
	sc231hai_write_register(ViPipe, 0x3926,0xe0);
	sc231hai_write_register(ViPipe, 0x3933,0x80);
	sc231hai_write_register(ViPipe, 0x3934,0x02);
	sc231hai_write_register(ViPipe, 0x3937,0x6f);
	sc231hai_write_register(ViPipe, 0x3e00,0x00);
	sc231hai_write_register(ViPipe, 0x3e01,0x44);
	sc231hai_write_register(ViPipe, 0x3e02,0x10);
	sc231hai_write_register(ViPipe, 0x3e08,0x00);
	sc231hai_write_register(ViPipe, 0x4509,0x20);
	sc231hai_write_register(ViPipe, 0x450d,0x07);
	sc231hai_write_register(ViPipe, 0x4837,0x69);
	sc231hai_write_register(ViPipe, 0x5000,0x46);
	sc231hai_write_register(ViPipe, 0x5780,0x76);
	sc231hai_write_register(ViPipe, 0x5784,0x10);
	sc231hai_write_register(ViPipe, 0x5787,0x0a);
	sc231hai_write_register(ViPipe, 0x5788,0x0a);
	sc231hai_write_register(ViPipe, 0x5789,0x08);
	sc231hai_write_register(ViPipe, 0x578a,0x0a);
	sc231hai_write_register(ViPipe, 0x578b,0x0a);
	sc231hai_write_register(ViPipe, 0x578c,0x08);
	sc231hai_write_register(ViPipe, 0x578d,0x40);
	sc231hai_write_register(ViPipe, 0x5792,0x04);
	sc231hai_write_register(ViPipe, 0x5795,0x04);
	sc231hai_write_register(ViPipe, 0x57ac,0x00);
	sc231hai_write_register(ViPipe, 0x57ad,0x00);
	sc231hai_write_register(ViPipe, 0x5900,0xf1);
	sc231hai_write_register(ViPipe, 0x5901,0x04);
	sc231hai_write_register(ViPipe, 0x36e9,0x20);
	sc231hai_write_register(ViPipe, 0x37f9,0x20);
	sc231hai_write_register(ViPipe, 0x0100,0x01);

	sc231hai_default_reg_init(ViPipe);

	sc231hai_write_register(ViPipe, 0x0100, 0x01);

	printf("ViPipe:%d,===SC231HAI 480P 60fps 10bit LINE Init OK!===\n", ViPipe);
}

static void sc231hai_wdr_480p60_2to1_init(VI_PIPE ViPipe)
{
	sc231hai_write_register(ViPipe, 0x0103, 0x01);
	sc231hai_write_register(ViPipe, 0x36e9, 0x80);
	sc231hai_write_register(ViPipe, 0x37f9, 0x80);
	sc231hai_write_register(ViPipe, 0x301f, 0x51);
	sc231hai_write_register(ViPipe, 0x3058, 0x21);
	sc231hai_write_register(ViPipe, 0x3059, 0x53);
	sc231hai_write_register(ViPipe, 0x305a, 0x40);
	sc231hai_write_register(ViPipe, 0x3200, 0x01);
	sc231hai_write_register(ViPipe, 0x3201, 0x40);
	sc231hai_write_register(ViPipe, 0x3202, 0x00);
	sc231hai_write_register(ViPipe, 0x3203, 0x3c);
	sc231hai_write_register(ViPipe, 0x3204, 0x06);
	sc231hai_write_register(ViPipe, 0x3205, 0x47);
	sc231hai_write_register(ViPipe, 0x3206, 0x04);
	sc231hai_write_register(ViPipe, 0x3207, 0x03);
	sc231hai_write_register(ViPipe, 0x3208, 0x02);
	sc231hai_write_register(ViPipe, 0x3209, 0x80);
	sc231hai_write_register(ViPipe, 0x320a, 0x01);
	sc231hai_write_register(ViPipe, 0x320b, 0xe0);
	sc231hai_write_register(ViPipe, 0x320c, 0x09);
	sc231hai_write_register(ViPipe, 0x320d, 0xc4);
	sc231hai_write_register(ViPipe, 0x320e, 0x03);
	sc231hai_write_register(ViPipe, 0x320f, 0xf0);
	sc231hai_write_register(ViPipe, 0x3210, 0x00);
	sc231hai_write_register(ViPipe, 0x3211, 0x02);
	sc231hai_write_register(ViPipe, 0x3212, 0x00);
	sc231hai_write_register(ViPipe, 0x3213, 0x02);
	sc231hai_write_register(ViPipe, 0x3215, 0x31);
	sc231hai_write_register(ViPipe, 0x3220, 0x01);
	sc231hai_write_register(ViPipe, 0x3250, 0xff);
	sc231hai_write_register(ViPipe, 0x3281, 0x01);
	sc231hai_write_register(ViPipe, 0x3301, 0x0a);
	sc231hai_write_register(ViPipe, 0x3302, 0x20);
	sc231hai_write_register(ViPipe, 0x3304, 0x90);
	sc231hai_write_register(ViPipe, 0x3305, 0x00);
	sc231hai_write_register(ViPipe, 0x3306, 0x78);
	sc231hai_write_register(ViPipe, 0x3309, 0xd0);
	sc231hai_write_register(ViPipe, 0x330b, 0xe8);
	sc231hai_write_register(ViPipe, 0x330d, 0x08);
	sc231hai_write_register(ViPipe, 0x331c, 0x04);
	sc231hai_write_register(ViPipe, 0x331e, 0x81);
	sc231hai_write_register(ViPipe, 0x331f, 0xc1);
	sc231hai_write_register(ViPipe, 0x3323, 0x06);
	sc231hai_write_register(ViPipe, 0x3333, 0x10);
	sc231hai_write_register(ViPipe, 0x3334, 0x40);
	sc231hai_write_register(ViPipe, 0x3364, 0x5e);
	sc231hai_write_register(ViPipe, 0x336c, 0x8c);
	sc231hai_write_register(ViPipe, 0x337f, 0x13);
	sc231hai_write_register(ViPipe, 0x338f, 0x80);
	sc231hai_write_register(ViPipe, 0x3390, 0x08);
	sc231hai_write_register(ViPipe, 0x3391, 0x18);
	sc231hai_write_register(ViPipe, 0x3392, 0xb8);
	sc231hai_write_register(ViPipe, 0x3393, 0x10);
	sc231hai_write_register(ViPipe, 0x3394, 0x14);
	sc231hai_write_register(ViPipe, 0x3395, 0x10);
	sc231hai_write_register(ViPipe, 0x3396, 0x88);
	sc231hai_write_register(ViPipe, 0x3397, 0x98);
	sc231hai_write_register(ViPipe, 0x3398, 0xf8);
	sc231hai_write_register(ViPipe, 0x3399, 0x0a);
	sc231hai_write_register(ViPipe, 0x339a, 0x0e);
	sc231hai_write_register(ViPipe, 0x339b, 0x10);
	sc231hai_write_register(ViPipe, 0x339c, 0x14);
	sc231hai_write_register(ViPipe, 0x33ae, 0x80);
	sc231hai_write_register(ViPipe, 0x33af, 0xc0);
	sc231hai_write_register(ViPipe, 0x33b2, 0x50);
	sc231hai_write_register(ViPipe, 0x33b3, 0x08);
	sc231hai_write_register(ViPipe, 0x33f8, 0x00);
	sc231hai_write_register(ViPipe, 0x33f9, 0x78);
	sc231hai_write_register(ViPipe, 0x33fa, 0x00);
	sc231hai_write_register(ViPipe, 0x33fb, 0x78);
	sc231hai_write_register(ViPipe, 0x33fc, 0x48);
	sc231hai_write_register(ViPipe, 0x33fd, 0x78);
	sc231hai_write_register(ViPipe, 0x349f, 0x03);
	sc231hai_write_register(ViPipe, 0x34a6, 0x40);
	sc231hai_write_register(ViPipe, 0x34a7, 0x58);
	sc231hai_write_register(ViPipe, 0x34a8, 0x08);
	sc231hai_write_register(ViPipe, 0x34a9, 0x0c);
	sc231hai_write_register(ViPipe, 0x34f8, 0x78);
	sc231hai_write_register(ViPipe, 0x34f9, 0x18);
	sc231hai_write_register(ViPipe, 0x3619, 0x20);
	sc231hai_write_register(ViPipe, 0x361a, 0x90);
	sc231hai_write_register(ViPipe, 0x3633, 0x44);
	sc231hai_write_register(ViPipe, 0x3637, 0x5c);
	sc231hai_write_register(ViPipe, 0x363c, 0xc0);
	sc231hai_write_register(ViPipe, 0x363d, 0x02);
	sc231hai_write_register(ViPipe, 0x3660, 0x80);
	sc231hai_write_register(ViPipe, 0x3661, 0x81);
	sc231hai_write_register(ViPipe, 0x3662, 0x8f);
	sc231hai_write_register(ViPipe, 0x3663, 0x81);
	sc231hai_write_register(ViPipe, 0x3664, 0x81);
	sc231hai_write_register(ViPipe, 0x3665, 0x82);
	sc231hai_write_register(ViPipe, 0x3666, 0x8f);
	sc231hai_write_register(ViPipe, 0x3667, 0x08);
	sc231hai_write_register(ViPipe, 0x3668, 0x80);
	sc231hai_write_register(ViPipe, 0x3669, 0x88);
	sc231hai_write_register(ViPipe, 0x366a, 0x98);
	sc231hai_write_register(ViPipe, 0x366b, 0xb8);
	sc231hai_write_register(ViPipe, 0x366c, 0xf8);
	sc231hai_write_register(ViPipe, 0x3670, 0xc2);
	sc231hai_write_register(ViPipe, 0x3671, 0xc2);
	sc231hai_write_register(ViPipe, 0x3672, 0x98);
	sc231hai_write_register(ViPipe, 0x3680, 0x43);
	sc231hai_write_register(ViPipe, 0x3681, 0x54);
	sc231hai_write_register(ViPipe, 0x3682, 0x54);
	sc231hai_write_register(ViPipe, 0x36c0, 0x80);
	sc231hai_write_register(ViPipe, 0x36c1, 0x88);
	sc231hai_write_register(ViPipe, 0x36c8, 0x88);
	sc231hai_write_register(ViPipe, 0x36c9, 0xb8);
	sc231hai_write_register(ViPipe, 0x36ea, 0x0e);
	sc231hai_write_register(ViPipe, 0x36eb, 0x04);
	sc231hai_write_register(ViPipe, 0x36ec, 0x5c);
	sc231hai_write_register(ViPipe, 0x36ed, 0x14);
	sc231hai_write_register(ViPipe, 0x3718, 0x04);
	sc231hai_write_register(ViPipe, 0x3722, 0x8b);
	sc231hai_write_register(ViPipe, 0x3724, 0xd1);
	sc231hai_write_register(ViPipe, 0x3741, 0x08);
	sc231hai_write_register(ViPipe, 0x3770, 0x17);
	sc231hai_write_register(ViPipe, 0x3771, 0x9b);
	sc231hai_write_register(ViPipe, 0x3772, 0x9b);
	sc231hai_write_register(ViPipe, 0x37c0, 0x88);
	sc231hai_write_register(ViPipe, 0x37c1, 0xb8);
	sc231hai_write_register(ViPipe, 0x37fa, 0x0e);
	sc231hai_write_register(ViPipe, 0x37fc, 0x00);
	sc231hai_write_register(ViPipe, 0x37fd, 0x14);
	sc231hai_write_register(ViPipe, 0x3902, 0xc0);
	sc231hai_write_register(ViPipe, 0x3903, 0x40);
	sc231hai_write_register(ViPipe, 0x3909, 0x00);
	sc231hai_write_register(ViPipe, 0x391f, 0x41);
	sc231hai_write_register(ViPipe, 0x3926, 0xe0);
	sc231hai_write_register(ViPipe, 0x3933, 0x80);
	sc231hai_write_register(ViPipe, 0x3934, 0x02);
	sc231hai_write_register(ViPipe, 0x3937, 0x6f);
	sc231hai_write_register(ViPipe, 0x3e00, 0x00);
	sc231hai_write_register(ViPipe, 0x3e01, 0x71);
	sc231hai_write_register(ViPipe, 0x3e02, 0x00);
	sc231hai_write_register(ViPipe, 0x3e04, 0x07);
	sc231hai_write_register(ViPipe, 0x3e05, 0x10);
	sc231hai_write_register(ViPipe, 0x3e08, 0x00);
	sc231hai_write_register(ViPipe, 0x3e23, 0x00);
	sc231hai_write_register(ViPipe, 0x3e24, 0x42);
	sc231hai_write_register(ViPipe, 0x4509, 0x20);
	sc231hai_write_register(ViPipe, 0x450d, 0x07);
	sc231hai_write_register(ViPipe, 0x4816, 0x71);
	sc231hai_write_register(ViPipe, 0x4837, 0x34);
	sc231hai_write_register(ViPipe, 0x5000, 0x46);
	sc231hai_write_register(ViPipe, 0x5780, 0x76);
	sc231hai_write_register(ViPipe, 0x5784, 0x10);
	sc231hai_write_register(ViPipe, 0x5787, 0x0a);
	sc231hai_write_register(ViPipe, 0x5788, 0x0a);
	sc231hai_write_register(ViPipe, 0x5789, 0x08);
	sc231hai_write_register(ViPipe, 0x578a, 0x0a);
	sc231hai_write_register(ViPipe, 0x578b, 0x0a);
	sc231hai_write_register(ViPipe, 0x578c, 0x08);
	sc231hai_write_register(ViPipe, 0x578d, 0x40);
	sc231hai_write_register(ViPipe, 0x5792, 0x04);
	sc231hai_write_register(ViPipe, 0x5795, 0x04);
	sc231hai_write_register(ViPipe, 0x57ac, 0x00);
	sc231hai_write_register(ViPipe, 0x57ad, 0x00);
	sc231hai_write_register(ViPipe, 0x5900, 0x01);
	sc231hai_write_register(ViPipe, 0x5901, 0x04);
	sc231hai_write_register(ViPipe, 0x36e9, 0x24);
	sc231hai_write_register(ViPipe, 0x37f9, 0x24);


	sc231hai_default_reg_init(ViPipe);

	sc231hai_write_register(ViPipe, 0x0100, 0x01);

	delay_ms(50);

	printf("===SC231HAI sensor 480p60fps 12bit 2to1 WDR init success!=====\n");
}

static void sc231hai_linear_1080p60_init(VI_PIPE ViPipe)
{
	sc231hai_write_register(ViPipe, 0x0103, 0x01);
	sc231hai_write_register(ViPipe, 0x36e9, 0x80);
	sc231hai_write_register(ViPipe, 0x37f9, 0x80);
	sc231hai_write_register(ViPipe, 0x301f, 0x02);
	sc231hai_write_register(ViPipe, 0x3058, 0x21);
	sc231hai_write_register(ViPipe, 0x3059, 0x53);
	sc231hai_write_register(ViPipe, 0x305a, 0x40);
	sc231hai_write_register(ViPipe, 0x3250, 0x00);
	sc231hai_write_register(ViPipe, 0x3301, 0x0a);
	sc231hai_write_register(ViPipe, 0x3302, 0x20);
	sc231hai_write_register(ViPipe, 0x3304, 0x90);
	sc231hai_write_register(ViPipe, 0x3305, 0x00);
	sc231hai_write_register(ViPipe, 0x3306, 0x78);
	sc231hai_write_register(ViPipe, 0x3309, 0xd0);
	sc231hai_write_register(ViPipe, 0x330b, 0xe8);
	sc231hai_write_register(ViPipe, 0x330d, 0x08);
	sc231hai_write_register(ViPipe, 0x331c, 0x04);
	sc231hai_write_register(ViPipe, 0x331e, 0x81);
	sc231hai_write_register(ViPipe, 0x331f, 0xc1);
	sc231hai_write_register(ViPipe, 0x3323, 0x06);
	sc231hai_write_register(ViPipe, 0x3333, 0x10);
	sc231hai_write_register(ViPipe, 0x3334, 0x40);
	sc231hai_write_register(ViPipe, 0x3364, 0x5e);
	sc231hai_write_register(ViPipe, 0x336c, 0x8c);
	sc231hai_write_register(ViPipe, 0x337f, 0x13);
	sc231hai_write_register(ViPipe, 0x338f, 0x80);
	sc231hai_write_register(ViPipe, 0x3390, 0x08);
	sc231hai_write_register(ViPipe, 0x3391, 0x18);
	sc231hai_write_register(ViPipe, 0x3392, 0xb8);
	sc231hai_write_register(ViPipe, 0x3393, 0x0e);
	sc231hai_write_register(ViPipe, 0x3394, 0x14);
	sc231hai_write_register(ViPipe, 0x3395, 0x10);
	sc231hai_write_register(ViPipe, 0x3396, 0x88);
	sc231hai_write_register(ViPipe, 0x3397, 0x98);
	sc231hai_write_register(ViPipe, 0x3398, 0xf8);
	sc231hai_write_register(ViPipe, 0x3399, 0x0a);
	sc231hai_write_register(ViPipe, 0x339a, 0x0e);
	sc231hai_write_register(ViPipe, 0x339b, 0x10);
	sc231hai_write_register(ViPipe, 0x339c, 0x14);
	sc231hai_write_register(ViPipe, 0x33ae, 0x80);
	sc231hai_write_register(ViPipe, 0x33af, 0xc0);
	sc231hai_write_register(ViPipe, 0x33b2, 0x50);
	sc231hai_write_register(ViPipe, 0x33b3, 0x08);
	sc231hai_write_register(ViPipe, 0x33f8, 0x00);
	sc231hai_write_register(ViPipe, 0x33f9, 0x78);
	sc231hai_write_register(ViPipe, 0x33fa, 0x00);
	sc231hai_write_register(ViPipe, 0x33fb, 0x78);
	sc231hai_write_register(ViPipe, 0x33fc, 0x48);
	sc231hai_write_register(ViPipe, 0x33fd, 0x78);
	sc231hai_write_register(ViPipe, 0x349f, 0x03);
	sc231hai_write_register(ViPipe, 0x34a6, 0x40);
	sc231hai_write_register(ViPipe, 0x34a7, 0x58);
	sc231hai_write_register(ViPipe, 0x34a8, 0x08);
	sc231hai_write_register(ViPipe, 0x34a9, 0x0c);
	sc231hai_write_register(ViPipe, 0x34f8, 0x78);
	sc231hai_write_register(ViPipe, 0x34f9, 0x18);
	sc231hai_write_register(ViPipe, 0x3619, 0x20);
	sc231hai_write_register(ViPipe, 0x361a, 0x90);
	sc231hai_write_register(ViPipe, 0x3633, 0x44);
	sc231hai_write_register(ViPipe, 0x3637, 0x5c);
	sc231hai_write_register(ViPipe, 0x363c, 0xc0);
	sc231hai_write_register(ViPipe, 0x363d, 0x02);
	sc231hai_write_register(ViPipe, 0x3660, 0x80);
	sc231hai_write_register(ViPipe, 0x3661, 0x81);
	sc231hai_write_register(ViPipe, 0x3662, 0x8f);
	sc231hai_write_register(ViPipe, 0x3663, 0x81);
	sc231hai_write_register(ViPipe, 0x3664, 0x81);
	sc231hai_write_register(ViPipe, 0x3665, 0x82);
	sc231hai_write_register(ViPipe, 0x3666, 0x8f);
	sc231hai_write_register(ViPipe, 0x3667, 0x08);
	sc231hai_write_register(ViPipe, 0x3668, 0x80);
	sc231hai_write_register(ViPipe, 0x3669, 0x88);
	sc231hai_write_register(ViPipe, 0x366a, 0x98);
	sc231hai_write_register(ViPipe, 0x366b, 0xb8);
	sc231hai_write_register(ViPipe, 0x366c, 0xf8);
	sc231hai_write_register(ViPipe, 0x3670, 0xc2);
	sc231hai_write_register(ViPipe, 0x3671, 0xc2);
	sc231hai_write_register(ViPipe, 0x3672, 0x98);
	sc231hai_write_register(ViPipe, 0x3680, 0x43);
	sc231hai_write_register(ViPipe, 0x3681, 0x54);
	sc231hai_write_register(ViPipe, 0x3682, 0x54);
	sc231hai_write_register(ViPipe, 0x36c0, 0x80);
	sc231hai_write_register(ViPipe, 0x36c1, 0x88);
	sc231hai_write_register(ViPipe, 0x36c8, 0x88);
	sc231hai_write_register(ViPipe, 0x36c9, 0xb8);
	sc231hai_write_register(ViPipe, 0x3718, 0x04);
	sc231hai_write_register(ViPipe, 0x3722, 0x8b);
	sc231hai_write_register(ViPipe, 0x3724, 0xd1);
	sc231hai_write_register(ViPipe, 0x3741, 0x08);
	sc231hai_write_register(ViPipe, 0x3770, 0x17);
	sc231hai_write_register(ViPipe, 0x3771, 0x9b);
	sc231hai_write_register(ViPipe, 0x3772, 0x9b);
	sc231hai_write_register(ViPipe, 0x37c0, 0x88);
	sc231hai_write_register(ViPipe, 0x37c1, 0xb8);
	sc231hai_write_register(ViPipe, 0x3902, 0xc0);
	sc231hai_write_register(ViPipe, 0x3903, 0x40);
	sc231hai_write_register(ViPipe, 0x3909, 0x00);
	sc231hai_write_register(ViPipe, 0x391f, 0x41);
	sc231hai_write_register(ViPipe, 0x3926, 0xe0);
	sc231hai_write_register(ViPipe, 0x3933, 0x80);
	sc231hai_write_register(ViPipe, 0x3934, 0x02);
	sc231hai_write_register(ViPipe, 0x3937, 0x6f);
	sc231hai_write_register(ViPipe, 0x3e00, 0x00);
	sc231hai_write_register(ViPipe, 0x3e01, 0x8b);
	sc231hai_write_register(ViPipe, 0x3e02, 0xf0);
	sc231hai_write_register(ViPipe, 0x3e08, 0x00);
	sc231hai_write_register(ViPipe, 0x4509, 0x20);
	sc231hai_write_register(ViPipe, 0x450d, 0x07);
	sc231hai_write_register(ViPipe, 0x5780, 0x76);
	sc231hai_write_register(ViPipe, 0x5784, 0x10);
	sc231hai_write_register(ViPipe, 0x5787, 0x0a);
	sc231hai_write_register(ViPipe, 0x5788, 0x0a);
	sc231hai_write_register(ViPipe, 0x5789, 0x08);
	sc231hai_write_register(ViPipe, 0x578a, 0x0a);
	sc231hai_write_register(ViPipe, 0x578b, 0x0a);
	sc231hai_write_register(ViPipe, 0x578c, 0x08);
	sc231hai_write_register(ViPipe, 0x578d, 0x40);
	sc231hai_write_register(ViPipe, 0x5792, 0x04);
	sc231hai_write_register(ViPipe, 0x5795, 0x04);
	sc231hai_write_register(ViPipe, 0x57ac, 0x00);
	sc231hai_write_register(ViPipe, 0x57ad, 0x00);
	sc231hai_write_register(ViPipe, 0x36e9, 0x24);
	sc231hai_write_register(ViPipe, 0x37f9, 0x24);

	sc231hai_default_reg_init(ViPipe);

	sc231hai_write_register(ViPipe, 0x0100, 0x01);

	printf("ViPipe:%d,===SC231HAI 1080P 60fps 10bit LINE Init OK!===\n", ViPipe);
}