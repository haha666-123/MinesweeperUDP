#include "MenuScene.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace ms {

namespace {
const QColor kBg{ 0x16, 0x19, 0x1F };
const QColor kAccent{ 0x5F, 0xA8, 0xFF };
} // namespace

MenuScene::MenuScene(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void MenuScene::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto* spacerTop = new QSpacerItem(20, 60, QSizePolicy::Minimum, QSizePolicy::Expanding);
    root->addItem(spacerTop);

    // ---------------- 标题
    auto* title = new QLabel(QStringLiteral("扫雷 · 双人对战"), this);
    QFont f = title->font();
    f.setPointSize(34);
    f.setBold(true);
    title->setFont(f);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("color:#5FA8FF;"));
    root->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral("UDP 联机 · 三档 AI · C++17 + Qt"), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("color:#8A94A3;font-size:13px;"));
    root->addWidget(subtitle);

    root->addSpacing(40);

    // ---------------- 按钮
    auto makeButton = [this](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text, this);
        b->setMinimumSize(260, 46);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{"
            "  background:#252A33;color:#D8DEE8;border:1px solid #39404C;"
            "  border-radius:6px;font-size:15px;"
            "}"
            "QPushButton:hover{background:#2F3540;border-color:#5FA8FF;color:#FFFFFF;}"
            "QPushButton:pressed{background:#1E2229;}"));
        return b;
    };

    m_btnLocal = makeButton(QStringLiteral("本地双人"),
                            QStringLiteral("同一台机器两人轮流操作"));
    m_btnAI = makeButton(QStringLiteral("人机对战"),
                         QStringLiteral("与 AI 对局，难度可在设置里切换"));
    m_btnHost = makeButton(QStringLiteral("开房（主机）"),
                           QStringLiteral("监听 UDP 端口，等待对手加入"));
    m_btnClient = makeButton(QStringLiteral("加入房间"),
                             QStringLiteral("填写主机 IP 与端口加入对局"));

    for (QPushButton* b : { m_btnLocal, m_btnAI, m_btnHost, m_btnClient }) {
        auto* row = new QHBoxLayout();
        row->addStretch();
        row->addWidget(b);
        row->addStretch();
        root->addLayout(row);
        root->addSpacing(10);
    }

    root->addSpacing(16);

    m_btnSettings = makeButton(QStringLiteral("设置"), QStringLiteral("回合时间、棋盘、AI、网络"));
    m_btnSettings->setMinimumSize(160, 38);
    m_btnQuit = makeButton(QStringLiteral("退出"), QStringLiteral("关闭游戏"));
    m_btnQuit->setMinimumSize(160, 38);

    auto* row2 = new QHBoxLayout();
    row2->addStretch();
    row2->addWidget(m_btnSettings);
    row2->addSpacing(16);
    row2->addWidget(m_btnQuit);
    row2->addStretch();
    root->addLayout(row2);

    root->addStretch();

    m_version = new QLabel(QStringLiteral("v1.0.0  ·  插旗不结束回合  ·  右键标记"), this);
    m_version->setAlignment(Qt::AlignCenter);
    m_version->setStyleSheet(QStringLiteral("color:#5A6270;font-size:11px;"));
    root->addWidget(m_version);
    root->addSpacing(16);

    // ---------------- 信号
    connect(m_btnLocal,    &QPushButton::clicked, this, &MenuScene::startLocal);
    connect(m_btnAI,       &QPushButton::clicked, this, &MenuScene::startAI);
    connect(m_btnHost,     &QPushButton::clicked, this, &MenuScene::startHost);
    connect(m_btnClient,   &QPushButton::clicked, this, &MenuScene::startClient);
    connect(m_btnSettings, &QPushButton::clicked, this, &MenuScene::openSettings);
    connect(m_btnQuit,     &QPushButton::clicked, this, &MenuScene::quitApp);
}

void MenuScene::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), kBg);

    // 简单装饰：顶部一道渐变亮线
    QLinearGradient g(0, 0, width(), 0);
    g.setColorAt(0.0, Qt::transparent);
    g.setColorAt(0.5, kAccent);
    g.setColorAt(1.0, Qt::transparent);
    p.fillRect(QRect(0, 0, width(), 2), g);
}

} // namespace ms
