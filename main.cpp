#include "mainwindow.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QResource>
#include <QFile>

#include "engine.h"

int main(int argc, char *argv[])
{
    qRegisterMetaType<std::shared_ptr<const SampleBlock> >("std::shared_ptr<const SampleBlock>");
    qRegisterMetaType<std::shared_ptr<const RMSBlock> >("std::shared_ptr<const RMSBlock>");
    qRegisterMetaType<RMSBlock>("RMSBlock");
    qRegisterMetaType<std::shared_ptr<const RealFFTBlock> >("std::shared_ptr<const RealFFTBlock>");
    qRegisterMetaType<RealFFTBlock>("RealFFTBlock");

    QSurfaceFormat fmt;
    fmt.setAlphaBufferSize(8);
    fmt.setMajorVersion(3);
    fmt.setMinorVersion(3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(2);
    QSurfaceFormat::setDefaultFormat(fmt);

    QThread::currentThread()->setObjectName("sigalyze [main]");

    QApplication a(argc, argv);
    MainWindow w;
    w.show();


    return a.exec();
}
