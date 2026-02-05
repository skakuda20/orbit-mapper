#include "MainWindow.h"

#include "gl/OrbitGlWidget.h"

#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Orbit Mapper");

    glWidget_ = new OrbitGlWidget(this);
    setCentralWidget(glWidget_);

    // Side panel for editing orbital elements in real time
    auto* dock = new QDockWidget("Orbital Elements", this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    auto* panel = new QWidget(dock);
    auto* form = new QFormLayout(panel);
    panel->setLayout(form);

    auto* aSpin = new QDoubleSpinBox(panel);
    aSpin->setDecimals(6);
    aSpin->setRange(0.1, 1000.0);
    aSpin->setSingleStep(0.001);
    aSpin->setToolTip("Semi-major axis a (Earth radii)");

    auto* eSpin = new QDoubleSpinBox(panel);
    eSpin->setDecimals(8);
    eSpin->setRange(0.0, 0.99999999);
    eSpin->setSingleStep(0.0001);
    eSpin->setToolTip("Eccentricity e");

    auto* iSpin = new QDoubleSpinBox(panel);
    iSpin->setDecimals(4);
    iSpin->setRange(0.0, 180.0);
    iSpin->setSingleStep(0.1);
    iSpin->setToolTip("Inclination i (deg)");

    auto* raanSpin = new QDoubleSpinBox(panel);
    raanSpin->setDecimals(4);
    raanSpin->setRange(0.0, 360.0);
    raanSpin->setSingleStep(0.1);
    raanSpin->setToolTip("RAAN Ω (deg)");

    auto* argpSpin = new QDoubleSpinBox(panel);
    argpSpin->setDecimals(4);
    argpSpin->setRange(0.0, 360.0);
    argpSpin->setSingleStep(0.1);
    argpSpin->setToolTip("Argument of periapsis ω (deg)");

    auto* segmentsSpin = new QSpinBox(panel);
    segmentsSpin->setRange(32, 4096);
    segmentsSpin->setSingleStep(32);
    segmentsSpin->setToolTip("Polyline segments used for orbit rendering");

    form->addRow("a (Re)", aSpin);
    form->addRow("e", eSpin);
    form->addRow("i (deg)", iSpin);
    form->addRow("Ω (deg)", raanSpin);
    form->addRow("ω (deg)", argpSpin);
    form->addRow("Segments", segmentsSpin);

    // Initialize UI from the widget's current state
    const auto initial = glWidget_->orbitalElements();
    aSpin->setValue(initial.semiMajorAxis);
    eSpin->setValue(initial.eccentricity);
    iSpin->setValue(initial.inclinationDeg);
    raanSpin->setValue(initial.raanDeg);
    argpSpin->setValue(initial.argPeriapsisDeg);
    segmentsSpin->setValue(glWidget_->orbitSegments());

    auto pushToGl = [this, aSpin, eSpin, iSpin, raanSpin, argpSpin, segmentsSpin]() {
        OrbitalElements el;
        el.semiMajorAxis = aSpin->value();
        el.eccentricity = eSpin->value();
        el.inclinationDeg = iSpin->value();
        el.raanDeg = raanSpin->value();
        el.argPeriapsisDeg = argpSpin->value();
        glWidget_->setOrbitalElements(el, segmentsSpin->value());
    };

    connect(aSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
    connect(eSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
    connect(iSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
    connect(raanSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
    connect(argpSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
    connect(segmentsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [pushToGl](int) { pushToGl(); });

    dock->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}
