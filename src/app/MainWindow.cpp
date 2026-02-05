#include "MainWindow.h"

#include "gl/OrbitGlWidget.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Orbit Mapper");

    glWidget_ = new OrbitGlWidget(this);
    setCentralWidget(glWidget_);
}
