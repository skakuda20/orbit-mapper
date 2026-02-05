#pragma once

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPoint>

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

    void setOrbitalElements(const OrbitalElements& elements, int segments = 512);
    OrbitalElements orbitalElements() const { return elements_; }
    int orbitSegments() const { return orbitSegments_; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void rebuildEarthMesh(int stacks, int slices, float radius);
    void rebuildOrbitVbo();
    QMatrix4x4 buildViewProjection() const;

    void rebuildOrbitGeometry();

    void rebuildAxisVbo();

    QOpenGLShaderProgram program_;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;

    unsigned int axisVao_ = 0;
    unsigned int axisVbo_ = 0;

    unsigned int earthVao_ = 0;
    unsigned int earthVbo_ = 0;
    unsigned int earthEbo_ = 0;
    int earthIndexCount_ = 0;

    std::vector<float> orbitVertices_; // xyz triplets

    std::vector<float> axisVertices_; // xyz triplets

    std::vector<float> earthVertices_; // xyz triplets
    std::vector<unsigned int> earthIndices_;

    QPoint lastMousePos_;
    float yawDeg_ = -30.0f;
    float pitchDeg_ = -20.0f;
    float distance_ = 8.0f;

    bool glInitialized_ = false;
    OrbitalElements elements_;
    int orbitSegments_ = 512;

    QElapsedTimer timer_;
};
