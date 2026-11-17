#include "Synth.h"

#include <algorithm>
#include <cmath>

namespace ms {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int    kMilliPerBeat = 1000;
} // namespace

// ===========================================================================
//  ADSR
// ===========================================================================
float Adsr::evaluate(float t, float duration) const
{
    if (t < 0.0f || duration <= 0.0f)
        return 0.0f;

    // 释放段：从 duration - release 到 duration
    const float releaseStart = std::max(0.0f, duration - releaseSec);
    if (t >= releaseStart) {
        const float k = (t - releaseStart) / std::max(0.0001f, releaseSec);
        // 从「当前包络值」线性降到 0
        float base = sustain;
        if (t < attackSec) {
            base = attackSec > 0.0f ? (t / attackSec) : 1.0f;
        } else if (t < attackSec + decaySec) {
            const float d = (t - attackSec) / std::max(0.0001f, decaySec);
            base = 1.0f + (sustain - 1.0f) * d;
        }
        return std::max(0.0f, base * (1.0f - k));
    }

    // 起音段
    if (t < attackSec)
        return attackSec > 0.0f ? (t / attackSec) : 1.0f;

    // 衰减段
    if (t < attackSec + decaySec) {
        const float d = (t - attackSec) / std::max(0.0001f, decaySec);
        return 1.0f + (sustain - 1.0f) * d;
    }

    // 延音段
    return sustain;
}

