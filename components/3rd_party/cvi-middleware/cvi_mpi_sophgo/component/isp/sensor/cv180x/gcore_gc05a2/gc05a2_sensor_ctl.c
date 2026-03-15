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
#include "gc05a2_cmos_ex.h"

#define GC05A2_CHIP_ID_ADDR_H	0x03f0
#define GC05A2_CHIP_ID_ADDR_L	0x03f1
#define GC05A2_CHIP_ID			0x05A2

static void gc05a2_linear_720p60_init(VI_PIPE ViPipe);
static void gc05a2_linear_1296x972p30_init(VI_PIPE ViPipe);
static void gc05a2_linear_2560x1920p30_init(VI_PIPE ViPipe);

CVI_U8 gc05a2_i2c_addr = 0x37;
const CVI_U32 gc05a2_addr_byte = 2;
const CVI_U32 gc05a2_data_byte = 1;
static int g_fd[VI_MAX_PIPE_NUM] = {[0 ... (VI_MAX_PIPE_NUM - 1)] = -1};

int gc05a2_i2c_init(VI_PIPE ViPipe)
{
	char acDevFile[16] = {0};
	CVI_U8 u8DevNum;

	if (g_fd[ViPipe] >= 0)
		return CVI_SUCCESS;
	int ret;

	u8DevNum = g_aunGc05a2_BusInfo[ViPipe].s8I2cDev;
	snprintf(acDevFile, sizeof(acDevFile),  "/dev/i2c-%u", u8DevNum);

	g_fd[ViPipe] = open(acDevFile, O_RDWR, 0600);

	if (g_fd[ViPipe] < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "Open /dev/i2c-%u error!\n", u8DevNum);
		return CVI_FAILURE;
	}

	ret = ioctl(g_fd[ViPipe], I2C_SLAVE_FORCE, gc05a2_i2c_addr);
	if (ret < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_SLAVE_FORCE error!\n");
		close(g_fd[ViPipe]);
		g_fd[ViPipe] = -1;
		return ret;
	}

	return CVI_SUCCESS;
}

int gc05a2_i2c_exit(VI_PIPE ViPipe)
{
	if (g_fd[ViPipe] >= 0) {
		close(g_fd[ViPipe]);
		g_fd[ViPipe] = -1;
		return CVI_SUCCESS;
	}
	return CVI_FAILURE;
}

int gc05a2_read_register(VI_PIPE ViPipe, int addr)
{
	int ret, data;
	CVI_U8 buf[8];
	CVI_U8 idx = 0;

	if (g_fd[ViPipe] < 0)
		return CVI_FAILURE;

	if (gc05a2_addr_byte == 2)
		buf[idx++] = (addr >> 8) & 0xff;

	// add address byte 0
	buf[idx++] = addr & 0xff;

	ret = write(g_fd[ViPipe], buf, gc05a2_addr_byte);
	if (ret < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_WRITE error!\n");
		return ret;
	}

	buf[0] = 0;
	buf[1] = 0;
	ret = read(g_fd[ViPipe], buf, gc05a2_data_byte);
	if (ret < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_READ error!\n");
		return ret;
	}

	// pack read back data
	data = 0;
	if (gc05a2_data_byte == 2) {
		data = buf[0] << 8;
		data += buf[1];
	} else {
		data = buf[0];
	}

	syslog(LOG_DEBUG, "i2c r 0x%x = 0x%x\n", addr, data);
	return data;
}

int gc05a2_write_register(VI_PIPE ViPipe, int addr, int data)
{
	CVI_U8 idx = 0;
	int ret;
	CVI_U8 buf[8];

	if (g_fd[ViPipe] < 0)
		return CVI_SUCCESS;

	if (gc05a2_addr_byte == 2) {
		buf[idx] = (addr >> 8) & 0xff;
		idx++;
		buf[idx] = addr & 0xff;
		idx++;
	}
	if (gc05a2_data_byte == 1) {
		buf[idx] = data & 0xff;
		idx++;
	}

	ret = write(g_fd[ViPipe], buf, gc05a2_addr_byte + gc05a2_data_byte);
	if (ret < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "I2C_WRITE error!\n");
		return CVI_FAILURE;
	}
	ret = read(g_fd[ViPipe], buf, gc05a2_addr_byte + gc05a2_data_byte);
	syslog(LOG_DEBUG, "i2c w 0x%x 0x%x\n", addr, data);
	return CVI_SUCCESS;
}

static void delay_ms(int ms)
{
	usleep(ms * 1000);
}

void gc05a2_standby(VI_PIPE ViPipe)
{
	gc05a2_write_register(ViPipe, 0x0100, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0xc7);
	gc05a2_write_register(ViPipe, 0x0317, 0x01);

	printf("gc05a2_standby\n");
}

void gc05a2_restart(VI_PIPE ViPipe)
{
	gc05a2_write_register(ViPipe, 0x0317, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0xc6);
	gc05a2_write_register(ViPipe, 0x0100, 0x09);

	printf("gc05a2_restart\n");
}

void gc05a2_default_reg_init(VI_PIPE ViPipe)
{
	CVI_U32 i;

	for (i = 0; i < g_pastGc05a2[ViPipe]->astSyncInfo[0].snsCfg.u32RegNum; i++) {
		gc05a2_write_register(ViPipe,
				g_pastGc05a2[ViPipe]->astSyncInfo[0].snsCfg.astI2cData[i].u32RegAddr,
				g_pastGc05a2[ViPipe]->astSyncInfo[0].snsCfg.astI2cData[i].u32Data);
	}
}

int gc05a2_probe(VI_PIPE ViPipe)
{
	int nVal;
	int nVal2;

	usleep(50);
	if (gc05a2_i2c_init(ViPipe) != CVI_SUCCESS)
		return CVI_FAILURE;

	nVal  = gc05a2_read_register(ViPipe, GC05A2_CHIP_ID_ADDR_H);
	nVal2 = gc05a2_read_register(ViPipe, GC05A2_CHIP_ID_ADDR_L);
	if (nVal < 0 || nVal2 < 0) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "read sensor id error.\n");
		return nVal;
	}

	if ((((nVal & 0xFF) << 8) | (nVal2 & 0xFF)) != GC05A2_CHIP_ID) {
		CVI_TRACE_SNS(CVI_DBG_ERR, "Sensor ID Mismatch! Use the wrong sensor??\n");
		return CVI_FAILURE;
	}

	return CVI_SUCCESS;
}

void gc05a2_init(VI_PIPE ViPipe)
{
	WDR_MODE_E        enWDRMode;
	CVI_U8            u8ImgMode;

	enWDRMode   = g_pastGc05a2[ViPipe]->enWDRMode;
	u8ImgMode   = g_pastGc05a2[ViPipe]->u8ImgMode;

	gc05a2_i2c_init(ViPipe);

	if (enWDRMode == WDR_MODE_2To1_LINE) {
	} else {
		if (u8ImgMode == GC05A2_MODE_720P60)
			gc05a2_linear_720p60_init(ViPipe);
		else if (u8ImgMode == GC05A2_MODE_1280x960P30)
			gc05a2_linear_1296x972p30_init(ViPipe);
		else if (u8ImgMode == GC05A2_MODE_2560x1920P30)
			gc05a2_linear_2560x1920p30_init(ViPipe);
		else {
		}
	}

	g_pastGc05a2[ViPipe]->bInit = CVI_TRUE;
}

void gc05a2_exit(VI_PIPE ViPipe)
{
	gc05a2_i2c_exit(ViPipe);
}

