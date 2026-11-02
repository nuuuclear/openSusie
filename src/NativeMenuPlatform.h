#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <vector>

#include "NativeMenu.h"

bool showPlatformMenu(
    SDL_Window* parent,
    int x,
    int y,
    const std::vector<NativeMenuItem>& items,
    const std::function<void(int)>& activate
);
