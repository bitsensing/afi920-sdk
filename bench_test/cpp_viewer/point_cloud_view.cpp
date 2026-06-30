// point_cloud_view.cpp — OpenGL renderer implementation.
//
// Copyright (c) 2026 bitsensing Inc.
#include "point_cloud_view.hpp"

#include <QDebug>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace viewer {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr int kStride = 7;  // x,y,z,r,g,b,a

const float* sensorPalette(std::size_t i) {
    static const float P[6][4] = {
        {1.00f, 0.30f, 0.30f, 1.0f}, {0.35f, 1.00f, 0.45f, 1.0f},
        {0.35f, 0.65f, 1.00f, 1.0f}, {1.00f, 0.85f, 0.25f, 1.0f},
        {0.90f, 0.45f, 1.00f, 1.0f}, {0.30f, 1.00f, 1.00f, 1.0f}};
    return P[i % 6];
}

void valueToRgb(float v, float vmin, float vmax, float& r, float& g, float& b) {
    static const float C[6][3] = {{0, 0, 0.6f}, {0, 0.4f, 1}, {0, 1, 1},
                                  {0.2f, 1, 0.2f}, {1, 1, 0}, {1, 0.2f, 0}};
    float t = (vmax <= vmin) ? 0.0f : (v - vmin) / (vmax - vmin);
    t = std::min(std::max(t, 0.0f), 1.0f);
    float f = t * 5.0f;
    int lo = static_cast<int>(f);
    int hi = std::min(lo + 1, 5);
    float fr = f - lo;
    r = C[lo][0] * (1 - fr) + C[hi][0] * fr;
    g = C[lo][1] * (1 - fr) + C[hi][1] * fr;
    b = C[lo][2] * (1 - fr) + C[hi][2] * fr;
}

void push7(std::vector<float>& out, float x, float y, float z, const float* c) {
    out.insert(out.end(), {x, y, z, c[0], c[1], c[2], c[3]});
}

QPoint evPos(QMouseEvent* e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position().toPoint();
#else
    return e->pos();
#endif
}

}  // namespace

PointCloudView::PointCloudView(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(480, 360);
    setFocusPolicy(Qt::StrongFocus);
}

PointCloudView::~PointCloudView() {
    // Release GL resources with the context current. QOpenGLWidget::makeCurrent()
    // returns void and is safe to call here; destroy() is guarded by isCreated().
    makeCurrent();
    for (Mesh* m : {&staticLines_, &points_, &markers_, &dynLines_}) {
        if (m->vbo.isCreated()) m->vbo.destroy();
        if (m->vao.isCreated()) m->vao.destroy();
    }
    doneCurrent();
}

void PointCloudView::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);

    const char* vsrc =
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec4 aColor;\n"
        "uniform mat4 uMVP;\n"
        "uniform float uPointSize;\n"
        "out vec4 vColor;\n"
        "void main(){ gl_Position = uMVP*vec4(aPos,1.0); gl_PointSize=uPointSize;"
        " vColor=aColor; }\n";
    const char* fsrc =
        "#version 330 core\n"
        "in vec4 vColor; out vec4 FragColor;\n"
        "void main(){ FragColor = vColor; }\n";
    if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, vsrc) ||
        !program_.addShaderFromSourceCode(QOpenGLShader::Fragment, fsrc) ||
        !program_.link()) {
        // Without a valid program nothing renders; log so it isn't a silent
        // black screen on a driver/GLSL incompatibility.
        qCritical() << "PointCloudView shader build failed:" << program_.log();
        return;
    }

    initMesh(staticLines_);
    initMesh(points_);
    initMesh(markers_);
    initMesh(dynLines_);

    // Build static geometry once: ground grid (z=0) + X/Y/Z axes.
    std::vector<float> s;
    const float grey[4] = {0.35f, 0.36f, 0.40f, 1.0f};
    const int half = 50, step = 5;
    for (int g = -half; g <= half; g += step) {
        push7(s, float(g), float(-half), 0.0f, grey);
        push7(s, float(g), float(half), 0.0f, grey);
        push7(s, float(-half), float(g), 0.0f, grey);
        push7(s, float(half), float(g), 0.0f, grey);
    }
    const float red[4] = {1, 0.2f, 0.2f, 1}, green[4] = {0.2f, 1, 0.2f, 1},
                blue[4] = {0.3f, 0.5f, 1, 1};
    push7(s, 0, 0, 0, red);   push7(s, 6, 0, 0, red);    // X forward
    push7(s, 0, 0, 0, green); push7(s, 0, 6, 0, green);  // Y left
    push7(s, 0, 0, 0, blue);  push7(s, 0, 0, 6, blue);   // Z up
    uploadMesh(staticLines_, s);
}

void PointCloudView::initMesh(Mesh& m) {
    m.vao.create();
    m.vao.bind();
    m.vbo.create();
    m.vbo.bind();
    m.vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 3, kStride * sizeof(float));
    program_.enableAttributeArray(1);
    program_.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 4,
                                kStride * sizeof(float));
    m.vao.release();
    m.vbo.release();
}