// 1296 * 976
static void gc05a2_linear_1296x972p30_init(VI_PIPE ViPipe)
{
	gc05a2_write_register(ViPipe, 0x0100, 0x00);
	gc05a2_write_register(ViPipe, 0x0315, 0xd4);
	gc05a2_write_register(ViPipe, 0x0d06, 0x01);
	gc05a2_write_register(ViPipe, 0x0a70, 0x80);
	gc05a2_write_register(ViPipe, 0x031a, 0x00);
	gc05a2_write_register(ViPipe, 0x0314, 0x00);
	gc05a2_write_register(ViPipe, 0x0130, 0x08);
	gc05a2_write_register(ViPipe, 0x0132, 0x01);
	gc05a2_write_register(ViPipe, 0x0135, 0x05);
	gc05a2_write_register(ViPipe, 0x0136, 0x38);
	gc05a2_write_register(ViPipe, 0x0137, 0x03);
	gc05a2_write_register(ViPipe, 0x0134, 0x5b);
	gc05a2_write_register(ViPipe, 0x031c, 0xe0);
	gc05a2_write_register(ViPipe, 0x0d82, 0x14);
	gc05a2_write_register(ViPipe, 0x0dd1, 0x56);
	gc05a2_write_register(ViPipe, 0x0af4, 0x01);
	gc05a2_write_register(ViPipe, 0x0002, 0x10);
	gc05a2_write_register(ViPipe, 0x00c3, 0x34);
	gc05a2_write_register(ViPipe, 0x0084, 0x21);
	gc05a2_write_register(ViPipe, 0x0d05, 0xcc);
	gc05a2_write_register(ViPipe, 0x0218, 0x80);
	gc05a2_write_register(ViPipe, 0x005e, 0x49);
	gc05a2_write_register(ViPipe, 0x0d06, 0x81);
	gc05a2_write_register(ViPipe, 0x0007, 0x16);
	gc05a2_write_register(ViPipe, 0x0101, 0x00);
	gc05a2_write_register(ViPipe, 0x0342, 0x07); // line length 0x710 1808
	gc05a2_write_register(ViPipe, 0x0343, 0x10);
	gc05a2_write_register(ViPipe, 0x0220, 0x0f);
	gc05a2_write_register(ViPipe, 0x0221, 0xe0);
	gc05a2_write_register(ViPipe, 0x0202, 0x03);
	gc05a2_write_register(ViPipe, 0x0203, 0x32);
	gc05a2_write_register(ViPipe, 0x0340, 0x08); // frame length 0x810 2064
	gc05a2_write_register(ViPipe, 0x0341, 0x10);
	gc05a2_write_register(ViPipe, 0x0219, 0x00);
	gc05a2_write_register(ViPipe, 0x0346, 0x00); // row start 0x04 4
	gc05a2_write_register(ViPipe, 0x0347, 0x04);
	gc05a2_write_register(ViPipe, 0x0d14, 0x00); // col start 0x05 5
	gc05a2_write_register(ViPipe, 0x0d13, 0x05);
	gc05a2_write_register(ViPipe, 0x0d16, 0x05);
	gc05a2_write_register(ViPipe, 0x0d15, 0x1d);
	gc05a2_write_register(ViPipe, 0x00c0, 0x0a); // win_width 0xa30 2608
	gc05a2_write_register(ViPipe, 0x00c1, 0x30);
	gc05a2_write_register(ViPipe, 0x034a, 0x07); // win_height 0x7a8 1960
	gc05a2_write_register(ViPipe, 0x034b, 0xa8);
	gc05a2_write_register(ViPipe, 0x0e0a, 0x00);
	gc05a2_write_register(ViPipe, 0x0e0b, 0x00);
	gc05a2_write_register(ViPipe, 0x0e0e, 0x03);
	gc05a2_write_register(ViPipe, 0x0e0f, 0x00);
	gc05a2_write_register(ViPipe, 0x0e06, 0x0a);
	gc05a2_write_register(ViPipe, 0x0e23, 0x15);
	gc05a2_write_register(ViPipe, 0x0e24, 0x15);
	gc05a2_write_register(ViPipe, 0x0e2a, 0x10);
	gc05a2_write_register(ViPipe, 0x0e2b, 0x10);
	gc05a2_write_register(ViPipe, 0x0e17, 0x49);
	gc05a2_write_register(ViPipe, 0x0e1b, 0x1c);
	gc05a2_write_register(ViPipe, 0x0e3a, 0x36);
	gc05a2_write_register(ViPipe, 0x0d11, 0x84);
	gc05a2_write_register(ViPipe, 0x0e52, 0x14);
	gc05a2_write_register(ViPipe, 0x000b, 0x0e);
	gc05a2_write_register(ViPipe, 0x0008, 0x03);
	gc05a2_write_register(ViPipe, 0x0223, 0x16);
	gc05a2_write_register(ViPipe, 0x0d27, 0x39);
	gc05a2_write_register(ViPipe, 0x0d22, 0x00);
	gc05a2_write_register(ViPipe, 0x03f6, 0x0d);
	gc05a2_write_register(ViPipe, 0x0d04, 0x07);
	gc05a2_write_register(ViPipe, 0x03f3, 0x72);
	gc05a2_write_register(ViPipe, 0x03f4, 0xb8);
	gc05a2_write_register(ViPipe, 0x03f5, 0xbc);
	gc05a2_write_register(ViPipe, 0x0d02, 0x73);
	gc05a2_write_register(ViPipe, 0x00c4, 0x00);
	gc05a2_write_register(ViPipe, 0x00c5, 0x01);
	gc05a2_write_register(ViPipe, 0x0af6, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba0, 0x17);
	gc05a2_write_register(ViPipe, 0x0ba1, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba2, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba3, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba4, 0x03);
	gc05a2_write_register(ViPipe, 0x0ba5, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba6, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba7, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba8, 0x40);
	gc05a2_write_register(ViPipe, 0x0ba9, 0x00);
	gc05a2_write_register(ViPipe, 0x0baa, 0x00);
	gc05a2_write_register(ViPipe, 0x0bab, 0x00);
	gc05a2_write_register(ViPipe, 0x0bac, 0x40);
	gc05a2_write_register(ViPipe, 0x0bad, 0x00);
	gc05a2_write_register(ViPipe, 0x0bae, 0x00);
	gc05a2_write_register(ViPipe, 0x0baf, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb0, 0x02);
	gc05a2_write_register(ViPipe, 0x0bb1, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb2, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb3, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb8, 0x02);
	gc05a2_write_register(ViPipe, 0x0bb9, 0x00);
	gc05a2_write_register(ViPipe, 0x0bba, 0x00);
	gc05a2_write_register(ViPipe, 0x0bbb, 0x00);
	gc05a2_write_register(ViPipe, 0x0a70, 0x80);
	gc05a2_write_register(ViPipe, 0x0a71, 0x00);
	gc05a2_write_register(ViPipe, 0x0a72, 0x00);
	gc05a2_write_register(ViPipe, 0x0a66, 0x00);
	gc05a2_write_register(ViPipe, 0x0a67, 0x80);
	gc05a2_write_register(ViPipe, 0x0a4d, 0x4e);
	gc05a2_write_register(ViPipe, 0x0a50, 0x00);
	gc05a2_write_register(ViPipe, 0x0a4f, 0x0c);
	gc05a2_write_register(ViPipe, 0x0a66, 0x00);
	gc05a2_write_register(ViPipe, 0x00ca, 0x00);
	gc05a2_write_register(ViPipe, 0x00cb, 0x00);
	gc05a2_write_register(ViPipe, 0x00cc, 0x00);
	gc05a2_write_register(ViPipe, 0x00cd, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa1, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa2, 0xe0);
	gc05a2_write_register(ViPipe, 0x0aa3, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa4, 0x40);
	gc05a2_write_register(ViPipe, 0x0a90, 0x03);
	gc05a2_write_register(ViPipe, 0x0a91, 0x0e);
	gc05a2_write_register(ViPipe, 0x0a94, 0x80);
	gc05a2_write_register(ViPipe, 0x0af6, 0x20);
	gc05a2_write_register(ViPipe, 0x0b00, 0x91);
	gc05a2_write_register(ViPipe, 0x0b01, 0x17);
	gc05a2_write_register(ViPipe, 0x0b02, 0x01);
	gc05a2_write_register(ViPipe, 0x0b03, 0x00);
	gc05a2_write_register(ViPipe, 0x0b04, 0x01);
	gc05a2_write_register(ViPipe, 0x0b05, 0x17);
	gc05a2_write_register(ViPipe, 0x0b06, 0x01);
	gc05a2_write_register(ViPipe, 0x0b07, 0x00);
	gc05a2_write_register(ViPipe, 0x0ae9, 0x01);
	gc05a2_write_register(ViPipe, 0x0aea, 0x02);
	gc05a2_write_register(ViPipe, 0x0ae8, 0x53);
	gc05a2_write_register(ViPipe, 0x0ae8, 0x43);
	gc05a2_write_register(ViPipe, 0x0af6, 0x30);
	gc05a2_write_register(ViPipe, 0x0b00, 0x08);
	gc05a2_write_register(ViPipe, 0x0b01, 0x0f);
	gc05a2_write_register(ViPipe, 0x0b02, 0x00);
	gc05a2_write_register(ViPipe, 0x0b04, 0x1c);
	gc05a2_write_register(ViPipe, 0x0b05, 0x24);
	gc05a2_write_register(ViPipe, 0x0b06, 0x00);
	gc05a2_write_register(ViPipe, 0x0b08, 0x30);
	gc05a2_write_register(ViPipe, 0x0b09, 0x40);
	gc05a2_write_register(ViPipe, 0x0b0a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b0c, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b0d, 0x2a);
	gc05a2_write_register(ViPipe, 0x0b0e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b10, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b11, 0x2b);
	gc05a2_write_register(ViPipe, 0x0b12, 0x00);
	gc05a2_write_register(ViPipe, 0x0b14, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b15, 0x23);
	gc05a2_write_register(ViPipe, 0x0b16, 0x00);
	gc05a2_write_register(ViPipe, 0x0b18, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b19, 0x24);
	gc05a2_write_register(ViPipe, 0x0b1a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b1c, 0x0c);
	gc05a2_write_register(ViPipe, 0x0b1d, 0x0c);
	gc05a2_write_register(ViPipe, 0x0b1e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b20, 0x03);
	gc05a2_write_register(ViPipe, 0x0b21, 0x03);
	gc05a2_write_register(ViPipe, 0x0b22, 0x00);
	gc05a2_write_register(ViPipe, 0x0b24, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b25, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b26, 0x00);
	gc05a2_write_register(ViPipe, 0x0b28, 0x03);
	gc05a2_write_register(ViPipe, 0x0b29, 0x03);
	gc05a2_write_register(ViPipe, 0x0b2a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b2c, 0x12);
	gc05a2_write_register(ViPipe, 0x0b2d, 0x12);
	gc05a2_write_register(ViPipe, 0x0b2e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b30, 0x08);
	gc05a2_write_register(ViPipe, 0x0b31, 0x08);
	gc05a2_write_register(ViPipe, 0x0b32, 0x00);
	gc05a2_write_register(ViPipe, 0x0b34, 0x14);
	gc05a2_write_register(ViPipe, 0x0b35, 0x14);
	gc05a2_write_register(ViPipe, 0x0b36, 0x00);
	gc05a2_write_register(ViPipe, 0x0b38, 0x10);
	gc05a2_write_register(ViPipe, 0x0b39, 0x10);
	gc05a2_write_register(ViPipe, 0x0b3a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b3c, 0x16);
	gc05a2_write_register(ViPipe, 0x0b3d, 0x16);
	gc05a2_write_register(ViPipe, 0x0b3e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b40, 0x10);
	gc05a2_write_register(ViPipe, 0x0b41, 0x10);
	gc05a2_write_register(ViPipe, 0x0b42, 0x00);
	gc05a2_write_register(ViPipe, 0x0b44, 0x19);
	gc05a2_write_register(ViPipe, 0x0b45, 0x19);
	gc05a2_write_register(ViPipe, 0x0b46, 0x00);
	gc05a2_write_register(ViPipe, 0x0b48, 0x16);
	gc05a2_write_register(ViPipe, 0x0b49, 0x16);
	gc05a2_write_register(ViPipe, 0x0b4a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b4c, 0x19);
	gc05a2_write_register(ViPipe, 0x0b4d, 0x19);
	gc05a2_write_register(ViPipe, 0x0b4e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b50, 0x16);
	gc05a2_write_register(ViPipe, 0x0b51, 0x16);
	gc05a2_write_register(ViPipe, 0x0b52, 0x00);
	gc05a2_write_register(ViPipe, 0x0b80, 0x01);
	gc05a2_write_register(ViPipe, 0x0b81, 0x00);
	gc05a2_write_register(ViPipe, 0x0b82, 0x00);
	gc05a2_write_register(ViPipe, 0x0b84, 0x00);
	gc05a2_write_register(ViPipe, 0x0b85, 0x00);
	gc05a2_write_register(ViPipe, 0x0b86, 0x00);
	gc05a2_write_register(ViPipe, 0x0b88, 0x01);
	gc05a2_write_register(ViPipe, 0x0b89, 0x6a);
	gc05a2_write_register(ViPipe, 0x0b8a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b8c, 0x00);
	gc05a2_write_register(ViPipe, 0x0b8d, 0x01);
	gc05a2_write_register(ViPipe, 0x0b8e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b90, 0x01);
	gc05a2_write_register(ViPipe, 0x0b91, 0xf6);
	gc05a2_write_register(ViPipe, 0x0b92, 0x00);
	gc05a2_write_register(ViPipe, 0x0b94, 0x00);
	gc05a2_write_register(ViPipe, 0x0b95, 0x02);
	gc05a2_write_register(ViPipe, 0x0b96, 0x00);
	gc05a2_write_register(ViPipe, 0x0b98, 0x02);
	gc05a2_write_register(ViPipe, 0x0b99, 0xc4);
	gc05a2_write_register(ViPipe, 0x0b9a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b9c, 0x00);
	gc05a2_write_register(ViPipe, 0x0b9d, 0x03);
	gc05a2_write_register(ViPipe, 0x0b9e, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba0, 0x03);
	gc05a2_write_register(ViPipe, 0x0ba1, 0xd8);
	gc05a2_write_register(ViPipe, 0x0ba2, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba4, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba5, 0x04);
	gc05a2_write_register(ViPipe, 0x0ba6, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba8, 0x05);
	gc05a2_write_register(ViPipe, 0x0ba9, 0x4d);
	gc05a2_write_register(ViPipe, 0x0baa, 0x00);
	gc05a2_write_register(ViPipe, 0x0bac, 0x00);
	gc05a2_write_register(ViPipe, 0x0bad, 0x05);
	gc05a2_write_register(ViPipe, 0x0bae, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb0, 0x07);
	gc05a2_write_register(ViPipe, 0x0bb1, 0x3e);
	gc05a2_write_register(ViPipe, 0x0bb2, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb4, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb5, 0x06);
	gc05a2_write_register(ViPipe, 0x0bb6, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb8, 0x0a);
	gc05a2_write_register(ViPipe, 0x0bb9, 0x1a);
	gc05a2_write_register(ViPipe, 0x0bba, 0x00);
	gc05a2_write_register(ViPipe, 0x0bbc, 0x09);
	gc05a2_write_register(ViPipe, 0x0bbd, 0x36);
	gc05a2_write_register(ViPipe, 0x0bbe, 0x00);
	gc05a2_write_register(ViPipe, 0x0bc0, 0x0e);
	gc05a2_write_register(ViPipe, 0x0bc1, 0x66);
	gc05a2_write_register(ViPipe, 0x0bc2, 0x00);
	gc05a2_write_register(ViPipe, 0x0bc4, 0x10);
	gc05a2_write_register(ViPipe, 0x0bc5, 0x06);
	gc05a2_write_register(ViPipe, 0x0bc6, 0x00);
	gc05a2_write_register(ViPipe, 0x02c1, 0xe0);
	gc05a2_write_register(ViPipe, 0x0207, 0x04);
	gc05a2_write_register(ViPipe, 0x02c2, 0x10);
	gc05a2_write_register(ViPipe, 0x02c3, 0x74);
	gc05a2_write_register(ViPipe, 0x02c5, 0x09);
	gc05a2_write_register(ViPipe, 0x02c1, 0xe0);
	gc05a2_write_register(ViPipe, 0x0207, 0x04);
	gc05a2_write_register(ViPipe, 0x02c2, 0x10);
	gc05a2_write_register(ViPipe, 0x02c5, 0x09);
	gc05a2_write_register(ViPipe, 0x02c1, 0xe0);
	gc05a2_write_register(ViPipe, 0x0207, 0x04);
	gc05a2_write_register(ViPipe, 0x02c2, 0x10);
	gc05a2_write_register(ViPipe, 0x02c5, 0x09);
	gc05a2_write_register(ViPipe, 0x0aa1, 0x15);
	gc05a2_write_register(ViPipe, 0x0aa2, 0x50);
	gc05a2_write_register(ViPipe, 0x0aa3, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa4, 0x09);
	gc05a2_write_register(ViPipe, 0x0a90, 0x25);
	gc05a2_write_register(ViPipe, 0x0a91, 0x0e);
	gc05a2_write_register(ViPipe, 0x0a94, 0x80);
	gc05a2_write_register(ViPipe, 0x0050, 0x00);
	gc05a2_write_register(ViPipe, 0x0089, 0x83);
	gc05a2_write_register(ViPipe, 0x005a, 0x40);
	gc05a2_write_register(ViPipe, 0x00c3, 0x35);
	gc05a2_write_register(ViPipe, 0x00c4, 0x80);
	gc05a2_write_register(ViPipe, 0x0080, 0x10);
	gc05a2_write_register(ViPipe, 0x0040, 0x12);
	gc05a2_write_register(ViPipe, 0x0053, 0x0a);
	gc05a2_write_register(ViPipe, 0x0054, 0x44);
	gc05a2_write_register(ViPipe, 0x0055, 0x32);
	gc05a2_write_register(ViPipe, 0x0058, 0x89);
	gc05a2_write_register(ViPipe, 0x004a, 0x03);
	gc05a2_write_register(ViPipe, 0x0048, 0xf0);
	gc05a2_write_register(ViPipe, 0x0049, 0x0f);
	gc05a2_write_register(ViPipe, 0x0041, 0x20);
	gc05a2_write_register(ViPipe, 0x0043, 0x0a);
	gc05a2_write_register(ViPipe, 0x009d, 0x08);
	gc05a2_write_register(ViPipe, 0x0236, 0x40);
	gc05a2_write_register(ViPipe, 0x0204, 0x04);
	gc05a2_write_register(ViPipe, 0x0205, 0x00);
	gc05a2_write_register(ViPipe, 0x02b3, 0x00);
	gc05a2_write_register(ViPipe, 0x02b4, 0x00);
	gc05a2_write_register(ViPipe, 0x009e, 0x01);
	gc05a2_write_register(ViPipe, 0x009f, 0x94);
	gc05a2_write_register(ViPipe, 0x0350, 0x01); // win mode
	gc05a2_write_register(ViPipe, 0x0353, 0x00); // out_win_x1 0x04 4
	gc05a2_write_register(ViPipe, 0x0354, 0x04);
	gc05a2_write_register(ViPipe, 0x034c, 0x05); // out_win_width  0x510 1296
	gc05a2_write_register(ViPipe, 0x034d, 0x10);
	gc05a2_write_register(ViPipe, 0x021f, 0x14);
	gc05a2_write_register(ViPipe, 0x0aa1, 0x10);
	gc05a2_write_register(ViPipe, 0x0aa2, 0xf8);
	gc05a2_write_register(ViPipe, 0x0aa3, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa4, 0x1f);
	gc05a2_write_register(ViPipe, 0x0a90, 0x11);
	gc05a2_write_register(ViPipe, 0x0a91, 0x0e);
	gc05a2_write_register(ViPipe, 0x0a94, 0x80);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x0a94, 0x00);
	gc05a2_write_register(ViPipe, 0x0a70, 0x00);
	gc05a2_write_register(ViPipe, 0x0a67, 0x00);
	gc05a2_write_register(ViPipe, 0x0af4, 0x29);
	gc05a2_write_register(ViPipe, 0x0d80, 0x07);
	gc05a2_write_register(ViPipe, 0x0dd3, 0x18);
	gc05a2_write_register(ViPipe, 0x0107, 0x05);
	gc05a2_write_register(ViPipe, 0x0117, 0x01);
	gc05a2_write_register(ViPipe, 0x0d81, 0x00);
	gc05a2_write_register(ViPipe, 0x0d84, 0x06);
	gc05a2_write_register(ViPipe, 0x0d85, 0x54);
	gc05a2_write_register(ViPipe, 0x0d86, 0x03);
	gc05a2_write_register(ViPipe, 0x0d87, 0x2b);
	gc05a2_write_register(ViPipe, 0x0db3, 0x03);
	gc05a2_write_register(ViPipe, 0x0db4, 0x04);
	gc05a2_write_register(ViPipe, 0x0db5, 0x0d);
	gc05a2_write_register(ViPipe, 0x0db6, 0x01);
	gc05a2_write_register(ViPipe, 0x0db8, 0x04);
	gc05a2_write_register(ViPipe, 0x0db9, 0x06);
	gc05a2_write_register(ViPipe, 0x0d93, 0x03);
	gc05a2_write_register(ViPipe, 0x0d94, 0x04);
	gc05a2_write_register(ViPipe, 0x0d95, 0x05);
	gc05a2_write_register(ViPipe, 0x0d99, 0x06);
	gc05a2_write_register(ViPipe, 0x0084, 0x01);
	gc05a2_write_register(ViPipe, 0x031c, 0x80);
	gc05a2_write_register(ViPipe, 0x03fe, 0x30);
	gc05a2_write_register(ViPipe, 0x0d17, 0x06);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x0d17, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0x93);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0x80);
	gc05a2_write_register(ViPipe, 0x03fe, 0x30);
	gc05a2_write_register(ViPipe, 0x0d17, 0x06);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x0d17, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0x93);
	gc05a2_write_register(ViPipe, 0x0110, 0x01);

	gc05a2_default_reg_init(ViPipe);

	gc05a2_write_register(ViPipe, 0x0100, 0x01);

	delay_ms(50);

	printf("ViPipe:%d,===GC05A2 1296x972P 30fps Init OK!===\n", ViPipe);
}

