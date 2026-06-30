// main.cpp — AFI920 C++ 3D radar viewer entry point.
//
// Usage:
//   afi_radar_viewer [SENSOR_IP ...] [--streams rdi shii spi]
//                    [--transport tcp|udp] [--host-ip IP]
//                    [--sim] [--replay FILE] [--no-configure] [--outdir DIR]
//
// Copyright (c) 2026 bitsensing Inc.
#include <QApplication>
#include <QSurfaceFormat>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "main_window.hpp"

int main(int argc, char* argv[]) {
    viewer::ViewerArgs args;
    args.sensors = {"192.168.10.150", "192.168.10.151", "192.168.10.152"};
    args.streams = {"rdi", "shii", "spi"};
    args.outdir = "recordings";

    std::vector<std::string> sensors;
    std::vector<std::string> streams;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* def) -> std::string {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string(def);
        };
        if (a == "--streams") {
            // consume following tokens until the next option
            while (i + 1 < argc && argv[i + 1][0] != '-')
                streams.push_back(argv[++i]);
        } else if (a == "--transport") {
            args.transport = next("tcp");
        } else if (a == "--host-ip") {
            args.host_ip = next("");
        } else if (a == "--replay") {
            args.replay = next("");
        } else if (a == "--outdir") {
            args.outdir = next("recordings");
        } else if (a == "--sim") {
            args.sim = true;
        } else if (a == "--no-configure") {
            args.no_configure = true;
        } else if (a == "--help" || a == "-h") {
            std::printf(
                "Usage: afi_radar_viewer [SENSOR_IP ...] [--streams rdi shii spi]\n"
                "       [--transport tcp|udp] [--host-ip IP] [--sim]\n"
                "       [--replay FILE] [--no-configure] [--outdir DIR]\n");
            return 0;
        } else if (!a.empty() && a[0] != '-') {
            sensors.push_back(a);
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
            return 2;
        }
    }
    if (!sensors.empty()) args.sensors = sensors;
    if (!streams.empty()) args.streams = streams;

    if (args.transport == "udp" && args.host_ip.empty() && !args.sim &&
        args.replay.empty()) {
        std::fprintf(stderr, "ERROR: --transport udp requires --host-ip\n");
        return 2;
    }

    // Request an OpenGL 3.3 core profile with a depth buffer before any widget.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    viewer::MainWindow win(std::move(args));
    win.show();
    return app.exec();
}
