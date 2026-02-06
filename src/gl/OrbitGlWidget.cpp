#include "OrbitGlWidget.h"

#include "orbit/OrbitalElements.h"
#include "orbit/Kepler.h"
#include "orbit/OrbitSampler.h"
#include "orbit/Propagator.h"
#include "orbit/Sgp4Propagator.h"

#include <QMouseEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <QImage>
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

constexpr const char* kEarthTexVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMvp;
out vec2 vUV;
void main() {
        vUV = aUV;
        gl_Position = uMvp * vec4(aPos, 1.0);
}
 )";

constexpr const char* kEarthTexFragmentShader = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTexture;
void main() {
        FragColor = texture(uTexture, vUV);
}
 )";

static float clampf(float v, float lo, float hi)
{
    return std::fmax(lo, std::fmin(hi, v));
}

constexpr double kPi = 3.141592653589793238462643383279502884;

// Earth constants for simple two-body Kepler propagation.
// Units: We propagate in "Earth radii" distance units to match rendering.
// mu(Re^3/s^2) = mu(km^3/s^2) / Re(km)^3
constexpr double kEarthMuKm3PerS2 = 398600.4418;
constexpr double kEarthRadiusKm = 6378.137;
constexpr double kEarthMuRe3PerS2 = kEarthMuKm3PerS2 / (kEarthRadiusKm * kEarthRadiusKm * kEarthRadiusKm);

static double degToRad(double deg)
{
    return deg * (kPi / 180.0);
}

static double wrapTwoPi(double x)
{
    const double twoPi = 2.0 * kPi;
    x = std::fmod(x, twoPi);
    if (x < 0.0) {
        x += twoPi;
    }
    return x;
}

static double eccentricAnomalyFromMean(double M, double e)
{
    // Newton-Raphson solve: M = E - e sin(E)
    M = wrapTwoPi(M);
    double E = (e < 0.8) ? M : kPi;
    for (int iter = 0; iter < 12; ++iter) {
        const double f = E - e * std::sin(E) - M;
        const double fp = 1.0 - e * std::cos(E);
        const double dE = -f / fp;
        E += dE;
        if (std::abs(dE) < 1e-12) {
            break;
        }
    }
    return E;
}

static double trueAnomalyFromMean(double M, double e)
{
    const double E = eccentricAnomalyFromMean(M, e);
    const double sinE2 = std::sin(E / 2.0);
    const double cosE2 = std::cos(E / 2.0);
    const double num = std::sqrt(1.0 + e) * sinE2;
    const double den = std::sqrt(1.0 - e) * cosE2;
    return 2.0 * std::atan2(num, den);
}
}

OrbitGlWidget::OrbitGlWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    timer_.start();
    simTime_ = std::chrono::system_clock::now();
    lastSimTickNs_ = timer_.nsecsElapsed();

    // Drive animation/simulation.
    simTimer_ = new QTimer(this);
    simTimer_->setInterval(16);
    connect(simTimer_, &QTimer::timeout, this, [this]() {
        const qint64 nowNs = timer_.nsecsElapsed();
        const double dt = static_cast<double>(nowNs - lastSimTickNs_) / 1e9;
        lastSimTickNs_ = nowNs;

        if (timeScale_ > 0.0 && dt > 0.0) {
            const auto delta = std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(dt * timeScale_));
            simTime_ += delta;
        }
        update();
    });
    simTimer_->start();

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

    sat.keplerEpoch = simTime_;

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
    sat->keplerEpoch = simTime_;
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

    if (markerVbo_ != 0) {
        glDeleteBuffers(1, &markerVbo_);
        markerVbo_ = 0;
    }
    if (markerVao_ != 0) {
        glDeleteVertexArrays(1, &markerVao_);
        markerVao_ = 0;
    }
    doneCurrent();
}

void OrbitGlWidget::setTimeScale(double timeScale)
{
    timeScale_ = std::max(0.0, timeScale);
}

bool OrbitGlWidget::setSatelliteTle(int id, const QString& line1, const QString& line2)
{
    auto* sat = findSatellite(id);
    if (!sat) {
        return false;
    }

    sat->propagator = std::make_unique<Sgp4Propagator>(line1.toStdString(), line2.toStdString());

    // If possible, sync the visualized orbit to the TLE mean elements so the
    // orbit polyline matches the propagated marker.
    if (auto* sgp4 = dynamic_cast<Sgp4Propagator*>(sat->propagator.get())) {
        OrbitalElements meanEl;
        if (sgp4->tryGetMeanElements(meanEl)) {
            sat->info.elements = meanEl;
            sat->keplerEpoch = simTime_;
            sat->vertices = OrbitSampler::sampleOrbitPolyline(sat->info.elements, sat->info.segments);
            if (glInitialized_) {
                makeCurrent();
                rebuildSatelliteVbo(*sat);
                doneCurrent();
            }
        }
    }

    update();
    return true;
}

void OrbitGlWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LINE_SMOOTH);

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    program_.link();

    earthTexProgram_.addShaderFromSourceCode(QOpenGLShader::Vertex, kEarthTexVertexShader);
    earthTexProgram_.addShaderFromSourceCode(QOpenGLShader::Fragment, kEarthTexFragmentShader);
    earthTexProgram_.link();

    glGenVertexArrays(1, &earthVao_);
    glGenBuffers(1, &earthVbo_);
    glGenBuffers(1, &earthEbo_);

    glGenVertexArrays(1, &axisVao_);
    glGenBuffers(1, &axisVbo_);

    glGenVertexArrays(1, &markerVao_);
    glGenBuffers(1, &markerVbo_);

    // Earth mesh at origin.
    rebuildEarthMesh(/*stacks=*/48, /*slices=*/96, /*radius=*/1.0f);

    // Load Earth texture
    QImage img(QStringLiteral("../assets/2k_earth_nightmap.jpg"));
    if (!img.isNull()) {
        img = img.convertToFormat(QImage::Format_RGBA8888);
        glGenTextures(1, &earthTex_);
        glBindTexture(GL_TEXTURE_2D, earthTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Create buffers for any satellites added before GL init.
    for (auto& sat : satellites_) {
        glGenVertexArrays(1, &sat.vao);
        glGenBuffers(1, &sat.vbo);
        sat.vertices = OrbitSampler::sampleOrbitPolyline(sat.info.elements, sat.info.segments);
        rebuildSatelliteVbo(sat);
    }

    rebuildAxisGeometry();

    // Marker VAO/VBO: single vec3 position, updated per draw.
    glBindVertexArray(markerVao_);
    glBindBuffer(GL_ARRAY_BUFFER, markerVbo_);
    glBufferData(GL_ARRAY_BUFFER, 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

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

    // Draw textured Earth sphere
    if (earthVao_ != 0 && earthIndexCount_ > 0 && earthTex_ != 0 && earthTexProgram_.isLinked()) {
        earthTexProgram_.bind();
        earthTexProgram_.setUniformValue("uMvp", mvp);
        earthTexProgram_.setUniformValue("uTexture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, earthTex_);
        glBindVertexArray(earthVao_);
        glDrawElements(GL_TRIANGLES, earthIndexCount_, GL_UNSIGNED_INT, reinterpret_cast<void*>(0));
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        earthTexProgram_.release();
    }

    // Draw satellite orbits
    program_.bind();
    program_.setUniformValue("uMvp", mvp);
    for (const auto& sat : satellites_) {
        if (sat.vao == 0 || sat.vertices.empty()) {
            continue;
        }
        program_.setUniformValue("uColor", sat.info.color);
        glBindVertexArray(sat.vao);
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<int>(sat.vertices.size() / 3));
        glBindVertexArray(0);
    }

    // Draw satellite markers (Keplerian propagation so the marker lies on the drawn Kepler orbit)
    if (markerVao_ != 0 && markerVbo_ != 0) {
        glPointSize(6.0f);
        glBindVertexArray(markerVao_);
        glBindBuffer(GL_ARRAY_BUFFER, markerVbo_);
        for (const auto& sat : satellites_) {
            const auto dt = simTime_ - sat.keplerEpoch;
            const double dtSec = std::chrono::duration_cast<std::chrono::duration<double>>(dt).count();

            const double a = sat.info.elements.semiMajorAxis;
            const double e = sat.info.elements.eccentricity;
            const double n = std::sqrt(kEarthMuRe3PerS2 / (a * a * a)); // rad/s

            const double M0 = degToRad(sat.info.elements.meanAnomalyDeg);
            const double M = M0 + n * dtSec;
            const double nu = trueAnomalyFromMean(M, e);
            const auto pos = Kepler::positionEciFromElements(sat.info.elements, nu);

            const float p[3] = {static_cast<float>(pos[0]), static_cast<float>(pos[1]), static_cast<float>(pos[2])};
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(p), p);
            program_.setUniformValue("uColor", sat.info.color);
            glDrawArrays(GL_POINTS, 0, 1);
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    // Draw axes
    program_.setUniformValue("uMvp", mvp);
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

    // Vertices (xyzuv)
    earthVertices_.reserve(static_cast<size_t>((stacks + 1) * (slices + 1) * 5));
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
            const float texU = static_cast<float>(u);
            const float texV = static_cast<float>(v);

            earthVertices_.push_back(x);
            earthVertices_.push_back(y);
            earthVertices_.push_back(z);
            earthVertices_.push_back(texU);
            earthVertices_.push_back(texV);
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

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
