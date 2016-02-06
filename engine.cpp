#include <ccomplex>
#include "engine.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>

#include <QAudioOutput>
#include <QTimerEvent>


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
    bool read_and_convert(QIODevice *source,
                          uint32_t bytes_to_read,
                          std::vector<float> &dest) override
    {
        assert(bytes_to_read % sizeof(int_t) == 0);
        const uint32_t samples_to_read = (bytes_to_read / sizeof(int_t));
        m_buffer.resize(samples_to_read);

        const uint32_t bytes_total = samples_to_read*sizeof(int_t);
        const int64_t bytes_read = source->read(
                    (char*)m_buffer.data(),
                    bytes_total);
        if (bytes_read == -1) {
            return false;
        }

        assert(bytes_read % sizeof(int_t) == 0);
        m_buffer.resize(bytes_read / sizeof(int_t));

        sample_converter<int_t>::convert(m_buffer, dest);
        return true;
    }

};

/* VirtualAudioSource */

bool VirtualAudioSource::is_seekable() const
{
    return false;
}

bool VirtualAudioSource::seek(const uint64_t)
{
    return false;
}

uint64_t VirtualAudioSource::tell() const
{
    return 0;
}


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


/* AudioInputSource */

AudioInputSource::AudioInputSource(const QAudioDeviceInfo &device,
                                   const QAudioFormat &format,
                                   const float initial_volume,
                                   QObject *parent):
    VirtualAudioSource(parent),
    m_device(device),
    m_format(format),
    m_volume(initial_volume),
    m_input(nullptr),
    m_source(nullptr),
    m_converter()
{

}

AudioInputSource::~AudioInputSource()
{
    stop();
}

global_clock::time_point AudioInputSource::time() const
{
    //return m_t0 + std::chrono::microseconds(m_frames_read * 1000000 / (uint64_t)m_input->format().sampleRate());
    return m_t0 + std::chrono::microseconds(m_input->processedUSecs()) - m_buffer_delay;
}

uint32_t AudioInputSource::channel_count() const
{
    return m_format.channelCount();
}

uint32_t AudioInputSource::sample_rate() const
{
    return m_format.sampleRate();
}

std::pair<bool, global_clock::time_point> AudioInputSource::read_samples(
        std::vector<float> &dest)
{
    if (!m_source) {
        return std::make_pair(false, global_clock::time_point());
    }

    const uint32_t channel_count = m_input->format().channelCount();
    const int64_t frames_to_read = std::max(
                dest.capacity() / channel_count,
                m_input->periodSize() / sizeof(float) / channel_count
                );
    const int64_t bytes_to_read = frames_to_read * sizeof(float) * channel_count;
    const global_clock::time_point t = time();
    m_source->waitForReadyRead(-1);
    if (m_converter) {
        if (!m_converter->read_and_convert(
                    m_source,
                    bytes_to_read,
                    dest))
        {
            return std::make_pair(false, global_clock::time_point());
        }
        assert(dest.size() % channel_count == 0);
    } else {
        dest.resize(frames_to_read * channel_count);
        int64_t bytes_read = m_source->read(
                    (char*)dest.data(),
                    bytes_to_read);
        if (bytes_read < 0) {
            return std::make_pair(false, global_clock::time_point());
        }

        assert(bytes_read % (sizeof(float)*channel_count) == 0);
        dest.resize(bytes_read / sizeof(float));
    }
    m_frames_read += dest.size() / channel_count;
    return std::make_pair(true, t);
}

void AudioInputSource::start()
{
    m_input = std::make_unique<QAudioInput>(m_device, m_format, this);
    m_input->setVolume(m_volume);
    m_converter = AbstractSampleConverter::make_converter(
                m_input->format().sampleType(),
                m_input->format().sampleSize());
    m_source = m_input->start();
    m_t0 = global_clock::now();
    m_buffer_delay = std::chrono::microseconds(
                m_input->bufferSize() / sizeof(float) * 1000000 / m_input->format().sampleRate() / m_input->format().channelCount());
}

