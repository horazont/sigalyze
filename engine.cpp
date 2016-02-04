#include "engine.h"

#include <cassert>
#include <iostream>

#include <QAudioOutput>
#include <QTimerEvent>


static int32_t SAMPLING_INTERVAL = 20;


template <typename int_t>
struct sample_converter
{
    static constexpr float range =
            ((float)std::numeric_limits<int_t>::max() - (float)std::numeric_limits<int_t>::min());

    static void convert(const std::vector<int_t> &in,
                        std::vector<float> &out)
    {
        out.resize(in.size());
        for (typename std::vector<int_t>::size_type i = 0;
             i < in.size();
             ++i)
        {
            const int_t value = in[i];
            out[i] = ((float)value - (float)std::numeric_limits<int_t>::min()) / range * 2.f - 1.f;
        }
    }
};


template <typename int_t>
class IntToFloatConverter: public AbstractSampleConverter
{
private:
    std::vector<int_t> m_buffer;

public:
    void read_and_convert(QIODevice *source,
                          uint32_t bytes_to_read,
                          std::vector<float> &dest) override
    {
        const uint32_t samples_to_read = (bytes_to_read / sizeof(int_t));
        m_buffer.resize(samples_to_read);

        const uint32_t bytes_total = samples_to_read*sizeof(int_t);
        uint32_t offset = 0;
        while (offset < bytes_total) {
            offset += source->read(
                        (char*)m_buffer.data() + offset,
                        bytes_total - offset);
        }

        sample_converter<int_t>::convert(m_buffer, dest);
    }

};


AbstractSampleConverter::~AbstractSampleConverter()
{

}

std::unique_ptr<AbstractSampleConverter> AbstractSampleConverter::make_converter(
        QAudioFormat::SampleType sample_type,
        uint32_t bits)
{

    switch (sample_type) {
    case QAudioFormat::SignedInt:
    {
        switch (bits) {
        case 16:
        {
            return std::make_unique<IntToFloatConverter<int16_t> >();
        }
        case 32:
        {
            return std::make_unique<IntToFloatConverter<int32_t> >();
        }
        default:
            throw std::runtime_error("unsupported sample format: s"+
                                     std::to_string(bits));
        }
    }
    case QAudioFormat::UnSignedInt:
    {
        switch (bits) {
        case 16:
        {
            return std::make_unique<IntToFloatConverter<uint16_t> >();
        }
        case 32:
        {
            return std::make_unique<IntToFloatConverter<uint32_t> >();
        }
        default:
            throw std::runtime_error("unsupported sample format: u"+
                                     std::to_string(bits));
        }
    }
    case QAudioFormat::Float:
    {
        switch (bits) {
        case 32:
        {
            return nullptr;
        }
        default:
            throw std::runtime_error("unsupported sample format: f"+
                                     std::to_string(bits));
        }
    }
    default:
        throw std::runtime_error("unknown sample format");
    }
}



/* VirtualAudioSource */

bool VirtualAudioSource::is_seekable() const
{
    return false;
}

bool VirtualAudioSource::read_samples(uint64_t, uint64_t, std::vector<float>&)
{
    return false;
}

uint64_t VirtualAudioSource::samples() const
{
    return 0;
}

void VirtualAudioSource::seek(uint64_t)
{

}

QAudio::State VirtualAudioSource::state() const
{
    return QAudio::StoppedState;
}


/* AudioInputSource */

AudioInputSource::AudioInputSource(std::unique_ptr<QAudioInput> &&from, QObject *parent):
    VirtualAudioSource(parent),
    m_input(std::move(from)),
    m_source(nullptr),
    m_converter(
        AbstractSampleConverter::make_converter(m_input->format().sampleType(),
                                                m_input->format().sampleSize())
        )
{
    connect(m_input.get(), &QAudioInput::stateChanged,
            this, &AudioInputSource::state_changed);
    m_buffer.sample_rate = m_input->format().sampleRate();
}

AudioInputSource::~AudioInputSource()
{
    killTimer(m_sampling_timer);
    m_source = nullptr;
    m_input->stop();
}

void AudioInputSource::timerEvent(QTimerEvent *ev)
{
    if (ev->timerId() == m_sampling_timer) {
        on_notify();
    } else {
        std::cout << "unknown timer: " << ev->timerId() << std::endl;
    }
}

void AudioInputSource::on_notify()
{
    if (!m_source) {
        return;
    }

    while (m_input->bytesReady() >= m_input->periodSize()) {
        m_buffer.t = global_clock::now();
        if (m_converter) {
            m_converter->read_and_convert(
                        m_source,
                        m_input->periodSize(),
                        m_buffer.samples);
        } else {
            m_buffer.samples.resize(m_input->periodSize() / sizeof(float));
            int64_t bytes_read = m_source->read(
                        (char*)m_buffer.samples.data(),
                        m_buffer.samples.size() * sizeof(float));
            assert(bytes_read % sizeof(float) == 0);
            m_buffer.samples.resize(bytes_read / sizeof(float));
        }
        samples_available(&m_buffer);
    }
}

void AudioInputSource::pause()
{
    m_input->suspend();
    m_input->reset();
    killTimer(m_sampling_timer);
}

void AudioInputSource::resume()
{
    m_input->resume();
    m_sampling_timer = startTimer(SAMPLING_INTERVAL, Qt::PreciseTimer);
}

uint32_t AudioInputSource::sample_rate() const
{
    return m_input->format().sampleRate();
}

void AudioInputSource::start()
{
    m_source = m_input->start();
    if (!m_source) {
        throw std::runtime_error("failed to start source");
    }
    std::cout << "opened! notify interval: " << m_input->notifyInterval() << std::endl;
    m_sampling_timer = startTimer(SAMPLING_INTERVAL, Qt::PreciseTimer);
}

