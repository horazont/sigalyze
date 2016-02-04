#ifndef ENGINE_H
#define ENGINE_H

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


typedef std::chrono::steady_clock global_clock;


struct TimestampedData
{
    global_clock::time_point t;
};


struct SampleBlock: public TimestampedData
{
    uint32_t sample_rate;
    std::vector<float> samples;
};


struct FFTBlock: public TimestampedData
{
    std::vector<float> fft;
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
    void on_notify();

    // QObject interface
protected:
    void timerEvent(QTimerEvent *ev);

public:
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
            return;
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

private slots:
    void process_samples(SampleBlock data);

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


class Engine: public QObject
{
    Q_OBJECT

public:
    Engine();
    ~Engine();

private:
    std::unique_ptr<VirtualAudioSource> m_source;
    QAudioDeviceInfo m_output_device_info;
    std::unique_ptr<QAudioOutput> m_output;
    QIODevice *m_output_sink;

    int m_clock_timer;

    int64_t m_output_latency;
    int64_t m_output_samples_written;
    int32_t m_target_output_latency;
    std::chrono::microseconds m_output_delay;
    global_clock::time_point m_output_t0;
    global_clock::time_point m_output_time;

private:
    void reopen_output();

private slots:
    void on_samples_available(const SampleBlock *samples);

    // QObject interface
protected:
    void timerEvent(QTimerEvent *ev);

public:
    inline int64_t output_latency() const
    {
        return m_output_latency;
    }

    inline global_clock::time_point output_time() const
    {
        return m_output_time;
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