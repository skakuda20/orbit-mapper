#include "MainWindow.h"

#include "gl/OrbitGlWidget.h"

#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace {

static QGroupBox* makeCollapsibleGroup(const QString& title, QWidget* parent, QWidget** outContent)
{
    auto* group = new QGroupBox(title, parent);
    group->setCheckable(true);
    group->setChecked(true);

    auto* outer = new QVBoxLayout(group);
    outer->setContentsMargins(8, 8, 8, 8);

    auto* content = new QWidget(group);
    outer->addWidget(content);

    QObject::connect(group, &QGroupBox::toggled, content, &QWidget::setVisible);
    content->setVisible(true);

    if (outContent != nullptr) {
        *outContent = content;
    }
    return group;
}

static OrbitalElements defaultLeoElements()
{
    OrbitalElements el;
    const double earthRadiusKm = 6378.137;
    const double altitudeKm = 400.0;
    el.semiMajorAxis = (earthRadiusKm + altitudeKm) / earthRadiusKm;
    el.eccentricity = 0.001;
    el.inclinationDeg = 55.0;
    el.raanDeg = 40.0;
    el.argPeriapsisDeg = 30.0;
    return el;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Orbit Mapper");

    glWidget_ = new OrbitGlWidget(this);
    setCentralWidget(glWidget_);

    // Side panel for managing multiple satellites
    auto* dock = new QDockWidget("Satellites", this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    auto* panel = new QWidget(dock);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);
    panelLayout->setSpacing(8);

    auto* addBtn = new QPushButton("Add Satellite", panel);
    panelLayout->addWidget(addBtn);

    auto* scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    panelLayout->addWidget(scroll, 1);

    auto* listHost = new QWidget(scroll);
    auto* listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(10);
    listLayout->addStretch(1);
    scroll->setWidget(listHost);

    int satelliteNumber = 1;

    auto addSatelliteEditor = [this, listHost, listLayout](int id, const QString& name, const OrbitalElements& initial, int segments) {
        QWidget* satContent = nullptr;
        auto* satGroup = makeCollapsibleGroup(name, listHost, &satContent);

        auto* satLayout = new QVBoxLayout(satContent);
        satLayout->setContentsMargins(0, 0, 0, 0);
        satLayout->setSpacing(8);

        // Remove button row
        auto* topRow = new QWidget(satContent);
        auto* topRowLayout = new QHBoxLayout(topRow);
        topRowLayout->setContentsMargins(0, 0, 0, 0);
        topRowLayout->addStretch(1);
        auto* removeBtn = new QPushButton("Remove", topRow);
        topRowLayout->addWidget(removeBtn);
        satLayout->addWidget(topRow);

        // Nested: Orbital Elements
        QWidget* elementsContent = nullptr;
        auto* elementsGroup = makeCollapsibleGroup("Orbital Elements", satContent, &elementsContent);
        satLayout->addWidget(elementsGroup);

        auto* elementsForm = new QFormLayout(elementsContent);
        elementsForm->setContentsMargins(0, 0, 0, 0);

        auto* aSpin = new QDoubleSpinBox(elementsContent);
        aSpin->setDecimals(6);
        aSpin->setRange(0.1, 1000.0);
        aSpin->setSingleStep(0.001);
        aSpin->setToolTip("Semi-major axis a (Earth radii)");
        aSpin->setValue(initial.semiMajorAxis);

        auto* eSpin = new QDoubleSpinBox(elementsContent);
        eSpin->setDecimals(8);
        eSpin->setRange(0.0, 0.99999999);
        eSpin->setSingleStep(0.0001);
        eSpin->setToolTip("Eccentricity e");
        eSpin->setValue(initial.eccentricity);

        auto* iSpin = new QDoubleSpinBox(elementsContent);
        iSpin->setDecimals(4);
        iSpin->setRange(0.0, 180.0);
        iSpin->setSingleStep(0.1);
        iSpin->setToolTip("Inclination i (deg)");
        iSpin->setValue(initial.inclinationDeg);

        auto* raanSpin = new QDoubleSpinBox(elementsContent);
        raanSpin->setDecimals(4);
        raanSpin->setRange(0.0, 360.0);
        raanSpin->setSingleStep(0.1);
        raanSpin->setToolTip("RAAN Ω (deg)");
        raanSpin->setValue(initial.raanDeg);

        auto* argpSpin = new QDoubleSpinBox(elementsContent);
        argpSpin->setDecimals(4);
        argpSpin->setRange(0.0, 360.0);
        argpSpin->setSingleStep(0.1);
        argpSpin->setToolTip("Argument of periapsis ω (deg)");
        argpSpin->setValue(initial.argPeriapsisDeg);

        elementsForm->addRow("a (Re)", aSpin);
        elementsForm->addRow("e", eSpin);
        elementsForm->addRow("i (deg)", iSpin);
        elementsForm->addRow("Ω (deg)", raanSpin);
        elementsForm->addRow("ω (deg)", argpSpin);

        // Nested: Rendering
        QWidget* renderContent = nullptr;
        auto* renderGroup = makeCollapsibleGroup("Rendering", satContent, &renderContent);
        satLayout->addWidget(renderGroup);

        auto* renderForm = new QFormLayout(renderContent);
        renderForm->setContentsMargins(0, 0, 0, 0);
        auto* segmentsSpin = new QSpinBox(renderContent);
        segmentsSpin->setRange(32, 4096);
        segmentsSpin->setSingleStep(32);
        segmentsSpin->setToolTip("Polyline segments used for orbit rendering");
        segmentsSpin->setValue(segments);
        renderForm->addRow("Segments", segmentsSpin);

        auto pushToGl = [this, id, aSpin, eSpin, iSpin, raanSpin, argpSpin, segmentsSpin]() {
            OrbitalElements el;
            el.semiMajorAxis = aSpin->value();
            el.eccentricity = eSpin->value();
            el.inclinationDeg = iSpin->value();
            el.raanDeg = raanSpin->value();
            el.argPeriapsisDeg = argpSpin->value();
            glWidget_->updateSatellite(id, el, segmentsSpin->value());
        };

        connect(aSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(eSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(iSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(raanSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(argpSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(segmentsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [pushToGl](int) { pushToGl(); });

        connect(removeBtn, &QPushButton::clicked, this, [this, id, satGroup]() {
            glWidget_->removeSatellite(id);
            satGroup->deleteLater();
        });

        // Insert before the stretch at the end
        listLayout->insertWidget(listLayout->count() - 1, satGroup);
    };

    // Start with one satellite by default
    {
        const OrbitalElements el = defaultLeoElements();
        const int segments = 512;
        const QString name = QString("Satellite %1").arg(satelliteNumber++);
        const int id = glWidget_->addSatellite(name, el, segments);
        addSatelliteEditor(id, name, el, segments);
    }

    connect(addBtn, &QPushButton::clicked, this, [this, &satelliteNumber, addSatelliteEditor]() mutable {
        const OrbitalElements el = defaultLeoElements();
        const int segments = 512;
        const QString name = QString("Satellite %1").arg(satelliteNumber++);
        const int id = glWidget_->addSatellite(name, el, segments);
        addSatelliteEditor(id, name, el, segments);
    });

    dock->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}
