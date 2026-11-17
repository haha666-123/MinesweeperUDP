#include "MainWindow.h"

#include "MenuScene.h"
#include "SettingsDialog.h"

#include "../ai/LlmAI.h"
#include "../ai/ProbabilityAI.h"
#include "../ai/RandomAI.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace ms {

namespace {

QString posText(const Pos& p)
{
    return QStringLiteral("(%1, %2)").arg(p.x).arg(p.y);
}

std::uint32_t nowMs()
{
    return static_cast<std::uint32_t>(
        QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFFu);
}

} // namespace

// ===========================================================================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_rng(std::random_device{}())
{
    setWindowTitle(QStringLiteral("扫雷 · 双人对战"));
    resize(1180, 780);

    m_settings.load();

    buildUi();

    // 背景音乐：启动即播放菜单主题（可用设置关闭）
    applyMusicSettings();
    if (m_settings.musicEnabled)
        m_music.playMenuTheme();

    m_turnTimer = new QTimer(this);
    m_turnTimer->setInterval(kTickMs);
    connect(m_turnTimer, &QTimer::timeout, this, &MainWindow::onTick);
    m_turnTimer->start();

    m_stack->setCurrentIndex(0);          // 启动进入菜单
}

MainWindow::~MainWindow()
{
    stopAll();
}

void MainWindow::stopAll()
{
    if (m_turnTimer)
        m_turnTimer->stop();
    m_host.stop();
    m_client.disconnect();
}

// ===========================================================================
//  界面
// ===========================================================================
void MainWindow::buildUi()
{
    m_stack = new QStackedWidget(this);

    // ---- 场景 0：菜单
    m_menu = new MenuScene(m_stack);
    connect(m_menu, &MenuScene::startLocal,  this, &MainWindow::startLocalGame);
    connect(m_menu, &MenuScene::startAI,     this, &MainWindow::startAiGame);
    connect(m_menu, &MenuScene::startHost,   this, &MainWindow::startHostGame);
    connect(m_menu, &MenuScene::startClient, this, &MainWindow::startClientGame);
    connect(m_menu, &MenuScene::openSettings, this, &MainWindow::openSettings);
    connect(m_menu, &MenuScene::quitApp,     this, &QWidget::close);
    m_stack->addWidget(m_menu);

    // ---- 场景 1：游戏
    m_gamePage = buildGamePage();
    m_stack->addWidget(m_gamePage);

    setCentralWidget(m_stack);
    statusBar()->showMessage(QStringLiteral("就绪"));
}