static void gc05a2_linear_720p60_init(VI_PIPE ViPipe)
{
	gc05a2_write_register(ViPipe, 0x0100, 0x00);
	/*system*/
	gc05a2_write_register(ViPipe, 0x0315, 0xd4);
	gc05a2_write_register(ViPipe, 0x0d06, 0x01);
	gc05a2_write_register(ViPipe, 0x0a70, 0x80);
	gc05a2_write_register(ViPipe, 0x031a, 0x00);
	gc05a2_write_register(ViPipe, 0x0314, 0x00);
	gc05a2_write_register(ViPipe, 0x0130, 0x08);
	gc05a2_write_register(ViPipe, 0x0132, 0x01);
	gc05a2_write_register(ViPipe, 0x0135, 0x05);
	gc05a2_write_register(ViPipe, 0x0136, 0x38);
	gc05a2_write_register(ViPipe, 0x0137, 0x03);
	gc05a2_write_register(ViPipe, 0x0134, 0x5b);
	gc05a2_write_register(ViPipe, 0x031c, 0xe0);
	gc05a2_write_register(ViPipe, 0x0d82, 0x14);
	gc05a2_write_register(ViPipe, 0x0dd1, 0x56);

	/*gate_mode*/
	gc05a2_write_register(ViPipe, 0x0af4, 0x01);
	gc05a2_write_register(ViPipe, 0x0002, 0x10);
	gc05a2_write_register(ViPipe, 0x00c3, 0x34);

	/*pre_setting*/
	gc05a2_write_register(ViPipe, 0x0084, 0x21);
	gc05a2_write_register(ViPipe, 0x0d05, 0xcc);
	gc05a2_write_register(ViPipe, 0x0218, 0x80);
	gc05a2_write_register(ViPipe, 0x005e, 0x49);
	gc05a2_write_register(ViPipe, 0x0d06, 0x81);
	gc05a2_write_register(ViPipe, 0x0007, 0x16);
	gc05a2_write_register(ViPipe, 0x0101, 0x00);

	/*analog*/
	gc05a2_write_register(ViPipe, 0x0342, 0x07);
	gc05a2_write_register(ViPipe, 0x0343, 0x10);
	gc05a2_write_register(ViPipe, 0x0220, 0x07);
	gc05a2_write_register(ViPipe, 0x0221, 0xd0);
	gc05a2_write_register(ViPipe, 0x0202, 0x03);
	gc05a2_write_register(ViPipe, 0x0203, 0x32);
	gc05a2_write_register(ViPipe, 0x0340, 0x04);
	gc05a2_write_register(ViPipe, 0x0341, 0x08);
	gc05a2_write_register(ViPipe, 0x0219, 0x00);
	gc05a2_write_register(ViPipe, 0x0346, 0x01);
	gc05a2_write_register(ViPipe, 0x0347, 0x00);
	gc05a2_write_register(ViPipe, 0x0d14, 0x00);
	gc05a2_write_register(ViPipe, 0x0d13, 0x05);
	gc05a2_write_register(ViPipe, 0x0d16, 0x05);
	gc05a2_write_register(ViPipe, 0x0d15, 0x1d);
	gc05a2_write_register(ViPipe, 0x00c0, 0x0a);
	gc05a2_write_register(ViPipe, 0x00c1, 0x30);
	gc05a2_write_register(ViPipe, 0x034a, 0x05);
	gc05a2_write_register(ViPipe, 0x034b, 0xb0);
	gc05a2_write_register(ViPipe, 0x0e0a, 0x00);
	gc05a2_write_register(ViPipe, 0x0e0b, 0x00);
	gc05a2_write_register(ViPipe, 0x0e0e, 0x03);
	gc05a2_write_register(ViPipe, 0x0e0f, 0x00);
	gc05a2_write_register(ViPipe, 0x0e06, 0x0a);
	gc05a2_write_register(ViPipe, 0x0e23, 0x15);
	gc05a2_write_register(ViPipe, 0x0e24, 0x15);
	gc05a2_write_register(ViPipe, 0x0e2a, 0x10);
	gc05a2_write_register(ViPipe, 0x0e2b, 0x10);
	gc05a2_write_register(ViPipe, 0x0e17, 0x49);
	gc05a2_write_register(ViPipe, 0x0e1b, 0x1c);
	gc05a2_write_register(ViPipe, 0x0e3a, 0x36);
	gc05a2_write_register(ViPipe, 0x0d11, 0x84);
	gc05a2_write_register(ViPipe, 0x0e52, 0x14);
	gc05a2_write_register(ViPipe, 0x000b, 0x0e);
	gc05a2_write_register(ViPipe, 0x0008, 0x03);
	gc05a2_write_register(ViPipe, 0x0223, 0x16);
	gc05a2_write_register(ViPipe, 0x0d27, 0x39);
	gc05a2_write_register(ViPipe, 0x0d22, 0x00);
	gc05a2_write_register(ViPipe, 0x03f6, 0x0d);
	gc05a2_write_register(ViPipe, 0x0d04, 0x07);
	gc05a2_write_register(ViPipe, 0x03f3, 0x72);
	gc05a2_write_register(ViPipe, 0x03f4, 0xb8);
	gc05a2_write_register(ViPipe, 0x03f5, 0xbc);
	gc05a2_write_register(ViPipe, 0x0d02, 0x73);

	/*auto load start*/
	gc05a2_write_register(ViPipe, 0x00c4, 0x00);
	gc05a2_write_register(ViPipe, 0x00c5, 0x01);
	gc05a2_write_register(ViPipe, 0x0af6, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba0, 0x17);
	gc05a2_write_register(ViPipe, 0x0ba1, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba2, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba3, 0x00);

	gc05a2_write_register(ViPipe, 0x0ba4, 0x03);
	gc05a2_write_register(ViPipe, 0x0ba5, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba6, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba7, 0x00);

	gc05a2_write_register(ViPipe, 0x0ba8, 0x40);
	gc05a2_write_register(ViPipe, 0x0ba9, 0x00);
	gc05a2_write_register(ViPipe, 0x0baa, 0x00);
	gc05a2_write_register(ViPipe, 0x0bab, 0x00);

	gc05a2_write_register(ViPipe, 0x0bac, 0x40);
	gc05a2_write_register(ViPipe, 0x0bad, 0x00);
	gc05a2_write_register(ViPipe, 0x0bae, 0x00);
	gc05a2_write_register(ViPipe, 0x0baf, 0x00);

	gc05a2_write_register(ViPipe, 0x0bb0, 0x02);
	gc05a2_write_register(ViPipe, 0x0bb1, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb2, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb3, 0x00);

	gc05a2_write_register(ViPipe, 0x0bb8, 0x02);
	gc05a2_write_register(ViPipe, 0x0bb9, 0x00);
	gc05a2_write_register(ViPipe, 0x0bba, 0x00);
	gc05a2_write_register(ViPipe, 0x0bbb, 0x00);

	gc05a2_write_register(ViPipe, 0x0a70, 0x80);
	gc05a2_write_register(ViPipe, 0x0a71, 0x00);
	gc05a2_write_register(ViPipe, 0x0a72, 0x00);
	gc05a2_write_register(ViPipe, 0x0a66, 0x00);
	gc05a2_write_register(ViPipe, 0x0a67, 0x80);
	gc05a2_write_register(ViPipe, 0x0a4d, 0x4e);
	gc05a2_write_register(ViPipe, 0x0a50, 0x00);
	gc05a2_write_register(ViPipe, 0x0a4f, 0x0c);

	gc05a2_write_register(ViPipe, 0x0a66, 0x00);
	gc05a2_write_register(ViPipe, 0x00ca, 0x00);
	gc05a2_write_register(ViPipe, 0x00cb, 0xfc);
	gc05a2_write_register(ViPipe, 0x00cc, 0x00);
	gc05a2_write_register(ViPipe, 0x00cd, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa1, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa2, 0xe0);
	gc05a2_write_register(ViPipe, 0x0aa3, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa4, 0x40);
	gc05a2_write_register(ViPipe, 0x0a90, 0x03);
	gc05a2_write_register(ViPipe, 0x0a91, 0x0e);
	gc05a2_write_register(ViPipe, 0x0a94, 0x80);

	/*standby*/
	gc05a2_write_register(ViPipe, 0x0af6, 0x20);
	gc05a2_write_register(ViPipe, 0x0b00, 0x91);
	gc05a2_write_register(ViPipe, 0x0b01, 0x17);
	gc05a2_write_register(ViPipe, 0x0b02, 0x01);
	gc05a2_write_register(ViPipe, 0x0b03, 0x00);

	gc05a2_write_register(ViPipe, 0x0b04, 0x01);
	gc05a2_write_register(ViPipe, 0x0b05, 0x17);
	gc05a2_write_register(ViPipe, 0x0b06, 0x01);
	gc05a2_write_register(ViPipe, 0x0b07, 0x00);

	gc05a2_write_register(ViPipe, 0x0ae9, 0x01);
	gc05a2_write_register(ViPipe, 0x0aea, 0x02);
	gc05a2_write_register(ViPipe, 0x0ae8, 0x53);
	gc05a2_write_register(ViPipe, 0x0ae8, 0x43);

	/*gain_partition*/
	gc05a2_write_register(ViPipe, 0x0af6, 0x30);
	gc05a2_write_register(ViPipe, 0x0b00, 0x08);
	gc05a2_write_register(ViPipe, 0x0b01, 0x0f);
	gc05a2_write_register(ViPipe, 0x0b02, 0x00);

	gc05a2_write_register(ViPipe, 0x0b04, 0x1c);
	gc05a2_write_register(ViPipe, 0x0b05, 0x24);
	gc05a2_write_register(ViPipe, 0x0b06, 0x00);

	gc05a2_write_register(ViPipe, 0x0b08, 0x30);
	gc05a2_write_register(ViPipe, 0x0b09, 0x40);
	gc05a2_write_register(ViPipe, 0x0b0a, 0x00);

	gc05a2_write_register(ViPipe, 0x0b0c, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b0d, 0x2a);
	gc05a2_write_register(ViPipe, 0x0b0e, 0x00);

	gc05a2_write_register(ViPipe, 0x0b10, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b11, 0x2b);
	gc05a2_write_register(ViPipe, 0x0b12, 0x00);

	gc05a2_write_register(ViPipe, 0x0b14, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b15, 0x23);
	gc05a2_write_register(ViPipe, 0x0b16, 0x00);

	gc05a2_write_register(ViPipe, 0x0b18, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b19, 0x24);
	gc05a2_write_register(ViPipe, 0x0b1a, 0x00);

	gc05a2_write_register(ViPipe, 0x0b1c, 0x0c);
	gc05a2_write_register(ViPipe, 0x0b1d, 0x0c);
	gc05a2_write_register(ViPipe, 0x0b1e, 0x00);

	gc05a2_write_register(ViPipe, 0x0b20, 0x03);
	gc05a2_write_register(ViPipe, 0x0b21, 0x03);
	gc05a2_write_register(ViPipe, 0x0b22, 0x00);

	gc05a2_write_register(ViPipe, 0x0b24, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b25, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b26, 0x00);

	gc05a2_write_register(ViPipe, 0x0b28, 0x03);
	gc05a2_write_register(ViPipe, 0x0b29, 0x03);
	gc05a2_write_register(ViPipe, 0x0b2a, 0x00);

	gc05a2_write_register(ViPipe, 0x0b2c, 0x12);
	gc05a2_write_register(ViPipe, 0x0b2d, 0x12);
	gc05a2_write_register(ViPipe, 0x0b2e, 0x00);

	gc05a2_write_register(ViPipe, 0x0b30, 0x08);
	gc05a2_write_register(ViPipe, 0x0b31, 0x08);
	gc05a2_write_register(ViPipe, 0x0b32, 0x00);

	gc05a2_write_register(ViPipe, 0x0b34, 0x14);
	gc05a2_write_register(ViPipe, 0x0b35, 0x14);
	gc05a2_write_register(ViPipe, 0x0b36, 0x00);

	gc05a2_write_register(ViPipe, 0x0b38, 0x10);
	gc05a2_write_register(ViPipe, 0x0b39, 0x10);
	gc05a2_write_register(ViPipe, 0x0b3a, 0x00);

	gc05a2_write_register(ViPipe, 0x0b3c, 0x16);
	gc05a2_write_register(ViPipe, 0x0b3d, 0x16);
	gc05a2_write_register(ViPipe, 0x0b3e, 0x00);

	gc05a2_write_register(ViPipe, 0x0b40, 0x10);
	gc05a2_write_register(ViPipe, 0x0b41, 0x10);
	gc05a2_write_register(ViPipe, 0x0b42, 0x00);

	gc05a2_write_register(ViPipe, 0x0b44, 0x19);
	gc05a2_write_register(ViPipe, 0x0b45, 0x19);
	gc05a2_write_register(ViPipe, 0x0b46, 0x00);

	gc05a2_write_register(ViPipe, 0x0b48, 0x16);
	gc05a2_write_register(ViPipe, 0x0b49, 0x16);
	gc05a2_write_register(ViPipe, 0x0b4a, 0x00);

	gc05a2_write_register(ViPipe, 0x0b4c, 0x19);
	gc05a2_write_register(ViPipe, 0x0b4d, 0x19);
	gc05a2_write_register(ViPipe, 0x0b4e, 0x00);

	gc05a2_write_register(ViPipe, 0x0b50, 0x16);
	gc05a2_write_register(ViPipe, 0x0b51, 0x16);
	gc05a2_write_register(ViPipe, 0x0b52, 0x00);

	gc05a2_write_register(ViPipe, 0x0b80, 0x01);
	gc05a2_write_register(ViPipe, 0x0b81, 0x00);
	gc05a2_write_register(ViPipe, 0x0b82, 0x00);
	gc05a2_write_register(ViPipe, 0x0b84, 0x00);
	gc05a2_write_register(ViPipe, 0x0b85, 0x00);
	gc05a2_write_register(ViPipe, 0x0b86, 0x00);

	gc05a2_write_register(ViPipe, 0x0b88, 0x01);
	gc05a2_write_register(ViPipe, 0x0b89, 0x6a);
	gc05a2_write_register(ViPipe, 0x0b8a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b8c, 0x00);
	gc05a2_write_register(ViPipe, 0x0b8d, 0x01);
	gc05a2_write_register(ViPipe, 0x0b8e, 0x00);

	gc05a2_write_register(ViPipe, 0x0b90, 0x01);
	gc05a2_write_register(ViPipe, 0x0b91, 0xf6);
	gc05a2_write_register(ViPipe, 0x0b92, 0x00);
	gc05a2_write_register(ViPipe, 0x0b94, 0x00);
	gc05a2_write_register(ViPipe, 0x0b95, 0x02);
	gc05a2_write_register(ViPipe, 0x0b96, 0x00);

	gc05a2_write_register(ViPipe, 0x0b98, 0x02);
	gc05a2_write_register(ViPipe, 0x0b99, 0xc4);
	gc05a2_write_register(ViPipe, 0x0b9a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b9c, 0x00);
	gc05a2_write_register(ViPipe, 0x0b9d, 0x03);
	gc05a2_write_register(ViPipe, 0x0b9e, 0x00);

	gc05a2_write_register(ViPipe, 0x0ba0, 0x03);
	gc05a2_write_register(ViPipe, 0x0ba1, 0xd8);
	gc05a2_write_register(ViPipe, 0x0ba2, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba4, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba5, 0x04);
	gc05a2_write_register(ViPipe, 0x0ba6, 0x00);

	gc05a2_write_register(ViPipe, 0x0ba8, 0x05);
	gc05a2_write_register(ViPipe, 0x0ba9, 0x4d);
	gc05a2_write_register(ViPipe, 0x0baa, 0x00);
	gc05a2_write_register(ViPipe, 0x0bac, 0x00);
	gc05a2_write_register(ViPipe, 0x0bad, 0x05);
	gc05a2_write_register(ViPipe, 0x0bae, 0x00);

	gc05a2_write_register(ViPipe, 0x0bb0, 0x07);
	gc05a2_write_register(ViPipe, 0x0bb1, 0x3e);
	gc05a2_write_register(ViPipe, 0x0bb2, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb4, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb5, 0x06);
	gc05a2_write_register(ViPipe, 0x0bb6, 0x00);

	gc05a2_write_register(ViPipe, 0x0bb8, 0x0a);
	gc05a2_write_register(ViPipe, 0x0bb9, 0x1a);
	gc05a2_write_register(ViPipe, 0x0bba, 0x00);
	gc05a2_write_register(ViPipe, 0x0bbc, 0x09);
	gc05a2_write_register(ViPipe, 0x0bbd, 0x36);
	gc05a2_write_register(ViPipe, 0x0bbe, 0x00);

	gc05a2_write_register(ViPipe, 0x0bc0, 0x0e);
	gc05a2_write_register(ViPipe, 0x0bc1, 0x66);
	gc05a2_write_register(ViPipe, 0x0bc2, 0x00);
	gc05a2_write_register(ViPipe, 0x0bc4, 0x10);
	gc05a2_write_register(ViPipe, 0x0bc5, 0x06);
	gc05a2_write_register(ViPipe, 0x0bc6, 0x00);

	gc05a2_write_register(ViPipe, 0x02c1, 0xe0);
	gc05a2_write_register(ViPipe, 0x0207, 0x04);
	gc05a2_write_register(ViPipe, 0x02c2, 0x10);
	gc05a2_write_register(ViPipe, 0x02c3, 0x74);
	gc05a2_write_register(ViPipe, 0x02c5, 0x09);
	gc05a2_write_register(ViPipe, 0x02c1, 0xe0);
	gc05a2_write_register(ViPipe, 0x0207, 0x04);
	gc05a2_write_register(ViPipe, 0x02c2, 0x10);
	gc05a2_write_register(ViPipe, 0x02c5, 0x09);
	gc05a2_write_register(ViPipe, 0x02c1, 0xe0);
	gc05a2_write_register(ViPipe, 0x0207, 0x04);
	gc05a2_write_register(ViPipe, 0x02c2, 0x10);
	gc05a2_write_register(ViPipe, 0x02c5, 0x09);

	/*auto load CH_GAIN*/
	gc05a2_write_register(ViPipe, 0x0aa1, 0x15);
	gc05a2_write_register(ViPipe, 0x0aa2, 0x50);
	gc05a2_write_register(ViPipe, 0x0aa3, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa4, 0x09);
	gc05a2_write_register(ViPipe, 0x0a90, 0x25);
	gc05a2_write_register(ViPipe, 0x0a91, 0x0e);
	gc05a2_write_register(ViPipe, 0x0a94, 0x80);

	/*ISP*/
	gc05a2_write_register(ViPipe, 0x0050, 0x00);
	gc05a2_write_register(ViPipe, 0x0089, 0x83);
	gc05a2_write_register(ViPipe, 0x005a, 0x40);
	gc05a2_write_register(ViPipe, 0x00c3, 0x35);
	gc05a2_write_register(ViPipe, 0x00c4, 0x80);
	gc05a2_write_register(ViPipe, 0x0080, 0x10);
	gc05a2_write_register(ViPipe, 0x0040, 0x12);
	gc05a2_write_register(ViPipe, 0x0053, 0x0a);
	gc05a2_write_register(ViPipe, 0x0054, 0x44);
	gc05a2_write_register(ViPipe, 0x0055, 0x32);
	gc05a2_write_register(ViPipe, 0x0058, 0x89);
	gc05a2_write_register(ViPipe, 0x004a, 0x03);
	gc05a2_write_register(ViPipe, 0x0048, 0xf0);
	gc05a2_write_register(ViPipe, 0x0049, 0x0f);
	gc05a2_write_register(ViPipe, 0x0041, 0x20);
	gc05a2_write_register(ViPipe, 0x0043, 0x0a);
	gc05a2_write_register(ViPipe, 0x009d, 0x08);
	gc05a2_write_register(ViPipe, 0x0236, 0x40);

	/*gain*/
	gc05a2_write_register(ViPipe, 0x0204, 0x04);
	gc05a2_write_register(ViPipe, 0x0205, 0x00);
	gc05a2_write_register(ViPipe, 0x02b3, 0x00);
	gc05a2_write_register(ViPipe, 0x02b4, 0x00);
	gc05a2_write_register(ViPipe, 0x009e, 0x01);
	gc05a2_write_register(ViPipe, 0x009f, 0x94);

	/*OUT 1280x720*/
	gc05a2_write_register(ViPipe, 0x0350, 0x01);
	gc05a2_write_register(ViPipe, 0x0353, 0x00);
	gc05a2_write_register(ViPipe, 0x0354, 0x0c);
	gc05a2_write_register(ViPipe, 0x034c, 0x05);
	gc05a2_write_register(ViPipe, 0x034d, 0x00);
	gc05a2_write_register(ViPipe, 0x021f, 0x14);

	/*auto load REG*/
	gc05a2_write_register(ViPipe, 0x0aa1, 0x10);
	gc05a2_write_register(ViPipe, 0x0aa2, 0xf8);
	gc05a2_write_register(ViPipe, 0x0aa3, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa4, 0x1f);
	gc05a2_write_register(ViPipe, 0x0a90, 0x11);
	gc05a2_write_register(ViPipe, 0x0a91, 0x0e);
	gc05a2_write_register(ViPipe, 0x0a94, 0x80);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x0a90, 0x00);
	gc05a2_write_register(ViPipe, 0x0a70, 0x00);
	gc05a2_write_register(ViPipe, 0x0a67, 0x00);
	gc05a2_write_register(ViPipe, 0x0af4, 0x29);

	/*DPHY*/
	gc05a2_write_register(ViPipe, 0x0d80, 0x07);
	gc05a2_write_register(ViPipe, 0x0dd3, 0x18);

	/*MIPI*/
	gc05a2_write_register(ViPipe, 0x0107, 0x05);
	gc05a2_write_register(ViPipe, 0x0117, 0x01);
	gc05a2_write_register(ViPipe, 0x0d81, 0x00);
	gc05a2_write_register(ViPipe, 0x0d84, 0x06);
	gc05a2_write_register(ViPipe, 0x0d85, 0x40);
	gc05a2_write_register(ViPipe, 0x0d86, 0x03);
	gc05a2_write_register(ViPipe, 0x0d87, 0x21);
	gc05a2_write_register(ViPipe, 0x0db3, 0x03);
	gc05a2_write_register(ViPipe, 0x0db4, 0x04);
	gc05a2_write_register(ViPipe, 0x0db5, 0x0d);
	gc05a2_write_register(ViPipe, 0x0db6, 0x01);
	gc05a2_write_register(ViPipe, 0x0db8, 0x04);
	gc05a2_write_register(ViPipe, 0x0db9, 0x06);
	gc05a2_write_register(ViPipe, 0x0d93, 0x03);
	gc05a2_write_register(ViPipe, 0x0d94, 0x04);
	gc05a2_write_register(ViPipe, 0x0d95, 0x05);
	gc05a2_write_register(ViPipe, 0x0d99, 0x06);
	gc05a2_write_register(ViPipe, 0x0084, 0x01);

	/*CISCTL_Reset*/
	gc05a2_write_register(ViPipe, 0x031c, 0x80);
	gc05a2_write_register(ViPipe, 0x03fe, 0x30);
	gc05a2_write_register(ViPipe, 0x0d17, 0x06);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x0d17, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0x93);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0x80);
	gc05a2_write_register(ViPipe, 0x03fe, 0x30);
	gc05a2_write_register(ViPipe, 0x0d17, 0x06);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x0d17, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0x93);

	/*OUT*/
	gc05a2_write_register(ViPipe, 0x0110, 0x01);


	gc05a2_default_reg_init(ViPipe);

	gc05a2_write_register(ViPipe, 0x0100, 0x01);

	delay_ms(50);

	printf("ViPipe:%d,===GC05A2 720P 60fps Init OK!===\n", ViPipe);
};

