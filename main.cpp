#include <QApplication>
#include <QFile>
#include <QDebug>
#include "widget.h"
#include <QSurfaceFormat>
int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication a(argc, argv);




    // 获取可执行文件所在目录
    QString appDir = QCoreApplication::applicationDirPath();
    QString qssPath = appDir + "/style.qss";
    
    QFile file(qssPath);
    if (file.open(QFile::ReadOnly)) {
        QTextStream stream(&file);
        app.setStyleSheet(stream.readAll());
        file.close();
    } else {
        qWarning() << "Failed to load QSS from" << qssPath;
    }

    Widget w;
    w.show();

    return a.exec();
}
