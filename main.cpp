#include <QApplication>
#include "mainwindow.h"
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication::setStyle("windowsvista");
    qDebug() << QStyleFactory::keys();
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
