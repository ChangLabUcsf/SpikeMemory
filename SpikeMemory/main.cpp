#include "controlwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    ControlWindow w;
    w.show();
//    w.setStyleSheet("background-color:darkGray;");
    return a.exec();
}
