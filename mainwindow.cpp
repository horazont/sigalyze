#include "epoxy/gl.h"

#include "mainwindow.h"

#include <iostream>

#include <QFileDialog>
#include <QPaintEvent>
#include <QPainter>
#include <QVector2D>



template <typename T>
struct OverrideIterator
{
public:
    OverrideIterator():
        m_dest(nullptr)
    {

    }

    OverrideIterator(T *dest):
        m_dest(dest)
    {

    }

    OverrideIterator(const OverrideIterator &other) = default;
    OverrideIterator &operator=(const OverrideIterator &other) = default;

private:
    T *m_dest;

public:
    T &operator*()
    {
        return *m_dest;
    }

    T &operator->()
    {
        return *m_dest;
    }

    OverrideIterator &operator++(int)
    {
        return *this;
    }

    OverrideIterator operator++()
    {
        return *this;
    }

    bool operator==(const OverrideIterator &other) const
    {
        return m_dest == other.m_dest;
    }

};



float VisualisationContext::map_db(float dB) const
{
    return std::min(std::max((dB - dB_min) / (dB_max - dB_min), 0.f), 1.f);
}


/* RMSWidget */

RMSWidget::RMSWidget(const Engine &engine,
                     const VisualisationContext &context,
                     QWidget *parent):
    QWidget(parent),
    m_engine(engine),
    m_context(context),
    m_queue(32)
{
    setMinimumWidth(128);
}

void RMSWidget::push_value(RMSBlock data)
{
    m_queue.push_block(std::move(data));
    update();
}

void RMSWidget::paintEvent(QPaintEvent*)
{
    if (m_engine.is_running()) {
        m_queue.fetch_up_to(m_engine.sink_time(), OverrideIterator<RMSBlock>(&m_most_recent));
    }

    const float curr_db = m_context.map_db(20*std::log10(m_most_recent.curr));
    const float peak_db = m_context.map_db(20*std::log10(m_most_recent.recent_peak));

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0));
    painter.drawRect(0, 0, size().width() * curr_db, size().height());

    painter.setPen(QColor(255, 0, 0));
    painter.drawLine(size().width() * peak_db, 0,
                     size().width() * peak_db, size().height());
}


/* FFTWidget */

FFTWidget::FFTWidget(const Engine &engine,
                     const VisualisationContext &context,
                     QWidget *parent):
    QOpenGLWidget(parent),
    m_engine(engine),
    m_context(context),
    m_queue(128),
    m_data(QOpenGLTexture::Target1D)
{
    setMinimumHeight(250);
}

void FFTWidget::push_value(RealFFTBlock data)
{
    m_queue.push_block(std::move(data));
    update();
}

void FFTWidget::initializeGL()
{
    if (!m_shader.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                          ":/shaders/fft.vert") or
            !m_shader.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                              ":/shaders/fft.frag") or
            !m_shader.link())
    {
        throw std::runtime_error("failed to load fft shaders");
    }

    static const Vertex data[] = {
        {QVector2D(-1, 1), QVector2D(0, 1)},
        {QVector2D(-1, -1), QVector2D(0, 0)},
        {QVector2D(1, 1), QVector2D(1, 1)},
        {QVector2D(1, -1), QVector2D(1, 0)},
    };

    m_vao.create();
    m_vao.bind();

    m_geometry.create();
    m_geometry.bind();
    m_geometry.allocate(data, sizeof(data));

    m_shader.bind();
    int attr_loc = m_shader.attributeLocation("position");
    m_shader.enableAttributeArray(attr_loc);
    m_shader.setAttributeBuffer(attr_loc, GL_FLOAT, 0, 2, sizeof(Vertex));

    attr_loc = m_shader.attributeLocation("tc0");
    m_shader.enableAttributeArray(attr_loc);
    m_shader.setAttributeBuffer(attr_loc, GL_FLOAT, sizeof(QVector2D), 2, sizeof(Vertex));

    m_shader.setUniformValue("data", 0);

    m_vao.release();
}

