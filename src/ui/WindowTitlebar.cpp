#include "WindowTitlebar.h"

#include "UI.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr int DragButton = -2;
constexpr float DragThreshold = 3.0f;

// orignal colours in susie
// constexpr SDL_Color TitlebarColor           {153, 180, 209, 255};
// constexpr SDL_Color TitlebarInactiveColor   {191, 205, 219, 255};

constexpr SDL_Color TitlebarColor           {48,  50,  54,  255};
constexpr SDL_Color TitlebarInactiveColor   {75,  80,  85,  255};
constexpr SDL_Color TextColor               {240, 240, 240, 255};

std::string joinPath(const std::string& base, const char* filename) {
    if (base.empty())
        return filename;

    const char last = base.back();

    if (last == '/' || last == '\\')
        return base + filename;

    return base + "/" + filename;
}

bool isInside(float x, float y, const SDL_FRect& rect) {
    return x >= rect.x &&
        y >= rect.y &&
        x < rect.x + rect.w &&
        y < rect.y + rect.h;
}

bool IsWindowFocused(SDL_Window* window) {
    Uint32 flags = SDL_GetWindowFlags(window);
    return (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
}

} // namespace

WindowTitlebar::~WindowTitlebar() {
	shutdown();
}

bool WindowTitlebar::initialize(
	SDL_Renderer* renderer,
	const Config& newConfig,
	const std::string& resourcePath
) {
	shutdown();

	if (!renderer)
		return false;

	config = newConfig;
	config.height = std::max(1, config.height);
	config.buttonCount = std::clamp(config.buttonCount, 0, 3);

	if (config.buttonCount >= 1) {
		closeTexture = loadButtonTexture(
			renderer,
			joinPath(resourcePath, "close.bmp")
        );
	}

	if (config.buttonCount >= 2) {
		maximizeTexture = loadButtonTexture(
			renderer,
			joinPath(resourcePath, "full.bmp")
        );
	}

	if (config.buttonCount >= 3) {
		minimizeTexture = loadButtonTexture(
			renderer,
			joinPath(resourcePath, "minimize.bmp")
        );
	}

	const bool texturesLoaded
    =   (config.buttonCount < 1 || closeTexture != nullptr)
    &&  (config.buttonCount < 2 || maximizeTexture != nullptr)
    &&  (config.buttonCount < 3 || minimizeTexture != nullptr);

	if (!texturesLoaded) {
		shutdown();
		return false;
	}

	resetPress();
	pendingAction = Action::None;

	return true;
}

void WindowTitlebar::shutdown() {
	if (closeTexture) {
		SDL_DestroyTexture(closeTexture);
		closeTexture = nullptr;
	}

	if (minimizeTexture) {
		SDL_DestroyTexture(minimizeTexture);
		minimizeTexture = nullptr;
	}

	if (maximizeTexture) {
		SDL_DestroyTexture(maximizeTexture);
		maximizeTexture = nullptr;
	}

	resetPress();
	pendingAction = Action::None;
	actionX = 0;
	actionY = 0;
}

SDL_Texture* WindowTitlebar::loadButtonTexture(SDL_Renderer* renderer, const std::string& path) {
	if (!renderer || path.empty())
		return nullptr;

	SDL_Surface* surface = SDL_LoadBMP(path.c_str());

	if (!surface) {
		SDL_Log("WindowTitlebar: Failed to load %s: %s",
			path.c_str(),
			SDL_GetError());

		return nullptr;
	}

	const Uint32 transparentColor = SDL_MapSurfaceRGB(surface, 0, 0, 0);

	if (!SDL_SetSurfaceColorKey(surface, true, transparentColor)) {
		SDL_Log("WindowTitlebar: Failed to set color key for %s: %s",
			path.c_str(),
			SDL_GetError());

		SDL_DestroySurface(surface);
		return nullptr;
	}

	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

	if (!texture) {
		SDL_Log("WindowTitlebar: Failed to create texture for %s: %s",
			path.c_str(),
			SDL_GetError()
        );
	} else {
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    }

	SDL_DestroySurface(surface);

	return texture;
}


int WindowTitlebar::buttonAt(SDL_Window* window, float x, float y) const {
	if (!window || config.buttonCount <= 0)
		return -1;

	int windowWidth = 0;
	SDL_GetWindowSize(window, &windowWidth, nullptr);

	SDL_Texture* textures[] = {
		closeTexture,
		maximizeTexture,
		minimizeTexture
	};

	float right = static_cast<float>(windowWidth);

	for (int i = 0; i < config.buttonCount; ++i) {
		SDL_Texture* texture = textures[i];

		if (!texture)
			continue;

		float width = 0.0f;
		float height = 0.0f;

		SDL_GetTextureSize(texture, &width, &height);

		const float buttonY = (config.height - height) * 0.5f;
		const float buttonX = right - width;

		if (x >= buttonX && x < right
        &&  y >= buttonY && y < buttonY + height
        ) {
			return i;
		}

		right -= width;
	}

	return -1;
}

