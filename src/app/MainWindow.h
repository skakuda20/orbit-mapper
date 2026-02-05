#pragma once

#include <QMainWindow>

class OrbitGlWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    OrbitGlWidget* glWidget_ = nullptr;
};