void FFTWidget::paintGL()
{
    if (m_engine.is_running()) {
        m_queue.fetch_up_to(m_engine.sink_time(), OverrideIterator<RealFFTBlock>(&m_most_recent));
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    m_shader.bind();
    m_vao.bind();

    m_shader.setUniformValue("dB_min", m_context.dB_min);
    m_shader.setUniformValue("dB_max", m_context.dB_max);

    if (m_data.textureId() == 0 && m_most_recent.fft.size() != 0) {
        m_data.create();
        m_data.bind();
        m_data.setSize(m_most_recent.fft.size());
        m_data.setFormat(QOpenGLTexture::R32F);
        m_data.allocateStorage();
        m_data.setMagnificationFilter(QOpenGLTexture::Linear);
        m_data.setMinificationFilter(QOpenGLTexture::Linear);
    }

    if (m_data.textureId() != 0) {
        m_data.bind();

        m_buffer.clear();
        m_buffer.reserve(m_most_recent.fft.size());
        std::copy(m_most_recent.fft.begin(),
                  m_most_recent.fft.end(),
                  std::back_inserter(m_buffer));

        glTexSubImage1D(GL_TEXTURE_1D, 0, 0, m_buffer.size(),
                        GL_RED, GL_FLOAT,
                        m_buffer.data());
    }


    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}


/* WaterfallWidget */

WaterfallWidget::WaterfallWidget(const Engine &engine,
                                 const VisualisationContext &context,
                                 QWidget *parent):
    QOpenGLWidget(parent),
    m_engine(engine),
    m_context(context),
    m_queue(64),
    m_data(QOpenGLTexture::Target2DArray),
    m_last_block_rows(0)
{
    setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
    sizePolicy().setHorizontalStretch(1);
    sizePolicy().setVerticalStretch(1);
    m_free_layers.reserve(MAX_LAYERS);
    for (int32_t i = MAX_LAYERS-1; i >= 0; --i) {
        m_free_layers.emplace_back(i);
    }
}

void WaterfallWidget::append_block()
{
    if (m_blocks.size() == MAX_LAYERS) {
        m_free_layers.push_back(m_blocks.front().layer);
        m_blocks.erase(m_blocks.begin());
    }

    m_blocks.emplace_back();
    m_blocks.back().layer = m_free_layers.back();
    m_free_layers.erase(m_free_layers.end()-1);
    m_last_block_rows = 0;
}

void WaterfallWidget::append_row(const std::vector<float> &data)
{
    if (m_blocks.empty() or m_last_block_rows == ROWS_PER_LAYER) {
        append_block();
    }
    const BlockPlacement &last = m_blocks.back();
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                    0, m_last_block_rows, last.layer,
                    data.size(), 1, 1,
                    GL_RED,
                    GL_FLOAT,
                    data.data());
    m_last_block_rows += 1;
}

void WaterfallWidget::push_value(RealFFTBlock data)
{
    m_queue.push_block(std::move(data));
    update();
}

void WaterfallWidget::initializeGL()
{
    if (!m_shader.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                          ":/shaders/waterfall.vert") or
            !m_shader.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                              ":/shaders/waterfall.frag") or
            !m_shader.link())
    {
        throw std::runtime_error("failed to load waterfall shaders");
    }

    static const Vertex data[] = {
        {QVector2D(-1, ROWS_PER_LAYER), QVector2D(0, 0.5/ROWS_PER_LAYER)},
        {QVector2D(-1, 0), QVector2D(0, 1-0.5/ROWS_PER_LAYER)},
        {QVector2D(1, ROWS_PER_LAYER), QVector2D(1, 0.5/ROWS_PER_LAYER)},
        {QVector2D(1, 0), QVector2D(1, 1-0.5/ROWS_PER_LAYER)},
    };

    m_vao.create();
    m_vao.bind();

    m_geometry.create();
    m_geometry.bind();
    m_geometry.allocate(data, sizeof(data));

    m_shader.bind();
    int attr_loc = m_shader.attributeLocation("position");
    m_shader.enableAttributeArray(attr_loc);
    m_shader.setAttributeBuffer(attr_loc, GL_FLOAT, 0, 2, sizeof(Vertex));

    attr_loc = m_shader.attributeLocation("tc0");
    m_shader.enableAttributeArray(attr_loc);
    m_shader.setAttributeBuffer(attr_loc, GL_FLOAT, sizeof(QVector2D), 2, sizeof(Vertex));

    m_shader.setUniformValue("data", 0);

    m_vao.release();
}

