#include "ButtonsWindow.h"

#include "app/App.h"
#include "NativeMenu.h"
#include "core/Paths.h"
#include "viewer/Viewer.h"
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
	{   "Flip image vertically",
        4,
        96,
        32, 24
    },
    {   "Flip image horizontally",
        5,
        120,
        32, 24
    },
	{   "Fit to window",
        6,
        144,
        32, 24
    },
	{   "Image information",
        7,
        168,
        32, 24
    },
	{   "Menu",
        8,
        192,
        32, 16
    }
};
} // namespace


ButtonsWindow::ButtonsWindow(App* app, Viewer* imageViewer, NativeMenu* nativeMenu)
: 	Subroutine(app),
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
	const auto resourcePath = app->getFileSystem().resolve(Paths::Resources, "");

	if (!titlebar.initialize(
		app->ui().toolbarRenderer(),
		WindowTitlebar::Config{
			.height = ToolbarTitleBarHeight,
			.buttonCount = 1,
			.rightGap = 1,
			.showTitle = false
		},
		resourcePath.string())
    ) {
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"Could not initialize toolbar titlebar"
		);
	}

	buttons.setConfig(ButtonPanelConfig{
		.layout = ButtonLayout::Vertical,
		.x = 0,
		.y = titlebar.height(),
		.spacing = 0,
		.padding = 0
	});

	const auto buttonPath = app->getFileSystem().resolve(Paths::Resources, "buttons.bmp");
	SDL_Surface* surface = SDL_LoadBMP(buttonPath.string().c_str());

	if (!surface) {
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"Could not load button icons from %s: %s",
			buttonPath.string().c_str(),
			SDL_GetError()
		);
	} else {
		if (!SDL_SetSurfaceColorKey(
			surface,
			true,
			SDL_MapSurfaceRGB(surface, 0, 0, 0))
        ) {
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"Could not set button sprite transparency: %s",
				SDL_GetError()
			);
		}

		buttonTexture = SDL_CreateTextureFromSurface(
			app->ui().toolbarRenderer(),
			surface
		);

		if (!buttonTexture) {
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"Could not create button sprite texture: %s",
				SDL_GetError()
			);
		} else {
			SDL_SetTextureScaleMode(buttonTexture, SDL_SCALEMODE_NEAREST);
			SDL_SetTextureBlendMode(buttonTexture, SDL_BLENDMODE_BLEND);

			float width = 0.0f;
			float height = 0.0f;
			SDL_GetTextureSize(buttonTexture, &width, &height);

			SDL_Log("Loaded toolbar sprite sheet: %.0fx%.0f", width, height);
		}

		SDL_DestroySurface(surface);
	}

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
    titlebar.shutdown();

    SDL_DestroyTexture(buttonTexture);
}

void ButtonsWindow::event(const SDL_Event& event) {
	const SDL_WindowID buttonsWindowID = SDL_GetWindowID(app->ui().toolbarWindow());

	int titlebarX = 0;
	int titlebarY = 0;

	titlebar.handleEvent(app->ui().toolbarWindow(), event, app->ui());

    switch (titlebar.consumeAction(&titlebarX, &titlebarY)) {
        case WindowTitlebar::Action::Close:
            app->quit();
            return;

        case WindowTitlebar::Action::ShowMenu:
            menu->show(app->ui().toolbarWindow(), mouseX, mouseY);
            return;

        case WindowTitlebar::Action::Minimize:
            app->ui().minimizeWindows();
            return;

        case WindowTitlebar::Action::ToggleMaximize:
        case WindowTitlebar::Action::None:
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
        mouseX = event.motion.x;
        mouseY = event.motion.y;

		buttons.updateHover(event.motion.x, event.motion.y);
		return;
	}

	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
    &&  event.button.windowID == buttonsWindowID
    &&  event.button.button == SDL_BUTTON_LEFT
    ) {
		Button* button = buttons.hitTest(event.button.x, event.button.y);

		if (button) {
			activateButton(*button);
		}
	}

    if (event.type == SDL_EVENT_DROP_FILE
    &&  event.drop.windowID == buttonsWindowID
    ) {
        if (event.drop.data != NULL) {
            std::string dropped_file(event.drop.data);

            viewer->present(dropped_file);
        }
    }
}

void ButtonsWindow::draw() {
	SDL_Renderer* renderer = app->ui().toolbarRenderer();
	if (!renderer) return;

	titlebar.draw(
        app->ui().toolbarWindow(),
        app->ui().toolbarRenderer()
    );

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
			viewer->flipX();
			break;
        case 5:
			viewer->flipY();
			break;
		case 6:
			viewer->fitToWindow();
			break;
		case 7:
			viewer->showInformation();
			break;
		case 8:
            menu->show(
                app->ui().toolbarWindow(),
                32,
                button.getY() + button.getHeight()
            );
            break;
	}
}
