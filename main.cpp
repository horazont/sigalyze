#include "mainwindow.h"
#include <QApplication>

#include "engine.h"

int main(int argc, char *argv[])
{
    qRegisterMetaType<SampleBlock>("SampleBlock");
    qRegisterMetaType<RMSBlock>("RMSBlock");

    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
