// main_window.cpp — Qt main window implementation.
//
// Copyright (c) 2026 bitsensing Inc.
#include "main_window.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <exception>

#include "point_cloud_view.hpp"
#include "jsonl_recording.hpp"

namespace viewer {

namespace {

QColor sensorColor(std::size_t i) {
    static const float P[6][3] = {
        {1.00f, 0.30f, 0.30f}, {0.35f, 1.00f, 0.45f}, {0.35f, 0.65f, 1.00f},
        {1.00f, 0.85f, 0.25f}, {0.90f, 0.45f, 1.00f}, {0.30f, 1.00f, 1.00f}};
    const float* c = P[i % 6];
    return QColor::fromRgbF(c[0], c[1], c[2]);
}

// ── ISO 23150 SHII enum decode (raw value -> human label; nullptr if unknown) ──
const char* shiiDefect(int v) {
    switch (v) { case 0: return "fully functional"; case 1: return "not fully functional";
                 case 2: return "out of order"; default: return nullptr; }
}
const char* shiiReason(int v) {
    switch (v) { case 0: return "none"; case 1: return "internal memory";
                 case 2: return "HW defect"; case 3: return "thermal";
                 case 4: return "comm error"; case 5: return "calibration";
                 case 6: return "config"; case 7: return "mechanical";
                 case 8: return "software"; case 9: return "computing power";
                 case 10: return "out of time-sync"; case 11: return "ext. disturbed";
                 default: return nullptr; }
}
const char* shiiSupply(int v) {
    switch (v) { case 0: return "low"; case 1: return "pre-low";
                 case 2: return "within limits"; case 3: return "pre-high";
                 case 4: return "high"; default: return nullptr; }
}
const char* shiiTemp(int v) {
    switch (v) { case 0: return "under-temp"; case 1: return "pre-under-temp";
                 case 2: return "in limits"; case 3: return "pre-over-temp";
                 case 4: return "over-temp"; default: return nullptr; }
}
const char* shiiTimeSync(int v) {
    switch (v) { case 0: return "within limits"; case 1: return "out of limits";
                 case 2: return "timeout"; case 3: return "not synchronized";
                 default: return nullptr; }
}
const char* shiiOpMode(int v) {
    switch (v) { case 0: return "DR"; case 1: return "LR"; case 2: return "MR";
                 case 3: return "SR"; case 4: return "ULR"; case 5: return "USR";
                 case 10: return "degradation"; case 50: return "evaluation";
                 case 100: return "calibration"; case 200: return "initialising";
                 case 201: return "test"; default: return nullptr; }
}
const char* shiiCalStatus(int v) {
    switch (v) { case 0: return "calibrated"; case 1: return "not calibrated";
                 case 2: return "degraded"; default: return nullptr; }
}

QString enumName(const char* name, int value) {
    return name ? QString::fromLatin1(name) : QString("#%1").arg(value);
}

// `label: NAME` — green when value == okValue, else red.
QString enumSpan(const QString& label, int value, const char* name, int okValue) {
    const char* color = (value == okValue) ? "#6bd66b" : "#ff6b6b";
    return QString("%1: <span style='color:%2'>%3</span>")
        .arg(label).arg(QString(color)).arg(enumName(name, value));
}

}  // namespace

MainWindow::MainWindow(ViewerArgs args, QWidget* parent)
    : QMainWindow(parent), args_(std::move(args)) {
    origSensors_ = args_.sensors;
    sensors_ = args_.sensors;
    store_ = std::make_unique<FrameStore>(static_cast<int>(sensors_.size()));

    setWindowTitle("AFI920 Multi-Sensor 3D Radar Viewer (C++)");
    resize(1500, 900);
    buildUi();

    if (!args_.replay.empty()) {
        openReplay(args_.replay);
    } else {
        startInitial();
    }

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::tick);
    timer_->start(33);  // ~30 Hz
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    auto* central = new QWidget;
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);

    auto* split = new QSplitter(Qt::Horizontal);
    root->addWidget(split, 1);

    view_ = new PointCloudView;
    split->addWidget(view_);

    auto* right = new QWidget;
    right->setMinimumWidth(360);
    right->setMaximumWidth(480);
    auto* rlay = new QVBoxLayout(right);
    rlay->addWidget(new QLabel("<b>Sensor status</b>"));
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* inner = new QWidget;
    panelLayout_ = new QVBoxLayout(inner);
    scroll->setWidget(inner);
    rlay->addWidget(scroll, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);

    rebuildSensorBoxes(sensors_);

    // Toolbar
    auto* bar = new QHBoxLayout;
    btnRecord_ = new QPushButton(QString::fromUtf8("\xE2\x97\x8F Record"));
    btnReplay_ = new QPushButton(QString::fromUtf8("\xE2\x96\xB6 Replay\xE2\x80\xA6"));
    btnPause_ = new QPushButton(QString::fromUtf8("\xE2\x8F\xB8 Pause"));
    btnStop_ = new QPushButton(QString::fromUtf8("\xE2\x8F\xB9 Stop replay"));
    btnPause_->setEnabled(false);
    btnStop_->setEnabled(false);
    connect(btnRecord_, &QPushButton::clicked, this, &MainWindow::toggleRecord);
    connect(btnReplay_, &QPushButton::clicked, this, &MainWindow::pickReplay);
    connect(btnPause_, &QPushButton::clicked, this, &MainWindow::togglePause);
    connect(btnStop_, &QPushButton::clicked, this, &MainWindow::stopReplay);
    bar->addWidget(btnRecord_);
    bar->addSpacing(12);
    bar->addWidget(btnReplay_);
    bar->addWidget(btnPause_);
    bar->addWidget(btnStop_);
    bar->addSpacing(12);

    bar->addWidget(new QLabel("speed"));
    speed_ = new QSlider(Qt::Horizontal);
    speed_->setMinimum(25);
    speed_->setMaximum(400);
    speed_->setValue(100);
    speed_->setFixedWidth(110);
    connect(speed_, &QSlider::valueChanged, this, &MainWindow::onSpeed);
    speedLbl_ = new QLabel("1.0x");
    bar->addWidget(speed_);
    bar->addWidget(speedLbl_);
    bar->addSpacing(12);

    bar->addWidget(new QLabel("colour"));
    colorMode_ = new QComboBox;
    colorMode_->addItems({"sensor", "velocity", "snr"});
    bar->addWidget(colorMode_);

    bar->addWidget(new QLabel("size"));
    pointSize_ = new QSlider(Qt::Horizontal);
    pointSize_->setMinimum(1);
    pointSize_->setMaximum(10);
    pointSize_->setValue(4);
    pointSize_->setFixedWidth(80);
    bar->addWidget(pointSize_);

    auto* resetBtn = new QPushButton("reset view");
    connect(resetBtn, &QPushButton::clicked, this,
            [this]() { view_->resetCamera(); });
    bar->addWidget(resetBtn);

    bar->addStretch(1);
    status_ = new QLabel("");
    bar->addWidget(status_);
    root->addLayout(bar);
}

