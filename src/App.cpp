#include "App.h"

#include <utility>
#include <filesystem>
#include <stdexcept>

#include "Paths.h"

namespace {
constexpr Uint32 SplashDurationMs = 2000;

void showSplash(const std::filesystem::path& path) {
    SDL_Surface* surface = SDL_LoadBMP(path.string().c_str());
    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load splash image: %s", SDL_GetError());
        return;
    }

    SDL_Window* window = SDL_CreateWindow(
        "openSusie",
        surface->w,
        surface->h,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_HIDDEN
    );
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create splash window: %s", SDL_GetError());
        SDL_DestroySurface(surface);
        return;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create splash renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_DestroySurface(surface);
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create splash texture: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return;
    }
    if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not enable splash transparency: %s", SDL_GetError());
    }

    if (!SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not center splash window: %s", SDL_GetError());
    }
    if (!SDL_ShowWindow(window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not show splash window: %s", SDL_GetError());
    }
    if (!SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0)
    ||  !SDL_RenderClear(renderer)
    ||  !SDL_RenderTexture(renderer, texture, nullptr, nullptr)
    ||  !SDL_RenderPresent(renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not render splash image: %s", SDL_GetError());
    }
    SDL_Delay(SplashDurationMs);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}
}

App::App() {
}

App::~App() {
    SDL_RemoveEventWatch(App::eventWatch, this);

    subroutines.clear();
    userInterface.shutdown();
    SDL_Quit();
}

bool App::init(appConfig conf) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Video failed to init: %s", SDL_GetError());
        return false;
    }

    const char* basePath = SDL_GetBasePath();

    if (!basePath) {
        throw std::runtime_error(
            "Could not determine application path"
        );
    }

    std::filesystem::path root(basePath);
    filesys.set(Paths::Resources,
        root / "resources"
    );

    filesys.set(Paths::Plugins,
        root / "plugins"
    );

    showSplash(filesys.resolve(Paths::Resources, "BMTITLE.bmp"));

    if (!userInterface.initialize(
        conf.title,
        conf.windowWidth,
        conf.windowHeight,
        filesys
    )) {
        return false;
    }

    SDL_AddEventWatch(App::eventWatch, this);

    lastCounter = SDL_GetPerformanceCounter();
    deltaTime = 0.0;

    running = true;
    return true;
}

void App::run() {
    draw();
    while (running) {
        step();
    }
}

void App::quit() {
    running = false;
}

void App::addSubroutine(std::unique_ptr<Subroutine> subroutine) {
    if (!subroutine) return;

    subroutines.push_back(std::move(subroutine));
}

void App::step() {
    Sint32 timeoutMs = -1;
    for (const auto& subroutine : subroutines) {
        const Sint32 subroutineTimeout = subroutine->millisecondsUntilUpdate();
        if (subroutineTimeout >= 0
        &&  (timeoutMs < 0 || subroutineTimeout < timeoutMs)
        ) {
            timeoutMs = subroutineTimeout;
        }
    }

    SDL_Event event;
    const bool receivedEvent = SDL_WaitEventTimeout(&event, timeoutMs);
    if (!receivedEvent && timeoutMs < 0) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not wait for an SDL event: %s",
            SDL_GetError()
        );
        running = false;
        return;
    }

    auto dispatchEvent = [this](const SDL_Event& queuedEvent) {
        switch (queuedEvent.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (queuedEvent.key.key == SDLK_ESCAPE) {
                    running = false;
                }
                break;
        }

        userInterface.handleEvent(queuedEvent);

        for (auto& subroutine : subroutines) {
            subroutine->event(queuedEvent);
        }
    };

    if (receivedEvent) {
        dispatchEvent(event);
    }
    while (SDL_PollEvent(&event)) {
        dispatchEvent(event);
    }

    uint64_t currentCounter = SDL_GetPerformanceCounter();

    deltaTime =
        (double)(currentCounter - lastCounter) /
        SDL_GetPerformanceFrequency();

    lastCounter = currentCounter;

    // signal all updates
    for (auto& subroutine : subroutines) {
        subroutine->update(deltaTime);
    }
   
    draw();
}

void App::draw() {
    userInterface.beginFrame();

    // signal all drawing
    for (auto& subroutine : subroutines) {
        subroutine->draw();
    }

    userInterface.present();
}

bool SDLCALL App::eventWatch(void* userdata, SDL_Event* event) {
    App* application = static_cast<App*>(userdata);

    if (event->type == SDL_EVENT_WINDOW_EXPOSED) {
        application->draw();
    }

    return true;
}

FileSystem& App::getFileSystem() {
    return filesys;
}

UI& App::ui() {
    return userInterface;
}