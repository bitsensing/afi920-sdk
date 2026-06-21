/**
 * @file logger.h
 * @brief Simple SDK internal logger
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <cstdio>

namespace afi {

enum class LogLevel {
    kDebug   = 0,
    kInfo    = 1,
    kWarning = 2,
    kError   = 3,
    kNone    = 4,
};

class Logger {
public:
    static void SetLevel(LogLevel level);
    static LogLevel GetLevel();
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
    static void Log(LogLevel level, const char* fmt, ...);

private:
    static LogLevel level_;
};

} // namespace afi

#define AFI_LOG_DEBUG(fmt, ...)   afi::Logger::Log(afi::LogLevel::kDebug,   "[AFI-DBG] " fmt "\n", ##__VA_ARGS__)
#define AFI_LOG_INFO(fmt, ...)    afi::Logger::Log(afi::LogLevel::kInfo,    "[AFI-INF] " fmt "\n", ##__VA_ARGS__)
#define AFI_LOG_WARN(fmt, ...)    afi::Logger::Log(afi::LogLevel::kWarning, "[AFI-WRN] " fmt "\n", ##__VA_ARGS__)
#define AFI_LOG_ERROR(fmt, ...)   afi::Logger::Log(afi::LogLevel::kError,   "[AFI-ERR] " fmt "\n", ##__VA_ARGS__)