void WaterfallWidget::paintGL()
{
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    if (m_engine.is_running()) {
        m_queue.fetch_up_to(m_engine.sink_time(),
                            std::back_inserter(m_most_recent));
        if (m_most_recent.size() > 0 and m_data.textureId() == 0) {
            m_data.create();
            m_data.bind();
            m_data.setSize(
                        m_most_recent[0].fft.size(),
                    ROWS_PER_LAYER);
            m_data.setLayers(MAX_LAYERS);
            m_data.setFormat(QOpenGLTexture::R32F);
            m_data.allocateStorage();
            m_data.setMagnificationFilter(QOpenGLTexture::Linear);
            m_data.setMinificationFilter(QOpenGLTexture::Linear);
        } else if (m_data.textureId() != 0) {
            m_data.bind();
        }
        for (const RealFFTBlock &row: m_most_recent) {
            m_buffer.clear();
            m_buffer.reserve(row.fft.size());
            std::copy(row.fft.begin(),
                      row.fft.end(),
                      std::back_inserter(m_buffer));
            append_row(m_buffer);
        }
        m_most_recent.clear();
    }

    m_shader.setUniformValue("dB_min", m_context.dB_min);
    m_shader.setUniformValue("dB_max", m_context.dB_max);

    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_shader.bind();
    m_vao.bind();

    float offset = (int32_t)m_last_block_rows - (int32_t)ROWS_PER_LAYER;
    for (auto iter = m_blocks.crbegin();
         iter != m_blocks.crend();
         ++iter)
    {
        m_shader.setUniformValue("offset", offset);
        m_shader.setUniformValue("layer", iter->layer);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        offset += ROWS_PER_LAYER;
    }

}

void WaterfallWidget::resizeGL(int w, int h)
{
    QMatrix4x4 mat;
    mat.setToIdentity();
    mat.translate(0, -1, 0);
    mat.scale(1, 2.f/h, 1);
    m_shader.bind();
    m_shader.setUniformValue("proj", mat);
}



/* MainWindow */

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    m_latency_label(nullptr),
    m_rms_calc(m_engine),
    m_fft_calc(m_engine, 4096, 25)
{
    ui.setupUi(this);
    m_engine.set_output_device(QAudioDeviceInfo::defaultOutputDevice());
    m_context.dB_min = -192;
    m_context.dB_max = 0;
    m_stats_timer = startTimer(1000);

    m_latency_label = new QLabel(this);
    ui.statusBar->addPermanentWidget(m_latency_label);

    m_rms = new RMSWidget(m_engine, m_context, this);
    connect(&m_rms_calc.processor(), &RMSProcessor::result_available,
            m_rms, &RMSWidget::push_value,
            Qt::QueuedConnection);
    ui.statusBar->addPermanentWidget(m_rms);

    m_fft = new FFTWidget(m_engine, m_context, this);
    connect(&m_fft_calc.processor(), &FFTProcessor::result_available,
            m_fft, &FFTWidget::push_value,
            Qt::QueuedConnection);

    m_waterfall = new WaterfallWidget(m_engine, m_context, this);
    connect(&m_fft_calc.processor(), &FFTProcessor::result_available,
            m_waterfall, &WaterfallWidget::push_value,
            Qt::QueuedConnection);

    centralWidget()->layout()->addWidget(m_waterfall);
    centralWidget()->layout()->addWidget(m_fft);
}

void MainWindow::on_action_open_audio_device_triggered()
{
    m_audio_device_dialog.refresh();
    if (m_audio_device_dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (m_engine.is_running()) {
        m_engine.stop();
    }
    m_engine.set_source(std::make_unique<AudioInputSource>(
                            m_audio_device_dialog.device(),
                            m_audio_device_dialog.format(),
                            0.001));
    m_engine.start();
    m_context.dB_min = -std::log10(2ULL << (uint64_t)m_audio_device_dialog.format().sampleSize())*20;
}

void MainWindow::timerEvent(QTimerEvent *ev)
{
    if (ev->timerId() == m_stats_timer) {
        if (m_engine.is_running()) {
            m_latency_label->setText(
                        QString("Output latency: %1 ms").arg(std::chrono::duration_cast<std::chrono::duration<float, std::ratio<1, 1000> > >(global_clock::now() - m_engine.sink_time()).count())
                        );
        } else {
            m_latency_label->setText("idle");
        }
        return;
    }
    QMainWindow::timerEvent(ev);
}

void MainWindow::on_action_quit_triggered()
{
    close();
}
