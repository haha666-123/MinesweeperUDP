#include "MusicPlayer.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QDebug>

namespace ms {

// ===========================================================================
//  SynthDevice
// ===========================================================================
SynthDevice::SynthDevice(QObject* parent)
    : QIODevice(parent)
{
}

void SynthDevice::setSynth(std::shared_ptr<Synth> synth)
{
    std::scoped_lock lock(m_mutex);
    m_synth = std::move(synth);
}

qint64 SynthDevice::readData(char* data, qint64 maxlen)
{
    if (maxlen <= 0)
        return 0;

    std::scoped_lock lock(m_mutex);
    if (!m_synth)
        return 0;

    // Synth::render 直接写 16-bit 交错 PCM
    const std::size_t written =
        m_synth->render(reinterpret_cast<std::uint8_t*>(data),
                        static_cast<std::size_t>(maxlen));
    return static_cast<qint64>(written);
}

qint64 SynthDevice::writeData(const char*, qint64)
{
    return -1;                    // 只读数据源
}

qint64 SynthDevice::bytesAvailable() const
{
    // 报告「永远有数据」，让 sink 持续拉取（音乐是循环生成的）
    return QIODevice::bytesAvailable() + 4096;
}

// ===========================================================================
//  MusicPlayer
// ===========================================================================
MusicPlayer::MusicPlayer(QObject* parent)
    : QObject(parent)
{
    ensureInit();
}

MusicPlayer::~MusicPlayer()
{
    stop();
}

void MusicPlayer::ensureInit()
{
    // 选一个可用的输出设备
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) {
        m_available = false;
        m_lastError = QStringLiteral("没有可用的音频输出设备");
        return;
    }

    // 用设备偏好的格式；若拿不到则退化为 44100/2ch/Int16
    m_format = dev.preferredFormat();
    if (m_format.sampleFormat() != QAudioFormat::Int16) {
        m_format = QAudioFormat();
        m_format.setSampleRate(44100);
        m_format.setChannelCount(2);
        m_format.setSampleFormat(QAudioFormat::Int16);
    }

    SynthConfig cfg;
    cfg.sampleRate = m_format.sampleRate();
    cfg.channels   = m_format.channelCount();
    cfg.bpm        = 112.0;

    m_synth = std::make_shared<Synth>(cfg);

    m_device = new SynthDevice(this);
    m_device->setSynth(m_synth);

    m_available = true;
    m_lastError.clear();
}

void MusicPlayer::startWithTracks(std::vector<Track> tracks, const QString& name)
{
    if (!m_available)
        return;

    // 已经在放同一首就不重来（避免切页时音乐重置）
    if (m_playing && m_currentTheme == name)
        return;

    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }

    m_synth->clearTracks();
    for (const Track& t : tracks)
        m_synth->addTrack(t);
    m_synth->restart();

    m_sink = new QAudioSink(m_format, this);
    m_sink->setVolume(m_muted ? 0.0 : (m_volume / 100.0));

    m_device->open(QIODevice::ReadOnly);
    m_sink->start(m_device);

    m_playing = true;
    m_currentTheme = name;
}

void MusicPlayer::playMenuTheme()
{
    startWithTracks(makeMenuTheme(), QStringLiteral("menu"));
}

void MusicPlayer::playGameTheme()
{
    startWithTracks(makeGameTheme(), QStringLiteral("game"));
}

void MusicPlayer::stop()
{
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_device)
        m_device->close();
    m_playing = false;
    m_currentTheme.clear();
}

void MusicPlayer::pause()
{
    if (m_sink && m_playing)
        m_sink->suspend();
}

void MusicPlayer::resume()
{
    if (m_sink && m_playing)
        m_sink->resume();
}

void MusicPlayer::setVolume(int percent)
{
    m_volume = std::clamp(percent, 0, 100);
    if (m_sink)
        m_sink->setVolume(m_muted ? 0.0 : (m_volume / 100.0));
}

void MusicPlayer::setMuted(bool muted)
{
    m_muted = muted;
    if (m_sink)
        m_sink->setVolume(m_muted ? 0.0 : (m_volume / 100.0));
}

} // namespace ms
