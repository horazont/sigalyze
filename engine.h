#ifndef ENGINE_H
#define ENGINE_H

#include <atomic>
#include <complex>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <iostream>

#include <QAudioInput>
#include <QAudioOutput>
#include <QThread>
#include <QObject>

#include "fftw3.h"

typedef std::chrono::steady_clock global_clock;


struct TimestampedData
{
    global_clock::time_point t;
};


struct SampleBlock: public TimestampedData
{
    uint32_t sample_rate;
    std::vector<float> mono_samples;
    std::vector<float> original_samples;
};


struct RealFFTBlock: public TimestampedData
{
    std::vector<double> fft;
    float fmax;
};


struct RMSBlock: public TimestampedData
{
    float curr;
    float recent_peak;
};


class VirtualAudioSource: public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual ~VirtualAudioSource() = default;

public:
    virtual uint32_t channel_count() const = 0;
    virtual bool is_seekable() const;
    virtual void pause() = 0;
    virtual bool read_samples(uint64_t start, uint64_t count,
                              std::vector<float> &dest);
    virtual void resume() = 0;
    virtual uint32_t sample_rate() const = 0;
    virtual uint64_t samples() const;
    virtual void seek(uint64_t sample);
    virtual void start() = 0;
    virtual QAudio::State state() const;
    virtual void stop() = 0;

signals:
    void samples_available(const SampleBlock *samples);
    void state_changed(QAudio::State new_state);

};


class AbstractSampleConverter
{
public:
    virtual ~AbstractSampleConverter();

public:
    virtual void read_and_convert(QIODevice *source,
                                  uint32_t bytes_to_read,
                                  std::vector<float> &dest) = 0;

public:
    static std::unique_ptr<AbstractSampleConverter> make_converter(
            QAudioFormat::SampleType sample_type,
            uint32_t bits);
};


class AudioInputSource: public VirtualAudioSource
{
    Q_OBJECT

public:
    explicit AudioInputSource(std::unique_ptr<QAudioInput> &&from, QObject *parent = nullptr);
    ~AudioInputSource() override;

private:
    std::unique_ptr<QAudioInput> m_input;
    QIODevice *m_source;
    std::unique_ptr<AbstractSampleConverter> m_converter;
    SampleBlock m_buffer;
    int m_sampling_timer;

private:
    void downmix_to_mono(const std::vector<float> &src, std::vector<float> &dest);
    void on_notify();

    // QObject interface
protected:
    void timerEvent(QTimerEvent *ev);

public:
    uint32_t channel_count() const;
    void pause();
    void resume();
    uint32_t sample_rate() const;
    void start();
    QAudio::State state() const;
    void stop();
};


template <typename data_t>
class TimedDataQueue
{
public:
    TimedDataQueue(const uint32_t max_blocks):
        m_max_blocks(max_blocks)
    {

    }

private:
    uint32_t m_max_blocks;
    std::queue<data_t> m_blocks;

public:
    template <typename OutputIterator>
    inline void fetch_up_to(const global_clock::time_point &t,
                            OutputIterator dest)
    {
        while (!m_blocks.empty()) {
            data_t &block = m_blocks.front();
            if (block.t <= t) {
                *dest++ = std::move(block);
                m_blocks.pop();
            } else {
                break;
            }
        }
    }

    inline void push_block(data_t &&block)
    {
        if (m_blocks.size() >= m_max_blocks) {
            m_blocks.pop();
        }
        m_blocks.emplace(std::move(block));
    }

};


using SampleQueue = TimedDataQueue<SampleBlock>;


class Engine;


class RMSProcessor: public QObject
{
    Q_OBJECT
public:
    RMSProcessor() = delete;
    explicit RMSProcessor(const Engine &engine);
    RMSProcessor(const RMSProcessor &other) = delete;
    RMSProcessor(RMSProcessor &&src) = delete;
    RMSProcessor &operator=(const RMSProcessor &other) = delete;
    RMSProcessor &operator=(RMSProcessor &&src) = delete;

private:
    global_clock::time_point m_t0;
    uint32_t m_sample_rate;
    std::vector<float> m_sample_buffer;

    std::array<float, 32> m_backlog;
    decltype(m_backlog)::size_type m_backlog_index;

private slots:
    void process_samples(SampleBlock data);

private:
    float get_recent_peak();

signals:
    void result_available(RMSBlock data);

};


