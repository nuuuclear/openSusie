#pragma once

#include <SDL3/SDL.h>

#include "core/Subroutine.h"
#include "ButtonPanel.h"

class Viewer;
class NativeMenu;
class Button;

class ButtonsWindow : public Subroutine {
public:
    ButtonsWindow(App* app, Viewer* viewer, NativeMenu* menu);
    ~ButtonsWindow() override;

    void event(const SDL_Event& event) override;
    void draw() override;
private:
    Viewer* viewer;
    NativeMenu* menu;
    SDL_Texture* buttonTexture = nullptr;

    ButtonPanel buttons;

	void activateButton(const Button& button);
};