void WindowTitlebar::handleEvent(SDL_Window* window, const SDL_Event& event, UI& ui) {
	if (!window) return;

	const Uint32 windowID = SDL_GetWindowID(window);

	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (event.button.windowID != windowID)
			return;

		if (event.button.button == SDL_BUTTON_LEFT) {
			
            if (event.button.y < 0.0f
            ||  event.button.y >= static_cast<float>(config.height)
            ) {
                return;
            }

            resetPress();

            pressedMouseButton = event.button.button;
            pressedButton = buttonAt(window, event.button.x, event.button.y);

            if (pressedButton < 0) {
                pressedButton = DragButton;

                float globalX = 0.0f;
                float globalY = 0.0f;

                SDL_GetGlobalMouseState(&globalX, &globalY);

                dragStartGlobalX = globalX;
                dragStartGlobalY = globalY;
            }

            return;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            pendingAction = Action::ShowMenu;
            return;
        } else {
            return;
        }
	}

	if (event.type == SDL_EVENT_MOUSE_MOTION) {
		if (event.motion.windowID != windowID)
			return;

		if (pressedMouseButton != SDL_BUTTON_LEFT)
			return;

		if (pressedButton != DragButton)
			return;

		float globalX = 0.0f;
		float globalY = 0.0f;

		SDL_GetGlobalMouseState(&globalX, &globalY);

		const float deltaX = globalX - dragStartGlobalX;
		const float deltaY = globalY - dragStartGlobalY;

		if (!pressDragged) {
			if (deltaX * deltaX + deltaY * deltaY < DragThreshold * DragThreshold) {
				return;
			}

			pressDragged = true;

			const bool viewerIsMaximized
            =   window == ui.viewerWindow()
            &&  (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;

			if (viewerIsMaximized) {
				ui.restoreViewerForTitlebarDrag(
					globalX,
					globalY,
					event.motion.x,
					event.motion.y
				);

				dragStartGlobalX = globalX;
				dragStartGlobalY = globalY;
				return;
			}
		}

		int windowX = 0;
		int windowY = 0;
		SDL_GetWindowPosition(window, &windowX, &windowY);

		const int targetX = windowX + static_cast<int>(std::lround(deltaX));
		const int targetY = windowY + static_cast<int>(std::lround(deltaY));
		ui.moveWindowFromTitlebar(window, targetX, targetY);

		dragStartGlobalX = globalX;
		dragStartGlobalY = globalY;

		return;
	}

	if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
		if (event.button.windowID != windowID)
			return;

		if (event.button.button != pressedMouseButton)
			return;

		if (pressedButton >= 0 && !pressDragged) {
			const int releasedButton = buttonAt(
                window,
                event.button.x,
                event.button.y
            );

			if (releasedButton == pressedButton) {
				switch (pressedButton) {
					case 0:
						pendingAction = Action::Close;
						break;

					case 1:
						pendingAction = Action::ToggleMaximize;
						break;

					case 2:
						pendingAction = Action::Minimize;
						break;

					default:
						break;
				}

				float globalX = 0.0f;
				float globalY = 0.0f;

				SDL_GetGlobalMouseState(&globalX, &globalY);

				actionX = static_cast<int>(globalX);
				actionY = static_cast<int>(globalY);
			}
		}

		resetPress();
	}
}

void WindowTitlebar::draw(SDL_Window* window, SDL_Renderer* renderer, const std::string& title) {
	if (!window || !renderer)
		return;

	int windowWidth = 0;
	SDL_GetWindowSize(window, &windowWidth, nullptr);

	SDL_FRect titlebarRect{
		0.0f,
		0.0f,
		static_cast<float>(windowWidth),
		static_cast<float>(config.height)
	};

    if (IsWindowFocused(window)) {
        SDL_SetRenderDrawColor(
            renderer,
            TitlebarColor.r,
            TitlebarColor.g,
            TitlebarColor.b,
            TitlebarColor.a
        );
    } else {
        SDL_SetRenderDrawColor(
            renderer,
            TitlebarInactiveColor.r,
            TitlebarInactiveColor.g,
            TitlebarInactiveColor.b,
            TitlebarInactiveColor.a
        );
    }

	SDL_RenderFillRect(renderer, &titlebarRect);

    SDL_Texture* textures[] = {
        closeTexture,
        maximizeTexture,
        minimizeTexture
    };

    float buttonRight = static_cast<float>(windowWidth);

    for (int i = 0; i < config.buttonCount; ++i) {
        SDL_Texture* texture = textures[i];

        if (!texture)
            continue;

        float width = 0.0f;
        float height = 0.0f;

        SDL_GetTextureSize(texture, &width, &height);

        SDL_FRect destination{
            buttonRight - width,
            (config.height - height) * 0.5f,
            width,
            height
        };

        SDL_RenderTexture(renderer, texture, nullptr, &destination);

        buttonRight -= width;
    }

	if (config.showTitle && !title.empty()) {
		SDL_SetRenderDrawColor(
			renderer,
			TextColor.r,
			TextColor.g,
			TextColor.b,
			TextColor.a
        );

		const int titleX = 6;
		const int titleY = std::max(0, (config.height - 8) / 2);

		SDL_RenderDebugText(
			renderer,
			static_cast<float>(titleX),
			static_cast<float>(titleY),
			title.c_str()
        );
	}
}

WindowTitlebar::Action WindowTitlebar::consumeAction(int* x, int* y) {
	const Action result = pendingAction;

	if (x)
		*x = actionX;

	if (y)
		*y = actionY;

	pendingAction = Action::None;

	return result;
}

void WindowTitlebar::resetPress() {
	pressedButton = -1;
	pressedMouseButton = 0;
	pressDragged = false;
	dragStartGlobalX = 0.0f;
	dragStartGlobalY = 0.0f;
}