void MainWindow::rebuildSensorBoxes(const std::vector<std::string>& ips) {
    while (QLayoutItem* item = panelLayout_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    sensorBoxes_.clear();
    for (std::size_t i = 0; i < ips.size(); ++i) {
        auto* box = new QGroupBox(QString::fromUtf8("\xE2\x97\x8F ") +
                                  QString::fromStdString(ips[i]));
        const QString hex = sensorColor(i).name();
        box->setStyleSheet(
            QString("QGroupBox{border:2px solid %1;border-radius:5px;"
                    "margin-top:8px;font-weight:bold;}"
                    "QGroupBox::title{subcontrol-origin:margin;left:8px;"
                    "color:%1;}").arg(hex));
        auto* blay = new QVBoxLayout(box);
        auto* health = new QLabel(QString::fromUtf8("waiting\xE2\x80\xA6"));
        health->setTextFormat(Qt::RichText);
        health->setWordWrap(true);
        auto* perf = new QLabel("");
        perf->setTextFormat(Qt::RichText);
        perf->setWordWrap(true);
        blay->addWidget(health);
        blay->addWidget(perf);
        panelLayout_->addWidget(box);
        sensorBoxes_.emplace_back(health, perf);
    }
    panelLayout_->addStretch(1);
}

void MainWindow::setSensorCount(const std::vector<std::string>& ips) {
    sensors_ = ips;
    store_ = std::make_unique<FrameStore>(static_cast<int>(ips.size()));
    rebuildSensorBoxes(ips);
}

void MainWindow::startInitial() {
    stopSource();
    if (sensors_ != origSensors_) setSensorCount(origSensors_);
    if (args_.sim) {
        auto src = std::make_unique<SimSource>(sensors_, args_.streams, *store_,
                                               recCtrl_);
        src->start();
        source_ = std::move(src);
        mode_ = "SIM";
    } else {
        if (status_) status_->setText("connecting to sensors\xE2\x80\xA6");
        QApplication::processEvents();
        auto src = std::make_unique<LiveSource>(
            sensors_, args_.streams, args_.transport, args_.host_ip, *store_,
            recCtrl_, !args_.no_configure);
        src->start();
        source_ = std::move(src);
        mode_ = "LIVE";
    }
    btnPause_->setEnabled(false);
    btnStop_->setEnabled(false);
    btnRecord_->setEnabled(true);
}

void MainWindow::stopSource() {
    if (source_) {
        source_->stop();
        source_.reset();
    }
}

void MainWindow::pickReplay() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open recording", QString::fromStdString(args_.outdir),
        "AFI recordings (*.jsonl);;All files (*)");
    if (!path.isEmpty()) openReplay(path.toStdString());
}

