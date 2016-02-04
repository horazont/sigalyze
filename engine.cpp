#include "engine.h"

#include <cassert>
#include <iostream>

#include <QAudioOutput>
#include <QTimerEvent>


static int32_t SAMPLING_INTERVAL = 10;


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
        assert(bytes_to_read % sizeof(int_t) == 0);
        const uint32_t samples_to_read = (bytes_to_read / sizeof(int_t));
        m_buffer.resize(samples_to_read);

        const uint32_t bytes_total = samples_to_read*sizeof(int_t);
        const uint64_t bytes_read = source->read(
                    (char*)m_buffer.data(),
                    bytes_total);

        assert(bytes_read % sizeof(int_t) == 0);
        m_buffer.resize(bytes_read / sizeof(int_t));

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
    m_input->setVolume(0.001);
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

    while (m_input->bytesReady()) {
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
        if (m_buffer.samples.empty()) {
            return;
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

RMSProcessor::RMSProcessor(const Engine &engine):
    m_backlog{0},
    m_backlog_index(0)
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
        m_backlog[m_backlog_index] = rms;
        m_backlog_index = (m_backlog_index+1) % m_backlog.size();
        block.recent_peak = get_recent_peak();
        result_available(block);

        m_sample_buffer.erase(m_sample_buffer.begin(),
                              m_sample_buffer.begin()+per_block);
        processed += per_block;
    }
    auto tmp = std::chrono::microseconds(processed * 1000000 / m_sample_rate);
    m_t0 += tmp;
}

float RMSProcessor::get_recent_peak()
{
    float result = 0;
    for (float sample: m_backlog) {
        result = std::max(result, sample);
    }
    return result;
}


/* RootMeanSquare */

RootMeanSquare::RootMeanSquare(const Engine &engine):
    m_processor(engine)
{
    start();
    m_processor.moveToThread(this);
}


/* AbstractOutputDriver */

void AbstractOutputDriver::samples_available(const std::vector<float> &)
{

}

/* AudioOutputDriver */

AudioOutputDriver::AudioOutputDriver(
        std::unique_ptr<QAudioOutput> &&output,
        int32_t buffer_msecs,
        uint32_t drop_msecs,
        QObject *parent):
    AbstractOutputDriver(parent),
    m_output(std::move(output)),
    m_sink(nullptr),
    m_samples_written(0),
    m_drop_samples(drop_msecs * m_output->format().sampleRate() / 1000),
    m_dropped(0),
    m_time(global_clock::time_point())
{
    uint32_t sample_rate = m_output->format().sampleRate();
    m_output->setBufferSize(buffer_msecs * sample_rate / 1000 * sizeof(float));
    m_sink = m_output->start();
    if (!m_sink) {
        throw std::runtime_error("failed to open audio output");
    }
    m_t0 = global_clock::now();
    m_buffer_delay = std::chrono::microseconds(
                m_output->bufferSize() / sizeof(float) * 1000000 / sample_rate);

    m_clock_timer = startTimer(SAMPLING_INTERVAL, Qt::PreciseTimer);
}

void AudioOutputDriver::samples_available(const std::vector<float> &samples)
{
    // std::cout << samples.size() << std::endl;

    if (!m_outer_buffer.empty()) {
        int64_t written = m_sink->write(
                    (const char*)m_outer_buffer.data(),
                    m_outer_buffer.size() * sizeof(float));
        assert(written % sizeof(float) == 0);
        m_outer_buffer.erase(m_outer_buffer.begin(),
                             m_outer_buffer.begin()+written/sizeof(float));
    }

    if (!m_outer_buffer.empty()) {
        const uint64_t total_samples = m_outer_buffer.size() + samples.size();
        if (total_samples >= m_drop_samples) {
            m_dropped += std::chrono::microseconds(
                        total_samples * 1000000 / m_output->format().sampleRate()
                        );
            m_outer_buffer.clear();
            std::cout << "dropped " << total_samples << " samples" << std::endl;
            return;
        }
        m_outer_buffer.reserve(m_outer_buffer.size() + samples.size());
        std::copy(samples.begin(), samples.end(),
                  std::back_inserter(m_outer_buffer));
        return;
    }

    int64_t written = m_sink->write(
                (const char*)samples.data(),
                samples.size() * sizeof(float));
    assert(written % sizeof(float) == 0);
    int64_t to_rescue = samples.size() - written / sizeof(float);
    if (to_rescue > 0) {
        m_outer_buffer.reserve(to_rescue);
        std::copy(samples.begin() + written/sizeof(float),
                  samples.end(),
                  std::back_inserter(m_outer_buffer));
    }
}

void AudioOutputDriver::timerEvent(QTimerEvent *ev)
{
    if (ev->timerId() == m_clock_timer) {
        std::chrono::microseconds outer_buffer_delay(m_outer_buffer.size() * 1000000 / m_output->format().sampleRate());
        m_time.store(
                    m_t0 + std::chrono::microseconds(m_output->processedUSecs()) - m_buffer_delay - outer_buffer_delay + m_dropped,
                    std::memory_order_release);
        return;
    }
    AbstractOutputDriver::timerEvent(ev);
}

uint32_t AudioOutputDriver::sample_rate()
{
    return m_output->format().sampleRate();
}

std::chrono::_V2::steady_clock::time_point AudioOutputDriver::time()
{
    return m_time.load(std::memory_order_acquire);
}



/* Engine */

Engine::Engine():
    m_sink(nullptr)
{
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
    m_sink = nullptr;

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

    m_sink = std::make_unique<AudioOutputDriver>(
                std::make_unique<QAudioOutput>(m_output_device_info, fmt, this),
                1000,
                500,
                this);
}

void Engine::on_samples_available(const SampleBlock *samples)
{
    if (m_sink) {
        m_sink->samples_available(samples->samples);
    }
    samples_available(*samples);
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
    reopen_output();
}

void Engine::stop()
{
    m_source->stop();
}
