/**
 * @file sample_discovery.cpp
 * @brief Sample: Discover AFI920 sensors on the network (multi-LAN)
 *
 * Enumerates all local network interfaces and sends a UDP broadcast
 * Discovery request from each one, collecting responses across all LANs.
 *
 * Usage: sample_discovery [timeout_ms] [bind_ip]
 *
 *   If bind_ip is given, discovery is limited to that single interface.
 *   Otherwise, all non-loopback interfaces are scanned automatically.
 */
#include <afi/afi_sdk.h>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

struct LocalInterface {
    std::string ip;
    std::string name;
};

/// Enumerate local non-loopback IPv4 interfaces
static std::vector<LocalInterface> enumerate_local_ipv4() {
    std::vector<LocalInterface> results;

#ifdef _WIN32
    ULONG size = 15000;
    PIP_ADAPTER_ADDRESSES addrs = (PIP_ADAPTER_ADDRESSES)malloc(size);
    if (GetAdaptersAddresses(AF_INET, 0, nullptr, addrs, &size) == ERROR_SUCCESS) {
        for (auto a = addrs; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            for (auto u = a->FirstUnicastAddress; u; u = u->Next) {
                if (u->Address.lpSockaddr->sa_family == AF_INET) {
                    auto* sa = (sockaddr_in*)u->Address.lpSockaddr;
                    char buf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
                    std::string ip(buf);
                    if (ip.substr(0, 4) != "127.") {
                        char name_buf[256];
                        WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1,
                                            name_buf, sizeof(name_buf), nullptr, nullptr);
                        results.push_back({ip, name_buf});
                    }
                }
            }
        }
    }
    free(addrs);
#else
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == 0) {
        for (auto* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;

            auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
            results.push_back({buf, ifa->ifa_name ? ifa->ifa_name : ""});
        }
        freeifaddrs(ifaddr);
    }
#endif

    return results;
}

static void print_sensor(size_t idx, const afi::AfiSensorInfo& s) {
    printf("[Sensor %zu]\n", idx);
    printf("  IP:             %s:%d\n", s.ip.c_str(), s.port);
    printf("  Serial:         %s\n", s.serial_number.c_str());
    printf("  Model:          %s\n", s.model.c_str());
    printf("  SW Version:     %s (bl=%s drv=%s fw=%s)\n", s.software_version.c_str(),
           s.bootloader_hash.c_str(), s.driver_hash.c_str(), s.fw_hash.c_str());
    printf("  HW Version:     %s\n", s.hw_version.c_str());
    printf("  MAC:            %s\n", s.mac_address.c_str());
    printf("  Subnet:         %s\n", s.subnet_mask.c_str());
    printf("  Gateway:        %s\n", s.gateway.c_str());
    printf("  Range Mode:     %s (%d)\n", s.range_mode_name.c_str(),
           static_cast<int>(s.range_mode));
    printf("  Position Code:  %d\n", s.position_code);
    printf("  Mounting:       (%.3f, %.3f, %.3f) m\n",
           s.offset_x, s.offset_y, s.offset_z);
    printf("  Orientation:    yaw=%.1f pitch=%.1f roll=%.1f deg\n",
           s.yaw_deg, s.pitch_deg, s.roll_deg);
    printf("  Ports:          det=%d health=%d perf=%d\n",
           s.detection_port, s.health_port, s.performance_port);
    printf("  Host IP:        %s\n", s.host_ip.c_str());
    printf("  Det IP:         %s\n", s.detection_ip.c_str());
    printf("\n");
}

int main(int argc, char* argv[]) {
    int timeout_ms = 3000;
    std::string bind_ip;
    if (argc > 1) timeout_ms = std::atoi(argv[1]);
    if (argc > 2) bind_ip = argv[2];

    // Determine interfaces to scan
    std::vector<LocalInterface> interfaces;
    if (!bind_ip.empty()) {
        interfaces.push_back({bind_ip, "user-specified"});
    } else {
        interfaces = enumerate_local_ipv4();
        if (interfaces.empty()) {
            interfaces.push_back({"", "default"});
        }
    }

    printf("Discovering AFI920 sensors (timeout: %d ms, %zu interface(s))...\n\n",
           timeout_ms, interfaces.size());

    // Discover on each interface, dedup by ip+serial
    std::vector<afi::AfiSensorInfo> all_sensors;
    std::set<std::string> seen_keys;

    for (const auto& iface : interfaces) {
        std::string label = iface.name.empty()
                            ? (iface.ip.empty() ? "any" : iface.ip)
                            : iface.name + " (" + iface.ip + ")";
        printf("  Scanning on %s ...\n", label.c_str());

        auto found = afi::AfiSdk::Discover(timeout_ms, 30520, iface.ip);

        for (auto& s : found) {
            std::string key = s.ip + ":" + s.serial_number;
            if (seen_keys.find(key) == seen_keys.end()) {
                seen_keys.insert(key);
                all_sensors.push_back(std::move(s));
            }
        }

        printf("    -> %zu response(s)\n", found.size());
    }

    printf("\nTotal unique sensors found: %zu\n\n", all_sensors.size());

    if (all_sensors.empty()) {
        printf("No sensors found.\n");
        return 0;
    }

    for (size_t i = 0; i < all_sensors.size(); i++) {
        print_sensor(i + 1, all_sensors[i]);
    }

    return 0;
}