class RootMeanSquare: public QThread
{
    Q_OBJECT

public:
    RootMeanSquare() = delete;
    explicit RootMeanSquare(const Engine &engine);
    RootMeanSquare(const RootMeanSquare &other) = delete;
    RootMeanSquare(RootMeanSquare &&src) = delete;
    RootMeanSquare &operator=(const RootMeanSquare &other) = delete;
    RootMeanSquare &operator=(RootMeanSquare &&src) = delete;

private:
    RMSProcessor m_processor;

public:
    inline const RMSProcessor &processor() const
    {
        return m_processor;
    }

};


class FFTProcessor: public QObject
{
    Q_OBJECT
public:
    FFTProcessor() = delete;
    explicit FFTProcessor(const Engine &engine,
                          uint32_t size,
                          uint32_t period_msec);
    FFTProcessor(const FFTProcessor &other) = delete;
    FFTProcessor(FFTProcessor &&src) = delete;
    FFTProcessor &operator=(const FFTProcessor &other) = delete;
    FFTProcessor &operator=(FFTProcessor &&src) = delete;

private:
    std::vector<double> m_in;
    std::vector<std::complex<double> > m_out_buffer;
    const uint32_t m_size;
    fftw_plan m_plan;
    uint32_t m_period_msec;
    global_clock::time_point m_t;
    uint32_t m_sample_rate;
    uint32_t m_shift_remaining;

    std::vector<double> m_in_buffer;
    std::vector<double> m_window;
    RealFFTBlock m_out;

private:
    void make_window(std::vector<double> &dest);

private slots:
    void process_samples(SampleBlock data);

signals:
    void result_available(RealFFTBlock data);

};


class FFT: public QThread
{
    Q_OBJECT

public:
    FFT() = delete;
    explicit FFT(const Engine &engine,
                 uint32_t size,
                 uint32_t period_msec);
    FFT(const FFT &other) = delete;
    FFT(FFT &&src) = delete;
    FFT &operator=(const FFT &other) = delete;
    FFT &operator=(FFT &&src) = delete;

private:
    FFTProcessor m_processor;

public:
    inline const FFTProcessor &processor() const
    {
        return m_processor;
    }

};


class AbstractOutputDriver: public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

public:
    virtual uint32_t sample_rate() = 0;
    virtual global_clock::time_point time() = 0;

public:
    virtual void samples_available(const std::vector<float> &samples);

};


class AudioOutputDriver: public AbstractOutputDriver
{
    Q_OBJECT
public:
    explicit AudioOutputDriver(
            std::unique_ptr<QAudioOutput> &&output,
            int32_t buffer_msecs = 100,
            uint32_t drop_msecs = 1000,
            QObject *parent = nullptr);

private:
    std::unique_ptr<QAudioOutput> m_output;
    QIODevice *m_sink;
    int64_t m_samples_written;
    uint32_t m_drop_samples;
    std::chrono::microseconds m_buffer_delay;
    std::chrono::microseconds m_dropped;
    global_clock::time_point m_t0;
    std::atomic<global_clock::time_point> m_time;
    std::vector<float> m_outer_buffer;

    int m_clock_timer;

public:
    void samples_available(const std::vector<float> &samples) override;

    // QObject interface
protected:
    void timerEvent(QTimerEvent *);

    // AbstractOutputDriver interface
public:
    uint32_t sample_rate() override;
    std::chrono::_V2::steady_clock::time_point time() override;

};


class Engine: public QObject
{
    Q_OBJECT

public:
    Engine();
    ~Engine();

private:
    std::unique_ptr<VirtualAudioSource> m_source;
    QAudioDeviceInfo m_output_device_info;
    std::unique_ptr<AbstractOutputDriver> m_sink;

private:
    void reopen_output();

private slots:
    void on_samples_available(const SampleBlock *samples);

public:
    inline global_clock::time_point output_time() const
    {
        return (m_sink ? m_sink->time() : global_clock::now());
    }

    void resume();

    inline const VirtualAudioSource *source() const
    {
        return m_source.get();
    }

    void start();
    void set_audio_output(const QAudioDeviceInfo &device);
    void set_source(std::unique_ptr<VirtualAudioSource> &&source);
    void set_target_output_latency(int32_t latency);
    void stop();
    void suspend();

signals:
    void samples_available(SampleBlock samples);
    void source_changed(const VirtualAudioSource *new_source);

};

#endif // ENGINE_H
