#pragma once

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPoint>
#include <QString>
#include <QVector3D>

#include <vector>

#include "orbit/OrbitalElements.h"

class QMouseEvent;
class QWheelEvent;

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

    std::vector<float> axisVertices_; // xyz triplets

    std::vector<float> earthVertices_; // xyz triplets
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
};
