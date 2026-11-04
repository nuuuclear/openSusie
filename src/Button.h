#pragma once

#include <string>
#include <SDL3/SDL.h>

struct ButtonSprite {
	int x = 0;
	int y = 0;
	int width = 32;
	int height = 24;
	int hoverOffsetX = 32;
};

class Button {
public:
	Button(std::string tooltip, int action, ButtonSprite sprite);

	const std::string& getTooltip() const;
	int getAction() const;

	int getX() const;
	int getY() const;
	int getWidth() const;
	int getHeight() const;

	bool isHovered() const;
	void setHovered(bool hovered);
	void setPosition(int x, int y);
	bool contains(float x, float y) const;

	void draw(SDL_Renderer* renderer, SDL_Texture* texture) const;

private:
	std::string tooltip;
	int action;
	ButtonSprite sprite;
	int x = 0;
	int y = 0;
	bool hovered = false;
};