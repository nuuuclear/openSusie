#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>
#include <vector>

class App;
class Viewer;

struct NativeMenuItem {
    std::string label;
    std::string action;
    int command = 0;
    bool enabled = true;
    bool checkable = false;
    bool checked = false;
    bool radio = false;
    bool separator = false;
    std::vector<NativeMenuItem> children;
};

class NativeMenu {
public:
    NativeMenu(App* app, Viewer* viewer);

    void show(SDL_Window* parent, int x, int y);
    std::optional<std::size_t> chooseOption(const std::vector<std::string>& labels);

private:
    void loadItems();
    std::vector<NativeMenuItem> buildItems();
    std::vector<NativeMenuItem> buildDecoderPreferences();
    void activate(int command);

    App* app;
    Viewer* viewer;
    std::vector<NativeMenuItem> menuItems;
    std::vector<std::string> recentFiles;
    std::vector<std::string> commandActions;
};
