#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include "app/App.h"
#include "ui/ButtonsWindow.h"
#include "ui/NativeMenu.h"
#include "viewer/Viewer.h"
#include "Core/Paths.h"

#include <memory>

int main(int argc, const char** argv) {
    AppConfig config;
    config.title = "openSusie";

    App app;

    if (!app.init(config)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "App failed to initialize: %s", SDL_GetError());

        return 1;
    }

    auto viewer = std::make_unique<Viewer>(&app);

    auto pluginsPath = app.getFileSystem().resolve(Paths::Plugins);
    viewer->loadPlugins(pluginsPath.string());

    auto menu = std::make_unique<NativeMenu>(&app, viewer.get());
    viewer->setNativeMenu(menu.get());
    auto buttons = std::make_unique<ButtonsWindow>(&app, viewer.get(), menu.get());

    if (argc > 1) viewer->present(argv[1]);

    app.addSubroutine(std::move(viewer));
    app.addSubroutine(std::move(buttons));

    app.run();
    
    return 0;
}