void PointCloudView::uploadMesh(Mesh& m, const std::vector<float>& data) {
    m.vbo.bind();
    m.vbo.allocate(data.data(), static_cast<int>(data.size() * sizeof(float)));
    m.vbo.release();
    m.count = static_cast<int>(data.size() / kStride);
}

void PointCloudView::drawMesh(Mesh& m, unsigned int mode) {
    if (m.count <= 0) return;
    m.vao.bind();
    glDrawArrays(mode, 0, m.count);
    m.vao.release();
}

void PointCloudView::setFrame(const std::vector<SensorSnapshot>& snap,
                              ColorMode mode, float pointSize) {
    pointsData_.clear();
    markersData_.clear();
    dynLinesData_.clear();

    for (std::size_t i = 0; i < snap.size(); ++i) {
        const auto& s = snap[i];
        const float* col = sensorPalette(i);
        const auto& pts = s.cloud.points;
        for (std::size_t k = 0; k < pts.size(); ++k) {
            float c[4] = {col[0], col[1], col[2], 1.0f};
            if (mode == ColorMode::Velocity && k < s.cloud.velocity.size())
                valueToRgb(s.cloud.velocity[k], -10.0f, 10.0f, c[0], c[1], c[2]);
            else if (mode == ColorMode::Snr && k < s.cloud.snr.size())
                valueToRgb(s.cloud.snr[k], 0.0f, 30.0f, c[0], c[1], c[2]);
            push7(pointsData_, pts[k].x, pts[k].y, pts[k].z, c);
        }
        const Vec3 p0 = s.pose.position();
        push7(markersData_, p0.x, p0.y, p0.z, col);
        const Vec3 fwd = s.pose.forward();
        push7(dynLinesData_, p0.x, p0.y, p0.z, col);
        push7(dynLinesData_, p0.x + fwd.x * 3.0f, p0.y + fwd.y * 3.0f,
              p0.z + fwd.z * 3.0f, col);
    }
    pointSize_ = pointSize;
    dirty_ = true;
    update();
}

void PointCloudView::resetCamera() {
    az_ = 125.0f;
    el_ = 22.0f;
    dist_ = 45.0f;
    target_ = QVector3D(8.0f, 0.0f, 1.0f);
    update();
}

void PointCloudView::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

void PointCloudView::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (dirty_) {
        uploadMesh(points_, pointsData_);
        uploadMesh(markers_, markersData_);
        uploadMesh(dynLines_, dynLinesData_);
        dirty_ = false;
    }

    QMatrix4x4 proj;
    proj.perspective(45.0f, float(width()) / std::max(1, height()), 0.1f, 2000.0f);
    const float a = az_ * kDeg2Rad, e = el_ * kDeg2Rad;
    QVector3D dir(std::cos(e) * std::cos(a), std::cos(e) * std::sin(a),
                  std::sin(e));
    QVector3D eye = target_ + dir * dist_;
    QMatrix4x4 view;
    view.lookAt(eye, target_, QVector3D(0, 0, 1));
    QMatrix4x4 mvp = proj * view;

    program_.bind();
    program_.setUniformValue("uMVP", mvp);

    program_.setUniformValue("uPointSize", 1.0f);
    drawMesh(staticLines_, GL_LINES);
    drawMesh(dynLines_, GL_LINES);
    program_.setUniformValue("uPointSize", pointSize_);
    drawMesh(points_, GL_POINTS);
    program_.setUniformValue("uPointSize", 12.0f);
    drawMesh(markers_, GL_POINTS);

    program_.release();
}

void PointCloudView::mousePressEvent(QMouseEvent* e) { lastMouse_ = evPos(e); }

void PointCloudView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint p = evPos(e);
    const float dx = float(p.x() - lastMouse_.x());
    const float dy = float(p.y() - lastMouse_.y());
    lastMouse_ = p;
    if (e->buttons() & Qt::LeftButton) {
        az_ -= dx * 0.4f;
        el_ = std::min(89.0f, std::max(-89.0f, el_ + dy * 0.4f));
        update();
    } else if (e->buttons() & (Qt::RightButton | Qt::MiddleButton)) {
        // Pan the camera target in the view plane.
        const float a = az_ * kDeg2Rad, el = el_ * kDeg2Rad;
        QVector3D dir(std::cos(el) * std::cos(a), std::cos(el) * std::sin(a),
                      std::sin(el));
        QVector3D right = QVector3D::crossProduct(dir, QVector3D(0, 0, 1));
        right.normalize();
        QVector3D up = QVector3D::crossProduct(right, dir);
        target_ += (-right * dx + up * dy) * (dist_ * 0.0015f);
        update();
    }
}

void PointCloudView::wheelEvent(QWheelEvent* e) {
    const float steps = e->angleDelta().y() / 120.0f;
    dist_ *= std::pow(0.88f, steps);
    dist_ = std::min(500.0f, std::max(1.0f, dist_));
    update();
}

}  // namespace viewer
