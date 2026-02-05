#pragma once

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

#include <vector>

class OrbitGlWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit OrbitGlWidget(QWidget* parent = nullptr);
    ~OrbitGlWidget() override;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void rebuildOrbitVbo();
    QMatrix4x4 buildViewProjection() const;

    QOpenGLShaderProgram program_;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;

    std::vector<float> orbitVertices_; // xyz triplets

    QPoint lastMousePos_;
    float yawDeg_ = -30.0f;
    float pitchDeg_ = -20.0f;
    float distance_ = 8.0f;

    QElapsedTimer timer_;
};
