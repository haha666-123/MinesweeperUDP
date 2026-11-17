#pragma once
// ============================================================================
//  设置
//  ---------------------------------------------------------------------------
//  集中管理可调参数，由设置对话框修改，主窗口读取。
//  用 QSettings 持久化到注册表/配置文件，下次启动自动恢复。
// ============================================================================
#include <QSettings>
#include <QSize>

namespace ms {

struct Settings {
    // ---- 对局
    QSize   boardSize{ 12, 12 };
    double  mineRatio   = 0.18;
    int     startLives  = 3;
    int     turnSeconds = 20;
    bool    allowInterfere = true;

    // ---- AI
    int     aiDifficulty = 1;        // 0=Random 1=Probability 2=LLM
    QString llmEndpoint  = QStringLiteral("https://api.deepseek.com/chat/completions");
    QString llmKey;
    QString llmModel     = QStringLiteral("deepseek-chat");

    // ---- 网络
    QString hostAddress = QStringLiteral("127.0.0.1");
    int     port        = 9527;

    // ---- 音乐
    bool    musicEnabled = true;
    int     musicVolume  = 45;          // 0..100

    // ---- 界面
    bool    showCoordinates = false;  // 是否在格子上显示坐标（调试用）

    // ---------------------------------------------------------------- 持久化
    void load()
    {
        QSettings s(QStringLiteral("SongGuohui"), QStringLiteral("UdpMinesweeper"));
        boardSize       = s.value(QStringLiteral("board/size"), boardSize).toSize();
        mineRatio       = s.value(QStringLiteral("board/mineRatio"), mineRatio).toDouble();
        startLives      = s.value(QStringLiteral("game/lives"), startLives).toInt();
        turnSeconds     = s.value(QStringLiteral("game/turnSeconds"), turnSeconds).toInt();
        allowInterfere  = s.value(QStringLiteral("game/interfere"), allowInterfere).toBool();
        aiDifficulty    = s.value(QStringLiteral("ai/difficulty"), aiDifficulty).toInt();
        llmEndpoint     = s.value(QStringLiteral("ai/endpoint"), llmEndpoint).toString();
        llmKey          = s.value(QStringLiteral("ai/key"), llmKey).toString();
        llmModel        = s.value(QStringLiteral("ai/model"), llmModel).toString();
        hostAddress     = s.value(QStringLiteral("net/host"), hostAddress).toString();
        port            = s.value(QStringLiteral("net/port"), port).toInt();
        musicEnabled    = s.value(QStringLiteral("audio/enabled"), musicEnabled).toBool();
        musicVolume     = s.value(QStringLiteral("audio/volume"), musicVolume).toInt();
        showCoordinates = s.value(QStringLiteral("ui/coords"), showCoordinates).toBool();
    }

    void save() const
    {
        QSettings s(QStringLiteral("SongGuohui"), QStringLiteral("UdpMinesweeper"));
        s.setValue(QStringLiteral("board/size"), boardSize);
        s.setValue(QStringLiteral("board/mineRatio"), mineRatio);
        s.setValue(QStringLiteral("game/lives"), startLives);
        s.setValue(QStringLiteral("game/turnSeconds"), turnSeconds);
        s.setValue(QStringLiteral("game/interfere"), allowInterfere);
        s.setValue(QStringLiteral("ai/difficulty"), aiDifficulty);
        s.setValue(QStringLiteral("ai/endpoint"), llmEndpoint);
        s.setValue(QStringLiteral("ai/key"), llmKey);
        s.setValue(QStringLiteral("ai/model"), llmModel);
        s.setValue(QStringLiteral("net/host"), hostAddress);
        s.setValue(QStringLiteral("net/port"), port);
        s.setValue(QStringLiteral("audio/enabled"), musicEnabled);
        s.setValue(QStringLiteral("audio/volume"), musicVolume);
        s.setValue(QStringLiteral("ui/coords"), showCoordinates);
    }
};

} // namespace ms
