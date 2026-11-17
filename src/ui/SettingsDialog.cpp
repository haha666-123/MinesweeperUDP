#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ms {

SettingsDialog::SettingsDialog(const Settings& current, QWidget* parent)
    : QDialog(parent)
    , m_result(current)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumWidth(520);
    buildUi();
}

void SettingsDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);

    // ================================================================ 对局
    auto* gamePage = new QWidget(tabs);
    auto* gameForm = new QFormLayout(gamePage);

    m_sizeCombo = new QComboBox(gamePage);
    m_sizeCombo->addItem(QStringLiteral("8 × 8（入门）"), QSize(8, 8));
    m_sizeCombo->addItem(QStringLiteral("12 × 12（标准）"), QSize(12, 12));
    m_sizeCombo->addItem(QStringLiteral("16 × 16（专家）"), QSize(16, 16));
    m_sizeCombo->addItem(QStringLiteral("20 × 20（地狱）"), QSize(20, 20));
    for (int i = 0; i < m_sizeCombo->count(); ++i) {
        if (m_sizeCombo->itemData(i).toSize() == m_result.boardSize) {
            m_sizeCombo->setCurrentIndex(i);
            break;
        }
    }
    gameForm->addRow(QStringLiteral("棋盘尺寸"), m_sizeCombo);

    m_mineSpin = new QDoubleSpinBox(gamePage);
    m_mineSpin->setRange(0.08, 0.35);
    m_mineSpin->setSingleStep(0.01);
    m_mineSpin->setDecimals(2);
    m_mineSpin->setValue(m_result.mineRatio);
    m_mineSpin->setSuffix(QStringLiteral("  （雷密度）"));
    gameForm->addRow(QStringLiteral("雷密度"), m_mineSpin);

    m_livesSpin = new QSpinBox(gamePage);
    m_livesSpin->setRange(1, 9);
    m_livesSpin->setValue(m_result.startLives);
    m_livesSpin->setSuffix(QStringLiteral(" 点"));
    gameForm->addRow(QStringLiteral("初始生命"), m_livesSpin);

    m_turnSpin = new QSpinBox(gamePage);
    m_turnSpin->setRange(5, 180);
    m_turnSpin->setValue(m_result.turnSeconds);
    m_turnSpin->setSingleStep(5);
    m_turnSpin->setSuffix(QStringLiteral(" 秒"));
    gameForm->addRow(QStringLiteral("回合时间"), m_turnSpin);

    m_interfereCheck = new QCheckBox(QStringLiteral("允许干扰对手（消耗 3 秒换对手下回合 -3 秒）"),
                                     gamePage);
    m_interfereCheck->setChecked(m_result.allowInterfere);
    gameForm->addRow(QString(), m_interfereCheck);

    auto* hint = new QLabel(
        QStringLiteral("说明：\n"
                       "· 插旗不结束回合，可以在一个回合内连续标记多个格子\n"
                       "· 只有「翻开格子」才把回合交给对手\n"
                       "· 回合时间耗尽会自动换人"),
        gamePage);
    hint->setStyleSheet(QStringLiteral("color:#666;"));
    hint->setWordWrap(true);
    gameForm->addRow(hint);

    tabs->addTab(gamePage, QStringLiteral("对局规则"));

    // ================================================================ AI
    auto* aiPage = new QWidget(tabs);
    auto* aiForm = new QFormLayout(aiPage);

    m_aiCombo = new QComboBox(aiPage);
    m_aiCombo->addItems({
        QStringLiteral("随机（对照组）"),
        QStringLiteral("概率推理（约束求解）"),
        QStringLiteral("大模型（失败自动降级）"),
    });
    m_aiCombo->setCurrentIndex(std::clamp(m_result.aiDifficulty, 0, 2));
    aiForm->addRow(QStringLiteral("AI 难度"), m_aiCombo);

    m_endpoint = new QLineEdit(m_result.llmEndpoint, aiPage);
    aiForm->addRow(QStringLiteral("接口地址"), m_endpoint);

    m_key = new QLineEdit(m_result.llmKey, aiPage);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(QStringLiteral("留空则只用前两档本地算法"));
    aiForm->addRow(QStringLiteral("API Key"), m_key);

    m_model = new QLineEdit(m_result.llmModel, aiPage);
    aiForm->addRow(QStringLiteral("模型"), m_model);

    auto* aiHint = new QLabel(
        QStringLiteral("接口地址必须指向 API 端点（以 /chat/completions 结尾），\n"
                       "不是浏览器里管理 Key 的控制台页面。"),
        aiPage);
    aiHint->setStyleSheet(QStringLiteral("color:#666;"));
    aiHint->setWordWrap(true);
    aiForm->addRow(aiHint);

    tabs->addTab(aiPage, QStringLiteral("AI"));

    // ================================================================ 网络
    auto* netPage = new QWidget(tabs);
    auto* netForm = new QFormLayout(netPage);

    m_host = new QLineEdit(m_result.hostAddress, netPage);
    netForm->addRow(QStringLiteral("主机地址"), m_host);

    m_port = new QSpinBox(netPage);
    m_port->setRange(1024, 65535);
    m_port->setValue(m_result.port);
    netForm->addRow(QStringLiteral("端口"), m_port);

    auto* netHint = new QLabel(
        QStringLiteral("联机时：主机记下自己的内网 IP 并开房，\n"
                       "客户端填同一个 IP 和端口加入。\n"
                       "若连不上，请检查 Windows 防火墙是否放行该 UDP 端口。"),
        netPage);
    netHint->setStyleSheet(QStringLiteral("color:#666;"));
    netHint->setWordWrap(true);
    netForm->addRow(netHint);

    tabs->addTab(netPage, QStringLiteral("网络"));

    // ================================================================ 音乐
    auto* musicPage = new QWidget(tabs);
    auto* musicForm = new QFormLayout(musicPage);

    m_musicCheck = new QCheckBox(QStringLiteral("启用背景音乐"), musicPage);
    m_musicCheck->setChecked(m_result.musicEnabled);
    musicForm->addRow(QString(), m_musicCheck);

    m_volumeSlider = new QSlider(Qt::Horizontal, musicPage);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(m_result.musicVolume);
    m_volumeSlider->setTickPosition(QSlider::TicksBelow);
    m_volumeSlider->setTickInterval(10);
    musicForm->addRow(QStringLiteral("音量"), m_volumeSlider);

    auto* musicHint = new QLabel(
        QStringLiteral("音乐由程序实时合成（8-bit 波形），不包含任何音频文件。\n"
                       "\n"
                       "· 主菜单：轻快的五声音阶循环（方波旋律 + 三角波低音）\n"
                       "· 对局中：更紧凑的 A 小调循环（含噪声打点）\n"
                       "\n"
                       "所有波形都由代码生成：方波/三角波/噪声 + ADSR 包络，\n"
                       "以 16-bit PCM 推给 QAudioSink 播放。"),
        musicPage);
    musicHint->setStyleSheet(QStringLiteral("color:#666;"));
    musicHint->setWordWrap(true);
    musicForm->addRow(musicHint);

    tabs->addTab(musicPage, QStringLiteral("音乐"));

    root->addWidget(tabs);

    // ================================================================ 按钮
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                         | QDialogButtonBox::RestoreDefaults,
                                     this);
    root->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, [this] {
        collect();
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(box->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            this, [this] {
                Settings def;
                m_sizeCombo->setCurrentIndex(1);
                m_mineSpin->setValue(def.mineRatio);
                m_livesSpin->setValue(def.startLives);
                m_turnSpin->setValue(def.turnSeconds);
                m_interfereCheck->setChecked(def.allowInterfere);
                m_aiCombo->setCurrentIndex(def.aiDifficulty);
                m_endpoint->setText(def.llmEndpoint);
                m_key->clear();
                m_model->setText(def.llmModel);
                m_host->setText(def.hostAddress);
                m_port->setValue(def.port);
                m_coordCheck->setChecked(def.showCoordinates);
                m_musicCheck->setChecked(def.musicEnabled);
                m_volumeSlider->setValue(def.musicVolume);
            });

    // 坐标显示（放在对局页底部）——为简单起见这里直接加个隐藏复选框
    m_coordCheck = new QCheckBox(QStringLiteral("在格子上显示坐标（调试）"), gamePage);
    m_coordCheck->setChecked(m_result.showCoordinates);
    gameForm->addRow(QString(), m_coordCheck);
}

void SettingsDialog::collect()
{
    m_result.boardSize      = m_sizeCombo->currentData().toSize();
    m_result.mineRatio      = m_mineSpin->value();
    m_result.startLives     = m_livesSpin->value();
    m_result.turnSeconds    = m_turnSpin->value();
    m_result.allowInterfere = m_interfereCheck->isChecked();
    m_result.aiDifficulty   = m_aiCombo->currentIndex();
    m_result.llmEndpoint    = m_endpoint->text().trimmed();
    m_result.llmKey         = m_key->text().trimmed();
    m_result.llmModel       = m_model->text().trimmed();
    m_result.hostAddress    = m_host->text().trimmed();
    m_result.port           = m_port->value();
    if (m_coordCheck)
        m_result.showCoordinates = m_coordCheck->isChecked();
    if (m_musicCheck)
        m_result.musicEnabled = m_musicCheck->isChecked();
    if (m_volumeSlider)
        m_result.musicVolume = m_volumeSlider->value();
}

} // namespace ms
