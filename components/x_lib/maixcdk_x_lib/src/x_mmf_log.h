#ifndef __X_MMF_LOG_H__
#define __X_MMF_LOG_H__

#include "x_mmf.h"

void x_mmf_log_write(X_MMF_LOG_LEVEL_E level, const char *func, int line, const char *fmt, ...);

#define XLOGE(fmt, ...) x_mmf_log_write(X_MMF_LOG_ERROR, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define XLOGW(fmt, ...) x_mmf_log_write(X_MMF_LOG_WARN, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define XLOGI(fmt, ...) x_mmf_log_write(X_MMF_LOG_INFO, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define XLOGD(fmt, ...) x_mmf_log_write(X_MMF_LOG_DEBUG, __func__, __LINE__, fmt, ##__VA_ARGS__)

#endif
