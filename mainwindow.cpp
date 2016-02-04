#include "mainwindow.h"

#include <iostream>

#include <QFileDialog>
#include <QPaintEvent>
#include <QPainter>


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


/* RMSWidget */

RMSWidget::RMSWidget(const Engine &engine, QWidget *parent):
    QWidget(parent),
    m_engine(engine),
    m_queue(32)
{
    setMinimumWidth(128);
}

void RMSWidget::push_value(RMSBlock data)
{
    m_queue.push_block(std::move(data));
    update();
}

void RMSWidget::paintEvent(QPaintEvent *ev)
{
    m_queue.fetch_up_to(m_engine.output_time(), OverrideIterator<RMSBlock>(&m_most_recent));

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0));
    painter.drawRect(0, 0, size().width() * m_most_recent.curr, size().height());
}



MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    m_latency_label(nullptr),
    m_rms_calc(m_engine)
{
    ui.setupUi(this);
    m_engine.set_audio_output(QAudioDeviceInfo::defaultOutputDevice());
    m_audio_thread.start();
    m_engine.moveToThread(&m_audio_thread);
    m_stats_timer = startTimer(1000);

    m_latency_label = new QLabel(this);
    ui.statusBar->addPermanentWidget(m_latency_label);

    m_rms = new RMSWidget(m_engine, this);
    connect(&m_rms_calc.processor(), &RMSProcessor::result_available,
            m_rms, &RMSWidget::push_value,
            Qt::QueuedConnection);

    ui.statusBar->addPermanentWidget(m_rms);

}

void MainWindow::on_action_open_audio_device_triggered()
{
    m_audio_device_dialog.refresh();
    m_audio_device_dialog.exec();
    m_engine.set_source(std::make_unique<AudioInputSource>(std::make_unique<QAudioInput>(
                                                               m_audio_device_dialog.device(),
                                                               m_audio_device_dialog.format()
                                                               )));
    m_engine.start();
}

void MainWindow::timerEvent(QTimerEvent *ev)
{
    if (ev->timerId() == m_stats_timer) {
        m_latency_label->setText(
                    QString("Output latency: %1 ms").arg(std::chrono::duration_cast<std::chrono::duration<float, std::ratio<1, 1000> > >(global_clock::now() - m_engine.output_time()).count())
                    );
        return;
    }
    QMainWindow::timerEvent(ev);
}
