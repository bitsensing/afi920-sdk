// point_cloud_view.hpp — OpenGL 3D point-cloud widget (one colour per sensor,
// grid + X/Y/Z axes, sensor markers + boresight lines, orbit camera).
//
// Copyright (c) 2026 bitsensing Inc.
#pragma once

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>

#include <vector>

#include "frame_store.hpp"

namespace viewer {

enum class ColorMode { Sensor, Velocity, Snr };

class PointCloudView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit PointCloudView(QWidget* parent = nullptr);
    ~PointCloudView() override;

    // Called from the GUI thread with the latest store snapshot.
    void setFrame(const std::vector<SensorSnapshot>& snap, ColorMode mode,
                  float pointSize);
    void resetCamera();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    struct Mesh {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        int count = 0;
    };
    void initMesh(Mesh& m);
    void uploadMesh(Mesh& m, const std::vector<float>& data);
    void drawMesh(Mesh& m, unsigned int mode);

    QOpenGLShaderProgram program_;
    Mesh staticLines_, points_, markers_, dynLines_;

    // CPU-side vertex data (interleaved x,y,z,r,g,b,a); uploaded in paintGL.
    std::vector<float> pointsData_, markersData_, dynLinesData_;
    bool dirty_ = false;
    float pointSize_ = 4.0f;

    // Orbit camera (around target_).
    float az_ = 125.0f;
    float el_ = 22.0f;
    float dist_ = 45.0f;
    QVector3D target_{8.0f, 0.0f, 1.0f};
    QPoint lastMouse_;
};

}  // namespace viewer
