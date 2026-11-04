#pragma once

#include <vector>
#include "Button.h"

enum class ButtonLayout {
	Vertical,
	Horizontal,
	Grid
};

struct ButtonPanelConfig {
	ButtonLayout layout = ButtonLayout::Vertical;

	int x = 0;
	int y = 0;

	int spacing = 0;
	int padding = 0;

	int columns = 1;
	int columnSpacing = 0;
	int rowSpacing = 0;

	bool centerButtons = false;
};

class ButtonPanel {
public:
	explicit ButtonPanel(ButtonPanelConfig config = {});

	Button& addButton(Button button);
	void clear();

	void setConfig(const ButtonPanelConfig& config);
	const ButtonPanelConfig& getConfig() const;

	void setup();

	Button* getButton(size_t index);
	Button* findByAction(int action);
	Button* hitTest(float x, float y);

	void clearHover();
	void updateHover(float x, float y);
	void draw(SDL_Renderer* renderer, SDL_Texture* texture) const;

	int getWidth() const;
	int getHeight() const;

private:
	std::vector<Button> buttons;
	ButtonPanelConfig config;

	int width = 0;
	int height = 0;
};