QWidget* MainWindow::buildGamePage()
{
    auto* page = new QWidget(this);
    auto* rootLay = new QVBoxLayout(page);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    // ================= 顶部工具条（返回 / 当前模式 / 提示）
    auto* topBar = new QWidget(page);
    topBar->setStyleSheet(QStringLiteral("background:#20242C;"));
    auto* topLay = new QHBoxLayout(topBar);
    topLay->setContentsMargins(10, 6, 10, 6);

    auto* backBtn = new QPushButton(QStringLiteral("‹ 返回菜单"), topBar);
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#2F3540;color:#D8DEE8;border:1px solid #39404C;"
        "border-radius:4px;padding:6px 14px;}"
        "QPushButton:hover{background:#3A4150;border-color:#5FA8FF;}"));
    connect(backBtn, &QPushButton::clicked, this, &MainWindow::backToMenu);
    topLay->addWidget(backBtn);

    topLay->addSpacing(16);

    m_modeLabel = new QLabel(topBar);
    m_modeLabel->setStyleSheet(QStringLiteral("color:#8A94A3;font-size:13px;"));
    topLay->addWidget(m_modeLabel);

    topLay->addStretch();

    m_flagHint = new QLabel(
        QStringLiteral("左键翻开（结束回合）　·　右键插旗（不结束回合，可连续标记）"), topBar);
    m_flagHint->setStyleSheet(QStringLiteral("color:#5FA8FF;font-size:12px;"));
    topLay->addWidget(m_flagHint);

    topLay->addSpacing(16);
    m_muteBtn = new QPushButton(topBar);
    m_muteBtn->setCursor(Qt::PointingHandCursor);
    m_muteBtn->setFixedWidth(90);
    m_muteBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#2F3540;color:#D8DEE8;border:1px solid #39404C;"
        "border-radius:4px;padding:5px 10px;}"
        "QPushButton:hover{background:#3A4150;border-color:#5FA8FF;}"));
    connect(m_muteBtn, &QPushButton::clicked, this, [this] {
        m_settings.musicEnabled = !m_settings.musicEnabled;
        m_settings.save();
        applyMusicSettings();
        if (m_settings.musicEnabled) {
            if (m_stack->currentIndex() == 0) m_music.playMenuTheme();
            else                              m_music.playGameTheme();
        } else {
            m_music.stop();
        }
        updateMuteButton();
    });
    topLay->addWidget(m_muteBtn);

    rootLay->addWidget(topBar);

    // ================= 主体
    m_board = new BoardWidget(page);
    connect(m_board, &BoardWidget::revealRequested, this, &MainWindow::onRevealRequested);
    connect(m_board, &BoardWidget::flagRequested, this, &MainWindow::onFlagRequested);

    auto* right = new QWidget(page);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(12, 12, 12, 12);
    rv->setSpacing(10);

    // ---- 对局信息卡片
    auto* info = new QWidget(right);
    info->setStyleSheet(QStringLiteral(
        "background:#20242C;border:1px solid #2C313A;border-radius:6px;"));
    auto* infoLay = new QVBoxLayout(info);
    infoLay->setContentsMargins(14, 12, 14, 12);
    infoLay->setSpacing(6);

    m_p1Label = new QLabel(info);
    m_p2Label = new QLabel(info);
    m_turnLabel = new QLabel(info);
    m_timerLabel = new QLabel(info);

    m_p1Label->setStyleSheet(QStringLiteral("color:#5FA8FF;font-size:14px;font-family:Consolas;"));
    m_p2Label->setStyleSheet(QStringLiteral("color:#FF9F5A;font-size:14px;font-family:Consolas;"));
    m_turnLabel->setStyleSheet(QStringLiteral("color:#E8ECF2;font-size:15px;font-weight:bold;"));
    m_timerLabel->setStyleSheet(QStringLiteral("color:#67D68A;font-size:26px;font-weight:bold;font-family:Consolas;"));

    infoLay->addWidget(m_p1Label);
    infoLay->addWidget(m_p2Label);
    infoLay->addSpacing(4);
    infoLay->addWidget(m_turnLabel);
    infoLay->addWidget(m_timerLabel);

    rv->addWidget(info);

    // ---- 日志
    auto* logTitle = new QLabel(QStringLiteral("事件日志"), right);
    logTitle->setStyleSheet(QStringLiteral("color:#8A94A3;font-size:12px;"));
    rv->addWidget(logTitle);

    m_log = new QTextEdit(right);
    m_log->setReadOnly(true);
    m_log->setStyleSheet(QStringLiteral(
        "font-family:Consolas;font-size:12px;background:#1A1D23;color:#C8D0DC;"
        "border:1px solid #2C313A;border-radius:6px;"));
    rv->addWidget(m_log, 1);

    auto* split = new QSplitter(Qt::Horizontal, page);
    split->addWidget(m_board);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({ 720, 440 });

    rootLay->addWidget(split, 1);
    return page;
}

// ===========================================================================
//  菜单动作
// ===========================================================================
void MainWindow::startLocalGame()  { beginGame(Mode::Local); }
void MainWindow::startAiGame()     { beginGame(Mode::AI); }
void MainWindow::startHostGame()   { beginGame(Mode::Host); }
void MainWindow::startClientGame() { beginGame(Mode::Client); }

