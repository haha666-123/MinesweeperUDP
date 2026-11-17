#pragma once
// ============================================================================
//  BoardWidget：棋盘绘制与鼠标交互
//  ---------------------------------------------------------------------------
//  为什么用 QWidget + QPainter 自绘，而不是 QTableWidget：
//    * 棋盘是「格子矩阵 + 每格多种状态（未翻开/已翻开/数字/雷/旗）」
//    * 用表格控件要为每格创建 item 对象，几百格就是几百个对象
//    * 自绘只需一次 paintEvent 里画 64~900 个矩形，性能与代码量都更好
//  这正是 Qt 岗位面试会问的「什么时候该自绘，什么时候该用现成控件」。
// ============================================================================
#include "../core/GameState.h"

#include <QWidget>

namespace ms {

class BoardWidget : public QWidget {
    Q_OBJECT
public:
    explicit BoardWidget(QWidget* parent = nullptr);

    //! 绑定对局状态（不持有所有权）
    void setGame(const GameState* game);
    [[nodiscard]] const GameState* game() const { return m_game; }

    //! 只读模式（观看对手回合 / 游戏结束时禁用输入）
    void setInteractive(bool on);
    [[nodiscard]] bool interactive() const { return m_interactive; }

    //! 高亮某个格子（AI 思考提示、鼠标悬停）
    void setHighlight(Pos p, bool on);
    void clearHighlight();

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    //! 玩家左键点了某格（未翻开）
    void revealRequested(ms::Pos pos);
    //! 玩家右键点了某格（插旗/取消）
    void flagRequested(ms::Pos pos);
    //! 鼠标悬停到某格
    void cellHovered(ms::Pos pos);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    [[nodiscard]] int cellSize() const;
    [[nodiscard]] QPoint gridOrigin() const;
    [[nodiscard]] bool posToCell(const QPoint& widgetPos, Pos& out) const;
    [[nodiscard]] QRect cellRect(Pos p) const;

    void drawCell(QPainter& painter, Pos p, const QRect& r);
    void drawClosed(QPainter& painter, const QRect& r, int owner);
    void drawMine(QPainter& painter, const QRect& r, bool exploded);
    void drawFlag(QPainter& painter, const QRect& r, int owner);
    void drawNumber(QPainter& painter, const QRect& r, int n);

    const GameState* m_game = nullptr;
    bool  m_interactive = true;
    Pos   m_highlight{ -1, -1 };
    Pos   m_hover{ -1, -1 };
    bool  m_hasHighlight = false;

    // 数字颜色表（模仿经典扫雷配色，但适配深色主题）
    static constexpr int kColors[9] = {
        0x000000,  // 0 不用
        0x5FA8FF,  // 1 蓝
        0x67D68A,  // 2 绿
        0xFF7B72,  // 3 红
        0xC792EA,  // 4 紫
        0xFFB86C,  // 5 橙
        0x4DD0E1,  // 6 青
        0xE0E0E0,  // 7 浅灰
        0x9E9E9E,  // 8 灰
    };
};

} // namespace ms
