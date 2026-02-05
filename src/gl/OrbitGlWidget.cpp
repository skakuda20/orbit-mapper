#include "OrbitGlWidget.h"

#include "orbit/OrbitalElements.h"
#include "orbit/OrbitSampler.h"

#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

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

constexpr double kPi = 3.141592653589793238462643383279502884;
}

OrbitGlWidget::OrbitGlWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    timer_.start();

    // In "Earth radii" units, a typical LEO orbit is ~1.06.
    // Keep the default camera fairly close.
    distance_ = 4.0f;

    // Satellites are managed from the Qt side panel (MainWindow).
}

int OrbitGlWidget::addSatellite(const QString& name, const OrbitalElements& elements, int segments)
{
    static const QVector3D palette[] = {
        {0.20f, 0.80f, 1.00f},
        {1.00f, 0.75f, 0.20f},
        {0.85f, 0.35f, 0.85f},
        {0.35f, 0.85f, 0.45f},
        {0.95f, 0.35f, 0.30f},
    };

    Satellite sat;
    sat.info.id = nextSatelliteId_++;
    sat.info.name = name;
    sat.info.elements = elements;
    sat.info.segments = std::max(8, segments);
    sat.info.color = palette[paletteIndex_++ % (sizeof(palette) / sizeof(palette[0]))];

    sat.vertices = OrbitSampler::sampleOrbitPolyline(sat.info.elements, sat.info.segments);

    if (glInitialized_) {
        makeCurrent();
        glGenVertexArrays(1, &sat.vao);
        glGenBuffers(1, &sat.vbo);
        rebuildSatelliteVbo(sat);
        doneCurrent();
        update();
    }

    satellites_.push_back(std::move(sat));
    return satellites_.back().info.id;
}

bool OrbitGlWidget::removeSatellite(int id)
{
    for (size_t i = 0; i < satellites_.size(); ++i) {
        if (satellites_[i].info.id != id) {
            continue;
        }

        if (glInitialized_) {
            makeCurrent();
            if (satellites_[i].vbo != 0) {
                glDeleteBuffers(1, &satellites_[i].vbo);
                satellites_[i].vbo = 0;
            }
            if (satellites_[i].vao != 0) {
                glDeleteVertexArrays(1, &satellites_[i].vao);
                satellites_[i].vao = 0;
            }
            doneCurrent();
        }

        satellites_.erase(satellites_.begin() + static_cast<long>(i));
        update();
        return true;
    }
    return false;
}

bool OrbitGlWidget::updateSatellite(int id, const OrbitalElements& elements, int segments)
{
    Satellite* sat = findSatellite(id);
    if (sat == nullptr) {
        return false;
    }

    sat->info.elements = elements;
    sat->info.segments = std::max(8, segments);
    sat->vertices = OrbitSampler::sampleOrbitPolyline(sat->info.elements, sat->info.segments);

    if (!glInitialized_) {
        update();
        return true;
    }

    makeCurrent();
    rebuildSatelliteVbo(*sat);
    doneCurrent();
    update();
    return true;
}

std::vector<OrbitGlWidget::SatelliteInfo> OrbitGlWidget::satellites() const
{
    std::vector<SatelliteInfo> out;
    out.reserve(satellites_.size());
    for (const auto& sat : satellites_) {
        out.push_back(sat.info);
    }
    return out;
}

OrbitGlWidget::~OrbitGlWidget()
{
    makeCurrent();
    for (auto& sat : satellites_) {
        if (sat.vbo != 0) {
            glDeleteBuffers(1, &sat.vbo);
            sat.vbo = 0;
        }
        if (sat.vao != 0) {
            glDeleteVertexArrays(1, &sat.vao);
            sat.vao = 0;
        }
    }

    if (earthEbo_ != 0) {
        glDeleteBuffers(1, &earthEbo_);
        earthEbo_ = 0;
    }
    if (earthVbo_ != 0) {
        glDeleteBuffers(1, &earthVbo_);
        earthVbo_ = 0;
    }
    if (earthVao_ != 0) {
        glDeleteVertexArrays(1, &earthVao_);
        earthVao_ = 0;
    }
    if (axisVbo_ != 0) {
        glDeleteBuffers(1, &axisVbo_);
        axisVbo_ = 0;
    }
    if (axisVao_ != 0) {
        glDeleteVertexArrays(1, &axisVao_);
        axisVao_ = 0;
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

    glGenVertexArrays(1, &earthVao_);
    glGenBuffers(1, &earthVbo_);
    glGenBuffers(1, &earthEbo_);

    glGenVertexArrays(1, &axisVao_);
    glGenBuffers(1, &axisVbo_);

    // Earth mesh at origin.
    rebuildEarthMesh(/*stacks=*/48, /*slices=*/96, /*radius=*/1.0f);

    // Create buffers for any satellites added before GL init.
    for (auto& sat : satellites_) {
        glGenVertexArrays(1, &sat.vao);
        glGenBuffers(1, &sat.vbo);
        sat.vertices = OrbitSampler::sampleOrbitPolyline(sat.info.elements, sat.info.segments);
        rebuildSatelliteVbo(sat);
    }

    rebuildAxisGeometry();

    glInitialized_ = true;
}

void OrbitGlWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void OrbitGlWidget::paintGL()
{
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!program_.isLinked()) {
        return;
    }

    QMatrix4x4 mvp = buildViewProjection();

    program_.bind();
    program_.setUniformValue("uMvp", mvp);

    // Draw Earth sphere
    if (earthVao_ != 0 && earthIndexCount_ > 0) {
        program_.setUniformValue("uColor", QVector3D(0.12f, 0.32f, 0.62f));
        glBindVertexArray(earthVao_);
        glDrawElements(GL_TRIANGLES, earthIndexCount_, GL_UNSIGNED_INT, reinterpret_cast<void*>(0));
        glBindVertexArray(0);
    }

    // Draw satellite orbits
    for (const auto& sat : satellites_) {
        if (sat.vao == 0 || sat.vertices.empty()) {
            continue;
        }
        program_.setUniformValue("uColor", sat.info.color);
        glBindVertexArray(sat.vao);
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<int>(sat.vertices.size() / 3));
        glBindVertexArray(0);
    }

    // Draw axes
    if (axisVao_ != 0 && axisVertices_.size() >= 18) {
        glBindVertexArray(axisVao_);

        const QVector3D axisGrey(0.65f, 0.65f, 0.65f);

        // X axis
        program_.setUniformValue("uColor", axisGrey);
        glDrawArrays(GL_LINES, 0, 2);

        // Y axis
        program_.setUniformValue("uColor", axisGrey);
        glDrawArrays(GL_LINES, 2, 2);

        // Z axis
        program_.setUniformValue("uColor", axisGrey);
        glDrawArrays(GL_LINES, 4, 2);

        glBindVertexArray(0);
    }

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

