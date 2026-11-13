#pragma once

#include <SDL3/SDL.h>
#include <textr.h>

#include <string>

class UI;

class WindowTitlebar {
public:
	enum class Action {
		None,
		Close,
		Minimize,
		ToggleMaximize,
		ShowMenu
	};

	struct Config {
		int height = 18;
		int buttonCount = 3;
		bool showTitle = true;
	};

	WindowTitlebar() = default;
	~WindowTitlebar();

	WindowTitlebar(const WindowTitlebar&) = delete;
	WindowTitlebar& operator=(const WindowTitlebar&) = delete;

	bool initialize(SDL_Renderer* renderer, const Config& config, const std::string& resourcePath);

	void shutdown();

	void handleEvent(SDL_Window* window, const SDL_Event& event, UI& ui);

	void draw(SDL_Window* window, SDL_Renderer* renderer, const std::string& title = {});

	Action consumeAction(int* x = nullptr, int* y = nullptr);

	int height() const { return config.height; }

private:
	Config config{};

	SDL_Texture* closeTexture = nullptr;
	SDL_Texture* minimizeTexture = nullptr;
	SDL_Texture* maximizeTexture = nullptr;

	TR_Font* font = nullptr;

	int pressedButton = -1;
	Uint8 pressedMouseButton = 0;

	bool pressDragged = false;

	Action pendingAction = Action::None;
	int actionX = 0;
	int actionY = 0;

	float dragStartGlobalX = 0.0f;
	float dragStartGlobalY = 0.0f;

	int buttonAt(SDL_Window* window, float x, float y) const;

	static SDL_Texture* loadButtonTexture(
		SDL_Renderer* renderer,
		const std::string& path
    );

	void resetPress();
};