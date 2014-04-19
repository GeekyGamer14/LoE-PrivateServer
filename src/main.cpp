#include <QtWidgets/QApplication>
#include "widget.h"

int argc=0;

QApplication a(argc,(char**)0);
Widget win;

int main(int, char**)
{
    // Windows DLL hell fix
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());
    a.addLibraryPath("platforms");

    win.show();
    win.startServer();

    return a.exec(); // win's dtor will quick_exit (we won't run the atexits)
}
