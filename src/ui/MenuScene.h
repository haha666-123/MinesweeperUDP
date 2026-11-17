#pragma once
// ============================================================================
//  MenuScene：标题菜单
//  ---------------------------------------------------------------------------
//  对应你原项目的 MenuScence + SceneManager 思路：
//  用一个「场景栈」切换 标题 -> 游戏 -> 返回标题。
//
//  Qt 里最简洁的做法是 QStackedWidget：把菜单和游戏页都放进去，
//  切换时只需 setCurrentIndex。比手动 new/delete 场景对象安全得多。
// ============================================================================
#include <QWidget>

QT_BEGIN_NAMESPACE
class QPushButton;
class QLabel;
QT_END_NAMESPACE

namespace ms {

class MenuScene : public QWidget {
    Q_OBJECT
public:
    explicit MenuScene(QWidget* parent = nullptr);

signals:
    void startLocal();
    void startAI();
    void startHost();
    void startClient();
    void openSettings();
    void quitApp();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    void buildUi();

    QPushButton* m_btnLocal    = nullptr;
    QPushButton* m_btnAI       = nullptr;
    QPushButton* m_btnHost     = nullptr;
    QPushButton* m_btnClient   = nullptr;
    QPushButton* m_btnSettings = nullptr;
    QPushButton* m_btnQuit     = nullptr;
    QLabel*      m_version     = nullptr;
};

} // namespace ms
