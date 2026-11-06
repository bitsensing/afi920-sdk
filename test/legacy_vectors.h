/**
 * @file legacy_vectors.h
 * @brief Interface v1.0.x frames captured from a firmware 3.0.0 sensor
 *
 * test/data/legacy_v1_0/ holds the first three RDI, SHII and SPI messages of
 * each TCP stream from 192.168.10.210 (SOME/IP header included), the
 * Discovery response, and the GetDeviceInfo answer to a v1.1.0 request.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

/// Whole file under test/data/legacy_v1_0/ (empty when missing).
inline std::vector<uint8_t> legacy_file(const char* name) {
    const std::string path = std::string(AFI_TEST_DATA_DIR) + "/legacy_v1_0/" + name;
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return out;
}

/// SOME/IP messages of a stream file, split by the SOME/IP Length field.
inline std::vector<std::vector<uint8_t>> legacy_frames(const char* name) {
    const std::vector<uint8_t> all = legacy_file(name);
    std::vector<std::vector<uint8_t>> out;
    size_t off = 0;
    while (off + 16 <= all.size()) {
        const uint32_t length = (uint32_t(all[off + 4]) << 24) | (uint32_t(all[off + 5]) << 16) |
                                (uint32_t(all[off + 6]) << 8) | all[off + 7];
        if (off + 8 + length > all.size()) break;
        out.emplace_back(all.begin() + off, all.begin() + off + 8 + length);
        off += 8 + length;
    }
    return out;
}
