#include "ButtonsWindow.h"

#include "App.h"
#include "NativeMenu.h"
#include "Paths.h"
#include "Viewer.h"

namespace {
constexpr int ButtonY[] = {0, 24, 48, 72, 96, 120, 144, 168};
constexpr int ButtonHeights[] = {24, 24, 24, 24, 24, 24, 24, 16};
constexpr int ButtonOffsetY = ToolbarTitleBarHeight;
}

ButtonsWindow::ButtonsWindow(App* app, Viewer* imageViewer, NativeMenu* nativeMenu)
    : Subroutine(app),
      viewer(imageViewer),
      menu(nativeMenu) {
    const auto path = app->getFileSystem().resolve(Paths::Resources, "buttons.bmp");
    SDL_Surface* surface = SDL_LoadBMP(path.string().c_str());
    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load buttons.bmp: %s", SDL_GetError());
        return;
    }

    if (!SDL_SetSurfaceColorKey(surface, true, SDL_MapSurfaceRGB(surface, 0, 0, 0))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not set button sprite transparency: %s", SDL_GetError());
    } else {
        buttonTexture = SDL_CreateTextureFromSurface(app->ui().toolbarRenderer(), surface);
        if (!buttonTexture) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create button sprite texture: %s", SDL_GetError());
        }
    }

    SDL_DestroySurface(surface);
}

ButtonsWindow::~ButtonsWindow() {
    SDL_DestroyTexture(buttonTexture);
}

void ButtonsWindow::event(const SDL_Event& event) {
    const SDL_WindowID buttonsWindowID = SDL_GetWindowID(app->ui().toolbarWindow());

    int titlebarX = 0;
    int titlebarY = 0;
    switch (app->ui().consumeTitlebarAction(UI::Window::Toolbar, &titlebarX, &titlebarY)) {
        case UI::Action::Close:
            app->quit();
            return;
        case UI::Action::ShowMenu:
            menu->show(app->ui().toolbarWindow(), titlebarX, titlebarY);
            return;
        case UI::Action::Minimize:
        case UI::Action::ToggleMaximize:
        case UI::Action::None:
            break;
    }

    if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE
    &&  event.window.windowID == buttonsWindowID) {
        for (bool& hover : hovering) {
            hover = false;
        }
        return;
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION
    &&  event.motion.windowID == buttonsWindowID) {
        for (size_t i = 0; i < buttonCount; ++i) {
            hovering[i] = event.motion.x >= 0
                && event.motion.x < buttonWidth
                && event.motion.y >= ButtonOffsetY + ButtonY[i]
                && event.motion.y < ButtonOffsetY + ButtonY[i] + ButtonHeights[i];
        }
        return;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP
    &&  event.button.windowID == buttonsWindowID
    &&  event.button.button == SDL_BUTTON_LEFT) {
        const float x = event.button.x;
        const float y = event.button.y;

        for (size_t i = 0; i < buttonCount; ++i) {
            if (x >= 0 && x < buttonWidth
            &&  y >= ButtonOffsetY + ButtonY[i]
            &&  y < ButtonOffsetY + ButtonY[i] + ButtonHeights[i]) {
                activateButton(i);
                return;
            }
        }
    }
}

void ButtonsWindow::draw() {
    SDL_Renderer* renderer = app->ui().toolbarRenderer();
    if (!renderer) return;

    app->ui().drawToolbarTitlebar();
    if (!buttonTexture) return;
    for (size_t i = 0; i < buttonCount; ++i) {
        const SDL_FRect source{
            hovering[i] ? static_cast<float>(buttonWidth) : 0.0f,
            static_cast<float>(ButtonY[i]),
            static_cast<float>(buttonWidth),
            static_cast<float>(ButtonHeights[i])
        };
        const SDL_FRect destination{
            0.0f,
            static_cast<float>(ButtonOffsetY + ButtonY[i]),
            static_cast<float>(buttonWidth),
            static_cast<float>(ButtonHeights[i])
        };
        SDL_RenderTexture(renderer, buttonTexture, &source, &destination);
    }
}

void ButtonsWindow::activateButton(size_t index) {
    switch (index) {
        case 0:
            viewer->openFileDialog();
            break;
        case 1:
            viewer->closeImage();
            break;
        case 2:
            viewer->rotateLeft();
            break;
        case 3:
            viewer->rotateRight();
            break;
        case 4:
            viewer->zoomIn();
            break;
        case 5:
            viewer->zoomOut();
            break;
        case 6:
            viewer->showInformation();
            break;
        case 7:
            menu->show(
                app->ui().toolbarWindow(),
                buttonWidth,
                ButtonOffsetY + ButtonY[7]
            );
            break;
    }
}
