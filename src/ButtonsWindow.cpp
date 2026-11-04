#include "ButtonsWindow.h"

#include "App.h"
#include "NativeMenu.h"
#include "Paths.h"
#include "Viewer.h"
#include "Button.h"

namespace {
struct ButtonDefinition {
	const char* tooltip;
	int action;
	int spriteY;
	int width;
	int height;
};

constexpr ButtonDefinition ButtonDefinitions[] = {
	{   "Open file",
        0,
        0, 
        32, 24
    },
	{   "Close image",
        1,
        24,
        32, 24
    },
	{   "Rotate left",
        2,
        48,
        32, 24
    },
	{   "Rotate right",
        3,
        72,
        32, 24
    },
	{   "Flip image",
        4,
        96,
        32, 24
    },
	{   "Fit to window",
        5,
        120,
        32, 24
    },
	{   "Image information",
        6,
        144,
        32, 24
    },
	{   "Menu",
        7,
        168,
        32, 16
    }
};
} // namespace

ButtonsWindow::ButtonsWindow(App* app, Viewer* imageViewer, NativeMenu* nativeMenu)
:   Subroutine(app),
    viewer(imageViewer),
    menu(nativeMenu),
    buttons(ButtonPanelConfig{
        .layout = ButtonLayout::Vertical,
        .x = 0,
        .y = ToolbarTitleBarHeight,
        .spacing = 0,
        .padding = 0
    })
{
    const auto path = app->getFileSystem().resolve(Paths::Resources, "buttons.bmp");
    SDL_Surface* surface = SDL_LoadBMP(path.string().c_str());
    if (!surface) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not load button icons: %s",
            SDL_GetError()
        );
        return;
    }

    if (!SDL_SetSurfaceColorKey(surface, true, SDL_MapSurfaceRGB(surface, 0, 0, 0))) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not set button sprite transparency: %s",
            SDL_GetError()
        );
    } else {
        buttonTexture = SDL_CreateTextureFromSurface(app->ui().toolbarRenderer(), surface);
        if (!buttonTexture) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "Could not create button sprite texture: %s",
                SDL_GetError()
            );
        }
    }

    SDL_DestroySurface(surface);

    for (const auto& definition : ButtonDefinitions) {
        buttons.addButton(Button(
            definition.tooltip,
            definition.action,
            ButtonSprite{
                .x = 0,
                .y = definition.spriteY,
                .width = definition.width,
                .height = definition.height,
                .hoverOffsetX = 32
            }
        ));
    }

    buttons.setup();
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
    &&  event.window.windowID == buttonsWindowID
    ) {
		buttons.clearHover();
		return;
	}

	if (event.type == SDL_EVENT_MOUSE_MOTION
    &&  event.motion.windowID == buttonsWindowID
    ) {
		buttons.updateHover(event.motion.x, event.motion.y);
		return;
	}

	if (event.type == SDL_EVENT_MOUSE_BUTTON_UP
    &&  event.button.windowID == buttonsWindowID
    &&  event.button.button == SDL_BUTTON_LEFT
    ) {
		Button* button = buttons.hitTest(event.button.x, event.button.y);

		if (button) {
			activateButton(*button);
		}
	}
}

void ButtonsWindow::draw() {
	SDL_Renderer* renderer = app->ui().toolbarRenderer();
	if (!renderer) return;

	app->ui().drawToolbarTitlebar();
	if (!buttonTexture) return;

	buttons.draw(renderer, buttonTexture);
}

void ButtonsWindow::activateButton(const Button& button) {
	switch (button.getAction()) {
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
			// Flip image
			break;
		case 5:
			viewer->fitToWindow();
			break;
		case 6:
			viewer->showInformation();
			break;
		case 7:
            menu->show(
                app->ui().toolbarWindow(),
                32,
                button.getY() + button.getHeight()
            );
            break;
	}
}