// ===========================================================================
//  纯函数
// ===========================================================================
double Synth::midiToFreq(int midi)
{
    if (midi <= 0)
        return 0.0;
    // 十二平均律：A4(69) = 440Hz
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

float Synth::waveSample(Waveform w, double phase, std::uint32_t& noiseState)
{
    // 相位归一到 0..1
    phase -= std::floor(phase);

    switch (w) {
    case Waveform::Square:
        // 方波：正半周 +1，负半周 -1
        return (phase < 0.5) ? 1.0f : -1.0f;

    case Waveform::Triangle: {
        // 三角波：0->1->0->-1->0
        const double t = phase * 4.0;
        if (t < 1.0)  return static_cast<float>(t);          // 0..1
        if (t < 3.0)  return static_cast<float>(2.0 - t);     // 1..-1
        return static_cast<float>(t - 4.0);                   // -1..0
    }

    case Waveform::Noise: {
        // xorshift32 伪随机：便宜且够用
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return (static_cast<float>(noiseState & 0xFFFF) / 32768.0f) - 1.0f;
    }
    }
    return 0.0f;
}

// ===========================================================================
//  构造与音轨
// ===========================================================================
Synth::Synth(const SynthConfig& cfg)
    : m_cfg(cfg)
{
}

void Synth::addTrack(const Track& t)
{
    m_tracks.push_back(t);
    rebuildTimeline();
}

void Synth::clearTracks()
{
    m_tracks.clear();
    m_voices.clear();
    m_noteStartMs.clear();
    m_noteDurMs.clear();
    m_milliBeatPos = 0.0;
    m_cursorMilliBeat = 0;
    m_finished = false;
}

void Synth::rebuildTimeline()
{
    m_noteStartMs.assign(m_tracks.size(), {});
    m_noteDurMs.assign(m_tracks.size(), {});

    for (std::size_t t = 0; t < m_tracks.size(); ++t) {
        const Track& tr = m_tracks[t];
        int cursor = 0;
        for (const Note& n : tr.notes) {
            const int dur = std::max(1, n.milliBeats);
            m_noteStartMs[t].push_back(cursor);
            m_noteDurMs[t].push_back(dur);
            cursor += dur;
        }
    }

    m_voices.assign(m_tracks.size(), VoiceState{});
    for (std::size_t i = 0; i < m_voices.size(); ++i)
        m_voices[i].noiseState = 0x9E3779B9u * static_cast<std::uint32_t>(i + 1);
}

int Synth::totalMilliBeats() const
{
    int maxLen = 0;
    for (const Track& tr : m_tracks) {
        int len = 0;
        for (const Note& n : tr.notes)
            len += std::max(1, n.milliBeats);
        maxLen = std::max(maxLen, len);
    }
    return maxLen;
}

void Synth::restart()
{
    m_milliBeatPos = 0.0;
    m_cursorMilliBeat = 0;
    m_finished = false;

    for (auto& v : m_voices) {
        v.noteIndex = -1;
        v.noteStartMs = 0;
        v.noteDurMs = 0;
        v.phase = 0.0;
    }
    advanceVoices();
}

bool Synth::finished() const
{
    return m_finished;
}

// ===========================================================================
//  推进
// ===========================================================================
void Synth::advanceVoices()
{
    const int pos = static_cast<int>(m_milliBeatPos);
    m_cursorMilliBeat = pos;

    const int total = totalMilliBeats();
    if (total <= 0)
        return;

    for (std::size_t t = 0; t < m_tracks.size(); ++t) {
        const Track& tr = m_tracks[t];
        if (tr.notes.empty() || t >= m_noteStartMs.size())
            continue;

        // 循环处理：把位置映射回音轨长度内
        int local = pos;
        if (tr.loop && total > 0)
            local = pos % std::max(1, total);

        // 找到当前应该播放的音符
        int idx = -1;
        const auto& starts = m_noteStartMs[t];
        const auto& durs   = m_noteDurMs[t];
        for (std::size_t i = 0; i < starts.size(); ++i) {
            if (local >= starts[i] && local < starts[i] + durs[i]) {
                idx = static_cast<int>(i);
                break;
            }
        }

        VoiceState& v = m_voices[t];
        if (idx != v.noteIndex) {
            v.noteIndex = idx;
            v.phase = 0.0;                       // 新音符重置相位
            if (idx >= 0) {
                v.noteStartMs = starts[static_cast<std::size_t>(idx)];
                v.noteDurMs   = durs[static_cast<std::size_t>(idx)];
            }
        }
    }
}

std::vector<Synth::NowPlaying> Synth::nowPlaying() const
{
    std::vector<NowPlaying> out;
    for (std::size_t t = 0; t < m_voices.size(); ++t) {
        const VoiceState& v = m_voices[t];
        if (v.noteIndex < 0 || t >= m_tracks.size())
            continue;
        const Track& tr = m_tracks[t];
        if (static_cast<std::size_t>(v.noteIndex) >= tr.notes.size())
            continue;
        const Note& n = tr.notes[static_cast<std::size_t>(v.noteIndex)];
        if (n.midi == pitch::Rest)
            continue;
        out.push_back(NowPlaying{ n.midi, n.wave });
    }
    return out;
}

// ===========================================================================
//  渲染
// ===========================================================================
std::size_t Synth::render(std::uint8_t* out, std::size_t maxBytes)
{
    if (!out || maxBytes == 0)
        return 0;

    const int ch = std::max(1, m_cfg.channels);
    const int sampleRate = std::max(8000, m_cfg.sampleRate);
    const std::size_t bytesPerFrame = static_cast<std::size_t>(ch) * 2;
    const std::size_t frames = maxBytes / bytesPerFrame;

    const int total = totalMilliBeats();
    if (total <= 0 || m_tracks.empty()) {
        std::fill(out, out + frames * bytesPerFrame, 0);
        return frames * bytesPerFrame;
    }

    // 每帧推进多少 milli-beat
    const double msPerBeat = 60000.0 / std::max(1.0, m_cfg.bpm);
    const double milliBeatPerFrame = 1000.0 / (msPerBeat * sampleRate / 1000.0) / 1000.0;
    // 上面化简为：1000 milli-beat 占 msPerBeat 毫秒，每帧 1000/sampleRate 毫秒
    const double milliBeatPerFrame2 = (1000.0 / msPerBeat) * (1000.0 / sampleRate);

    auto* pcm = reinterpret_cast<std::int16_t*>(out);

    for (std::size_t f = 0; f < frames; ++f) {
        // 一次只推进 1 帧的拍位
        m_milliBeatPos += milliBeatPerFrame2;

        // 每帧都检查是否跨越到新音符
        advanceVoices();

        // ---- 混音：把所有音轨当前音符相加
        float mix = 0.0f;

        for (std::size_t t = 0; t < m_tracks.size(); ++t) {
            const Track& tr = m_tracks[t];
            VoiceState& v = m_voices[t];
            if (v.noteIndex < 0 || static_cast<std::size_t>(v.noteIndex) >= tr.notes.size())
                continue;

            const Note& n = tr.notes[static_cast<std::size_t>(v.noteIndex)];
            if (n.midi == pitch::Rest)
                continue;

            const double freq = midiToFreq(n.midi);
            if (freq <= 0.0)
                continue;

            // 相位推进
            v.phase += freq / static_cast<double>(sampleRate);

            // 音符内已过时间（秒）
            const float durSec = static_cast<float>(v.noteDurMs) / 1000.0f *
                                 static_cast<float>(msPerBeat) / 1000.0f;

            // 用「在音符内的相对位置」估算已过时间
            const int localPos = m_cursorMilliBeat - v.noteStartMs;
            const float elapsedSec = std::max(0.0f,
                static_cast<float>(localPos) / 1000.0f * static_cast<float>(msPerBeat) / 1000.0f);

            const Adsr env;   // 默认包络
            const float amp = env.evaluate(elapsedSec, std::max(0.01f, durSec)) *
                              n.velocity * tr.gain;

            mix += waveSample(n.wave, v.phase, v.noiseState) * amp;
        }

        mix *= m_cfg.masterGain;

        // 软限幅：tanh 近似，避免削波爆音
        mix = std::tanh(mix * 1.2f);

        // 转 16-bit
        const float clamped = std::clamp(mix, -1.0f, 1.0f);
        const auto s = static_cast<std::int16_t>(clamped * 32767.0f);

        for (int c = 0; c < ch; ++c)
            *pcm++ = s;

        // 非循环且播放到头
        if (m_milliBeatPos >= total) {
            const bool anyLoop = std::any_of(m_tracks.begin(), m_tracks.end(),
                                             [](const Track& tr) { return tr.loop; });
            if (!anyLoop) {
                m_finished = true;
                // 后续帧填静音
                const std::size_t remain = (frames - f - 1) * bytesPerFrame;
                if (remain > 0)
                    std::fill(reinterpret_cast<std::uint8_t*>(pcm),
                              reinterpret_cast<std::uint8_t*>(pcm) + remain, 0);
                return frames * bytesPerFrame;
            }
            m_milliBeatPos -= total;      // 循环
        }
    }

    return frames * bytesPerFrame;
}

} // namespace ms
