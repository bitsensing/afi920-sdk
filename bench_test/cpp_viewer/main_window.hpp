// main_window.hpp — Qt main window: 3D view + per-sensor status panels +
// record / replay toolbar.
//
// Copyright (c) 2026 bitsensing Inc.
#pragma once

#include <QMainWindow>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "frame_store.hpp"
#include "radar_source.hpp"

class QLabel;
class QPushButton;
class QSlider;
class QComboBox;
class QVBoxLayout;
class QTimer;

namespace viewer {

class PointCloudView;

struct ViewerArgs {
    std::vector<std::string> sensors;
    std::vector<std::string> streams;
    std::string transport = "tcp";
    std::string host_ip;
    bool sim = false;
    std::string replay;
    bool no_configure = false;
    std::string outdir;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(ViewerArgs args, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void tick();
    void toggleRecord();
    void pickReplay();
    void togglePause();
    void stopReplay();
    void onSpeed(int value);

private:
    void buildUi();
    void rebuildSensorBoxes(const std::vector<std::string>& ips);
    void setSensorCount(const std::vector<std::string>& ips);
    void startInitial();
    void stopSource();
    void openReplay(const std::string& path);
    QString fmtHealth(const SensorSnapshot& s) const;
    QString fmtPerf(const SensorSnapshot& s) const;

    ViewerArgs args_;
    std::vector<std::string> origSensors_;
    std::vector<std::string> sensors_;
    std::unique_ptr<FrameStore> store_;
    RecordController recCtrl_;
    std::unique_ptr<Source> source_;
    std::string mode_;

    PointCloudView* view_ = nullptr;
    QVBoxLayout* panelLayout_ = nullptr;
    std::vector<std::pair<QLabel*, QLabel*>> sensorBoxes_;
    QPushButton* btnRecord_ = nullptr;
    QPushButton* btnReplay_ = nullptr;
    QPushButton* btnPause_ = nullptr;
    QPushButton* btnStop_ = nullptr;
    QSlider* speed_ = nullptr;
    QSlider* pointSize_ = nullptr;
    QComboBox* colorMode_ = nullptr;
    QLabel* speedLbl_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* timer_ = nullptr;
};

}  // namespace viewer
