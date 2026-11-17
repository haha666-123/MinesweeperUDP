#include "ui/MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QLoggingCategory>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    // 关掉 Qt 内部高频日志，避免干扰游戏控制台输出
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.*=false"));

    QApplication app(argc, argv);

    QApplication::setOrganizationName(QStringLiteral("SongGuohui"));
    QApplication::setApplicationName(QStringLiteral("UdpMinesweeper"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    // 固定使用 Fusion 风格，保证跨平台外观一致
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    ms::MainWindow w;
    w.show();

    return app.exec();
}