static void gc05a2_linear_2560x1920p30_init(VI_PIPE ViPipe)
{
	gc05a2_write_register(ViPipe, 0x0100, 0x00);
	gc05a2_write_register(ViPipe, 0x0315, 0xd4);
	gc05a2_write_register(ViPipe, 0x0d06, 0x01);
	gc05a2_write_register(ViPipe, 0x0a70, 0x80);
	gc05a2_write_register(ViPipe, 0x031a, 0x00);
	gc05a2_write_register(ViPipe, 0x0314, 0x00);
	gc05a2_write_register(ViPipe, 0x0130, 0x08);
	gc05a2_write_register(ViPipe, 0x0132, 0x01);
	gc05a2_write_register(ViPipe, 0x0135, 0x01);
	gc05a2_write_register(ViPipe, 0x0136, 0x38);
	gc05a2_write_register(ViPipe, 0x0137, 0x03);
	gc05a2_write_register(ViPipe, 0x0134, 0x5b);
	gc05a2_write_register(ViPipe, 0x031c, 0xe0);
	gc05a2_write_register(ViPipe, 0x0d82, 0x14);
	gc05a2_write_register(ViPipe, 0x0dd1, 0x56);
	gc05a2_write_register(ViPipe, 0x0af4, 0x01);
	gc05a2_write_register(ViPipe, 0x0002, 0x10);
	gc05a2_write_register(ViPipe, 0x00c3, 0x34);
	gc05a2_write_register(ViPipe, 0x0084, 0x21);
	gc05a2_write_register(ViPipe, 0x0d05, 0xcc);
	gc05a2_write_register(ViPipe, 0x0218, 0x00);
	gc05a2_write_register(ViPipe, 0x005e, 0x48);
	gc05a2_write_register(ViPipe, 0x0d06, 0x01);
	gc05a2_write_register(ViPipe, 0x0007, 0x16);
	gc05a2_write_register(ViPipe, 0x0101, 0x00);
	gc05a2_write_register(ViPipe, 0x0342, 0x07);
	gc05a2_write_register(ViPipe, 0x0343, 0x28);
	gc05a2_write_register(ViPipe, 0x0220, 0x07);
	gc05a2_write_register(ViPipe, 0x0221, 0xd0);
	gc05a2_write_register(ViPipe, 0x0202, 0x07);
	gc05a2_write_register(ViPipe, 0x0203, 0x32);
	gc05a2_write_register(ViPipe, 0x0340, 0x07);
	gc05a2_write_register(ViPipe, 0x0341, 0xf0);
	gc05a2_write_register(ViPipe, 0x0219, 0x00);
	gc05a2_write_register(ViPipe, 0x0346, 0x00);
	gc05a2_write_register(ViPipe, 0x0347, 0x04);
	gc05a2_write_register(ViPipe, 0x0d14, 0x00);
	gc05a2_write_register(ViPipe, 0x0d13, 0x05);
	gc05a2_write_register(ViPipe, 0x0d16, 0x05);
	gc05a2_write_register(ViPipe, 0x0d15, 0x1d);
	gc05a2_write_register(ViPipe, 0x00c0, 0x0a);
	gc05a2_write_register(ViPipe, 0x00c1, 0x30);
	gc05a2_write_register(ViPipe, 0x034a, 0x07);
	gc05a2_write_register(ViPipe, 0x034b, 0xa8);
	gc05a2_write_register(ViPipe, 0x0e0a, 0x00);
	gc05a2_write_register(ViPipe, 0x0e0b, 0x00);
	gc05a2_write_register(ViPipe, 0x0e0e, 0x03);
	gc05a2_write_register(ViPipe, 0x0e0f, 0x00);
	gc05a2_write_register(ViPipe, 0x0e06, 0x0a);
	gc05a2_write_register(ViPipe, 0x0e23, 0x15);
	gc05a2_write_register(ViPipe, 0x0e24, 0x15);
	gc05a2_write_register(ViPipe, 0x0e2a, 0x10);
	gc05a2_write_register(ViPipe, 0x0e2b, 0x10);
	gc05a2_write_register(ViPipe, 0x0e17, 0x49);
	gc05a2_write_register(ViPipe, 0x0e1b, 0x1c);
	gc05a2_write_register(ViPipe, 0x0e3a, 0x36);
	gc05a2_write_register(ViPipe, 0x0d11, 0x84);
	gc05a2_write_register(ViPipe, 0x0e52, 0x14);
	gc05a2_write_register(ViPipe, 0x000b, 0x10);
	gc05a2_write_register(ViPipe, 0x0008, 0x08);
	gc05a2_write_register(ViPipe, 0x0223, 0x17);
	gc05a2_write_register(ViPipe, 0x0d27, 0x39);
	gc05a2_write_register(ViPipe, 0x0d22, 0x00);
	gc05a2_write_register(ViPipe, 0x03f6, 0x0d);
	gc05a2_write_register(ViPipe, 0x0d04, 0x07);
	gc05a2_write_register(ViPipe, 0x03f3, 0x72);
	gc05a2_write_register(ViPipe, 0x03f4, 0xb8);
	gc05a2_write_register(ViPipe, 0x03f5, 0xbc);
	gc05a2_write_register(ViPipe, 0x0d02, 0x73);
	gc05a2_write_register(ViPipe, 0x00c4, 0x00);
	gc05a2_write_register(ViPipe, 0x00c5, 0x01);
	gc05a2_write_register(ViPipe, 0x0af6, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba0, 0x17);
	gc05a2_write_register(ViPipe, 0x0ba1, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba2, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba3, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba4, 0x03);
	gc05a2_write_register(ViPipe, 0x0ba5, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba6, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba7, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba8, 0x40);
	gc05a2_write_register(ViPipe, 0x0ba9, 0x00);
	gc05a2_write_register(ViPipe, 0x0baa, 0x00);
	gc05a2_write_register(ViPipe, 0x0bab, 0x00);
	gc05a2_write_register(ViPipe, 0x0bac, 0x40);
	gc05a2_write_register(ViPipe, 0x0bad, 0x00);
	gc05a2_write_register(ViPipe, 0x0bae, 0x00);
	gc05a2_write_register(ViPipe, 0x0baf, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb0, 0x02);
	gc05a2_write_register(ViPipe, 0x0bb1, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb2, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb3, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb8, 0x02);
	gc05a2_write_register(ViPipe, 0x0bb9, 0x00);
	gc05a2_write_register(ViPipe, 0x0bba, 0x00);
	gc05a2_write_register(ViPipe, 0x0bbb, 0x00);
	gc05a2_write_register(ViPipe, 0x0a70, 0x80);
	gc05a2_write_register(ViPipe, 0x0a71, 0x00);
	gc05a2_write_register(ViPipe, 0x0a72, 0x00);
	gc05a2_write_register(ViPipe, 0x0a66, 0x00);
	gc05a2_write_register(ViPipe, 0x0a67, 0x80);
	gc05a2_write_register(ViPipe, 0x0a4d, 0x4e);
	gc05a2_write_register(ViPipe, 0x0a50, 0x00);
	gc05a2_write_register(ViPipe, 0x0a4f, 0x0c);
	gc05a2_write_register(ViPipe, 0x0a66, 0x00);
	gc05a2_write_register(ViPipe, 0x00ca, 0x00);
	gc05a2_write_register(ViPipe, 0x00cb, 0x00);
	gc05a2_write_register(ViPipe, 0x00cc, 0x00);
	gc05a2_write_register(ViPipe, 0x00cd, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa1, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa2, 0xe0);
	gc05a2_write_register(ViPipe, 0x0aa3, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa4, 0x40);
	gc05a2_write_register(ViPipe, 0x0a90, 0x03);
	gc05a2_write_register(ViPipe, 0x0a91, 0x0e);
	gc05a2_write_register(ViPipe, 0x0a94, 0x80);
	gc05a2_write_register(ViPipe, 0x0af6, 0x20);
	gc05a2_write_register(ViPipe, 0x0b00, 0x91);
	gc05a2_write_register(ViPipe, 0x0b01, 0x17);
	gc05a2_write_register(ViPipe, 0x0b02, 0x01);
	gc05a2_write_register(ViPipe, 0x0b03, 0x00);
	gc05a2_write_register(ViPipe, 0x0b04, 0x01);
	gc05a2_write_register(ViPipe, 0x0b05, 0x17);
	gc05a2_write_register(ViPipe, 0x0b06, 0x01);
	gc05a2_write_register(ViPipe, 0x0b07, 0x00);
	gc05a2_write_register(ViPipe, 0x0ae9, 0x01);
	gc05a2_write_register(ViPipe, 0x0aea, 0x02);
	gc05a2_write_register(ViPipe, 0x0ae8, 0x53);
	gc05a2_write_register(ViPipe, 0x0ae8, 0x43);
	gc05a2_write_register(ViPipe, 0x0af6, 0x30);
	gc05a2_write_register(ViPipe, 0x0b00, 0x08);
	gc05a2_write_register(ViPipe, 0x0b01, 0x0f);
	gc05a2_write_register(ViPipe, 0x0b02, 0x00);
	gc05a2_write_register(ViPipe, 0x0b04, 0x1c);
	gc05a2_write_register(ViPipe, 0x0b05, 0x24);
	gc05a2_write_register(ViPipe, 0x0b06, 0x00);
	gc05a2_write_register(ViPipe, 0x0b08, 0x30);
	gc05a2_write_register(ViPipe, 0x0b09, 0x40);
	gc05a2_write_register(ViPipe, 0x0b0a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b0c, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b0d, 0x2a);
	gc05a2_write_register(ViPipe, 0x0b0e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b10, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b11, 0x2b);
	gc05a2_write_register(ViPipe, 0x0b12, 0x00);
	gc05a2_write_register(ViPipe, 0x0b14, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b15, 0x23);
	gc05a2_write_register(ViPipe, 0x0b16, 0x00);
	gc05a2_write_register(ViPipe, 0x0b18, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b19, 0x24);
	gc05a2_write_register(ViPipe, 0x0b1a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b1c, 0x0c);
	gc05a2_write_register(ViPipe, 0x0b1d, 0x0c);
	gc05a2_write_register(ViPipe, 0x0b1e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b20, 0x03);
	gc05a2_write_register(ViPipe, 0x0b21, 0x03);
	gc05a2_write_register(ViPipe, 0x0b22, 0x00);
	gc05a2_write_register(ViPipe, 0x0b24, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b25, 0x0e);
	gc05a2_write_register(ViPipe, 0x0b26, 0x00);
	gc05a2_write_register(ViPipe, 0x0b28, 0x03);
	gc05a2_write_register(ViPipe, 0x0b29, 0x03);
	gc05a2_write_register(ViPipe, 0x0b2a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b2c, 0x12);
	gc05a2_write_register(ViPipe, 0x0b2d, 0x12);
	gc05a2_write_register(ViPipe, 0x0b2e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b30, 0x08);
	gc05a2_write_register(ViPipe, 0x0b31, 0x08);
	gc05a2_write_register(ViPipe, 0x0b32, 0x00);
	gc05a2_write_register(ViPipe, 0x0b34, 0x14);
	gc05a2_write_register(ViPipe, 0x0b35, 0x14);
	gc05a2_write_register(ViPipe, 0x0b36, 0x00);
	gc05a2_write_register(ViPipe, 0x0b38, 0x10);
	gc05a2_write_register(ViPipe, 0x0b39, 0x10);
	gc05a2_write_register(ViPipe, 0x0b3a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b3c, 0x16);
	gc05a2_write_register(ViPipe, 0x0b3d, 0x16);
	gc05a2_write_register(ViPipe, 0x0b3e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b40, 0x10);
	gc05a2_write_register(ViPipe, 0x0b41, 0x10);
	gc05a2_write_register(ViPipe, 0x0b42, 0x00);
	gc05a2_write_register(ViPipe, 0x0b44, 0x19);
	gc05a2_write_register(ViPipe, 0x0b45, 0x19);
	gc05a2_write_register(ViPipe, 0x0b46, 0x00);
	gc05a2_write_register(ViPipe, 0x0b48, 0x16);
	gc05a2_write_register(ViPipe, 0x0b49, 0x16);
	gc05a2_write_register(ViPipe, 0x0b4a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b4c, 0x19);
	gc05a2_write_register(ViPipe, 0x0b4d, 0x19);
	gc05a2_write_register(ViPipe, 0x0b4e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b50, 0x16);
	gc05a2_write_register(ViPipe, 0x0b51, 0x16);
	gc05a2_write_register(ViPipe, 0x0b52, 0x00);
	gc05a2_write_register(ViPipe, 0x0b80, 0x01);
	gc05a2_write_register(ViPipe, 0x0b81, 0x00);
	gc05a2_write_register(ViPipe, 0x0b82, 0x00);
	gc05a2_write_register(ViPipe, 0x0b84, 0x00);
	gc05a2_write_register(ViPipe, 0x0b85, 0x00);
	gc05a2_write_register(ViPipe, 0x0b86, 0x00);
	gc05a2_write_register(ViPipe, 0x0b88, 0x01);
	gc05a2_write_register(ViPipe, 0x0b89, 0x6a);
	gc05a2_write_register(ViPipe, 0x0b8a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b8c, 0x00);
	gc05a2_write_register(ViPipe, 0x0b8d, 0x01);
	gc05a2_write_register(ViPipe, 0x0b8e, 0x00);
	gc05a2_write_register(ViPipe, 0x0b90, 0x01);
	gc05a2_write_register(ViPipe, 0x0b91, 0xf6);
	gc05a2_write_register(ViPipe, 0x0b92, 0x00);
	gc05a2_write_register(ViPipe, 0x0b94, 0x00);
	gc05a2_write_register(ViPipe, 0x0b95, 0x02);
	gc05a2_write_register(ViPipe, 0x0b96, 0x00);
	gc05a2_write_register(ViPipe, 0x0b98, 0x02);
	gc05a2_write_register(ViPipe, 0x0b99, 0xc4);
	gc05a2_write_register(ViPipe, 0x0b9a, 0x00);
	gc05a2_write_register(ViPipe, 0x0b9c, 0x00);
	gc05a2_write_register(ViPipe, 0x0b9d, 0x03);
	gc05a2_write_register(ViPipe, 0x0b9e, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba0, 0x03);
	gc05a2_write_register(ViPipe, 0x0ba1, 0xd8);
	gc05a2_write_register(ViPipe, 0x0ba2, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba4, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba5, 0x04);
	gc05a2_write_register(ViPipe, 0x0ba6, 0x00);
	gc05a2_write_register(ViPipe, 0x0ba8, 0x05);
	gc05a2_write_register(ViPipe, 0x0ba9, 0x4d);
	gc05a2_write_register(ViPipe, 0x0baa, 0x00);
	gc05a2_write_register(ViPipe, 0x0bac, 0x00);
	gc05a2_write_register(ViPipe, 0x0bad, 0x05);
	gc05a2_write_register(ViPipe, 0x0bae, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb0, 0x07);
	gc05a2_write_register(ViPipe, 0x0bb1, 0x3e);
	gc05a2_write_register(ViPipe, 0x0bb2, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb4, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb5, 0x06);
	gc05a2_write_register(ViPipe, 0x0bb6, 0x00);
	gc05a2_write_register(ViPipe, 0x0bb8, 0x0a);
	gc05a2_write_register(ViPipe, 0x0bb9, 0x1a);
	gc05a2_write_register(ViPipe, 0x0bba, 0x00);
	gc05a2_write_register(ViPipe, 0x0bbc, 0x09);
	gc05a2_write_register(ViPipe, 0x0bbd, 0x36);
	gc05a2_write_register(ViPipe, 0x0bbe, 0x00);
	gc05a2_write_register(ViPipe, 0x0bc0, 0x0e);
	gc05a2_write_register(ViPipe, 0x0bc1, 0x66);
	gc05a2_write_register(ViPipe, 0x0bc2, 0x00);
	gc05a2_write_register(ViPipe, 0x0bc4, 0x10);
	gc05a2_write_register(ViPipe, 0x0bc5, 0x06);
	gc05a2_write_register(ViPipe, 0x0bc6, 0x00);
	gc05a2_write_register(ViPipe, 0x02c1, 0xe0);
	gc05a2_write_register(ViPipe, 0x0207, 0x04);
	gc05a2_write_register(ViPipe, 0x02c2, 0x10);
	gc05a2_write_register(ViPipe, 0x02c3, 0x74);
	gc05a2_write_register(ViPipe, 0x02c5, 0x09);
	gc05a2_write_register(ViPipe, 0x02c1, 0xe0);
	gc05a2_write_register(ViPipe, 0x0207, 0x04);
	gc05a2_write_register(ViPipe, 0x02c2, 0x10);
	gc05a2_write_register(ViPipe, 0x02c5, 0x09);
	gc05a2_write_register(ViPipe, 0x02c1, 0xe0);
	gc05a2_write_register(ViPipe, 0x0207, 0x04);
	gc05a2_write_register(ViPipe, 0x02c2, 0x10);
	gc05a2_write_register(ViPipe, 0x02c5, 0x09);
	gc05a2_write_register(ViPipe, 0x0aa1, 0x15);
	gc05a2_write_register(ViPipe, 0x0aa2, 0x50);
	gc05a2_write_register(ViPipe, 0x0aa3, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa4, 0x09);
	gc05a2_write_register(ViPipe, 0x0a90, 0x25);
	gc05a2_write_register(ViPipe, 0x0a91, 0x0e);
	gc05a2_write_register(ViPipe, 0x0a94, 0x80);
	gc05a2_write_register(ViPipe, 0x0050, 0x00);
	gc05a2_write_register(ViPipe, 0x0089, 0x83);
	gc05a2_write_register(ViPipe, 0x005a, 0x40);
	gc05a2_write_register(ViPipe, 0x00c3, 0x35);
	gc05a2_write_register(ViPipe, 0x00c4, 0x80);
	gc05a2_write_register(ViPipe, 0x0080, 0x10);
	gc05a2_write_register(ViPipe, 0x0040, 0x12);
	gc05a2_write_register(ViPipe, 0x0053, 0x0a);
	gc05a2_write_register(ViPipe, 0x0054, 0x44);
	gc05a2_write_register(ViPipe, 0x0055, 0x32);
	gc05a2_write_register(ViPipe, 0x0058, 0x89);
	gc05a2_write_register(ViPipe, 0x004a, 0x03);
	gc05a2_write_register(ViPipe, 0x0048, 0xf0);
	gc05a2_write_register(ViPipe, 0x0049, 0x0f);
	gc05a2_write_register(ViPipe, 0x0041, 0x20);
	gc05a2_write_register(ViPipe, 0x0043, 0x0a);
	gc05a2_write_register(ViPipe, 0x009d, 0x08);
	gc05a2_write_register(ViPipe, 0x0236, 0x40);
	gc05a2_write_register(ViPipe, 0x0204, 0x04);
	gc05a2_write_register(ViPipe, 0x0205, 0x00);
	gc05a2_write_register(ViPipe, 0x02b3, 0x00);
	gc05a2_write_register(ViPipe, 0x02b4, 0x00);
	gc05a2_write_register(ViPipe, 0x009e, 0x01);
	gc05a2_write_register(ViPipe, 0x009f, 0x94);
	gc05a2_write_register(ViPipe, 0x0350, 0x01); // win mode
	gc05a2_write_register(ViPipe, 0x0353, 0x00); // out_win_x1 0x08 8
	gc05a2_write_register(ViPipe, 0x0354, 0x08);
	gc05a2_write_register(ViPipe, 0x034c, 0x0a); // out_win_width  0xa20 2592
	gc05a2_write_register(ViPipe, 0x034d, 0x20);
	gc05a2_write_register(ViPipe, 0x021f, 0x14);
	gc05a2_write_register(ViPipe, 0x0aa1, 0x10);
	gc05a2_write_register(ViPipe, 0x0aa2, 0xf8);
	gc05a2_write_register(ViPipe, 0x0aa3, 0x00);
	gc05a2_write_register(ViPipe, 0x0aa4, 0x1f);
	gc05a2_write_register(ViPipe, 0x0a90, 0x11);
	gc05a2_write_register(ViPipe, 0x0a91, 0x0e);
	gc05a2_write_register(ViPipe, 0x0a94, 0x80);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x0a90, 0x00);
	gc05a2_write_register(ViPipe, 0x0a70, 0x00);
	gc05a2_write_register(ViPipe, 0x0a67, 0x00);
	gc05a2_write_register(ViPipe, 0x0af4, 0x29);
	gc05a2_write_register(ViPipe, 0x0d80, 0x07);
	gc05a2_write_register(ViPipe, 0x0dd3, 0x18);
	gc05a2_write_register(ViPipe, 0x0107, 0x05);
	gc05a2_write_register(ViPipe, 0x0117, 0x01);
	gc05a2_write_register(ViPipe, 0x0d81, 0x00);
	gc05a2_write_register(ViPipe, 0x0d84, 0x0c);
	gc05a2_write_register(ViPipe, 0x0d85, 0xa8);
	gc05a2_write_register(ViPipe, 0x0d86, 0x06);
	gc05a2_write_register(ViPipe, 0x0d87, 0x55);
	gc05a2_write_register(ViPipe, 0x0db3, 0x06);
	gc05a2_write_register(ViPipe, 0x0db4, 0x08);
	gc05a2_write_register(ViPipe, 0x0db5, 0x1e);
	gc05a2_write_register(ViPipe, 0x0db6, 0x02);
	gc05a2_write_register(ViPipe, 0x0db8, 0x12);
	gc05a2_write_register(ViPipe, 0x0db9, 0x0a);
	gc05a2_write_register(ViPipe, 0x0d93, 0x06);
	gc05a2_write_register(ViPipe, 0x0d94, 0x09);
	gc05a2_write_register(ViPipe, 0x0d95, 0x0d);
	gc05a2_write_register(ViPipe, 0x0d99, 0x0b);
	gc05a2_write_register(ViPipe, 0x0084, 0x01);
	gc05a2_write_register(ViPipe, 0x031c, 0x80);
	gc05a2_write_register(ViPipe, 0x03fe, 0x30);
	gc05a2_write_register(ViPipe, 0x0d17, 0x06);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x0d17, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0x93);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0x80);
	gc05a2_write_register(ViPipe, 0x03fe, 0x30);
	gc05a2_write_register(ViPipe, 0x0d17, 0x06);
	gc05a2_write_register(ViPipe, 0x03fe, 0x00);
	gc05a2_write_register(ViPipe, 0x0d17, 0x00);
	gc05a2_write_register(ViPipe, 0x031c, 0x93);
	gc05a2_write_register(ViPipe, 0x0110, 0x01);

	gc05a2_default_reg_init(ViPipe);

	gc05a2_write_register(ViPipe, 0x0100, 0x01);

	delay_ms(50);

	printf("ViPipe:%d,===GC05A2 2560x1920P 30fps Init OK!===\n", ViPipe);
};
