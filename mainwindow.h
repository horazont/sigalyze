#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"

#include <QProgressBar>
#include <QOpenGLWidget>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLTexture>

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


class FFTWidget: public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit FFTWidget(
            const Engine &engine,
            const VisualisationContext &context,
            QWidget *parent = nullptr);

private:
    const Engine &m_engine;
    const VisualisationContext &m_context;
    TimedDataQueue<RealFFTBlock> m_queue;
    RealFFTBlock m_most_recent;
    std::vector<float> m_buffer;

    QOpenGLShaderProgram m_shader;
    QOpenGLBuffer m_geometry;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLTexture m_data;

    struct Vertex {
        QVector2D pos;
        QVector2D tc;
    };

public slots:
    void push_value(RealFFTBlock data);

protected:
    void initializeGL() override;
    void paintGL() override;

};


class WaterfallWidget: public QOpenGLWidget
{
    Q_OBJECT
public:
    static constexpr uint32_t MAX_SIMULTANOUS_ROWS = 2048;
    static constexpr uint32_t ROWS_PER_LAYER = 64;
    static constexpr uint32_t MAX_LAYERS = MAX_SIMULTANOUS_ROWS / ROWS_PER_LAYER;
    static_assert(MAX_LAYERS * ROWS_PER_LAYER == MAX_SIMULTANOUS_ROWS,
                  "invalid values for ROWS_PER_LAYER and MAX_SIMULTANOUS_ROWS");

public:
    explicit WaterfallWidget(
            const Engine &engine,
            const VisualisationContext &context,
            QWidget *parent = nullptr);

private:
    const Engine &m_engine;
    const VisualisationContext &m_context;
    TimedDataQueue<RealFFTBlock> m_queue;
    std::vector<float> m_buffer;
    std::vector<RealFFTBlock> m_most_recent;

    QOpenGLShaderProgram m_shader;
    QOpenGLBuffer m_geometry;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLTexture m_data;

    struct Vertex {
        QVector2D pos;
        QVector2D tc;
    };

    struct BlockPlacement {
        int layer;
    };

    std::vector<BlockPlacement> m_blocks;
    std::vector<int> m_free_layers;
    uint32_t m_last_block_rows;

private:
    void append_block();
    void append_row(const std::vector<float> &data);

public slots:
    void push_value(RealFFTBlock data);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

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
    FFTWidget *m_fft;
    WaterfallWidget *m_waterfall;

    Engine m_engine;
    VisualisationContext m_context;
    OpenAudioDeviceDialog m_audio_device_dialog;

    RootMeanSquare m_rms_calc;
    FFT m_fft_calc;

    int m_stats_timer;

private slots:
    void on_action_open_audio_device_triggered();


    // QObject interface
protected:
    void timerEvent(QTimerEvent *ev);
};

#endif // MAINWINDOW_H