QAudio::State AudioInputSource::state() const
{
    return m_input->state();
}

void AudioInputSource::stop()
{
    killTimer(m_sampling_timer);
    m_source = nullptr;
    m_input->stop();
}


/* RMSProcessor */

RMSProcessor::RMSProcessor(const Engine &engine)
{
    connect(&engine, &Engine::samples_available,
            this, &RMSProcessor::process_samples,
            Qt::QueuedConnection);
}

void RMSProcessor::process_samples(SampleBlock data)
{
    if (m_sample_buffer.empty() || m_sample_rate != data.sample_rate) {
        m_sample_rate = data.sample_rate;
        m_sample_buffer = std::move(data.samples);
        m_t0 = data.t;
        std::cout << "resync" << std::endl;
        assert(data.t >= m_t0);
    } else {
        std::copy(data.samples.begin(),
                  data.samples.end(),
                  std::back_inserter(m_sample_buffer));
    }

    RMSBlock block;
    const uint64_t per_block = m_sample_rate / 10;
    uint64_t processed = 0;
    while (m_sample_buffer.size() >= per_block) {
        float rms = 0.f;
        for (unsigned int i = 0; i < per_block; ++i) {
            rms += m_sample_buffer[i]*m_sample_buffer[i];
        }
        rms /= per_block;
        rms = std::sqrt(rms);

        auto tmp = std::chrono::microseconds(processed * 1000000 / m_sample_rate);
        block.t = m_t0 + tmp;
        block.curr = rms;
        result_available(block);

        m_sample_buffer.erase(m_sample_buffer.begin(),
                              m_sample_buffer.begin()+per_block);
        processed += per_block;
    }
    auto tmp = std::chrono::microseconds(processed * 1000000 / m_sample_rate);
    std::cout << m_t0.time_since_epoch().count()
              << " " << processed
              << " " << tmp.count()
              << std::endl;
    // std::cout << tmp.count() << std::endl;
    m_t0 += tmp;
}


/* RootMeanSquare */

RootMeanSquare::RootMeanSquare(const Engine &engine):
    m_processor(engine)
{
    start();
    m_processor.moveToThread(this);
}



/* Engine */

Engine::Engine():
    m_output_sink(nullptr),
    m_output_latency(0),
    m_target_output_latency(1000)
{
    m_clock_timer = startTimer(SAMPLING_INTERVAL, Qt::PreciseTimer);
}

Engine::~Engine()
{
    if (m_source) {
        disconnect(m_source.get(), &VirtualAudioSource::samples_available,
                   this, &Engine::on_samples_available);
    }
}

void Engine::reopen_output()
{
    m_output = nullptr;
    m_output_sink = nullptr;

    if (m_output_device_info.isNull() or !m_source) {
        return;
    }

    QAudioFormat fmt = m_output_device_info.preferredFormat();
    fmt.setSampleType(QAudioFormat::Float);
    fmt.setSampleSize(32);
    fmt.setChannelCount(1);
    fmt.setCodec("audio/pcm");
    fmt.setByteOrder(QAudioFormat::LittleEndian);
    fmt.setSampleRate(m_source->sample_rate());
    if (!m_output_device_info.isFormatSupported(fmt)) {
        throw std::runtime_error("format not supported by sink");
    }

    m_output = std::make_unique<QAudioOutput>(m_output_device_info, fmt, this);
    // 100 ms worth of buffer
    m_output->setBufferSize(4*m_output->format().sampleRate() * m_target_output_latency / 1000);
    m_output_t0 = global_clock::now();
    m_output_sink = m_output->start();
    m_output_t0 = global_clock::now();
    m_output_delay = std::chrono::microseconds(m_output->bufferSize() / sizeof(float) * 1000000 / m_output->format().sampleRate());
    if (!m_output_sink) {
        m_output = nullptr;
        throw std::runtime_error("failed to open audio output");
    }
    m_output_samples_written = 0;
}

void Engine::on_samples_available(const SampleBlock *samples)
{
    if (m_output_sink) {
        uint64_t bytes_written =
                m_output_sink->write((const char*)samples->samples.data(),
                                     samples->samples.size() * sizeof(float));
        m_output_samples_written += bytes_written / sizeof(float);
        m_output_latency = (m_output_samples_written + m_output->bufferSize() / sizeof(float)) * 1000000 / m_output->format().sampleRate() - m_output->processedUSecs();
    }
    samples_available(*samples);
}

void Engine::timerEvent(QTimerEvent *ev)
{
    if (ev->timerId() == m_clock_timer) {
        if (m_output_sink) {
            m_output_time = m_output_t0 + std::chrono::microseconds(m_output->processedUSecs()) - m_output_delay;
        }
        return;
    }

    QObject::timerEvent(ev);
}

void Engine::start()
{
    m_source->start();
}

void Engine::set_audio_output(const QAudioDeviceInfo &device)
{
    m_output_device_info = device;
    reopen_output();
}

void Engine::set_source(std::unique_ptr<VirtualAudioSource> &&source)
{
    if (m_source) {
        disconnect(m_source.get(), &VirtualAudioSource::samples_available,
                   this, &Engine::on_samples_available);
    }
    m_source = std::move(source);
    if (m_source) {
        connect(m_source.get(), &VirtualAudioSource::samples_available,
                this, &Engine::on_samples_available,
                Qt::DirectConnection);
    }
    source_changed(m_source.get());
    reopen_output();
}

void Engine::set_target_output_latency(int32_t latency)
{
    m_target_output_latency = latency;
    reopen_output();
}

void Engine::stop()
{
    m_source->stop();
}
