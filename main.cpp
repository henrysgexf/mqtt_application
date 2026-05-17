#include <QApplication>
#include <QFile>
#include <QDebug>
#include "widget.h"
#include <QSurfaceFormat>
int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication a(argc, argv);




    QFile file("style.qss");

    if(file.open(QFile::ReadOnly))
    {
        QString style = file.readAll();
        a.setStyleSheet(style);
        qDebug()<<"QSS loaded";
    }
    else
    {
        qDebug()<<"QSS load failed";
    }

    Widget w;
    w.show();

    return a.exec();
}
