#include "shell/main_window.hpp"
#include "theme/theme.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>

/// @brief 启动 ERNC Studio。主题装上之后显示无设备主窗口。
/// @param argc 参数个数，由进程入口传入。
/// @param argv 参数文本，只在本次调用期间借用。`--exit-after-show` 表示窗口显示后立即退出，供无交互自检使用。
/// @return 主题装上并正常离开事件循环时返回 Qt 的退出码。主题资源或令牌不完整时返回 1，不显示窗口。
int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("ERNC Studio"));
    application.setOrganizationName(QStringLiteral("ERNC"));

    const ernc::presentation::ThemeLoadResult theme = ernc::presentation::applyLightTheme(application);
    if (!theme.applied) {
        QTextStream(stderr) << theme.message << '\n';
        if (!application.arguments().contains(QStringLiteral("--exit-after-show"))) {
            QMessageBox::critical(nullptr, QStringLiteral("ERNC Studio"), theme.message);
        }
        return 1;
    }

    ernc::presentation::MainWindow window;
    window.show();

    if (application.arguments().contains(QStringLiteral("--exit-after-show"))) {
        QTimer::singleShot(0, &application, &QApplication::quit);
    }

    return application.exec();
}