void OrbitGlWidget::rebuildEarthMesh(int stacks, int slices, float radius)
{
    stacks = std::max(8, stacks);
    slices = std::max(8, slices);

    earthVertices_.clear();
    earthIndices_.clear();

    // Vertices
    earthVertices_.reserve(static_cast<size_t>((stacks + 1) * (slices + 1) * 3));
    for (int i = 0; i <= stacks; ++i) {
        const double v = static_cast<double>(i) / static_cast<double>(stacks);
        const double phi = v * kPi; // 0..pi

        const double sinPhi = std::sin(phi);
        const double cosPhi = std::cos(phi);

        for (int j = 0; j <= slices; ++j) {
            const double u = static_cast<double>(j) / static_cast<double>(slices);
            const double theta = u * (2.0 * kPi); // 0..2pi

            const double sinTheta = std::sin(theta);
            const double cosTheta = std::cos(theta);

            const float x = radius * static_cast<float>(sinPhi * cosTheta);
            const float y = radius * static_cast<float>(cosPhi);
            const float z = radius * static_cast<float>(sinPhi * sinTheta);

            earthVertices_.push_back(x);
            earthVertices_.push_back(y);
            earthVertices_.push_back(z);
        }
    }

    // Indices (two triangles per quad)
    earthIndices_.reserve(static_cast<size_t>(stacks * slices * 6));
    const int stride = slices + 1;
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const unsigned int i0 = static_cast<unsigned int>(i * stride + j);
            const unsigned int i1 = static_cast<unsigned int>((i + 1) * stride + j);
            const unsigned int i2 = static_cast<unsigned int>((i + 1) * stride + (j + 1));
            const unsigned int i3 = static_cast<unsigned int>(i * stride + (j + 1));

            earthIndices_.push_back(i0);
            earthIndices_.push_back(i1);
            earthIndices_.push_back(i2);

            earthIndices_.push_back(i0);
            earthIndices_.push_back(i2);
            earthIndices_.push_back(i3);
        }
    }

    earthIndexCount_ = static_cast<int>(earthIndices_.size());

    // Upload
    glBindVertexArray(earthVao_);

    glBindBuffer(GL_ARRAY_BUFFER, earthVbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<long long>(earthVertices_.size() * sizeof(float)),
        earthVertices_.data(),
        GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, earthEbo_);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<long long>(earthIndices_.size() * sizeof(unsigned int)),
        earthIndices_.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));

    glBindVertexArray(0);
}

void OrbitGlWidget::rebuildSatelliteVbo(Satellite& sat)
{
    glBindVertexArray(sat.vao);
    glBindBuffer(GL_ARRAY_BUFFER, sat.vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<long long>(sat.vertices.size() * sizeof(float)),
        sat.vertices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void OrbitGlWidget::rebuildSatelliteGeometry(Satellite& sat)
{
    sat.vertices = OrbitSampler::sampleOrbitPolyline(sat.info.elements, sat.info.segments);
}

OrbitGlWidget::Satellite* OrbitGlWidget::findSatellite(int id)
{
    for (auto& sat : satellites_) {
        if (sat.info.id == id) {
            return &sat;
        }
    }
    return nullptr;
}

void OrbitGlWidget::rebuildAxisVbo()
{
    glBindVertexArray(axisVao_);
    glBindBuffer(GL_ARRAY_BUFFER, axisVbo_);

    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<long long>(axisVertices_.size() * sizeof(float)),
        axisVertices_.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void OrbitGlWidget::rebuildAxisGeometry()
{
    axisVertices_ = {
        // X axis
        -2.0f, 0.0f, 0.0f,
        2.0f, 0.0f, 0.0f,

        // Y axis
        0.0f, -2.0f, 0.0f,
        0.0f, 2.0f, 0.0f,

        // Z axis
        0.0f, 0.0f, -2.0f,
        0.0f, 0.0f, 2.0f,
    };

    // Caller must have a current GL context.
    rebuildAxisVbo();
}

QMatrix4x4 OrbitGlWidget::buildViewProjection() const
{
    QMatrix4x4 projection;
    projection.perspective(45.0f, float(width()) / float(std::max(1, height())), 0.01f, 200.0f);

    QMatrix4x4 view;
    view.translate(0.0f, 0.0f, -distance_);
    view.rotate(pitchDeg_, 1.0f, 0.0f, 0.0f);
    view.rotate(yawDeg_, 0.0f, 1.0f, 0.0f);

    // Simple world model: orbit around origin
    QMatrix4x4 model;

    return projection * view * model;
}
