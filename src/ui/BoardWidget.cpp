#include "BoardWidget.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace ms {

namespace {

// 配色（深色主题，与 ModbusMonitor 保持一致的视觉语言）
const QColor kBgColor{ 0x1A, 0x1D, 0x23 };
const QColor kGridLine{ 0x2C, 0x31, 0x3A };
const QColor kClosedTop{ 0x3A, 0x42, 0x50 };
const QColor kClosedBottom{ 0x2A, 0x30, 0x3B };
const QColor kClosedHover{ 0x4A, 0x54, 0x66 };
const QColor kOpenCell{ 0x22, 0x26, 0x2E };
const QColor kOpenBorder{ 0x2A, 0x2F, 0x38 };
const QColor kFlagP1{ 0x5F, 0xA8, 0xFF };   // 玩家 1 的旗：蓝
const QColor kFlagP2{ 0xFF, 0x9F, 0x5A };   // 玩家 2 的旗：橙
const QColor kMineColor{ 0xFF, 0x5C, 0x5C };
const QColor kExploded{ 0xD0, 0x3A, 0x3A };
const QColor kHighlight{ 0xFF, 0xD5, 0x4F };

} // namespace

BoardWidget::BoardWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAutoFillBackground(false);
    setMinimumSize(320, 320);
    setFocusPolicy(Qt::StrongFocus);
}

void BoardWidget::setGame(const GameState* game)
{
    m_game = game;
    clearHighlight();
    update();
}

void BoardWidget::setInteractive(bool on)
{
    if (m_interactive == on)
        return;
    m_interactive = on;
    update();
}

void BoardWidget::setHighlight(Pos p, bool on)
{
    m_hasHighlight = on;
    m_highlight = on ? p : Pos{ -1, -1 };
    update();
}

void BoardWidget::clearHighlight()
{
    m_hasHighlight = false;
    m_highlight = Pos{ -1, -1 };
    update();
}

QSize BoardWidget::sizeHint() const
{
    if (!m_game)
        return { 480, 480 };
    const int n = std::max(m_game->board().width(), m_game->board().height());
    const int s = std::min(720, std::max(360, n * 42));
    return { s, s };
}

QSize BoardWidget::minimumSizeHint() const
{
    return { 240, 240 };
}

int BoardWidget::cellSize() const
{
    if (!m_game)
        return 32;
    const int w = m_game->board().width();
    const int h = m_game->board().height();
    if (w <= 0 || h <= 0)
        return 32;
    const int margin = 12;
    return std::max(8, std::min((width() - margin * 2) / w,
                                (height() - margin * 2) / h));
}

QPoint BoardWidget::gridOrigin() const
{
    if (!m_game)
        return { 12, 12 };
    const int cs = cellSize();
    const int bw = m_game->board().width() * cs;
    const int bh = m_game->board().height() * cs;
    return { (width() - bw) / 2, (height() - bh) / 2 };
}

QRect BoardWidget::cellRect(Pos p) const
{
    const int cs = cellSize();
    const QPoint o = gridOrigin();
    return QRect(o.x() + p.x * cs, o.y() + p.y * cs, cs, cs);
}

bool BoardWidget::posToCell(const QPoint& widgetPos, Pos& out) const
{
    if (!m_game)
        return false;
    const int cs = cellSize();
    const QPoint o = gridOrigin();

    const int relX = widgetPos.x() - o.x();
    const int relY = widgetPos.y() - o.y();
    if (relX < 0 || relY < 0)
        return false;

    const int cx = relX / cs;
    const int cy = relY / cs;
    const Pos p{ cx, cy };
    if (!m_game->board().inBounds(p))
        return false;

    out = p;
    return true;
}

// ===========================================================================
//  绘制
// ===========================================================================
void BoardWidget::paintEvent(QPaintEvent*)
{
    QPainter pt(this);
    pt.setRenderHint(QPainter::Antialiasing, true);
    pt.setRenderHint(QPainter::TextAntialiasing, true);

    // 背景
    pt.fillRect(rect(), kBgColor);

    if (!m_game) {
        pt.setPen(QColor(0x70, 0x78, 0x88));
        pt.drawText(rect(), Qt::AlignCenter,
                    QStringLiteral("未开始对局"));
        return;
    }

    const Board& b = m_game->board();
    if (b.width() <= 0 || b.height() <= 0)
        return;

    for (int y = 0; y < b.height(); ++y)
        for (int x = 0; x < b.width(); ++x)
            drawCell(pt, Pos{ x, y }, cellRect(Pos{ x, y }));
}

