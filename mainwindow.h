#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"

#include <QProgressBar>

#include "openaudiodevicedialog.h"

#include "engine.h"


class RMSWidget: public QWidget
{
    Q_OBJECT
public:
    explicit RMSWidget(const Engine &engine, QWidget *parent = nullptr);

private:
    const Engine &m_engine;
    TimedDataQueue<RMSBlock> m_queue;
    RMSBlock m_most_recent;

public slots:
    void push_value(RMSBlock data);

    // QWidget interface
protected:
    void paintEvent(QPaintEvent *ev);

};


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);

private:
    Ui::MainWindow ui;
    QLabel *m_latency_label;
    RMSWidget *m_rms;

    QThread m_audio_thread;
    Engine m_engine;
    OpenAudioDeviceDialog m_audio_device_dialog;

    RootMeanSquare m_rms_calc;

    int m_stats_timer;

private slots:
    void on_action_open_audio_device_triggered();


    // QObject interface
protected:
    void timerEvent(QTimerEvent *ev);
};

#endif // MAINWINDOW_H
