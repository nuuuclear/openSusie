
#include "Button.h"

#include <utility>

Button::Button(std::string tooltip, int action, ButtonSprite sprite)
:   tooltip(std::move(tooltip)),
    action(action),
    sprite(sprite)
{
}

const std::string& Button::getTooltip() const {
	return tooltip;
}

int Button::getAction() const {
	return action;
}

int Button::getX() const {
	return x;
}

int Button::getY() const {
	return y;
}

int Button::getWidth() const {
	return sprite.width;
}

int Button::getHeight() const {
	return sprite.height;
}

bool Button::isHovered() const {
	return hovered;
}

void Button::setHovered(bool hovered) {
	this->hovered = hovered;
}

void Button::setPosition(int x, int y) {
	this->x = x;
	this->y = y;
}

bool Button::contains(float px, float py) const {
	return px >= x
		&& px < x + sprite.width
		&& py >= y
		&& py < y + sprite.height;
}

void Button::draw(SDL_Renderer* renderer, SDL_Texture* texture) const {
	if (!renderer || !texture) {
		return;
	}

	SDL_FRect source{
		static_cast<float>(sprite.x + (hovered ? sprite.hoverOffsetX : 0)),
		static_cast<float>(sprite.y),
		static_cast<float>(sprite.width),
		static_cast<float>(sprite.height)
	};

	SDL_FRect destination{
		static_cast<float>(x),
		static_cast<float>(y),
		static_cast<float>(sprite.width),
		static_cast<float>(sprite.height)
	};

	SDL_RenderTexture(renderer, texture, &source, &destination);
}