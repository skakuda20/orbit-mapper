#include "MainWindow.h"

#include "gl/OrbitGlWidget.h"

#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSlider>
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

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    glWidget_ = new OrbitGlWidget(central);
    centralLayout->addWidget(glWidget_, 1);

    // Bottom simulation controls
    auto* bottomBar = new QWidget(central);
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(8, 6, 8, 6);
    bottomLayout->setSpacing(8);
    bottomLayout->addStretch(1);

    auto* pauseBtn = new QPushButton("Pause", bottomBar);
    auto* x1Btn = new QPushButton("1x", bottomBar);
    auto* x10Btn = new QPushButton("10x", bottomBar);
    auto* x100Btn = new QPushButton("100x", bottomBar);
    auto* x1000Btn = new QPushButton("1000x", bottomBar);

    bottomLayout->addWidget(pauseBtn);
    bottomLayout->addWidget(x1Btn);
    bottomLayout->addWidget(x10Btn);
    bottomLayout->addWidget(x100Btn);
    bottomLayout->addWidget(x1000Btn);

    connect(pauseBtn, &QPushButton::clicked, this, [this]() { glWidget_->setTimeScale(0.0); });
    connect(x1Btn, &QPushButton::clicked, this, [this]() { glWidget_->setTimeScale(1.0); });
    connect(x10Btn, &QPushButton::clicked, this, [this]() { glWidget_->setTimeScale(10.0); });
    connect(x100Btn, &QPushButton::clicked, this, [this]() { glWidget_->setTimeScale(100.0); });
    connect(x1000Btn, &QPushButton::clicked, this, [this]() { glWidget_->setTimeScale(1000.0); });

    centralLayout->addWidget(bottomBar, 0);
    setCentralWidget(central);

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

        // Add sliders and spinboxes for each element
        auto* aSpin = new QDoubleSpinBox(elementsContent);
        aSpin->setDecimals(6);
        aSpin->setRange(0.1, 1000.0);
        aSpin->setSingleStep(0.001);
        aSpin->setToolTip("Semi-major axis a (Earth radii)");
        aSpin->setValue(initial.semiMajorAxis);
        auto* aSlider = new QSlider(Qt::Horizontal, elementsContent);
        aSlider->setRange(0, 10000); // 0.0 to 10.0, step 0.001 * 1000
        aSlider->setValue(static_cast<int>(initial.semiMajorAxis * 1000));
        QObject::connect(aSlider, &QSlider::valueChanged, aSpin, [aSpin](int v){ aSpin->setValue(v/1000.0); });
        QObject::connect(aSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), aSlider, [aSlider](double v){ aSlider->setValue(static_cast<int>(v*1000)); });
        auto* aWidget = new QWidget(elementsContent);
        auto* aLayout = new QVBoxLayout(aWidget);
        aLayout->setContentsMargins(0,0,0,0);
        aLayout->addWidget(aSpin);
        aLayout->addWidget(aSlider);
        elementsForm->addRow("a (Re)", aWidget);

        auto* eSpin = new QDoubleSpinBox(elementsContent);
        eSpin->setDecimals(8);
        eSpin->setRange(0.0, 0.99999999);
        eSpin->setSingleStep(0.0001);
        eSpin->setToolTip("Eccentricity e");
        eSpin->setValue(initial.eccentricity);
        auto* eSlider = new QSlider(Qt::Horizontal, elementsContent);
        eSlider->setRange(0, 99999999); // 0.0 to 0.99999999, step 0.00000001 * 1e8
        eSlider->setValue(static_cast<int>(initial.eccentricity * 1e8));
        QObject::connect(eSlider, &QSlider::valueChanged, eSpin, [eSpin](int v){ eSpin->setValue(v/1e8); });
        QObject::connect(eSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), eSlider, [eSlider](double v){ eSlider->setValue(static_cast<int>(v*1e8)); });
        auto* eWidget = new QWidget(elementsContent);
        auto* eLayout = new QVBoxLayout(eWidget);
        eLayout->setContentsMargins(0,0,0,0);
        eLayout->addWidget(eSpin);
        eLayout->addWidget(eSlider);
        elementsForm->addRow("e", eWidget);

        auto* iSpin = new QDoubleSpinBox(elementsContent);
        iSpin->setDecimals(4);
        iSpin->setRange(0.0, 180.0);
        iSpin->setSingleStep(0.1);
        iSpin->setToolTip("Inclination i (deg)");
        iSpin->setValue(initial.inclinationDeg);
        auto* iSlider = new QSlider(Qt::Horizontal, elementsContent);
        iSlider->setRange(0, 18000); // 0.0 to 180.0, step 0.01
        iSlider->setValue(static_cast<int>(initial.inclinationDeg * 100));
        QObject::connect(iSlider, &QSlider::valueChanged, iSpin, [iSpin](int v){ iSpin->setValue(v/100.0); });
        QObject::connect(iSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), iSlider, [iSlider](double v){ iSlider->setValue(static_cast<int>(v*100)); });
        auto* iWidget = new QWidget(elementsContent);
        auto* iLayout = new QVBoxLayout(iWidget);
        iLayout->setContentsMargins(0,0,0,0);
        iLayout->addWidget(iSpin);
        iLayout->addWidget(iSlider);
        elementsForm->addRow("i (deg)", iWidget);

        auto* raanSpin = new QDoubleSpinBox(elementsContent);
        raanSpin->setDecimals(4);
        raanSpin->setRange(0.0, 360.0);
        raanSpin->setSingleStep(0.1);
        raanSpin->setToolTip("RAAN Ω (deg)");
        raanSpin->setValue(initial.raanDeg);
        auto* raanSlider = new QSlider(Qt::Horizontal, elementsContent);
        raanSlider->setRange(0, 36000); // 0.0 to 360.0, step 0.01
        raanSlider->setValue(static_cast<int>(initial.raanDeg * 100));
        QObject::connect(raanSlider, &QSlider::valueChanged, raanSpin, [raanSpin](int v){ raanSpin->setValue(v/100.0); });
        QObject::connect(raanSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), raanSlider, [raanSlider](double v){ raanSlider->setValue(static_cast<int>(v*100)); });
        auto* raanWidget = new QWidget(elementsContent);
        auto* raanLayout = new QVBoxLayout(raanWidget);
        raanLayout->setContentsMargins(0,0,0,0);
        raanLayout->addWidget(raanSpin);
        raanLayout->addWidget(raanSlider);
        elementsForm->addRow("Ω (deg)", raanWidget);

        auto* argpSpin = new QDoubleSpinBox(elementsContent);
        argpSpin->setDecimals(4);
        argpSpin->setRange(0.0, 360.0);
        argpSpin->setSingleStep(0.1);
        argpSpin->setToolTip("Argument of periapsis ω (deg)");
        argpSpin->setValue(initial.argPeriapsisDeg);
        auto* argpSlider = new QSlider(Qt::Horizontal, elementsContent);
        argpSlider->setRange(0, 36000); // 0.0 to 360.0, step 0.01
        argpSlider->setValue(static_cast<int>(initial.argPeriapsisDeg * 100));
        QObject::connect(argpSlider, &QSlider::valueChanged, argpSpin, [argpSpin](int v){ argpSpin->setValue(v/100.0); });
        QObject::connect(argpSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), argpSlider, [argpSlider](double v){ argpSlider->setValue(static_cast<int>(v*100)); });
        auto* argpWidget = new QWidget(elementsContent);
        auto* argpLayout = new QVBoxLayout(argpWidget);
        argpLayout->setContentsMargins(0,0,0,0);
        argpLayout->addWidget(argpSpin);
        argpLayout->addWidget(argpSlider);
        elementsForm->addRow("ω (deg)", argpWidget);

        auto* meanAnomSpin = new QDoubleSpinBox(elementsContent);
        meanAnomSpin->setDecimals(4);
        meanAnomSpin->setRange(0.0, 360.0);
        meanAnomSpin->setSingleStep(0.1);
        meanAnomSpin->setToolTip("Mean anomaly M₀ (deg)");
        meanAnomSpin->setValue(initial.meanAnomalyDeg);
        auto* meanAnomSlider = new QSlider(Qt::Horizontal, elementsContent);
        meanAnomSlider->setRange(0, 36000); // 0.0 to 360.0, step 0.01
        meanAnomSlider->setValue(static_cast<int>(initial.meanAnomalyDeg * 100));
        QObject::connect(meanAnomSlider, &QSlider::valueChanged, meanAnomSpin, [meanAnomSpin](int v){ meanAnomSpin->setValue(v/100.0); });
        QObject::connect(meanAnomSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), meanAnomSlider, [meanAnomSlider](double v){ meanAnomSlider->setValue(static_cast<int>(v*100)); });
        auto* meanAnomWidget = new QWidget(elementsContent);
        auto* meanAnomLayout = new QVBoxLayout(meanAnomWidget);
        meanAnomLayout->setContentsMargins(0,0,0,0);
        meanAnomLayout->addWidget(meanAnomSpin);
        meanAnomLayout->addWidget(meanAnomSlider);
        elementsForm->addRow("M₀ (deg)", meanAnomWidget);

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


        auto pushToGl = [this, id, aSpin, eSpin, iSpin, raanSpin, argpSpin, meanAnomSpin, segmentsSpin]() {
            OrbitalElements el;
            el.semiMajorAxis = aSpin->value();
            el.eccentricity = eSpin->value();
            el.inclinationDeg = iSpin->value();
            el.raanDeg = raanSpin->value();
            el.argPeriapsisDeg = argpSpin->value();
            el.meanAnomalyDeg = meanAnomSpin->value();
            glWidget_->updateSatellite(id, el, segmentsSpin->value());
        };

        connect(aSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(eSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(iSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(raanSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(argpSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(meanAnomSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
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

        // Default TLE (ISS) so SGP4 propagation is visible immediately.
        // These lines can be replaced later by user-input UI.
        glWidget_->setSatelliteTle(
            id,
            QStringLiteral("1 25544U 98067A   24035.51098992  .00016717  00000-0  30206-3 0  9995"),
            QStringLiteral("2 25544  51.6424  64.6985 0003317  85.3223  38.9395 15.50156700441045"));
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
