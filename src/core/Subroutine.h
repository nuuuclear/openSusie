#pragma once

#include <SDL3/SDL.h>

class App;

class Subroutine {
public:
    virtual ~Subroutine() = default;

    virtual void event(const SDL_Event&) {}
    virtual void update(double) {}
    virtual void draw() {}
    virtual Sint32 millisecondsUntilUpdate() const { return -1; }

protected:
    App* app = nullptr;

    explicit Subroutine(App* parentApp) : app(parentApp)
    {
    }
};