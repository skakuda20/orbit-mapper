#include "MainWindow.h"

#include "gl/OrbitGlWidget.h"

#include <QDockWidget>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSlider>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QFormLayout>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <vector>

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

static bool parseEphemerisText(
    const QString& text,
    std::chrono::system_clock::time_point baseTime,
    std::vector<EphemerisSample>& outSamples,
    QString& outError)
{
    outSamples.clear();

    const QStringList lines = text.split('\n');
    int lineNum = 0;
    for (const QString& rawLine : lines) {
        ++lineNum;
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        line.replace(',', ' ');
        QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

        // Support timestamps that are split as "YYYY-MM-DD HH:MM:SS(.sss)Z"
        // by collapsing the first two tokens into ISO "YYYY-MM-DDTHH:MM:SS...".
        int idx = 0;
        QString timeToken;
        if (parts.size() >= 8 && parts[0].contains('-') && parts[1].contains(':')) {
            timeToken = parts[0] + 'T' + parts[1];
            idx = 2;
        } else if (parts.size() >= 7) {
            timeToken = parts[0];
            idx = 1;
        } else {
            outError = QStringLiteral("Line %1: expected at least 7 fields (t x y z vx vy vz)").arg(lineNum);
            return false;
        }

        std::chrono::system_clock::time_point tp{};
        {
            const QString tNorm = timeToken;
            QDateTime dt = QDateTime::fromString(tNorm, Qt::ISODateWithMs);
            if (!dt.isValid()) {
                dt = QDateTime::fromString(tNorm, Qt::ISODate);
            }

            if (dt.isValid()) {
                dt = dt.toUTC();
                tp = std::chrono::system_clock::time_point{std::chrono::milliseconds(dt.toMSecsSinceEpoch())};
            } else {
                bool ok = false;
                const double secs = tNorm.toDouble(&ok);
                if (!ok) {
                    outError = QStringLiteral("Line %1: invalid time '%2' (use ISO-8601 or seconds)").arg(lineNum).arg(timeToken);
                    return false;
                }

                // Heuristic: treat large values as Unix seconds; otherwise as seconds offset from baseTime.
                if (secs >= 946684800.0) {
                    const qint64 ms = static_cast<qint64>(std::llround(secs * 1000.0));
                    tp = std::chrono::system_clock::time_point{std::chrono::milliseconds(ms)};
                } else {
                    tp = baseTime + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::duration<double>(secs));
                }
            }
        }

        auto parseDouble = [&](int p, const char* label, double& outVal) -> bool {
            if (p >= parts.size()) {
                outError = QStringLiteral("Line %1: missing %2").arg(lineNum).arg(QString::fromUtf8(label));
                return false;
            }
            bool ok = false;
            outVal = parts[p].toDouble(&ok);
            if (!ok) {
                outError = QStringLiteral("Line %1: invalid %2 '%3'").arg(lineNum).arg(QString::fromUtf8(label)).arg(parts[p]);
                return false;
            }
            return true;
        };

        EphemerisSample s;
        s.t = tp;
        if (!parseDouble(idx + 0, "x", s.positionKm[0]) ||
            !parseDouble(idx + 1, "y", s.positionKm[1]) ||
            !parseDouble(idx + 2, "z", s.positionKm[2]) ||
            !parseDouble(idx + 3, "vx", s.velocityKmPerS[0]) ||
            !parseDouble(idx + 4, "vy", s.velocityKmPerS[1]) ||
            !parseDouble(idx + 5, "vz", s.velocityKmPerS[2])) {
            return false;
        }

        outSamples.push_back(s);
    }

    if (outSamples.size() < 2) {
        outError = QStringLiteral("Need at least 2 ephemeris samples.");
        return false;
    }

    return true;
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

    auto* clockLabel = new QLabel(bottomBar);
    clockLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bottomLayout->addWidget(clockLabel);

    auto* nowBtn = new QPushButton("Now", bottomBar);
    bottomLayout->addWidget(nowBtn);

    // UTC time input field
    auto* timeInput = new QLineEdit(bottomBar);
    timeInput->setPlaceholderText("Enter UTC time (YYYY-MM-DD HH:MM:SS)");
    timeInput->setMaximumWidth(250);
    bottomLayout->addWidget(timeInput);

    connect(timeInput, &QLineEdit::returnPressed, this, [this, timeInput]() {
        const QString input = timeInput->text().trimmed();
        if (input.isEmpty()) {
            return;
        }

        // Try parsing as ISO-8601 with various formats
        QDateTime dt = QDateTime::fromString(input, Qt::ISODateWithMs);
        if (!dt.isValid()) {
            dt = QDateTime::fromString(input, Qt::ISODate);
        }
        if (!dt.isValid()) {
            // Try without 'T' separator (space instead)
            dt = QDateTime::fromString(input, "yyyy-MM-dd HH:mm:ss");
        }
        if (!dt.isValid()) {
            dt = QDateTime::fromString(input, "yyyy-MM-dd HH:mm:ss.zzz");
        }

        if (dt.isValid()) {
            // Explicitly set as UTC time (not local time)
            dt.setTimeSpec(Qt::UTC);
            const auto tp = std::chrono::system_clock::time_point{std::chrono::milliseconds(dt.toMSecsSinceEpoch())};
            glWidget_->setSimulationTime(tp);
            timeInput->clear();
        } else {
            // Show error by changing background color temporarily
            timeInput->setStyleSheet("QLineEdit { background-color: #ffcccc; }");
            QTimer::singleShot(1500, timeInput, [timeInput]() {
                timeInput->setStyleSheet("");
            });
        }
    });

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

    connect(nowBtn, &QPushButton::clicked, this, [this]() {
        glWidget_->setSimulationTime(std::chrono::system_clock::now());
    });

    auto* clockTimer = new QTimer(this);
    clockTimer->setInterval(250);
    connect(clockTimer, &QTimer::timeout, this, [this, clockLabel]() {
        const auto tp = glWidget_->simulationTime();
        const auto secTp = std::chrono::time_point_cast<std::chrono::seconds>(tp);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp - secTp).count();

        const qint64 secs = static_cast<qint64>(std::chrono::system_clock::to_time_t(secTp));
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(secs, Qt::UTC).addMSecs(ms);
        clockLabel->setText(QStringLiteral("Sim (UTC): %1").arg(dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))));
    });
    clockTimer->start();

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
    auto* addTleBtn = new QPushButton("Add from TLE", panel);
    auto* addEphemBtn = new QPushButton("Add from Ephemeris", panel);
    auto* topBtnLayout = new QHBoxLayout();
    topBtnLayout->addWidget(addBtn);
    topBtnLayout->addWidget(addTleBtn);
    topBtnLayout->addWidget(addEphemBtn);
    panelLayout->addLayout(topBtnLayout);

    auto* scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    panelLayout->addWidget(scroll, 1);

    auto* listHost = new QWidget(scroll);
    auto* listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(10);
    listLayout->addStretch(1);
    scroll->setWidget(listHost);

    auto addSatelliteEditor = [this, listHost, listLayout](
                                  int id,
                                  const QString& name,
                                  const OrbitalElements& initial,
                                  bool elementsEditable) {
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

        if (!elementsEditable) {
            // TLE/SGP4-driven satellites ignore manual orbital elements.
            // Hide the manual controls to avoid confusing “sliders don’t work” UX.
            elementsGroup->setVisible(false);
        }

        auto* elementsForm = new QFormLayout(elementsContent);
        elementsForm->setContentsMargins(0, 0, 0, 0);

        // Add sliders and spinboxes for each element
        auto* aSpin = new QDoubleSpinBox(elementsContent);
        aSpin->setDecimals(1);
        aSpin->setRange(6378.137, 6378.137 * 3.0); // Earth radius to 3x Earth radius in km
        aSpin->setSingleStep(1.0);
        aSpin->setToolTip("Semi-major axis a (km)");
        // Convert Earth radii to km for display
        aSpin->setValue(initial.semiMajorAxis * 6378.137);
        auto* aSlider = new QSlider(Qt::Horizontal, elementsContent);
        aSlider->setRange(6378, 19134); // ~6378 km to ~19134 km
        aSlider->setValue(static_cast<int>(initial.semiMajorAxis * 6378.137));
        QObject::connect(aSlider, &QSlider::valueChanged, aSpin, [aSpin](int v){ aSpin->setValue(v); });
        QObject::connect(aSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), aSlider, [aSlider](double v){ aSlider->setValue(static_cast<int>(v)); });
        auto* aWidget = new QWidget(elementsContent);
        auto* aLayout = new QVBoxLayout(aWidget);
        aLayout->setContentsMargins(0,0,0,0);
        aLayout->addWidget(aSpin);
        aLayout->addWidget(aSlider);
        elementsForm->addRow("a (km)", aWidget);

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

        auto pushToGl = [this, id, aSpin, eSpin, iSpin, raanSpin, argpSpin, meanAnomSpin]() {
            OrbitalElements el;
            // Convert kilometers to Earth radii (Earth radius = 6378.137 km)
            el.semiMajorAxis = aSpin->value() / 6378.137;
            el.eccentricity = eSpin->value();
            el.inclinationDeg = iSpin->value();
            el.raanDeg = raanSpin->value();
            el.argPeriapsisDeg = argpSpin->value();
            el.meanAnomalyDeg = meanAnomSpin->value();
            glWidget_->updateSatellite(id, el, 512); // Use fixed 512 segments
        };

        connect(aSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(eSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(iSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(raanSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(argpSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });
        connect(meanAnomSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [pushToGl](double) { pushToGl(); });

        connect(removeBtn, &QPushButton::clicked, this, [this, id, satGroup]() {
            glWidget_->removeSatellite(id);
            satGroup->deleteLater();
        });

        // Insert before the stretch at the end
        listLayout->insertWidget(listLayout->count() - 1, satGroup);
    };

    auto getElementsForSatelliteId = [this](int id, const OrbitalElements& fallback) {
        for (const auto& info : glWidget_->satellites()) {
            if (info.id == id) {
                return info.elements;
            }
        }
        return fallback;
    };

    // Start with one satellite by default
    {
        const OrbitalElements el = defaultLeoElements();
        const int segments = 512;
        const QString name = QString("Satellite %1").arg(nextSatelliteNumber_++);
        const int id = glWidget_->addSatellite(name, el, segments);

        // Keep the initial satellite editable (Kepler-driven) by default.
        addSatelliteEditor(id, name, el, /*elementsEditable=*/true);
    }

    connect(addBtn, &QPushButton::clicked, this, [this, addSatelliteEditor]() mutable {
        const OrbitalElements el = defaultLeoElements();
        const int segments = 512;
        const QString name = QString("Satellite %1").arg(nextSatelliteNumber_++);
        const int id = glWidget_->addSatellite(name, el, segments);
        addSatelliteEditor(id, name, el, /*elementsEditable=*/true);
    });

    connect(addTleBtn,
            &QPushButton::clicked,
            this,
            [this, addSatelliteEditor, getElementsForSatelliteId]() mutable {
        // TLE input dialog
        auto* dialog = new QDialog(this);
        dialog->setWindowTitle("Add Satellite from TLE");
        dialog->setMinimumWidth(500);

        auto* layout = new QVBoxLayout(dialog);

        auto* instrLabel = new QLabel("Paste TLE data (two lines):", dialog);
        layout->addWidget(instrLabel);

        auto* textEdit = new QPlainTextEdit(dialog);
        textEdit->setPlaceholderText("1 25544U 98067A   24035.51098992  .00016717  00000-0  30206-3 0  9995\n"
                                      "2 25544  51.6424  64.6985 0003317  85.3223  38.9395 15.50156700441045");
        layout->addWidget(textEdit);

        auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
        layout->addWidget(buttonBox);

        connect(buttonBox,
            &QDialogButtonBox::accepted,
            dialog,
            [this, dialog, textEdit, addSatelliteEditor, getElementsForSatelliteId]() {
            const QString text = textEdit->toPlainText();
            const QStringList lines = text.split('\n', Qt::SkipEmptyParts);

            if (lines.size() < 2) {
                QMessageBox::warning(this, "Error", "Please provide both TLE lines.");
                return;
            }

            const QString line1 = lines[0].trimmed();
            const QString line2 = lines[1].trimmed();

            // Create satellite with default Keplerian elements
            const OrbitalElements el = defaultLeoElements();
            const int segments = 512;
            const QString name = QString("Satellite %1").arg(nextSatelliteNumber_++);
            const int id = glWidget_->addSatellite(name, el, segments);

            // Load TLE and sync orbit
            const bool tleOk = glWidget_->setSatelliteTle(id, line1, line2);
            if (!tleOk) {
                QMessageBox::warning(
                    this,
                    "SGP4 disabled",
                    "This build was compiled without SGP4 support, so TLE propagation is unavailable.\n\n"
                    "Reconfigure with -DORBIT_MAPPER_ENABLE_SGP4=ON and rebuild.");
            }

            // Show mean elements (if available) but disable editing for TLE satellites.
            const OrbitalElements uiEl = tleOk ? getElementsForSatelliteId(id, el) : el;
            addSatelliteEditor(id, name, uiEl, /*elementsEditable=*/!tleOk);

            dialog->accept();
        });

        connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

        dialog->exec();
        dialog->deleteLater();
    });

    connect(addEphemBtn,
            &QPushButton::clicked,
            this,
            [this, addSatelliteEditor]() mutable {
        auto* dialog = new QDialog(this);
        dialog->setWindowTitle("Add Satellite from Ephemeris");
        dialog->setMinimumWidth(650);

        auto* layout = new QVBoxLayout(dialog);
        auto* instrLabel = new QLabel(
            "Paste ephemeris samples, one per line:\n"
            "  t x y z vx vy vz\n\n"
            "t: ISO-8601 (UTC recommended) or seconds (Unix seconds, or small offsets from current sim time)\n"
            "Units: km and km/s (ECI axes)",
            dialog);
        instrLabel->setWordWrap(true);
        layout->addWidget(instrLabel);

        auto* textEdit = new QPlainTextEdit(dialog);
        textEdit->setPlaceholderText(
            "2026-02-14T12:00:00Z 7000 0 0 0 7.5 1.0\n"
            "2026-02-14T12:01:00Z 6950 450 30 -0.2 7.48 1.05");
        layout->addWidget(textEdit);

        auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
        layout->addWidget(buttonBox);

        connect(buttonBox,
                &QDialogButtonBox::accepted,
                dialog,
                [this, dialog, textEdit, addSatelliteEditor]() {
            std::vector<EphemerisSample> samples;
            QString error;
            const bool ok = parseEphemerisText(textEdit->toPlainText(), glWidget_->simulationTime(), samples, error);
            if (!ok) {
                QMessageBox::warning(this, "Error", error);
                return;
            }

            const OrbitalElements el = defaultLeoElements();
            const int segments = 512;
            const QString name = QString("Satellite %1").arg(nextSatelliteNumber_++);
            const int id = glWidget_->addSatellite(name, el, segments);

            const bool ephOk = glWidget_->setSatelliteEphemeris(id, samples);
            if (!ephOk) {
                QMessageBox::warning(this, "Error", "Failed to apply ephemeris to satellite.");
                glWidget_->removeSatellite(id);
                return;
            }

            addSatelliteEditor(id, name, el, /*elementsEditable=*/false);
            dialog->accept();
        });

        connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

        dialog->exec();
        dialog->deleteLater();
    });

    dock->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}