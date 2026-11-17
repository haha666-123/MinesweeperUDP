#pragma once
// ============================================================================
//  Synth：极简 8-bit 合成器（纯算法，零依赖，可单测）
//  ---------------------------------------------------------------------------
//  为什么不用音频文件：
//    * 任何现成歌曲都有版权，随代码分发有法律风险
//    * 程序化合成的音乐完全由代码产生，无素材、无版权、体积为 0
//
//  实现内容：
//    * 方波 / 三角波 / 噪声（三种经典 8-bit 音色）
//    * ADSR 包络（起音/衰减/延音/释放），避免音符边缘的爆音
//    * 音符 -> 频率（十二平均律，A4 = 440Hz）
//    * 旋律序列调度（按拍数推进）
//
//  这些都是游戏音频的教科书内容，面试可讲：
//    采样率、位深、波形叠加、包络、混音、缓冲区调度。
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace ms {

// ---------------------------------------------------------------- 波形
enum class Waveform : std::uint8_t {
    Square = 0,     // 方波：经典 FC 音色，明亮
    Triangle,       // 三角波：柔和，适合低音
    Noise,          // 白噪声：打击乐
};

// ---------------------------------------------------------------- 音名
// 用 MIDI 音高表示：60 = C4（中央 C），69 = A4 = 440Hz
// 0 表示休止符
namespace pitch {
constexpr int Rest = 0;
constexpr int C4   = 60;
constexpr int D4   = 62;
constexpr int E4   = 64;
constexpr int F4   = 65;
constexpr int G4   = 67;
constexpr int A4   = 69;
constexpr int B4   = 71;
constexpr int C5   = 72;
constexpr int D5   = 74;
constexpr int E5   = 76;
constexpr int G5   = 79;
constexpr int A5   = 81;
constexpr int C3   = 48;
constexpr int E3   = 52;
constexpr int G3   = 55;
constexpr int A3   = 57;
} // namespace pitch

// ---------------------------------------------------------------- 一个音符
struct Note {
    int midi   = pitch::Rest;   // MIDI 音高；0 = 休止
    int beats  = 1;             // 占多少拍（支持 1/2 拍：用浮点更灵活，这里用 milli-beats）
    int milliBeats = 1000;      // 1000 = 1 拍；500 = 半拍
    Waveform wave = Waveform::Square;
    float velocity = 0.6f;      // 音量 0..1
};

// ---------------------------------------------------------------- 音轨
struct Track {
    std::vector<Note> notes;
    std::string       name;
    Waveform          defaultWave = Waveform::Square;
    float             gain        = 0.5f;   // 该音轨的整体音量
    bool              loop        = true;
};

// ---------------------------------------------------------------- 合成器参数
struct SynthConfig {
    int sampleRate = 44100;
    int channels   = 2;          // 立体声
    double bpm     = 112.0;      // 速度
    float  masterGain = 0.55f;   // 总音量
};

// ---------------------------------------------------------------- ADSR
struct Adsr {
    float attackSec  = 0.005f;   // 起音（很短，8-bit 风格）
    float decaySec   = 0.06f;    // 衰减
    float sustain    = 0.55f;    // 延音电平（相对峰值）
    float releaseSec = 0.04f;    // 释放

    //! 给定「音符内已过秒数」与「音符总时长」，返回 0..1 的包络系数
    [[nodiscard]] float evaluate(float t, float duration) const;
};

// ---------------------------------------------------------------- 合成器
class Synth {
public:
    explicit Synth(const SynthConfig& cfg = SynthConfig{});

    void setConfig(const SynthConfig& cfg) { m_cfg = cfg; }
    [[nodiscard]] const SynthConfig& config() const { return m_cfg; }

    //! 添加音轨
    void addTrack(const Track& t);
    void clearTracks();
    [[nodiscard]] std::size_t trackCount() const { return m_tracks.size(); }
    [[nodiscard]] const std::vector<Track>& tracks() const { return m_tracks; }

    //! 单音轨总拍数
    [[nodiscard]] int totalMilliBeats() const;

    //! 从第 0 拍开始重新播放
    void restart();
    [[nodiscard]] bool finished() const;
    [[nodiscard]] int currentMilliBeat() const { return m_cursorMilliBeat; }

    //! 生成 numFrames 帧交错 PCM（16-bit little-endian）；返回实际写入字节数
    //! 播放结束（非循环）时写入静音并返回 0
    std::size_t render(std::uint8_t* out, std::size_t maxBytes);

    //! 当前正在播放的音符（给界面显示用）
    struct NowPlaying {
        int  midi = 0;
        Waveform wave = Waveform::Square;
    };
    [[nodiscard]] std::vector<NowPlaying> nowPlaying() const;

    //! 纯函数：MIDI 音高 -> 频率（Hz）；0 返回 0
    [[nodiscard]] static double midiToFreq(int midi);

    //! 纯函数：生成一个波形的采样值（phase 为 0..1 的相位）
    [[nodiscard]] static float waveSample(Waveform w, double phase, std::uint32_t& noiseState);

private:
    struct VoiceState {
        int   noteIndex   = -1;      // 当前正在播放的音符下标
        int   noteStartMs = 0;       // 该音符在音轨内的起始毫秒
        int   noteDurMs   = 0;
        double phase      = 0.0;     // 相位 0..1
        std::uint32_t noiseState = 0x12345678u;
    };

    SynthConfig          m_cfg;
    std::vector<Track>   m_tracks;
    std::vector<VoiceState> m_voices;

    double m_milliBeatPos = 0.0;     // 当前位置（milli-beat，浮点便于按采样推进）
    int    m_cursorMilliBeat = 0;
    bool   m_finished = false;

    // 每个音轨的「音符起始时间表」，避免每帧重复累加
    std::vector<std::vector<int>> m_noteStartMs;
    std::vector<std::vector<int>> m_noteDurMs;

    void rebuildTimeline();
    void advanceVoices();
};

} // namespace ms
