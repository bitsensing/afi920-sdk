/**
 * @file test_firmware_live.cpp
 * @brief Integration test: C++ firmware update chain (requires a live sensor)
 *
 * DESTRUCTIVE: writes the bootloader in place and switches firmware faces.
 *
 *  1. FirmwareUpdateStart refused in the normal access mode.
 *  2. Bootloader: START, CHUNK honouring received_bytes, COMMIT, STATUS until
 *     idle, FirmwareUpdateReboot; the device reports the expected hash and the
 *     active firmware face is unchanged.
 *  3. FirmwareSwitchSlot to the other face and back, each followed by a
 *     reboot; Boot-Info reports the requested face.
 *
 * The image is the plain bootloader component (decrypted bundle contents).
 *
 * Usage: test_firmware_live <sensor_ip> <bootloader.bin> <bootloader_hash> --yes-flash
 */
#include <afi/afi_sdk.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failed = 0;

void check(bool ok, const char* name, const std::string& detail = "") {
    printf("  %s: %s%s%s\n", ok ? "PASS" : "FAIL", name,
           detail.empty() ? "" : " - ", detail.c_str());
    if (!ok) ++g_failed;
}

std::string rc_str(int rc) {
    return std::string(afi::AfiErrorStr(rc)) + " (" + std::to_string(rc) + ")";
}

uint32_t crc32_ieee(const std::vector<uint8_t>& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t b : data) {
        crc ^= b;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool wait_idle(afi::AfiSdk& sdk, int timeout_s, afi::AfiOtaStatus& st) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    while (std::chrono::steady_clock::now() < end) {
        if (sdk.FirmwareUpdateStatus(st) == afi::kOk &&
            (st.state == "idle" || st.state == "error")) {
            return st.state == "idle";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

// The reboot answer precedes the reset; wait for a fresh uptime.
bool reboot_and_reconnect(afi::AfiSdk& sdk) {
    int rc = sdk.FirmwareUpdateReboot();
    check(rc == afi::kOk, "FirmwareUpdateReboot", rc_str(rc));
    for (int i = 0; i < 60; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (sdk.ReconnectConfigApi(2000) != afi::kOk) continue;
        afi::AfiSensorStatus s;
        if (sdk.GetSensorStatus(s) == afi::kOk && s.uptime_sec < 30) {
            check(true, "sensor back after reboot",
                  "uptime " + std::to_string(s.uptime_sec) + " s");
            return true;
        }
    }
    check(false, "sensor back after reboot");
    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 5 || std::string(argv[4]) != "--yes-flash") {
        printf("Usage: %s <sensor_ip> <bootloader.bin> <bootloader_hash> --yes-flash\n",
               argv[0]);
        return 2;
    }
    std::ifstream f(argv[2], std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    const std::string want_hash = argv[3];
    if (image.empty()) {
        printf("cannot read %s\n", argv[2]);
        return 2;
    }

    afi::AfiSdkConfig config;
    config.sensor_ip = argv[1];
    config.enable_rdi = false;
    config.enable_shi = false;
    config.enable_spi = false;
    afi::AfiSdk sdk;
    if (sdk.Init(config) != afi::kOk) {
        printf("Init failed - sensor unreachable at %s\n", argv[1]);
        return 1;
    }

    int rc = afi::kOk;
    afi::AfiBootInfo boot0;
    rc = sdk.GetBootInfo(boot0);
    check(rc == afi::kOk, "GetBootInfo", "active " + boot0.fw_update_active);

    // 1. Normal mode refuses the update chain.
    afi::AfiOtaStartRequest req;
    req.update_type = "bootloader";
    req.component = "bootloader";
    req.total_size = static_cast<uint32_t>(image.size());
    char crc[16];
    snprintf(crc, sizeof(crc), "0x%08X", crc32_ieee(image));
    req.crc32 = crc;
    afi::AfiOtaStartResponse start;
    rc = sdk.FirmwareUpdateStart(req, start);
    check(rc == afi::kSessionRequired, "START refused in Normal", rc_str(rc));

    // 2. Bootloader in place.
    rc = sdk.EnterMode(afi::AfiAccessMode::kProgramming);
    check(rc == afi::kOk, "EnterMode(Programming)", rc_str(rc));
    rc = sdk.FirmwareUpdateStart(req, start);
    check(rc == afi::kOk, "FirmwareUpdateStart(bootloader)",
          "max_chunk " + std::to_string(start.max_chunk_size) + " slot " + start.target_slot);
    if (rc != afi::kOk) return 1;
    uint32_t offset = 0;
    int chunks = 0, partial = 0;
    while (offset < image.size()) {
        const size_t len = std::min<size_t>(start.max_chunk_size, image.size() - offset);
        afi::AfiOtaChunkResult cr;
        rc = sdk.FirmwareDataChunk(image.data() + offset, len, &cr);
        if (rc != afi::kOk) break;
        ++chunks;
        if (cr.received_bytes < offset + len) ++partial;
        offset = cr.received_bytes;
    }
    check(rc == afi::kOk && offset == image.size(), "all chunks accepted",
          std::to_string(offset) + " B in " + std::to_string(chunks) + " chunks, " +
          std::to_string(partial) + " partly accepted");
    rc = sdk.FirmwareUpdateCommit();
    check(rc == afi::kOk, "FirmwareUpdateCommit", rc_str(rc));
    afi::AfiOtaStatus st;
    const bool idle = wait_idle(sdk, 120, st);
    check(idle, "commit reached idle", st.state + " " + st.error_message);
    afi::AfiBootInfo boot;
    sdk.GetBootInfo(boot);
    check(boot.fw_update_active == boot0.fw_update_active,
          "bootloader write leaves the firmware face", boot.fw_update_active);
    if (!reboot_and_reconnect(sdk)) return 1;
    afi::AfiDeviceInfo dev;
    sdk.GetDeviceInfo(dev);
    check(dev.bootloader_hash.rfind(want_hash, 0) == 0, "bootloader_hash after reboot",
          dev.bootloader_hash);

    // 3. Switch to the other face and back.
    const std::string home = boot0.fw_update_active;
    const std::string other = home == "A" ? "B" : "A";
    for (const std::string& face : {other, home}) {
        rc = sdk.EnterMode(afi::AfiAccessMode::kProgramming);
        check(rc == afi::kOk, "EnterMode(Programming)", rc_str(rc));
        afi::AfiOtaSwitchSlotResult sw;
        rc = sdk.FirmwareSwitchSlot("firmware", sw);
        check(rc == afi::kOk && sw.new_active == face, "FirmwareSwitchSlot",
              sw.previous_active + " -> " + sw.new_active);
        const bool settled = wait_idle(sdk, 60, st);
        check(settled, "switch reached idle", st.state);
        if (!reboot_and_reconnect(sdk)) return 1;
        sdk.GetBootInfo(boot);
        sdk.GetDeviceInfo(dev);
        check(boot.fw_update_active == face && boot.boot_info_status == "ok",
              "running the requested face",
              boot.fw_update_active + " fw_hash " + dev.fw_hash);
    }

    sdk.Stop();
    printf("\n%s (%d failures)\n", g_failed ? "FAILED" : "ALL PASS", g_failed);
    return g_failed ? 1 : 0;
}
