#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "core/FileSystem.h"
#include "core/Subroutine.h"
#include "ui/UI.h"

struct AppConfig {
    std::string title = "title";

    int windowWidth = 256;
    int windowHeight = 240;
};

class App {
public:
    App() = default;
    ~App();

    bool init(const AppConfig& config);
    void run(); // starts it running
    void quit();

    void step();
    void draw();

    FileSystem& getFileSystem();
    UI& ui();

    void addSubroutine(std::unique_ptr<Subroutine> subroutine);
private:
    FileSystem filesys;
    UI userInterface;

    std::uint64_t lastCounter;
    double deltaTime;

    bool running = false;

    static bool SDLCALL eventWatch(void* userdata, SDL_Event* event);

    std::vector<std::unique_ptr<Subroutine>> subroutines;
};