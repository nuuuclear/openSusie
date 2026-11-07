#include "ButtonPanel.h"

#include <algorithm>
#include <utility>

ButtonPanel::ButtonPanel(ButtonPanelConfig config)
: 	config(config)
{
}

Button& ButtonPanel::addButton(Button button) {
	buttons.push_back(std::move(button));
	setup();
	return buttons.back();
}

void ButtonPanel::clear() {
	buttons.clear();
	setup();
}

void ButtonPanel::setConfig(const ButtonPanelConfig& config) {
	this->config = config;
	setup();
}

const ButtonPanelConfig& ButtonPanel::getConfig() const {
	return config;
}

void ButtonPanel::setup() {
	width = config.padding * 2;
	height = config.padding * 2;

	if (buttons.empty()) {
		return;
	}

	if (config.layout == ButtonLayout::Vertical) {
		int y = config.y + config.padding;
		int maxWidth = 0;

		for (auto& button : buttons) {
			button.setPosition(config.x + config.padding, y);

			if (config.centerButtons) {
				maxWidth = std::max(maxWidth, button.getWidth());
			}

			y += button.getHeight() + config.spacing;
		}

		if (config.spacing > 0) {
			y -= config.spacing;
		}

		width += maxWidth;
		height = y - config.y + config.padding;

		if (!config.centerButtons) {
			width = config.padding * 2;
			for (const auto& button : buttons) {
				width = std::max(width, button.getWidth() + config.padding * 2);
			}
		} else {
			for (auto& button : buttons) {
				const int x = config.x + (width - button.getWidth()) / 2;
				button.setPosition(x, button.getY());
			}
		}

		return;
	}

	if (config.layout == ButtonLayout::Horizontal) {
		int x = config.x + config.padding;
		int maxHeight = 0;

		for (auto& button : buttons) {
			button.setPosition(x, config.y + config.padding);
			x += button.getWidth() + config.spacing;
			maxHeight = std::max(maxHeight, button.getHeight());
		}

		if (config.spacing > 0) {
			x -= config.spacing;
		}

		width = x - config.x + config.padding;
		height = maxHeight + config.padding * 2;
		return;
	}

	if (config.layout == ButtonLayout::Grid) {
		const int columns = std::max(1, config.columns);
		std::vector<int> columnWidths(columns, 0);
		std::vector<int> rowHeights((buttons.size() + columns - 1) / columns, 0);

		for (size_t i = 0; i < buttons.size(); ++i) {
			const int column = static_cast<int>(i) % columns;
			const int row = static_cast<int>(i) / columns;

			columnWidths[column] = std::max(columnWidths[column], buttons[i].getWidth());
			rowHeights[row] = std::max(rowHeights[row], buttons[i].getHeight());
		}

		for (size_t i = 0; i < buttons.size(); ++i) {
			const int column = static_cast<int>(i) % columns;
			const int row = static_cast<int>(i) / columns;

			int x = config.x + config.padding;
			int y = config.y + config.padding;

			for (int c = 0; c < column; ++c) {
				x += columnWidths[c] + config.columnSpacing;
			}

			for (int r = 0; r < row; ++r) {
				y += rowHeights[r] + config.rowSpacing;
			}

			buttons[i].setPosition(x, y);
		}

		width = config.padding * 2;
		for (int columnWidth : columnWidths) {
			width += columnWidth;
		}

		if (columns > 1) {
			width += (columns - 1) * config.columnSpacing;
		}

		height = config.padding * 2;
		for (int rowHeight : rowHeights) {
			height += rowHeight;
		}

		if (rowHeights.size() > 1) {
			height += static_cast<int>(rowHeights.size() - 1) * config.rowSpacing;
		}
	}
}

Button* ButtonPanel::getButton(size_t index) {
	if (index >= buttons.size()) {
		return nullptr;
	}

	return &buttons[index];
}

Button* ButtonPanel::findByAction(int action) {
	for (auto& button : buttons) {
		if (button.getAction() == action) {
			return &button;
		}
	}

	return nullptr;
}

Button* ButtonPanel::hitTest(float x, float y) {
	for (auto& button : buttons) {
		if (button.contains(x, y)) {
			return &button;
		}
	}

	return nullptr;
}

void ButtonPanel::clearHover() {
	for (auto& button : buttons) {
		button.setHovered(false);
	}
}

void ButtonPanel::updateHover(float x, float y) {
	for (auto& button : buttons) {
		button.setHovered(button.contains(x, y));
	}
}

void ButtonPanel::draw(SDL_Renderer* renderer, SDL_Texture* texture) const {
	for (const auto& button : buttons) {
		button.draw(renderer, texture);
	}
}

int ButtonPanel::getWidth() const {
	return width;
}

int ButtonPanel::getHeight() const {
	return height;
}