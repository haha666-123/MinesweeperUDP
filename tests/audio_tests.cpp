// ============================================================================
//  audio 层测试
//  ---------------------------------------------------------------------------
//  音乐是「看得见波形、听不出来的」——所以用程序验证：
//    1. MIDI 音高 -> 频率换算正确（A4 = 440Hz 等已知值）
//    2. 波形采样值落在 [-1, 1]，方波/三角波形状正确
//    3. ADSR 包络：起音从 0 升、释放降到 0
//    4. 渲染出的 PCM 有明显的左右声道与幅度（不是全零）
//    5. 曲目能循环，且循环点不产生静音断档
// ============================================================================
#include "audio/MusicLibrary.h"
#include "audio/Synth.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* what)
{
    if (cond) { ++g_pass; std::printf("  [PASS] %s\n", what); }
    else      { ++g_fail; std::printf("  [FAIL] %s\n", what); }
    std::fflush(stdout);
}

void section(const char* s)
{
    std::printf("\n[%s]\n", s);
    std::fflush(stdout);
}

} // namespace

int main()
{
    std::printf("=== audio layer tests ===\n");
    using namespace ms;

    // ---------------------------------------------------------------- 1 频率
    section("1. MIDI 音高 -> 频率");
    {
        auto near = [](double a, double b) { return std::fabs(a - b) < 0.5; };

        check(near(Synth::midiToFreq(69), 440.0), "A4(69) = 440 Hz");
        check(near(Synth::midiToFreq(60), 261.63), "C4(60) ≈ 261.63 Hz");
        check(near(Synth::midiToFreq(81), 880.0), "A5(81) = 880 Hz（八度翻倍）");
        check(near(Synth::midiToFreq(48), 130.81), "C3(48) ≈ 130.81 Hz");
        check(Synth::midiToFreq(0) == 0.0, "休止符(0) 返回 0");
    }

    // ---------------------------------------------------------------- 2 波形
    section("2. 波形采样");
    {
        std::uint32_t st = 12345;

        // 方波：前半周期 +1，后半周期 -1
        const float sq1 = Synth::waveSample(Waveform::Square, 0.25, st);
        const float sq2 = Synth::waveSample(Waveform::Square, 0.75, st);
        check(sq1 == 1.0f && sq2 == -1.0f, "方波：相位 0.25 -> +1，0.75 -> -1");

        // 三角波：0.25 处应为 +1
        const float tr = Synth::waveSample(Waveform::Triangle, 0.25, st);
        check(tr > 0.95f && tr <= 1.0f, "三角波：0.25 相位接近峰值 +1");

        // 噪声：应在 [-1, 1] 且不全相同
        std::uint32_t ns = 999;
        float n1 = Synth::waveSample(Waveform::Noise, 0.0, ns);
        float n2 = Synth::waveSample(Waveform::Noise, 0.0, ns);
        check(n1 >= -1.0f && n1 <= 1.0f, "噪声落在 [-1, 1]");
        check(std::fabs(n1 - n2) > 1e-6f, "噪声连续两次采样不同");

        // 所有波形都不越界
        std::uint32_t s2 = 7;
        bool inRange = true;
        for (int i = 0; i < 500; ++i) {
            const double ph = i / 500.0;
            for (auto w : { Waveform::Square, Waveform::Triangle, Waveform::Noise }) {
                const float v = Synth::waveSample(w, ph, s2);
                if (v < -1.0f || v > 1.0f)
                    inRange = false;
            }
        }
        check(inRange, "三种波形全部在 [-1, 1] 范围内");
    }

    // ---------------------------------------------------------------- 3 ADSR
    section("3. ADSR 包络");
    {
        Adsr env;
        env.attackSec = 0.01f;
        env.decaySec  = 0.05f;
        env.sustain   = 0.6f;
        env.releaseSec = 0.05f;
        const float dur = 0.5f;

        const float atStart = env.evaluate(0.0f, dur);
        const float atPeak  = env.evaluate(0.010f, dur);
        const float atSustain = env.evaluate(0.2f, dur);
        const float atEnd   = env.evaluate(dur, dur);

        check(atStart < 0.01f, "起音起点为 0（避免爆音）");
        check(atPeak > 0.95f, "起音结束到达峰值");
        check(std::fabs(atSustain - 0.6f) < 0.02f, "延音段稳定在 sustain 电平");
        check(atEnd < 0.02f, "释放到结尾衰减为 0");
    }

    // ---------------------------------------------------------------- 4 渲染
    section("4. 渲染 PCM");
    {
        SynthConfig cfg;
        cfg.sampleRate = 44100;
        cfg.channels   = 2;
        cfg.bpm        = 112.0;

        Synth synth(cfg);
        for (const Track& t : makeMenuTheme())
            synth.addTrack(t);

        check(synth.trackCount() == 2, "菜单主题有 2 个音轨");
        check(synth.totalMilliBeats() > 0, "总拍数 > 0");

        synth.restart();

        // 渲染 0.5 秒
        const std::size_t bytes = 44100 * 2 * 2 / 2;   // 半秒
        std::vector<std::uint8_t> buf(bytes, 0);
        const std::size_t written = synth.render(buf.data(), buf.size());
        check(written > 0, "渲染产生了数据");

        // 检查是否非全零，且峰值合理
        const auto* pcm = reinterpret_cast<const std::int16_t*>(buf.data());
        const std::size_t samples = written / 2;
        std::int16_t peak = 0;
        std::int64_t sumSq = 0;
        std::size_t nonzero = 0;
        for (std::size_t i = 0; i < samples; ++i) {
            const std::int16_t s = pcm[i];
            peak = std::max<std::int16_t>(peak, static_cast<std::int16_t>(std::abs(s)));
            sumSq += static_cast<std::int64_t>(s) * s;
            if (s != 0) ++nonzero;
        }

        const double rms = std::sqrt(static_cast<double>(sumSq) / samples);
        std::printf("         峰值=%d  RMS=%.0f  非零采样=%zu/%zu\n",
                    peak, rms, nonzero, samples);

        check(peak > 1000, "峰值足够大（确实有声音，不是静音）");
        check(peak <= 32767, "峰值未超出 16-bit 范围");
        check(rms > 500.0, "RMS 能量足够");
        check(nonzero > samples / 2, "超过一半采样非零");

        // 立体声：左右声道应基本相同（当前是单声道复制到双声道）
        bool sameChannel = true;
        for (std::size_t i = 0; i + 1 < samples; i += 2)
            if (pcm[i] != pcm[i + 1]) { sameChannel = false; break; }
        check(sameChannel, "左右声道一致（单声道复制）");
    }

    // ---------------------------------------------------------------- 5 循环
    section("5. 循环播放不中断");
    {
        SynthConfig cfg;
        Synth synth(cfg);
        for (const Track& t : makeGameTheme())
            synth.addTrack(t);

        check(synth.trackCount() == 3, "对局主题有 3 个音轨（含打击乐）");

        synth.restart();

        // 连续渲染 3 秒，跨越多个循环点，不应出现长时间静音
        const std::size_t chunk = 4096;
        std::vector<std::uint8_t> buf(chunk);
        int silentChunks = 0;
        int totalChunks = 0;

        // 每块 4096 字节 = 1024 帧 = 约 23ms；150 块 ≈ 3.5 秒
        for (int i = 0; i < 150; ++i) {
            const std::size_t n = synth.render(buf.data(), buf.size());
            if (n == 0) break;
            ++totalChunks;

            const auto* pcm = reinterpret_cast<const std::int16_t*>(buf.data());
            const std::size_t cnt = n / 2;
            bool allZero = true;
            for (std::size_t k = 0; k < cnt; ++k)
                if (pcm[k] != 0) { allZero = false; break; }
            if (allZero) ++silentChunks;
        }

        std::printf("         渲染 %d 块，其中静音 %d 块\n", totalChunks, silentChunks);
        check(totalChunks > 10, "渲染了足够多的块");
        check(silentChunks == 0, "循环播放过程中没有静音块（无断档）");
        check(!synth.finished(), "循环模式下 finished() 应为 false");
    }

    // ---------------------------------------------------------------- 6 非循环
    section("6. 非循环曲目会结束");
    {
        Synth synth;
        Track t;
        t.loop = false;
        Note n;
        n.midi = 69;
        n.milliBeats = 200;
        t.notes.push_back(n);
        synth.addTrack(t);
        synth.restart();

        std::vector<std::uint8_t> buf(8820);     // 25ms
        int guard = 0;
        while (!synth.finished() && guard++ < 2000)
            synth.render(buf.data(), buf.size());

        check(guard < 2000, "非循环曲目在有限步内结束");
    }

    std::printf("\n=== %s  通过 %d / 失败 %d ===\n",
                g_fail == 0 ? "ALL PASSED" : "SOME FAILED", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
