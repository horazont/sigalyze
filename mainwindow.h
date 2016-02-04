#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"

#include <QProgressBar>

#include "openaudiodevicedialog.h"

#include "engine.h"


struct VisualisationContext
{
    float dB_min;
    float dB_max;

    float map_db(float dB) const;
};


class RMSWidget: public QWidget
{
    Q_OBJECT
public:
    explicit RMSWidget(
            const Engine &engine,
            const VisualisationContext &context,
            QWidget *parent = nullptr);

private:
    const Engine &m_engine;
    const VisualisationContext &m_context;
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

    Engine m_engine;
    VisualisationContext m_context;
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
