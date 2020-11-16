#include "sdlprocess.h"
#include  <QDebug>


SDLprocess::SDLprocess(QObject *parent) : QObject(parent)
{

}

bool SDLprocess::init()
{
    if (SDL_Init(SDL_INIT_EVERYTHING) == -1) {
        qDebug() << "------xn------ init Failed";
    }
    m_SDLWindow = SDL_CreateWindow("SDLWindow", 100, 100, 640, 480,SDL_WINDOW_SHOWN);

}

void SDLprocess::process()
{


}
