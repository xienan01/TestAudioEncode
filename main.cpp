#include <QCoreApplication>
#include "audioencodetest.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    audioEncodeTest encodeTest;
    encodeTest.Test();
    return a.exec();
}
