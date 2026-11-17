#pragma once
// ============================================================================
//  MusicPlayer：后台音乐播放
//  ---------------------------------------------------------------------------
//  架构：
//    * QIODevice 子类作为「正弦数据源」，QAudioSink 主动从它拉数据
//    * 所有 PCM 由 Synth 实时生成，不读取任何音频文件
//    * 支持：切曲 / 音量 / 静音 / 暂停恢复
//
//  为什么用 QAudioSink + QIODevice 而不是自己开线程推数据：
//    Qt 的音频后端会按需回调 readData()，天然处理缓冲区欠载，
//    代码量小且不会有自己的线程与事件循环冲突。
//    （Qt5 叫 QAudioOutput，Qt6 改名为 QAudioSink。）
// ============================================================================
#include "Synth.h"
#include "MusicLibrary.h"

#include <QObject>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace ms {

//! 音频数据源：QAudioSink 从这里拉 PCM
class SynthDevice : public QIODevice {
    Q_OBJECT
public:
    explicit SynthDevice(QObject* parent = nullptr);

    void setSynth(std::shared_ptr<Synth> synth);
    [[nodiscard]] qint64 readData(char* data, qint64 maxlen) override;
    [[nodiscard]] qint64 writeData(const char* data, qint64 len) override;
    [[nodiscard]] qint64 bytesAvailable() const override;

private:
    std::mutex               m_mutex;
    std::shared_ptr<Synth>   m_synth;
    qint64                   m_pendingBytes = 0;
};

class MusicPlayer : public QObject {
    Q_OBJECT
public:
    explicit MusicPlayer(QObject* parent = nullptr);
    ~MusicPlayer() override;

    //! 播放内置曲目
    void playMenuTheme();
    void playGameTheme();

    void stop();
    void pause();
    void resume();

    //! 音量 0..100
    void setVolume(int percent);
    [[nodiscard]] int volume() const { return m_volume; }

    void setMuted(bool muted);
    [[nodiscard]] bool isMuted() const { return m_muted; }

    [[nodiscard]] bool isPlaying() const { return m_playing; }
    [[nodiscard]] QString currentTheme() const { return m_currentTheme; }

    //! 音频后端是否可用（某些环境没有音频设备）
    [[nodiscard]] bool available() const { return m_available; }
    [[nodiscard]] QString lastError() const { return m_lastError; }

private:
    void ensureInit();
    void startWithTracks(std::vector<Track> tracks, const QString& name);

    QAudioSink*                  m_sink = nullptr;
    SynthDevice*                 m_device = nullptr;
    std::shared_ptr<Synth>       m_synth;
    QAudioFormat                 m_format;

    bool    m_available = false;
    bool    m_playing   = false;
    bool    m_muted     = false;
    int     m_volume    = 45;          // 默认音量 45%，不高不低
    QString m_currentTheme;
    QString m_lastError;
};

} // namespace ms
