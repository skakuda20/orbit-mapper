#pragma once

#include <chrono>
#include <memory>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>
#include <QVector3D>

#include <string>
#include <vector>

#include "orbit/OrbitalElements.h"

class QMouseEvent;
class QWheelEvent;
class QTimer;

class Propagator;

class OrbitGlWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit OrbitGlWidget(QWidget* parent = nullptr);
    ~OrbitGlWidget() override;

    struct SatelliteInfo
    {
        int id = 0;
        QString name;
        OrbitalElements elements;
        int segments = 512;
        QVector3D color{0.2f, 0.8f, 1.0f};
    };

    int addSatellite(const QString& name, const OrbitalElements& elements, int segments = 512);
    bool removeSatellite(int id);
    bool updateSatellite(int id, const OrbitalElements& elements, int segments = 512);
    std::vector<SatelliteInfo> satellites() const;

    // Simulation clock controls
    // timeScale: 0 = paused, 1 = real-time, 10 = 10x faster, etc.
    void setTimeScale(double timeScale);

    // Assign a TLE to a satellite; if set, a moving marker is rendered using SGP4.
    bool setSatelliteTle(int id, const QString& line1, const QString& line2);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Satellite
    {
        SatelliteInfo info;
        unsigned int vao = 0;
        unsigned int vbo = 0;
        std::vector<float> vertices; // xyz triplets

        std::unique_ptr<Propagator> propagator;
    };

    void rebuildEarthMesh(int stacks, int slices, float radius);
    QMatrix4x4 buildViewProjection() const;

    void rebuildSatelliteVbo(Satellite& sat);
    void rebuildSatelliteGeometry(Satellite& sat);
    Satellite* findSatellite(int id);

    void rebuildAxisVbo();
    void rebuildAxisGeometry();

    QOpenGLShaderProgram program_;

    unsigned int axisVao_ = 0;
    unsigned int axisVbo_ = 0;

    unsigned int earthVao_ = 0;
    unsigned int earthVbo_ = 0;
    unsigned int earthEbo_ = 0;
    int earthIndexCount_ = 0;
    unsigned int earthTex_ = 0;
    QOpenGLShaderProgram earthTexProgram_;

    unsigned int markerVao_ = 0;
    unsigned int markerVbo_ = 0;

    std::vector<float> axisVertices_; // xyz triplets

    std::vector<float> earthVertices_; // xyzuv (5 floats per vertex)
    std::vector<unsigned int> earthIndices_;

    QPoint lastMousePos_;
    float yawDeg_ = -30.0f;
    float pitchDeg_ = -20.0f;
    float distance_ = 8.0f;

    bool glInitialized_ = false;
    int nextSatelliteId_ = 1;
    int paletteIndex_ = 0;
    std::vector<Satellite> satellites_;

    QElapsedTimer timer_;

    QTimer* simTimer_ = nullptr;
    double timeScale_ = 1.0;
    std::chrono::system_clock::time_point simTime_{};
    qint64 lastSimTickNs_ = 0;
};