void BoardWidget::drawCell(QPainter& pt, Pos p, const QRect& r)
{
    const Cell& c = m_game->cellAt(p);
    const int   cs = cellSize();

    const bool hovered = (m_hover == p);
    const bool highlighted = m_hasHighlight && (m_highlight == p);

    // 缩进 1px 形成格线
    const QRect rr = r.adjusted(1, 1, -1, -1);

    if (c.state == CellState::Revealed) {
        // ---- 已翻开
        pt.fillRect(rr, kOpenCell);
        pt.setPen(QPen(kOpenBorder, 1));
        pt.drawRect(rr);

        const int n = m_game->board().adjacentMines(p);
        if (n > 0)
            drawNumber(pt, rr, n);

    } else if (c.state == CellState::Exploded) {
        // ---- 踩中的雷
        pt.fillRect(rr, kExploded);
        pt.setPen(QPen(QColor(0xFF, 0x9A, 0x9A), 1));
        pt.drawRect(rr);
        drawMine(pt, rr, true);

    } else {
        // ---- 未翻开
        QColor top = hovered ? kClosedHover : kClosedTop;
        if (highlighted)
            top = kHighlight;

        // 轻微的立体感：上下渐变
        QLinearGradient grad(rr.topLeft(), rr.bottomLeft());
        grad.setColorAt(0.0, top);
        grad.setColorAt(1.0, kClosedBottom);
        pt.fillRect(rr, grad);

        pt.setPen(QPen(kGridLine, 1));
        pt.drawRect(rr);

        if (c.hasFlag())
            drawFlag(pt, rr, c.flagOwner);
    }

    // 高亮描边
    if (highlighted) {
        pt.setPen(QPen(kHighlight, 2));
        pt.drawRect(rr.adjusted(1, 1, -1, -1));
    }

    // 极小格子不画数字（避免糊成一团）
    (void)cs;
}

void BoardWidget::drawNumber(QPainter& pt, const QRect& r, int n)
{
    n = std::clamp(n, 1, 8);
    const QColor col(kColors[n]);

    QFont f = pt.font();
    f.setPixelSize(std::max(10, static_cast<int>(r.height() * 0.58)));
    f.setBold(true);
    pt.setFont(f);
    pt.setPen(col);
    pt.drawText(r, Qt::AlignCenter, QString::number(n));
}

void BoardWidget::drawMine(QPainter& pt, const QRect& r, bool exploded)
{
    const int cx = r.center().x();
    const int cy = r.center().y();
    const int rad = std::max(3, r.height() / 5);

    pt.setPen(Qt::NoPen);
    pt.setBrush(exploded ? QColor(0xFF, 0xFF, 0xFF) : kMineColor);

    // 圆 + 8 根刺
    pt.drawEllipse(QPoint(cx, cy), rad, rad);
    pt.setPen(QPen(exploded ? QColor(0xFF, 0xFF, 0xFF) : kMineColor, 1.5));
    for (int i = 0; i < 8; ++i) {
        const double a = i * 3.14159265 / 4.0;
        pt.drawLine(QPoint(cx + static_cast<int>(std::cos(a) * rad * 0.6),
                           cy + static_cast<int>(std::sin(a) * rad * 0.6)),
                    QPoint(cx + static_cast<int>(std::cos(a) * rad * 1.7),
                           cy + static_cast<int>(std::sin(a) * rad * 1.7)));
    }
    pt.setBrush(Qt::NoBrush);
}

void BoardWidget::drawFlag(QPainter& pt, const QRect& r, int owner)
{
    const QColor col = (owner == 2) ? kFlagP2 : kFlagP1;

    // 旗杆
    const int poleX = r.center().x();
    const int top    = r.top() + r.height() / 5;
    const int bottom = r.bottom() - r.height() / 5;

    pt.setPen(QPen(QColor(0xD0, 0xD0, 0xD0), std::max(1, r.width() / 16)));
    pt.drawLine(poleX, top, poleX, bottom);

    // 旗面（三角）
    QPainterPath path;
    path.moveTo(poleX, top);
    path.lineTo(poleX + r.width() / 3, top + r.height() / 8);
    path.lineTo(poleX, top + r.height() / 4);
    path.closeSubpath();
    pt.fillPath(path, col);

    // 底座
    pt.setPen(Qt::NoPen);
    pt.setBrush(col.darker(120));
    pt.drawRect(QRect(poleX - r.width() / 6, bottom - 2,
                      r.width() / 3, std::max(2, r.height() / 14)));
    pt.setBrush(Qt::NoBrush);
}

// ===========================================================================
//  交互
// ===========================================================================
void BoardWidget::mousePressEvent(QMouseEvent* e)
{
    Pos p;
    if (!posToCell(e->pos(), p))
        return;

    if (!m_interactive || !m_game || m_game->isOver())
        return;
    if (!m_game->board().inBounds(p))
        return;

    if (e->button() == Qt::LeftButton)
        emit revealRequested(p);
    else if (e->button() == Qt::RightButton)
        emit flagRequested(p);
}

void BoardWidget::mouseMoveEvent(QMouseEvent* e)
{
    Pos p;
    if (posToCell(e->pos(), p)) {
        if (!(m_hover == p)) {
            m_hover = p;
            emit cellHovered(p);
            update();
        }
    } else if (!(m_hover == Pos{ -1, -1 })) {
        m_hover = Pos{ -1, -1 };
        update();
    }
}

void BoardWidget::leaveEvent(QEvent*)
{
    m_hover = Pos{ -1, -1 };
    update();
}

} // namespace ms
