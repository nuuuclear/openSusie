#include "PlayerWindow.h"

#include "app/App.h"
#include "NativeMenu.h"
#include "core/Paths.h"
#include "viewer/Viewer.h"
#include "Button.h"

namespace {
struct ButtonDefinition {
	const char* tooltip;
	int action;
    int spriteX;
	int spriteY;
	int width;
	int height;
};

constexpr ButtonDefinition ButtonDefinitions[] = {
	{   "btn",
        0,
        0,  0,
        0,  0
    }
};
} // namespace

PlayerWindow::PlayerWindow(App* app, Viewer* imageViewer, NativeMenu* nativeMenu)
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

PlayerWindow::~PlayerWindow() {
    SDL_DestroyTexture(buttonTexture);
}

void PlayerWindow::event(const SDL_Event& event) {
	const SDL_WindowID PlayerWindowID = SDL_GetWindowID(app->ui().toolbarWindow());

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
    &&  event.window.windowID == PlayerWindowID
    ) {
		buttons.clearHover();
		return;
	}

	if (event.type == SDL_EVENT_MOUSE_MOTION
    &&  event.motion.windowID == PlayerWindowID
    ) {
		buttons.updateHover(event.motion.x, event.motion.y);
		return;
	}

	if (event.type == SDL_EVENT_MOUSE_BUTTON_UP
    &&  event.button.windowID == PlayerWindowID
    &&  event.button.button == SDL_BUTTON_LEFT
    ) {
		Button* button = buttons.hitTest(event.button.x, event.button.y);

		if (button) {
			activateButton(*button);
		}
	}

    if (event.type == SDL_EVENT_DROP_FILE
    &&  event.drop.windowID == PlayerWindowID
    ) {
        if (event.drop.data != NULL) {
            std::string dropped_file(event.drop.data);

            viewer->present(dropped_file);
        }
    }
}

void PlayerWindow::draw() {
	SDL_Renderer* renderer = app->ui().toolbarRenderer();
	if (!renderer) return;

	app->ui().drawToolbarTitlebar();
	if (!buttonTexture) return;

	buttons.draw(renderer, buttonTexture);
}

void PlayerWindow::activateButton(const Button& button) {
	switch (button.getAction()) {
		case 0:
			// viewer->...();
			break;
	}
}
