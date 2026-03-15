#ifndef __Z_MMF_PRIV_H__
#define __Z_MMF_PRIV_H__

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <unistd.h>

#include "cvi_buffer.h"
#include "cvi_ae_comm.h"
#include "cvi_awb_comm.h"
#include "cvi_comm_isp.h"
#include "z_mmf.h"

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

#endif