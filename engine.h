#ifndef ENGINE_H
#define ENGINE_H

#include <atomic>
#include <complex>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
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

public:
    virtual uint32_t channel_count() const = 0;
    virtual uint32_t sample_rate() const = 0;
    virtual bool is_seekable() const;
    virtual bool seek(const uint64_t to_frame);
    virtual uint64_t tell() const;

    virtual void start() = 0;
    virtual void stop() = 0;

public:
    virtual std::pair<bool, global_clock::time_point> read_samples(
            std::vector<float> &dest) = 0;

};


class AbstractSampleConverter
{
public:
    virtual ~AbstractSampleConverter();

public:
    virtual bool read_and_convert(QIODevice *source,
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
    global_clock::time_point m_t0;
    std::chrono::microseconds m_buffer_delay;
    std::unique_ptr<QAudioInput> m_input;
    QIODevice *m_source;
    std::unique_ptr<AbstractSampleConverter> m_converter;
    uint64_t m_frames_read;

private:
    global_clock::time_point time() const;

    // VirtualAudioSource interface
public:
    uint32_t channel_count() const;
    uint32_t sample_rate() const;
    std::pair<bool, global_clock::time_point> read_samples(std::vector<float> &dest);
    void start();
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
    void process_samples(std::shared_ptr<const SampleBlock> input_block);

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
    void process_samples(std::shared_ptr<const SampleBlock> input_block);

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
    virtual global_clock::time_point time() const = 0;

public:
    virtual void write_samples(const std::vector<float> &samples) = 0;

};


class NullOutputDriver: public AbstractOutputDriver
{
    Q_OBJECT
public:
    template <typename duration_t>
    explicit NullOutputDriver(duration_t &&latency, QObject *parent = nullptr):
        AbstractOutputDriver(parent),
        m_latency(std::forward<duration_t>(latency))
    {

    }

private:
    std::chrono::milliseconds m_latency;

public:
    global_clock::time_point time() const override;

public:
    void write_samples(const std::vector<float> &samples) override;

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
    global_clock::time_point m_t0;
    std::vector<float> m_outer_buffer;
    std::chrono::microseconds m_buffer_delay;

    mutable std::shared_timed_mutex m_time_mutex;
    std::chrono::microseconds m_dropped;
    std::chrono::microseconds m_outer_buffer_delay;

private:
    void update_buffer_delay();

public:
    void write_samples(const std::vector<float> &samples) override;

    // AbstractOutputDriver interface
public:
    global_clock::time_point time() const override;

};


class AudioPipe: public QThread
{
    Q_OBJECT
public:
    AudioPipe(
            std::unique_ptr<VirtualAudioSource> &&source,
            std::unique_ptr<AbstractOutputDriver> &&sink);
    explicit AudioPipe(std::unique_ptr<VirtualAudioSource> &&source,
            const std::chrono::milliseconds &output_delay = std::chrono::milliseconds(100));
    ~AudioPipe() override;

private:
    std::atomic_bool m_terminated;
    QThread *m_new_source_thread;
    std::chrono::microseconds m_sample_sleep;

    std::unique_ptr<VirtualAudioSource> m_source;
    std::unique_ptr<AbstractOutputDriver> m_sink;

private:
    void downmix_to_mono(const std::vector<float> &src,
                         std::vector<float> &dest,
                         uint32_t channels);
    std::shared_lock<std::shared_timed_mutex> wait_for_source_and_sink();

    // QThread interface
protected:
    void run();

public:
    inline global_clock::time_point sink_time() const
    {
        return m_sink->time();
    }

public:
    std::unique_ptr<VirtualAudioSource> stop(QThread &new_source_thread);

signals:
    void samples_available(std::shared_ptr<const SampleBlock> block);

};


class Engine: public QObject
{
    Q_OBJECT

public:
    Engine();
    ~Engine() override;

private:
    std::unique_ptr<VirtualAudioSource> m_source_latch;

    std::unique_ptr<AudioPipe> m_audio_pipe;
    QAudioDeviceInfo m_output_device_info;

private:
    void rebuild_pipe();

public:
    inline global_clock::time_point sink_time() const
    {
        return m_audio_pipe->sink_time();
    }

    bool is_running() const;

    void start();
    void stop();

    void set_source(std::unique_ptr<VirtualAudioSource> &&source);
    void set_output_device(const QAudioDeviceInfo &device);

signals:
    void samples_available(std::shared_ptr<const SampleBlock> samples);

};

#endif // ENGINE_H
