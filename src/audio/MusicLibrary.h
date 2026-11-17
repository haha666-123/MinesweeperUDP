#pragma once
// ============================================================================
//  MusicLibrary：内置曲目（纯代码生成，无音频素材）
//  ---------------------------------------------------------------------------
//  为什么全部用代码写旋律：
//    * 零版权风险 —— 不包含任何他人作品的录音或乐谱
//    * 零素材体积 —— 不需要附带 mp3/ogg
//    * 可参数化 —— 改个 BPM 或调式就是另一首
//
//  以下旋律均为原创的简单五声音阶循环，风格接近 8-bit 游戏背景音。
// ============================================================================
#include "Synth.h"

namespace ms {

//! 主菜单：轻快的五声音阶循环（三角波低音 + 方波主旋律）
Track makeMenuLeadTrack();

//! 主菜单低音声部
Track makeMenuBassTrack();

//! 游戏中对局时：节奏更紧，带噪声打点
Track makeGameLeadTrack();
Track makeGameBassTrack();
Track makeGamePercTrack();

//! 组成完整曲目
std::vector<Track> makeMenuTheme();
std::vector<Track> makeGameTheme();

} // namespace ms