void AudioInputSource::stop()
{
    if (!m_input) {
        return;
    }

    m_source = nullptr;
    m_volume = m_input->volume();
    m_input->stop();
    m_input = nullptr;
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

void RMSProcessor::process_samples(std::shared_ptr<const SampleBlock> input_block)
{
    const SampleBlock &data = *input_block;
    if (m_sample_buffer.empty() || m_sample_rate != data.sample_rate) {
        m_sample_rate = data.sample_rate;
        m_sample_buffer = data.mono_samples;
        m_t0 = data.t;
        assert(data.t >= m_t0);
    } else {
        std::copy(data.mono_samples.begin(),
                  data.mono_samples.end(),
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
    setObjectName("RMS");
    start();
    m_processor.moveToThread(this);
}

RootMeanSquare::~RootMeanSquare()
{
    exit();
    wait();
}


/* FFTProcessor */

FFTProcessor::FFTProcessor(const Engine &engine,
                           uint32_t size,
                           uint32_t period_msec):
    m_in(size),
    m_out_buffer(size/2+1),
    m_size(size),
    m_plan(fftw_plan_dft_r2c_1d(size, m_in.data(), reinterpret_cast<fftw_complex*>(m_out_buffer.data()), 0)),
    m_period_msec(period_msec),
    m_sample_rate(0),
    m_shift_remaining(0),
    m_window(size)
{
    connect(&engine, &Engine::samples_available,
            this, &FFTProcessor::process_samples,
            Qt::QueuedConnection);
    make_window(m_window);
    m_in = m_window;
    fftw_execute(m_plan);
    m_norm = std::abs(m_out_buffer[0]);
}

void FFTProcessor::make_window(std::vector<double> &dest)
{
    static constexpr float a0 = 0.3635819;
    static constexpr float a1 = 0.4891775;
    static constexpr float a2 = 0.1365995;
    static constexpr float a3 = 0.0106411;
    const unsigned int N = dest.size();
    for (unsigned int n = 0; n < N; ++n) {
        dest[n] = a0 - a1*std::cos(2*M_PIl*n/(N-1)) + a2*std::cos(4*M_PIl*n/(N-1)) + a3*std::cos(6*M_PIl*n/(N-1));
    }
}

void FFTProcessor::process_samples(std::shared_ptr<const SampleBlock> input_block)
{
    const SampleBlock &data = *input_block;

    if (m_sample_rate != data.sample_rate) {
        m_in_buffer.clear();
        m_sample_rate = data.sample_rate;
        m_t = data.t;
        m_shift_remaining = 0;
    }

    if (m_in_buffer.empty()) {
        // use the opportunity for a resync
        m_t = data.t;
    }

    if (m_shift_remaining > 0 and m_shift_remaining >= data.mono_samples.size()) {
        m_shift_remaining -= data.mono_samples.size();
        m_t += std::chrono::microseconds(data.mono_samples.size() * 1000000 / m_sample_rate);
        return;
    }

    m_in_buffer.reserve(m_in_buffer.size() + data.mono_samples.size() - m_shift_remaining);
    std::copy(data.mono_samples.begin()+m_shift_remaining, data.mono_samples.end(),
              std::back_inserter(m_in_buffer));
    m_t += std::chrono::microseconds(m_shift_remaining * 1000000 / m_sample_rate);
    m_shift_remaining = 0;

    const uint32_t shift = m_period_msec * m_sample_rate / 1000;
    const float norm = m_norm;

    while (m_in_buffer.size() >= m_size) {
        for (unsigned int i = 0; i < m_size; ++i) {
            m_in[i] = m_in_buffer[i] * m_window[i];
        }
        fftw_execute(m_plan);
        m_out.t = m_t;
        m_out.fmax = (float)m_sample_rate / 2;
        m_out.fft.clear();

        std::transform(m_out_buffer.begin(),
                       m_out_buffer.end(),
                       std::back_inserter(m_out.fft),
                       [norm](const std::complex<double> &v){ return std::abs(v) / norm; });

        emit result_available(m_out);

        if (shift >= m_in_buffer.size()) {
            m_shift_remaining = shift - m_in_buffer.size();
            m_t += std::chrono::microseconds(m_in_buffer.size() * 1000000 / m_sample_rate);
            m_in_buffer.clear();
            return;
        }

        m_in_buffer.erase(m_in_buffer.begin(),
                          m_in_buffer.begin() + shift);

        m_t += std::chrono::microseconds(shift * 1000000 / m_sample_rate);
    }
}


/* FFT */

FFT::FFT(const Engine &engine, uint32_t size, uint32_t period_msec):
    m_processor(engine, size, period_msec)
{
    setObjectName(QString("FFT:%1:%2ms").arg(size).arg(period_msec));
    start();
    m_processor.moveToThread(this);
}

FFT::~FFT()
{
    exit();
    wait();
}


/* NullOutputDriver */

void NullOutputDriver::start()
{

}

void NullOutputDriver::stop()
{

}

global_clock::time_point NullOutputDriver::time() const
{
    return global_clock::now() + m_latency;
}

void NullOutputDriver::write_samples(const std::vector<float> &)
{

}



/* AudioOutputDriver */

AudioOutputDriver::AudioOutputDriver(
        const QAudioDeviceInfo &device,
        const QAudioFormat &format,
        int32_t buffer_msecs,
        uint32_t drop_msecs,
        QObject *parent):
    AbstractOutputDriver(parent),
    m_device(device),
    m_format(format),
    m_buffer_msecs(buffer_msecs),
    m_drop_msecs(drop_msecs),
    m_output(nullptr),
    m_sink(nullptr),
    m_samples_written(0),
    m_drop_samples(m_drop_msecs * format.sampleRate() / 1000),
    m_dropped(0),
    m_outer_buffer_delay(0)
{

}

inline void AudioOutputDriver::update_buffer_delay()
{
    m_outer_buffer_delay = std::chrono::microseconds(m_outer_buffer.size() * 1000000 / m_output->format().sampleRate());
}

void AudioOutputDriver::write_samples(const std::vector<float> &samples)
{
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
            m_outer_buffer.clear();
            std::lock_guard<std::shared_timed_mutex> lock(m_time_mutex);
            m_dropped += std::chrono::microseconds(
                        total_samples * 1000000 / m_output->format().sampleRate()
                        );
            m_outer_buffer_delay = std::chrono::microseconds(0);
            std::cout << "dropped " << total_samples << " samples" << std::endl;
            return;
        }
        m_outer_buffer.reserve(m_outer_buffer.size() + samples.size());
        std::copy(samples.begin(), samples.end(),
                  std::back_inserter(m_outer_buffer));
        std::lock_guard<std::shared_timed_mutex> lock(m_time_mutex);
        update_buffer_delay();
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

    std::lock_guard<std::shared_timed_mutex> lock(m_time_mutex);
    update_buffer_delay();
}

void AudioOutputDriver::start()
{
    m_output = std::make_unique<QAudioOutput>(m_device, m_format, this);
    uint32_t sample_rate = m_output->format().sampleRate();
    uint32_t channel_count = m_output->format().channelCount();
    m_output->setBufferSize(m_buffer_msecs * channel_count * sample_rate / 1000 * sizeof(float));
    m_sink = m_output->start();
    if (!m_sink) {
        throw std::runtime_error("failed to open audio output");
    }
    m_t0 = global_clock::now();
    sample_rate = m_output->format().sampleRate();
    channel_count = m_output->format().channelCount();
    assert(m_output->bufferSize() % (sizeof(float)*sample_rate*channel_count) == 0);
    m_buffer_delay = std::chrono::microseconds(
                m_output->bufferSize() / sizeof(float) * 1000000 / sample_rate / channel_count);
}

void AudioOutputDriver::stop()
{
    m_sink = nullptr;
    m_output->stop();
    m_output = nullptr;
}

global_clock::time_point AudioOutputDriver::time() const
{
    std::shared_lock<std::shared_timed_mutex> lock(m_time_mutex);
    return m_t0 + std::chrono::microseconds(m_output->processedUSecs()) - m_buffer_delay - m_outer_buffer_delay + m_dropped;
}


/* AudioPipe */

AudioPipe::AudioPipe(std::unique_ptr<VirtualAudioSource> &&source,
                     std::unique_ptr<AbstractOutputDriver> &&sink):
    m_terminated(false),
    m_new_source_thread(nullptr),
    m_source(std::move(source)),
    m_sink(std::move(sink)),
    m_startup_done(false)
{
    m_sample_sleep = std::chrono::microseconds(100 * 1000000 / m_source->sample_rate());
    m_source->moveToThread(this);
    m_sink->moveToThread(this);
    setObjectName("AudioPipe");
    start();
    {
        std::unique_lock<std::mutex> lock(m_startup_mutex);
        auto predicate = [this](){ return m_startup_done; };
        m_startup_notify.wait(lock, predicate);
    }
}

AudioPipe::AudioPipe(std::unique_ptr<VirtualAudioSource> &&source,
                     const std::chrono::milliseconds &output_delay):
    AudioPipe::AudioPipe(std::move(source),
                         std::make_unique<NullOutputDriver>(output_delay))
{

}

AudioPipe::~AudioPipe()
{
    if (!m_terminated) {
        m_terminated = true;
        wait();
    }
}

void AudioPipe::downmix_to_mono(const std::vector<float> &src,
                                std::vector<float> &dest,
                                const uint32_t channels)
{
    assert(channels > 1);
    dest.resize(src.size() / channels);
    assert(src.size() % channels == 0);
    for (uint32_t i = 0; i < src.size() / channels; ++i) {
        float accum = 0.f;
        for (uint32_t j = 0; j < channels; ++j) {
            accum += src[i*channels+j];
        }
        dest[i] = accum;
    }
}

void AudioPipe::run()
{
    m_source->start();
    m_sink->start();
    {
        std::lock_guard<std::mutex> lock(m_startup_mutex);
        m_startup_done = true;
        m_startup_notify.notify_all();
    }
    while (!m_terminated) {
        bool success;
        global_clock::time_point t;
        std::tie(success, t) = m_source->read_samples(m_sample_buffer);
        if (!success) {
            throw std::runtime_error("failed to read from source");
        }
        if (m_sample_buffer.size() == 0) {
            std::this_thread::sleep_for(m_sample_sleep);
            continue;
        }
        {
            auto block = std::make_shared<SampleBlock>();
            block->t = t;
            const uint32_t channels = m_source->channel_count();
            if (channels > 1) {
                downmix_to_mono(m_sample_buffer,
                                block->mono_samples,
                                channels);
            }
            block->sample_rate = m_source->sample_rate();
            emit samples_available(block);
        }
        m_sink->write_samples(m_sample_buffer);
        m_sample_buffer.clear();
    }
    if (m_new_source_thread) {
        m_source->stop();
        m_source->moveToThread(m_new_source_thread);
    } else {
        m_source->stop();
        m_source = nullptr;
    }
    m_sink->stop();
    m_sink = nullptr;
}

std::unique_ptr<VirtualAudioSource> AudioPipe::stop(QThread &new_source_thread)
{
    if (!isRunning() || m_terminated) {
        return nullptr;
    }
    m_new_source_thread = &new_source_thread;
    m_terminated = true;
    wait();
    return std::move(m_source);
}



/* Engine */

Engine::Engine()
{

}

Engine::~Engine()
{

}

void Engine::rebuild_pipe()
{
    std::unique_ptr<AbstractOutputDriver> sink = nullptr;
    if (!m_output_device_info.isNull()) {
        QAudioFormat fmt = m_output_device_info.preferredFormat();
        fmt.setSampleType(QAudioFormat::Float);
        fmt.setSampleSize(32);
        fmt.setChannelCount(m_source_latch->channel_count());
        fmt.setCodec("audio/pcm");
        fmt.setByteOrder(QAudioFormat::LittleEndian);
        fmt.setSampleRate(m_source_latch->sample_rate());
        if (!m_output_device_info.isFormatSupported(fmt)) {
            throw std::runtime_error("format not supported by sink");
        }

        sink = std::make_unique<AudioOutputDriver>(
                    m_output_device_info, fmt,
                    1000,
                    500);
    } else {
        sink = std::make_unique<NullOutputDriver>(std::chrono::milliseconds(1000));
    }

    m_audio_pipe = std::make_unique<AudioPipe>(
                std::move(m_source_latch),
                std::move(sink));
    connect(m_audio_pipe.get(), &AudioPipe::samples_available,
            this, &Engine::samples_available);
}

bool Engine::is_running() const
{
    return bool(m_audio_pipe);
}

void Engine::start()
{
    if (m_audio_pipe) {
        throw std::logic_error("already running");
    }
    if (!m_source_latch) {
        throw std::logic_error("no source defined");
    }
    rebuild_pipe();
}

void Engine::stop()
{
    if (!m_audio_pipe) {
        throw std::logic_error("already stopped");
    }
    m_source_latch = m_audio_pipe->stop(*thread());
    m_audio_pipe = nullptr;
}

void Engine::set_source(std::unique_ptr<VirtualAudioSource> &&source)
{
    if (m_audio_pipe) {
        throw std::logic_error("already running");
    }
    m_source_latch = std::move(source);
}

void Engine::set_output_device(const QAudioDeviceInfo &device)
{
    if (m_audio_pipe) {
        throw std::logic_error("already running");
    }
    m_output_device_info = device;
}
