// Copyright (c) 2026 bitsensing Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
#include "util/logger.h"
#include <cstdarg>
#include <cstdio>

namespace afi {

LogLevel Logger::level_ = LogLevel::kInfo;

void Logger::SetLevel(LogLevel level) {
    level_ = level;
}

LogLevel Logger::GetLevel() {
    return level_;
}

void Logger::Log(LogLevel level, const char* fmt, ...) {
    if (level < level_) return;

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

} // namespace afi
