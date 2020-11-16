#ifndef SDLPROCESS_H
#define SDLPROCESS_H

#include <QObject>

extern "C" {

#include "SDL2/SDL.h"

}

class SDLprocess : public QObject
{
    Q_OBJECT

public:
    SDLprocess(QObject* parent = nullptr);
    ~SDLprocess();

private:
    bool init();
    void process();

public:

private:
    SDL_Window* m_SDLWindow = nullptr;
    SDL_Render* m_SDLRender = nullptr;
};

#endif // SDLPROCESS_H
