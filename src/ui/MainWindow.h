#pragma once
// ============================================================================
//  MainWindow：场景容器 + 游戏页
//  ---------------------------------------------------------------------------
//  结构：
//    QStackedWidget
//      ├─ MenuScene  标题菜单（对应你原项目的 MenuScence）
//      └─ GamePage   游戏页（棋盘 + 控制面板）
//
//  切换：菜单点「本地双人」-> setCurrentIndex(1) 并开始对局
//        游戏页点「返回菜单」-> setCurrentIndex(0)
// ============================================================================
#include "../ai/IAI.h"
#include "../core/GameState.h"
#include "../core/RuleEngine.h"
#include "../net/ClientSession.h"
#include "../net/HostSession.h"
#include "../audio/MusicPlayer.h"
#include "BoardWidget.h"
#include "Settings.h"

#include <QMainWindow>
#include <QTimer>

#include <memory>
#include <random>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QStackedWidget;
class QTextEdit;
QT_END_NAMESPACE

namespace ms {

class MenuScene;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    // 菜单回调
    void startLocalGame();
    void startAiGame();
    void startHostGame();
    void startClientGame();
    void openSettings();

    // 游戏页
    void onRevealRequested(ms::Pos pos);
    void onFlagRequested(ms::Pos pos);
    void onTick();
    void backToMenu();
    void applyMusicSettings();
    void updateMuteButton();

private:
    enum class Mode { Local, AI, Host, Client };

    void buildUi();
    QWidget* buildGamePage();

    void beginGame(Mode mode);
    void updateStatus();
    void log(const QString& text);
    [[nodiscard]] bool isMyTurn() const;
    [[nodiscard]] bool isNetworkMode() const;
    void stopAll();

    // ---- 场景
    QStackedWidget* m_stack = nullptr;
    MenuScene*      m_menu  = nullptr;
    QWidget*        m_gamePage = nullptr;

    // ---- 对局状态
    GameState   m_game;
    RuleEngine  m_engine;
    Mode        m_mode = Mode::Local;
    std::unique_ptr<IAI> m_ai;
    int         m_aiPlayer = 2;

    net::HostSession   m_host;
    net::ClientSession m_client;

    Settings    m_settings;
    MusicPlayer m_music;

    QTimer*     m_turnTimer = nullptr;
    int         m_msAccum   = 0;
    std::mt19937 m_rng;

    // ---- 控件
    BoardWidget* m_board = nullptr;
    QLabel*      m_p1Label = nullptr;
    QLabel*      m_p2Label = nullptr;
    QLabel*      m_turnLabel = nullptr;
    QLabel*      m_timerLabel = nullptr;
    QLabel*      m_modeLabel = nullptr;
    QTextEdit*   m_log = nullptr;
    QLabel*      m_flagHint = nullptr;
    QPushButton* m_muteBtn = nullptr;

    static constexpr int kTickMs = 200;
};

} // namespace ms
