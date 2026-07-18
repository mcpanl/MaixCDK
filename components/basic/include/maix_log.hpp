/**
 * @author neucrack@sipeed
 * @copyright Sipeed Ltd 2023-
 * @license Apache 2.0
 * @update 2023.9.8: Add framework, create this file.
 */

#ifndef __MAIX_LOG_H
#define __MAIX_LOG_H

#include <stdio.h>
#include "global_config.h"
#include <stdarg.h>

namespace maix::log
{
    /**
     * Error log level enums
     * @maixpy maix.log.LogLevel
     */
     enum class LogLevel
     {
         LEVEL_NONE  = 0,
         LEVEL_ERROR,
         LEVEL_WARN,
         LEVEL_INFO,
         LEVEL_DEBUG,
         LEVEL_MAX
     };

    /**
     * print error log
     * @param fmt format string
     * @param ... args
     * @maixcdk maix.log.error
    */
    void error(const char *fmt, ...);

    /**
     * print warning log
     * @param fmt format string
     * @param ... args
     * @maixcdk maix.log.warn
    */
    void warn(const char *fmt, ...);

    /**
     * print info log
     * @param fmt format string
     * @param ... args
     * @maixcdk maix.log.info
    */
    void info(const char *fmt, ...);

    /**
     * print debug log
     * @param fmt format string
     * @param ... args
     * @maixcdk maix.log.debug
    */
    void debug(const char *fmt, ...);


    /**
     * print error log, but not add '\n' at end
     * @param fmt format string
     * @param ... args
     * @maixcdk maix.log.error0
    */
    void error0(const char *fmt, ...);

    /**
     * print warning log, but not add '\n' at end
     * @param fmt format string
     * @param ... args
     * @maixcdk maix.log.warn0
    */
    void warn0(const char *fmt, ...);

    /**
     * print info log, but not add '\n' at end
     * @param fmt format string
     * @param ... args
     * @maixcdk maix.log.info0
    */
    void info0(const char *fmt, ...);

    /**
     * print debug log, but not add '\n' at end
     * @param fmt format string
     * @param ... args
     * @maixcdk maix.log.debug0
    */
    void debug0(const char *fmt, ...);

    /**
     * same as printf, but recommend use this function instead of printf
     * this function will add prefix like "-- [E] " to log
     * @param fmt format string
     * @param ... args
     * @maixcdk maix.log.print
    */
    void print(const char *fmt, ...);

    /**
     * print log info with specified log level, no '\n' at end and no prefix like "-- [E] ".
     * this function will add prefix like "-- [E] " to log
     * @param fmt format string
     * @param ... args
     * @maixcdk maix.log.print
    */
    void print(log::LogLevel level, const char *fmt, ...);

} // namespace maix::log



#endif
