#include "mainwindow.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QResource>
#include <QFile>

#include "engine.h"

int main(int argc, char *argv[])
{
    qRegisterMetaType<SampleBlock>("SampleBlock");
    qRegisterMetaType<RMSBlock>("RMSBlock");
    qRegisterMetaType<RealFFTBlock>("RealFFTBlock");

    QSurfaceFormat fmt;
    fmt.setAlphaBufferSize(8);
    fmt.setMajorVersion(3);
    fmt.setMinorVersion(3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(2);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
