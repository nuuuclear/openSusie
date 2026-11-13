#pragma once

#include <SDL3/SDL.h>

#include "core/Subroutine.h"
#include "ButtonPanel.h"
#include "WindowTitlebar.h"

class Viewer;
class NativeMenu;
class Button;

class PlayerWindow : public Subroutine {
public:
    PlayerWindow(App* app, Viewer* viewer, NativeMenu* menu);
    ~PlayerWindow() override;

    void event(const SDL_Event& event) override;
    void draw() override;
private:
    Viewer* viewer;
    NativeMenu* menu;
    SDL_Texture* buttonTexture = nullptr;

    ButtonPanel buttons;
    WindowTitlebar titlebar;

	void activateButton(const Button& button);
};