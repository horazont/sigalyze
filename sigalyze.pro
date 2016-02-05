#-------------------------------------------------
#
# Project created by QtCreator 2016-02-03T11:28:09
#
#-------------------------------------------------

QT       += core gui multimedia widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = sigalyze
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp \
    openaudiodevicedialog.cpp \
    engine.cpp

HEADERS  += mainwindow.h \
    openaudiodevicedialog.h \
    engine.h

FORMS    += mainwindow.ui \
    openaudiodevicedialog.ui

QMAKE_CXXFLAGS += -std=c++14

unix: CONFIG += link_pkgconfig
unix: PKGCONFIG += fftw3

DISTFILES += \
    fft.frag \
    fft.vert \
    waterfall.vert \
    waterfall.frag

RESOURCES += \
    data.qrc

unix: PKGCONFIG += epoxy
