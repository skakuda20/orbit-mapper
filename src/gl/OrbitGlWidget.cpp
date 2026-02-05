#include "OrbitGlWidget.h"

#include "orbit/OrbitalElements.h"
#include "orbit/OrbitSampler.h"

#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr const char* kVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 uMvp;

void main() {
  gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(
#version 330 core
out vec4 FragColor;

uniform vec3 uColor;

void main() {
  FragColor = vec4(uColor, 1.0);
}
)";

static float clampf(float v, float lo, float hi)
{
    return std::fmax(lo, std::fmin(hi, v));
}
}

OrbitGlWidget::OrbitGlWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    timer_.start();
}

OrbitGlWidget::~OrbitGlWidget()
{
    makeCurrent();
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    doneCurrent();
}

void OrbitGlWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LINE_SMOOTH);

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    program_.link();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    // Demo orbit: a slightly eccentric LEO-like orbit (in arbitrary units)
    OrbitalElements elements;
    elements.semiMajorAxis = 3.0;  // visualization scale
    elements.eccentricity = 0.05;
    elements.inclinationDeg = 55.0;
    elements.raanDeg = 40.0;
    elements.argPeriapsisDeg = 30.0;

    orbitVertices_ = OrbitSampler::sampleOrbitPolyline(elements, 512);
    rebuildOrbitVbo();
}

void OrbitGlWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void OrbitGlWidget::paintGL()
{
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!program_.isLinked() || vao_ == 0) {
        return;
    }

    QMatrix4x4 mvp = buildViewProjection();

    program_.bind();
    program_.setUniformValue("uMvp", mvp);

    // Draw orbit polyline
    program_.setUniformValue("uColor", QVector3D(0.2f, 0.8f, 1.0f));

    glBindVertexArray(vao_);
    glDrawArrays(GL_LINE_STRIP, 0, static_cast<int>(orbitVertices_.size() / 3));
    glBindVertexArray(0);

    program_.release();
}

void OrbitGlWidget::mousePressEvent(QMouseEvent* event)
{
    lastMousePos_ = event->pos();
}

void OrbitGlWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint delta = event->pos() - lastMousePos_;
    lastMousePos_ = event->pos();

    if (event->buttons() & Qt::LeftButton) {
        yawDeg_ += delta.x() * 0.3f;
        pitchDeg_ += delta.y() * 0.3f;
        pitchDeg_ = clampf(pitchDeg_, -89.0f, 89.0f);
        update();
    }
}

void OrbitGlWidget::wheelEvent(QWheelEvent* event)
{
    // Qt provides angleDelta in 1/8 degrees
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;
    distance_ *= std::pow(0.9f, steps);
    distance_ = clampf(distance_, 1.5f, 50.0f);
    update();
}

void OrbitGlWidget::rebuildOrbitVbo()
{
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<long long>(orbitVertices_.size() * sizeof(float)),
        orbitVertices_.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

QMatrix4x4 OrbitGlWidget::buildViewProjection() const
{
    QMatrix4x4 projection;
    projection.perspective(45.0f, float(width()) / float(std::max(1, height())), 0.1f, 200.0f);

    QMatrix4x4 view;
    view.translate(0.0f, 0.0f, -distance_);
    view.rotate(pitchDeg_, 1.0f, 0.0f, 0.0f);
    view.rotate(yawDeg_, 0.0f, 1.0f, 0.0f);

    // Simple world model: orbit around origin
    QMatrix4x4 model;

    return projection * view * model;
}