void MainWindow::openReplay(const std::string& path) {
    std::unique_ptr<JsonlReplayReader> reader;
    try {
        reader = std::make_unique<JsonlReplayReader>(path);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Replay",
                             QString("Cannot open:\n%1").arg(e.what()));
        return;
    }
    if (recCtrl_.active()) toggleRecord();  // stop recording before replay
    stopSource();

    std::vector<std::string> ips;
    for (const auto& s : reader->meta().sensors) ips.push_back(s.ip);
    if (!ips.empty()) setSensorCount(ips);

    RecMeta meta = reader->meta();
    auto rs = std::make_unique<ReplaySource>(std::move(meta),
                                             reader->takeRecords(), *store_,
                                             speed_->value() / 100.0);
    rs->start();
    source_ = std::move(rs);
    mode_ = "REPLAY";
    btnPause_->setEnabled(true);
    btnPause_->setText(QString::fromUtf8("\xE2\x8F\xB8 Pause"));
    btnStop_->setEnabled(true);
    btnRecord_->setEnabled(false);
}

void MainWindow::stopReplay() {
    btnRecord_->setEnabled(true);
    startInitial();
}

void MainWindow::togglePause() {
    if (auto* rs = dynamic_cast<ReplaySource*>(source_.get())) {
        if (rs->paused()) {
            rs->resume();
            btnPause_->setText(QString::fromUtf8("\xE2\x8F\xB8 Pause"));
        } else {
            rs->pause();
            btnPause_->setText(QString::fromUtf8("\xE2\x96\xB6 Resume"));
        }
    }
}

void MainWindow::onSpeed(int value) {
    const double s = value / 100.0;
    speedLbl_->setText(QString::number(s, 'f', 2) + "x");
    if (auto* rs = dynamic_cast<ReplaySource*>(source_.get())) rs->setSpeed(s);
}

void MainWindow::toggleRecord() {
    if (recCtrl_.active()) {
        recCtrl_.stop();
        btnRecord_->setText(QString::fromUtf8("\xE2\x97\x8F Record"));
        btnRecord_->setStyleSheet("");
        return;
    }
    if (!source_ || !source_->canRecord()) {
        QMessageBox::information(this, "Record",
                                 "Recording is only available in live/sim mode.");
        return;
    }
    QDir().mkpath(QString::fromStdString(args_.outdir));
    const QString stamp =
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    const std::string path =
        args_.outdir + "/rec_" + stamp.toStdString() + ".jsonl";
    try {
        recCtrl_.start(path, source_->recordingMeta());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Record",
                             QString("Cannot record:\n%1").arg(e.what()));
        return;
    }
    btnRecord_->setText(QString::fromUtf8("\xE2\x96\xA0 Stop rec"));
    btnRecord_->setStyleSheet("background-color:#aa3333;color:white;");
}

