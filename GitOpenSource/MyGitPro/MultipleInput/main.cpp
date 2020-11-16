#include <QCoreApplication>
#include "decodeunit.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    DecodeUnit decodeUnit;
    const char* formatname = "avfoundation";
    const char* devicename = "/Users/xienan/Downloads/test.mov";
    decodeUnit.SetParam(formatname, devicename);
    decodeUnit.run();
    return a.exec();
}


