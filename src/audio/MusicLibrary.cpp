#include "MusicLibrary.h"

namespace ms {

namespace {

// 便捷构造：N(midi, beats) —— beats 用 milli-beat 表达，1000 = 一拍
Note N(int midi, int milliBeats, Waveform w = Waveform::Square, float vel = 0.55f)
{
    Note n;
    n.midi = midi;
    n.milliBeats = milliBeats;
    n.wave = w;
    n.velocity = vel;
    return n;
}

constexpr int kQ  = 1000;    // 四分音符（一拍）
constexpr int kH  = 500;     // 八分音符
constexpr int kE  = 250;     // 十六分音符
constexpr int kDQ = 1500;    // 附点四分

} // namespace

// ===========================================================================
//  主菜单主题：C 大调五声（C D E G A），轻快上扬
// ===========================================================================
Track makeMenuLeadTrack()
{
    Track t;
    t.name  = "lead";
    t.gain  = 0.42f;
    t.defaultWave = Waveform::Square;
    t.loop  = true;

    //  小节 1：C5 D5 E5 G5 | A5 G5 E5 D5
    t.notes = {
        N(pitch::C5, kH), N(pitch::D5, kH), N(pitch::E5, kH), N(pitch::G5, kH),
        N(pitch::A5, kQ), N(pitch::G5, kH), N(pitch::E5, kH),
        N(pitch::D5, kH), N(pitch::C5, kH), N(pitch::D5, kH), N(pitch::E5, kH),
        N(pitch::G5, kDQ, Waveform::Square, 0.5f),

        // 小节 2
        N(pitch::E5, kH), N(pitch::D5, kH), N(pitch::C5, kH), N(pitch::D5, kH),
        N(pitch::E5, kQ), N(pitch::G5, kH), N(pitch::A5, kH),
        N(pitch::G5, kH), N(pitch::E5, kH), N(pitch::D5, kH), N(pitch::C5, kH),
        N(pitch::C5, kDQ, Waveform::Square, 0.5f),

        // 小节 3：上行
        N(pitch::C5, kE), N(pitch::E5, kE), N(pitch::G5, kE), N(pitch::A5, kE),
        N(pitch::C5 + 12, kQ), N(pitch::A5, kH), N(pitch::G5, kH),
        N(pitch::E5, kH), N(pitch::G5, kH), N(pitch::A5, kH), N(pitch::G5, kH),
        N(pitch::E5, kDQ, Waveform::Square, 0.5f),

        // 小节 4：回落
        N(pitch::D5, kH), N(pitch::E5, kH), N(pitch::D5, kH), N(pitch::C5, kH),
        N(pitch::D5, kQ), N(pitch::E5, kH), N(pitch::G5, kH),
        N(pitch::A5, kQ), N(pitch::G5, kH), N(pitch::E5, kH),
        N(pitch::C5, kQ * 2, Waveform::Triangle, 0.5f),
    };
    return t;
}

Track makeMenuBassTrack()
{
    Track t;
    t.name = "bass";
    t.gain = 0.30f;
    t.defaultWave = Waveform::Triangle;
    t.loop = true;

    // 简单根音走动：C - Am - F - G
    auto bar = [&](int root, int third, int fifth) {
        t.notes.push_back(N(root,  kQ, Waveform::Triangle, 0.5f));
        t.notes.push_back(N(fifth, kQ, Waveform::Triangle, 0.42f));
        t.notes.push_back(N(third, kQ, Waveform::Triangle, 0.38f));
        t.notes.push_back(N(fifth, kQ, Waveform::Triangle, 0.42f));
    };

    bar(pitch::C3,      pitch::E3,     pitch::G3);      // C
    bar(pitch::A3 - 3,  pitch::C4,     pitch::E4);      // Am（近似）
    bar(pitch::F4 - 12, pitch::A3,     pitch::C4);      // F
    bar(pitch::G3,      pitch::B4 - 12, pitch::D4);     // G

    return t;
}

// ===========================================================================
//  对局主题：更快、更紧张，加噪声打点
// ===========================================================================
Track makeGameLeadTrack()
{
    Track t;
    t.name = "lead";
    t.gain = 0.34f;
    t.loop = true;

    // 用 A 小调五声（A C D E G），紧张感更强
    t.notes = {
        N(pitch::A4, kH), N(pitch::A4, kH), N(pitch::C5, kH), N(pitch::A4, kH),
        N(pitch::D5, kH), N(pitch::C5, kH), N(pitch::A4, kQ),
        N(pitch::E5, kH), N(pitch::D5, kH), N(pitch::C5, kH), N(pitch::A4, kH),
        N(pitch::G4, kQ), N(pitch::A4, kQ),

        N(pitch::C5, kH), N(pitch::D5, kH), N(pitch::E5, kH), N(pitch::D5, kH),
        N(pitch::C5, kQ), N(pitch::A4, kQ),
        N(pitch::G4, kH), N(pitch::A4, kH), N(pitch::C5, kH), N(pitch::D5, kH),
        N(pitch::A4, kQ * 2),
    };
    return t;
}

Track makeGameBassTrack()
{
    Track t;
    t.name = "bass";
    t.gain = 0.26f;
    t.loop = true;

    // 反复的 A 根音 + 八分音型，营造推进感
    for (int rep = 0; rep < 4; ++rep) {
        t.notes.push_back(N(pitch::A3, kE, Waveform::Triangle, 0.45f));
        t.notes.push_back(N(pitch::A3, kE, Waveform::Triangle, 0.30f));
        t.notes.push_back(N(pitch::A3, kE, Waveform::Triangle, 0.40f));
        t.notes.push_back(N(pitch::A3, kE, Waveform::Triangle, 0.28f));
        t.notes.push_back(N(pitch::A3, kE, Waveform::Triangle, 0.45f));
        t.notes.push_back(N(pitch::A3, kE, Waveform::Triangle, 0.30f));
        t.notes.push_back(N(pitch::G3, kE, Waveform::Triangle, 0.38f));
        t.notes.push_back(N(pitch::A3, kE, Waveform::Triangle, 0.40f));
    }
    return t;
}

Track makeGamePercTrack()
{
    Track t;
    t.name = "perc";
    t.gain = 0.16f;
    t.loop = true;

    // 噪声打点：每拍一下「底鼓」，反拍一下「军鼓」
    for (int bar = 0; bar < 8; ++bar) {
        t.notes.push_back(N(40, kE, Waveform::Noise, 0.55f));   // 正拍重
        t.notes.push_back(N(40, kE, Waveform::Noise, 0.12f));
        t.notes.push_back(N(40, kE, Waveform::Noise, 0.10f));
        t.notes.push_back(N(40, kE, Waveform::Noise, 0.30f));   // 反拍
    }
    return t;
}

// ===========================================================================
std::vector<Track> makeMenuTheme()
{
    return { makeMenuLeadTrack(), makeMenuBassTrack() };
}

std::vector<Track> makeGameTheme()
{
    return { makeGameLeadTrack(), makeGameBassTrack(), makeGamePercTrack() };
}

} // namespace ms
