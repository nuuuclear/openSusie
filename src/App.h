#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "FileSystem.h"
#include "Subroutine.h"
#include "UI.h"

struct appConfig {
    std::string title = "title";

    int windowWidth = 256;
    int windowHeight = 240;

};

class App {
public:
    App();
    ~App();

    bool init(appConfig conf);
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
