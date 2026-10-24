#include "app/MainWindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SeeingForge"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.2"));
    QApplication::setOrganizationName(QStringLiteral("SeeingForge"));

    MainWindow window;
    window.show();
    return application.exec();
}
