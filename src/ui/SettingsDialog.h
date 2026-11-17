#pragma once
// ============================================================================
//  设置对话框
//  ---------------------------------------------------------------------------
//  分三组：对局规则 / AI / 网络。
//  所有改动先写到内部副本，点「确定」才提交给调用方，
//  点「取消」不产生任何副作用——这是对话框的常规正确做法。
// ============================================================================
#include "Settings.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;
class QSlider;
QT_END_NAMESPACE

namespace ms {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const Settings& current, QWidget* parent = nullptr);

    //! 确定后取回修改结果
    [[nodiscard]] Settings result() const { return m_result; }

private:
    void buildUi();
    void collect();

    Settings m_result;

    // 对局
    QComboBox*      m_sizeCombo = nullptr;
    QDoubleSpinBox* m_mineSpin  = nullptr;
    QSpinBox*       m_livesSpin = nullptr;
    QSpinBox*       m_turnSpin  = nullptr;
    QCheckBox*      m_interfereCheck = nullptr;

    // AI
    QComboBox* m_aiCombo   = nullptr;
    QLineEdit* m_endpoint  = nullptr;
    QLineEdit* m_key       = nullptr;
    QLineEdit* m_model     = nullptr;

    // 网络
    QLineEdit* m_host = nullptr;
    QSpinBox*  m_port = nullptr;

    // 音乐
    QCheckBox* m_musicCheck  = nullptr;
    QSlider*   m_volumeSlider = nullptr;

    // 界面
    QCheckBox* m_coordCheck = nullptr;
};

} // namespace ms