void MainWindow::openSettings()
{
    SettingsDialog dlg(m_settings, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_settings = dlg.result();
    m_settings.save();
    applyMusicSettings();

    if (m_settings.musicEnabled) {
        if (m_stack->currentIndex() == 0) m_music.playMenuTheme();
        else                              m_music.playGameTheme();
    } else {
        m_music.stop();
    }
    log(QStringLiteral("设置已更新：回合时间 %1 秒，棋盘 %2×%3，雷密度 %4")
            .arg(m_settings.turnSeconds)
            .arg(m_settings.boardSize.width())
            .arg(m_settings.boardSize.height())
            .arg(m_settings.mineRatio, 0, 'f', 2));
}

void MainWindow::applyMusicSettings()
{
    m_music.setVolume(m_settings.musicVolume);
    m_music.setMuted(!m_settings.musicEnabled);
    updateMuteButton();
}

void MainWindow::updateMuteButton()
{
    if (!m_muteBtn)
        return;
    if (!m_music.available()) {
        m_muteBtn->setText(QStringLiteral("无音频"));
        m_muteBtn->setEnabled(false);
        return;
    }
    m_muteBtn->setText(m_settings.musicEnabled
                           ? QStringLiteral("音乐 开")
                           : QStringLiteral("音乐 关"));
}

void MainWindow::backToMenu()
{
    stopAll();

    // 重新启动定时器（stopAll 里停了，回菜单不需要它跑，但进入新对局时会重启）
    m_stack->setCurrentIndex(0);
    if (m_settings.musicEnabled)
        m_music.playMenuTheme();
    statusBar()->showMessage(QStringLiteral("已返回菜单"));
}

// ===========================================================================
//  开始对局
// ===========================================================================
void MainWindow::beginGame(Mode mode)
{
    m_mode = mode;
    m_msAccum = 0;
    m_ai.reset();
    m_host.stop();
    m_client.disconnect();
    m_log->clear();

    m_stack->setCurrentIndex(1);
    if (!m_turnTimer->isActive())
        m_turnTimer->start();

    if (m_settings.musicEnabled)
        m_music.playGameTheme();

    // ---------------------------------------------------------------- 联机：主机
    if (mode == Mode::Host) {
        GameConfig cfg;
        cfg.mapW           = m_settings.boardSize.width();
        cfg.mapH           = m_settings.boardSize.height();
        cfg.mineRatio      = m_settings.mineRatio;
        cfg.startLives     = m_settings.startLives;
        cfg.turnSeconds    = m_settings.turnSeconds;
        cfg.allowInterfere = m_settings.allowInterfere;

        std::string err;
        if (!m_host.start(static_cast<std::uint16_t>(m_settings.port), cfg, &err)) {
            QMessageBox::warning(this, QStringLiteral("开房失败"),
                                 QString::fromStdString(err));
            m_stack->setCurrentIndex(0);
            return;
        }
        m_modeLabel->setText(QStringLiteral("模式：联机 · 主机　端口 %1").arg(m_host.localPort()));
        m_board->setGame(&m_host.game());
        m_board->setInteractive(false);
        log(QStringLiteral("已开房，监听 UDP 端口 %1（seed=%2），等待对手加入…")
                .arg(m_host.localPort()).arg(m_host.game().seed()));
        updateStatus();
        return;
    }

    // ---------------------------------------------------------------- 联机：客户端
    if (mode == Mode::Client) {
        std::string err;
        if (!m_client.connect(m_settings.hostAddress.toStdString(),
                              static_cast<std::uint16_t>(m_settings.port), &err)) {
            QMessageBox::warning(this, QStringLiteral("连接失败"),
                                 QString::fromStdString(err));
            m_stack->setCurrentIndex(0);
            return;
        }
        m_modeLabel->setText(QStringLiteral("模式：联机 · 客户端　→ %1:%2")
                                 .arg(m_settings.hostAddress).arg(m_settings.port));
        m_board->setGame(&m_client.game());
        m_board->setInteractive(false);
        log(QStringLiteral("正在连接 %1:%2 …")
                .arg(m_settings.hostAddress).arg(m_settings.port));
        updateStatus();
        return;
    }

    // ---------------------------------------------------------------- 本地 / AI
    GameConfig cfg;
    cfg.mapW           = m_settings.boardSize.width();
    cfg.mapH           = m_settings.boardSize.height();
    cfg.mineRatio      = m_settings.mineRatio;
    cfg.startLives     = m_settings.startLives;
    cfg.turnSeconds    = m_settings.turnSeconds;
    cfg.allowInterfere = m_settings.allowInterfere;
    cfg.seed           = 0;

    m_game.reset(cfg, 0);
    m_board->setGame(&m_game);
    m_board->setInteractive(true);

    if (mode == Mode::AI) {
        m_aiPlayer = 2;
        switch (m_settings.aiDifficulty) {
        case 0: m_ai = std::make_unique<RandomAI>(); break;
        case 2: {
            auto llm = std::make_unique<LlmAI>();
            LlmAI::Config lc;
            lc.endpoint = m_settings.llmEndpoint.toStdString();
            lc.apiKey   = m_settings.llmKey.toStdString();
            lc.model    = m_settings.llmModel.toStdString();
            llm->configure(lc);
            m_ai = std::move(llm);
            break;
        }
        default: m_ai = std::make_unique<ProbabilityAI>(); break;
        }
        m_modeLabel->setText(QStringLiteral("模式：人机对战　AI = %1")
                                 .arg(QString::fromUtf8(m_ai->name())));
        log(QStringLiteral("新对局：玩家 vs %1，棋盘 %2×%3，回合 %4 秒")
                .arg(QString::fromUtf8(m_ai->name()))
                .arg(cfg.mapW).arg(cfg.mapH).arg(cfg.turnSeconds));
    } else {
        m_modeLabel->setText(QStringLiteral("模式：本地双人"));
        log(QStringLiteral("新对局：本地双人，棋盘 %1×%2，回合 %3 秒，seed=%4")
                .arg(cfg.mapW).arg(cfg.mapH).arg(cfg.turnSeconds).arg(m_game.seed()));
    }

    updateStatus();
}

// ===========================================================================
//  操作
// ===========================================================================
void MainWindow::onRevealRequested(Pos pos)
{
    if (m_game.isOver())
        return;

    if (m_mode == Mode::Host) {
        if (!isMyTurn()) { log(QStringLiteral("还没轮到你")); return; }
        Action a;
        a.type  = ActionType::Reveal;
        a.pos   = pos;
        a.actor = 1;
        m_host.submitLocalAction(a);
        m_board->update();
        updateStatus();
        return;
    }

    if (m_mode == Mode::Client) {
        if (!isMyTurn()) { log(QStringLiteral("还没轮到你")); return; }
        Action a;
        a.type  = ActionType::Reveal;
        a.pos   = pos;
        a.actor = 2;
        if (m_client.submitAction(a))
            log(QStringLiteral("已发送：翻开 %1（等待主机确认）").arg(posText(pos)));
        return;
    }

    Action a;
    a.type  = ActionType::Reveal;
    a.pos   = pos;
    a.actor = m_game.currentPlayer();

    const auto r = m_engine.apply(m_game, a);
    if (r == TurnResult::Invalid) {
        log(QStringLiteral("无法翻开 %1（已翻开 / 有旗）").arg(posText(pos)));
        return;
    }

    log(QStringLiteral("玩家 %1 翻开 %2 → 得分 %3，生命 %4")
            .arg(a.actor).arg(posText(pos))
            .arg(m_game.player(a.actor).score)
            .arg(m_game.player(a.actor).lives));

    m_msAccum = 0;
    m_board->update();
    updateStatus();
}

void MainWindow::onFlagRequested(Pos pos)
{
    if (m_game.isOver())
        return;

    if (m_mode == Mode::Host) {
        if (!isMyTurn()) return;
        Action a;
        a.type  = ActionType::Flag;
        a.pos   = pos;
        a.actor = 1;
        m_host.submitLocalAction(a);
        m_board->update();
        return;
    }

    if (m_mode == Mode::Client) {
        if (!isMyTurn()) return;
        Action a;
        a.type  = ActionType::Flag;
        a.pos   = pos;
        a.actor = 2;
        m_client.submitAction(a);
        return;
    }

    Action a;
    a.type  = ActionType::Flag;
    a.pos   = pos;
    a.actor = m_game.currentPlayer();

    // 注意：插旗返回 Ok，不结束回合，所以这里不重置 m_msAccum
    if (m_engine.apply(m_game, a) == TurnResult::Invalid)
        return;

    const Cell& c = m_game.cellAt(pos);
    if (c.hasFlag())
        log(QStringLiteral("玩家 %1 标记 %2").arg(a.actor).arg(posText(pos)));
    else
        log(QStringLiteral("玩家 %1 取消标记 %2").arg(a.actor).arg(posText(pos)));

    m_board->update();
    updateStatus();
}

// ===========================================================================
//  定时器
// ===========================================================================
void MainWindow::onTick()
{
    if (m_stack->currentIndex() != 1)
        return;

    // ---- 联机
    if (m_mode == Mode::Host) {
        m_host.poll(nowMs());
        if (m_host.hasClient()) {
            m_board->setInteractive(!m_host.game().isOver() && isMyTurn());
        }
        m_game = m_host.game();
        m_board->update();
        updateStatus();
        return;
    }

    if (m_mode == Mode::Client) {
        m_client.poll(nowMs());
        m_game = m_client.game();
        m_board->setGame(&m_client.game());
        m_board->setInteractive(m_client.connected() && isMyTurn() &&
                                !m_client.game().isOver());
        updateStatus();
        return;
    }

    // ---- 本地 / AI 倒计时
    if (m_game.isOver())
        return;

    m_msAccum += kTickMs;
    while (m_msAccum >= 1000) {
        m_msAccum -= 1000;
        if (m_game.remainingSeconds() > 0) {
            m_game.decRemainingSeconds();
        } else {
            log(QStringLiteral("玩家 %1 回合超时，自动换人").arg(m_game.currentPlayer()));
            m_engine.onTurnTimeout(m_game);
            m_board->update();
        }
    }
    updateStatus();

    // ---- AI 回合
    if (m_mode == Mode::AI && !m_game.isOver() &&
        m_game.currentPlayer() == m_aiPlayer && m_ai) {
        const AIDecision d = m_ai->decide(m_game, m_rng);
        if (!d.valid)
            return;

        Action a;
        a.type  = d.type;
        a.pos   = d.pos;
        a.actor = m_aiPlayer;
        if (m_engine.apply(m_game, a) == TurnResult::Invalid)
            return;

        log(QStringLiteral("AI %1 %2 — %3")
                .arg(d.type == ActionType::Flag ? QStringLiteral("标记")
                                                : QStringLiteral("翻开"),
                     posText(d.pos),
                     QString::fromStdString(d.reason)));

        // AI 插旗不结束回合，只有翻开才重置计时
        if (d.type == ActionType::Reveal)
            m_msAccum = 0;

        m_board->update();
        updateStatus();
    }
}

// ===========================================================================
//  状态
// ===========================================================================
bool MainWindow::isNetworkMode() const
{
    return m_mode == Mode::Host || m_mode == Mode::Client;
}

bool MainWindow::isMyTurn() const
{
    if (m_mode == Mode::Host)   return m_game.currentPlayer() == 1;
    if (m_mode == Mode::Client) return m_game.currentPlayer() == 2;
    return true;
}

void MainWindow::updateStatus()
{
    const GameState& g = m_game;

    auto line = [&](int id) {
        const PlayerState& p = g.player(id);
        const QString who = (id == 1 ? QStringLiteral("玩家 1") : QStringLiteral("玩家 2"));
        const bool ai = (m_mode == Mode::AI && id == m_aiPlayer);
        const bool cur = (g.currentPlayer() == id && !g.isOver());
        return QStringLiteral("%1%2%3　生命 %4　得分 %5　踩雷 %6")
            .arg(cur ? QStringLiteral("▶ ") : QStringLiteral("   "),
                 who,
                 ai ? QStringLiteral("（AI）") : QString(),
                 QString::number(p.lives),
                 QString::number(p.score),
                 QString::number(p.minedHits));
    };

    m_p1Label->setText(line(1));
    m_p2Label->setText(line(2));

    if (g.isOver()) {
        m_turnLabel->setText(QStringLiteral("对局结束 — 胜者：%1")
                                 .arg(QLatin1String(toString(g.winner()))));
        m_timerLabel->setText(QStringLiteral("—"));
        statusBar()->showMessage(QStringLiteral("对局结束"));
        return;
    }

    const int cur = g.currentPlayer();
    QString turn = QStringLiteral("轮到 玩家 %1").arg(cur);
    if (m_mode == Mode::AI && cur == m_aiPlayer)
        turn += QStringLiteral("（AI 思考中）");
    if (isNetworkMode())
        turn += isMyTurn() ? QStringLiteral("（你的回合）") : QStringLiteral("（等待对手）");
    m_turnLabel->setText(turn);

    m_timerLabel->setText(QStringLiteral("%1 秒").arg(g.remainingSeconds()));
    statusBar()->showMessage(
        QStringLiteral("剩余安全格 %1 / 已翻开 %2")
            .arg(g.board().safeCount() - g.revealedSafeCount())
            .arg(g.revealedSafeCount()));
}

void MainWindow::log(const QString& text)
{
    if (!m_log)
        return;
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_log->append(QStringLiteral("<span style='color:#6B7684'>[%1]</span> %2")
                      .arg(ts, text.toHtmlEscaped()));
}

} // namespace ms