void MainWindow::tick() {
    if (!store_) return;
    auto snap = store_->snapshot();

    ColorMode cm = ColorMode::Sensor;
    if (colorMode_->currentIndex() == 1) cm = ColorMode::Velocity;
    else if (colorMode_->currentIndex() == 2) cm = ColorMode::Snr;
    view_->setFrame(snap, cm, static_cast<float>(pointSize_->value()));

    std::size_t totalPts = 0;
    for (std::size_t i = 0; i < snap.size(); ++i) {
        totalPts += snap[i].cloud.points.size();
        if (i < sensorBoxes_.size()) {
            sensorBoxes_[i].first->setText(fmtHealth(snap[i]));
            sensorBoxes_[i].second->setText(fmtPerf(snap[i]));
        }
    }

    QString extra;
    if (auto* rs = dynamic_cast<ReplaySource*>(source_.get())) {
        extra = QString("  replay %1%%").arg(int(rs->progress() * 100));
        if (rs->finished()) extra += " (end)";
    }
    const QString rec = recCtrl_.active() ? "  \xE2\x97\x8F""REC" : "";
    status_->setText(QString::fromStdString(mode_) + rec +
                     QString("   points=%1").arg(totalPts) + extra);
}

QString MainWindow::fmtHealth(const SensorSnapshot& s) const {
    QStringList lines;
    const QString conn = s.connected
        ? "<span style='color:#6bd66b'>connected</span>"
        : "<span style='color:#888'>\xE2\x80\x94</span>";
    lines << QString("<b>conn</b> %1 &nbsp; <b>fps</b> %2")
                 .arg(conn).arg(s.fps, 0, 'f', 1);
    lines << QString("<b>RDI</b> det=%1  frame#%2 (\xC3\x97%3)")
                 .arg(s.cloud.n).arg(s.cloud.counter).arg(s.cloud.frames);
    if (s.health.valid) {
        const auto& h = s.health;
        lines << "<b>Health (SHII)</b>";
        lines << enumSpan("defect", h.defect, shiiDefect(h.defect), 0);
        lines << enumSpan("reason", h.reason, shiiReason(h.reason), 0);
        lines << enumSpan("supply", h.supply, shiiSupply(h.supply), 2) + " &nbsp; " +
                     enumSpan("temp", h.temp, shiiTemp(h.temp), 2);
        lines << enumSpan("time_sync", h.time_sync, shiiTimeSync(h.time_sync), 0);

        QStringList mm;
        for (int m : h.modes) mm << enumName(shiiOpMode(m), m);
        lines << "modes: " + (mm.isEmpty() ? QString("&mdash;") : mm.join(", "));

        QStringList cc;
        bool calOk = true;
        for (int c : h.cal) {
            cc << enumName(shiiCalStatus(c), c);
            if (c != 0) calOk = false;
        }
        const char* calColor = calOk ? "#6bd66b" : "#ff6b6b";
        lines << QString("cal: <span style='color:%1'>%2</span> &nbsp; msg#%3")
                     .arg(QString(calColor),
                          cc.isEmpty() ? QString("&mdash;") : cc.join(", "))
                     .arg(h.counter);
    } else {
        lines << "<span style='color:#888'>no health yet</span>";
    }
    return lines.join("<br>");
}

QString MainWindow::fmtPerf(const SensorSnapshot& s) const {
    if (!s.perf.valid) return "<span style='color:#888'>no performance yet</span>";
    const auto& p = s.perf;
    const double r2d = 180.0 / 3.14159265358979323846;
    QStringList lines;
    lines << "<b>Performance (SPI)</b>";
    lines << QString("pose xyz=(%1, %2, %3) m")
                 .arg(p.pose[0], 0, 'f', 2).arg(p.pose[1], 0, 'f', 2)
                 .arg(p.pose[2], 0, 'f', 2);
    lines << QString("ypr=(%1, %2, %3)\xC2\xB0")
                 .arg(p.pose[3] * r2d, 0, 'f', 1)
                 .arg(p.pose[4] * r2d, 0, 'f', 1)
                 .arg(p.pose[5] * r2d, 0, 'f', 1);
    if (!p.fov.empty()) {
        int blocked = 0;
        for (const auto& f : p.fov)
            if (f.blockage != 0) ++blocked;
        const char* color = blocked ? "#ff6b6b" : "#6bd66b";
        lines << QString("FoV blockage: <span style='color:%1'>%2/%3 blocked"
                         "</span>").arg(color).arg(blocked).arg(p.fov.size());
    }
    lines << QString("msg#%1").arg(p.counter);
    return lines.join("<br>");
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (timer_) timer_->stop();
    recCtrl_.stop();
    stopSource();
    QMainWindow::closeEvent(e);
    // We disabled quitOnLastWindowClosed (see main.cpp), so closing the main
    // window must quit the app explicitly.
    QApplication::quit();
}

}  // namespace viewer
