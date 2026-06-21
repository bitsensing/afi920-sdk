/**
 * @file platform.h
 * @brief Platform abstraction for socket APIs (POSIX / Winsock)
 *
 * Copyright (c) 2026 bitsensing Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    using socket_t = SOCKET;
    static constexpr socket_t kInvalidSocket = INVALID_SOCKET;

    inline int platform_socket_error() { return WSAGetLastError(); }

    inline void platform_close_socket(socket_t s) {
        if (s != kInvalidSocket) closesocket(s);
    }

    inline bool platform_init_sockets() {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }

    inline void platform_cleanup_sockets() {
        WSACleanup();
    }

    inline int platform_set_recv_timeout(socket_t s, int timeout_ms) {
        DWORD tv = static_cast<DWORD>(timeout_ms);
        return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                          reinterpret_cast<const char*>(&tv), sizeof(tv));
    }

    inline int platform_set_send_timeout(socket_t s, int timeout_ms) {
        DWORD tv = static_cast<DWORD>(timeout_ms);
        return setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
                          reinterpret_cast<const char*>(&tv), sizeof(tv));
    }

#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <errno.h>

    using socket_t = int;
    static constexpr socket_t kInvalidSocket = -1;

    inline int platform_socket_error() { return errno; }

    inline void platform_close_socket(socket_t s) {
        if (s != kInvalidSocket) ::close(s);
    }

    inline bool platform_init_sockets() { return true; }
    inline void platform_cleanup_sockets() {}

    inline int platform_set_recv_timeout(socket_t s, int timeout_ms) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    inline int platform_set_send_timeout(socket_t s, int timeout_ms) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        return setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
#endif

// ─── Thread Naming ───

#ifdef _WIN32
    #include <windows.h>
    inline void platform_set_thread_name(const char* name) {
        // SetThreadDescription requires Win10 1607+ (_WIN32_WINNT >= 0x0A00)
        #if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0A00
            // Convert narrow string to wide
            wchar_t wname[64];
            int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 64);
            if (n > 0) SetThreadDescription(GetCurrentThread(), wname);
        #else
            (void)name;  // no-op on older Windows
        #endif
    }
#elif defined(__APPLE__)
    #include <pthread.h>
    inline void platform_set_thread_name(const char* name) {
        pthread_setname_np(name);  // macOS: no thread parameter
    }
#elif defined(__linux__)
    #include <pthread.h>
    inline void platform_set_thread_name(const char* name) {
        pthread_setname_np(pthread_self(), name);  // Linux: 15-char limit
    }
#else
    inline void platform_set_thread_name(const char*) {}  // no-op
#endif

namespace afi {

/**
 * @brief RAII guard for platform socket initialization
 */
class PlatformSocketInit {
public:
    PlatformSocketInit()  { platform_init_sockets(); }
    ~PlatformSocketInit() { platform_cleanup_sockets(); }
    PlatformSocketInit(const PlatformSocketInit&) = delete;
    PlatformSocketInit& operator=(const PlatformSocketInit&) = delete;
};

} // namespace afi
