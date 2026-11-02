#pragma once

#include <SDL3/SDL.h>

#include "Subroutine.h"

class Viewer;
class NativeMenu;

class ButtonsWindow : public Subroutine {
public:
    ButtonsWindow(App* app, Viewer* viewer, NativeMenu* menu);
    ~ButtonsWindow() override;

    void event(const SDL_Event& event) override;
    void draw() override;

private:
    static constexpr int buttonWidth = 32;
    static constexpr int buttonCount = 8;

    void activateButton(size_t index);

    Viewer* viewer;
    NativeMenu* menu;
    SDL_Texture* buttonTexture = nullptr;
    bool hovering[buttonCount]{};
};
