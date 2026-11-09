#include <QApplication>
#include <QMainWindow>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("ERNC Studio"));
    application.setOrganizationName(QStringLiteral("ERNC"));

    QMainWindow window;
    window.setObjectName(QStringLiteral("erncMainWindow"));
    window.setWindowTitle(QStringLiteral("ERNC Studio"));
    window.resize(1280, 800);
    window.show();

    if (application.arguments().contains(QStringLiteral("--exit-after-show"))) {
        QTimer::singleShot(0, &application, &QApplication::quit);
    }

    return application.exec();
}
