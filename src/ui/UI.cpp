#include "UI.h"

#include <algorithm>
#include <cmath>

#include "core/FileSystem.h"
#include "core/Paths.h"

UI::~UI() {
    shutdown();
}

bool UI::initialize(
    const std::string& title,
    int viewerWidth,
    int viewerHeight,
    const FileSystem& files
) {
    if (!windowList.empty()) {
        shutdown();
    }

    SDL_SetHint(SDL_HINT_WINDOWS_ENABLE_MESSAGELOOP, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "1");
    SDL_SetHint("SDL_BORDERLESS_RESIZABLE_STYLE", "1");

    if (!createManagedWindow(
        WindowRole::Viewer,
        title,
        viewerWidth,
        viewerHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS
    )) {
        shutdown();
        return false;
    }

    WindowData* viewerData = findWindow(WindowRole::Viewer);
    if (!viewerData) {
        shutdown();
        return false;
    }

    if (!SDL_SetWindowHitTest(viewerData->window, UI::viewerHitTest, this)) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not enable viewer window resize hit testing: %s",
            SDL_GetError()
        );
    }

    if (!createManagedWindow(
        WindowRole::Toolbar,
        "openSusie toolbar",
        32,
        ToolbarTitleBarHeight + 208,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_UTILITY
    )) {
        shutdown();
        return false;
    }

    const auto iconPath = files.resolve(Paths::Resources, "icon.png");
    SDL_Surface* icon = SDL_LoadPNG(iconPath.string().c_str());

    if (!icon) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not load application icon: %s",
            SDL_GetError()
        );
    } else {
        for (WindowData& data : windowList) {
            if (data.window) {
                SDL_SetWindowIcon(data.window, icon);
            }
        }
        SDL_DestroySurface(icon);
    }

    WindowData* toolbarData = findWindow(WindowRole::Toolbar);
    viewerData = findWindow(WindowRole::Viewer);

    if (!viewerData || !toolbarData) {
        shutdown();
        return false;
    }

    SDL_SetRenderVSync(viewerData->renderer, 1);

    SDL_SetWindowPosition(
        toolbarData->window,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED
    );
    updateWindowData(*toolbarData);

    int toolbarWidth = toolbarData->width;
    viewerX = toolbarData->x + toolbarWidth;
    viewerY = toolbarData->y;

    setWindowPositionIfChanged(viewerData->window, viewerX, viewerY);
    updateWindowData(*viewerData);

    toolbarX = toolbarData->x;
    toolbarY = toolbarData->y;

    toolbarData->participatesInSnapping = true;
    windowPositionsInitialized = true;
    windowsSnapped = true;
    toolbarOnRight = false;

    return true;
}

bool UI::createManagedWindow(
    WindowRole role,
    const std::string& title,
    int width,
    int height,
    SDL_WindowFlags flags
) {
    if (findWindow(role)) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Attempted to create a duplicate managed window"
        );
        return false;
    }

    WindowData data{};
    data.role = role;

    data.window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (!data.window) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not create window '%s': %s",
            title.c_str(),
            SDL_GetError()
        );
        return false;
    }

    data.renderer = SDL_CreateRenderer(data.window, nullptr);
    if (!data.renderer) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not create renderer for '%s': %s",
            title.c_str(),
            SDL_GetError()
        );
        SDL_DestroyWindow(data.window);
        return false;
    }

    data.ownsWindow = true;
    data.ownsRenderer = true;
    data.participatesInSnapping = role == WindowRole::Toolbar;

    updateWindowData(data);
    windowList.push_back(data);
    return true;
}

void UI::shutdown() {
    destroyManagedWindows();

    viewerX = 0;
    viewerY = 0;
    toolbarX = 0;
    toolbarY = 0;
    playerX = 0;
    playerY = 0;

    windowPositionsInitialized = false;
    windowsSnapped = false;
    toolbarOnRight = false;
    toolbarDockedToScreen = false;
}

void UI::destroyManagedWindows() {
    for (auto it = windowList.rbegin(); it != windowList.rend(); ++it) {
        if (it->ownsRenderer && it->renderer) {
            SDL_DestroyRenderer(it->renderer);
            it->renderer = nullptr;
        }

        if (it->ownsWindow && it->window) {
            SDL_DestroyWindow(it->window);
            it->window = nullptr;
        }
    }

    windowList.clear();
}

bool UI::registerWindow(
    WindowRole role,
    SDL_Window* window,
    SDL_Renderer* renderer,
    bool ownsWindow,
    bool ownsRenderer
) {
    if (!window || !renderer) {
        return false;
    }

    if (findWindow(role) || findWindow(window)) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Window is already registered or its role is already in use"
        );
        return false;
    }

    WindowData data{};
    data.role = role;
    data.window = window;
    data.renderer = renderer;
    data.ownsWindow = ownsWindow;
    data.ownsRenderer = ownsRenderer;
    data.participatesInSnapping = false;

    updateWindowData(data);
    windowList.push_back(data);
    return true;
}

void UI::unregisterWindow(SDL_Window* window) {
    if (!window) {
        return;
    }

    auto it = std::find_if(
        windowList.begin(),
        windowList.end(),
        [window](const WindowData& data) {
            return data.window == window;
        }
    );

    if (it == windowList.end()) {
        return;
    }

    if (it->ownsRenderer && it->renderer) {
        SDL_DestroyRenderer(it->renderer);
    }

    if (it->ownsWindow && it->window) {
        SDL_DestroyWindow(it->window);
    }

    windowList.erase(it);
}

UI::WindowData* UI::findWindow(SDL_Window* window) {
    if (!window) {
        return nullptr;
    }

    auto it = std::find_if(
        windowList.begin(),
        windowList.end(),
        [window](const WindowData& data) {
            return data.window == window;
        }
    );

    return it != windowList.end() ? &*it : nullptr;
}

const UI::WindowData* UI::findWindow(SDL_Window* window) const {
    if (!window) {
        return nullptr;
    }

    auto it = std::find_if(
        windowList.begin(),
        windowList.end(),
        [window](const WindowData& data) {
            return data.window == window;
        }
    );

    return it != windowList.end() ? &*it : nullptr;
}

UI::WindowData* UI::findWindow(WindowRole role) {
    auto it = std::find_if(
        windowList.begin(),
        windowList.end(),
        [role](const WindowData& data) {
            return data.role == role;
        }
    );

    return it != windowList.end() ? &*it : nullptr;
}

const UI::WindowData* UI::findWindow(WindowRole role) const {
    auto it = std::find_if(
        windowList.begin(),
        windowList.end(),
        [role](const WindowData& data) {
            return data.role == role;
        }
    );

    return it != windowList.end() ? &*it : nullptr;
}

SDL_Window* UI::window(WindowRole role) const {
    const WindowData* data = findWindow(role);
    return data ? data->window : nullptr;
}

SDL_Renderer* UI::renderer(WindowRole role) const {
    const WindowData* data = findWindow(role);
    return data ? data->renderer : nullptr;
}

SDL_Window* UI::viewerWindow() const {
    return window(WindowRole::Viewer);
}

SDL_Renderer* UI::viewerRenderer() const {
    return renderer(WindowRole::Viewer);
}

SDL_Window* UI::toolbarWindow() const {
    return window(WindowRole::Toolbar);
}

SDL_Renderer* UI::toolbarRenderer() const {
    return renderer(WindowRole::Toolbar);
}

SDL_Window* UI::playerWindow() const {
    return window(WindowRole::Player);
}

SDL_Renderer* UI::playerRenderer() const {
    return renderer(WindowRole::Player);
}

void UI::updateWindowData(WindowData& data) {
    if (!data.window) {
        return;
    }

    if ((SDL_GetWindowFlags(data.window) & SDL_WINDOW_MINIMIZED) != 0) {
        return;
    }

    SDL_GetWindowPosition(data.window, &data.x, &data.y);
    SDL_GetWindowSize(data.window, &data.width, &data.height);

    switch (data.role) {
        case WindowRole::Viewer:
            viewerX = data.x;
            viewerY = data.y;
            break;

        case WindowRole::Toolbar:
            toolbarX = data.x;
            toolbarY = data.y;
            break;

        case WindowRole::Player:
            playerX = data.x;
            playerY = data.y;
            break;

        case WindowRole::Other:
            break;
    }
}

void UI::updateAllWindowData() {
    for (WindowData& data : windowList) {
        updateWindowData(data);
    }
}

void UI::refreshWindowData(SDL_Window* window) {
    WindowData* data = findWindow(window);
    if (data) {
        updateWindowData(*data);
    }
}

void UI::handleEvent(const SDL_Event& event) {
    const WindowData* viewerData = findWindow(WindowRole::Viewer);
    const WindowData* toolbarData = findWindow(WindowRole::Toolbar);

    const SDL_WindowID viewerID = viewerData && viewerData->window
        ? SDL_GetWindowID(viewerData->window)
        : 0;

    const SDL_WindowID toolbarID = toolbarData && toolbarData->window
        ? SDL_GetWindowID(toolbarData->window)
        : 0;

    switch (event.type) {
        case SDL_EVENT_WINDOW_MOVED:
            syncWindowPositions(event.window);
            break;

        case SDL_EVENT_WINDOW_MAXIMIZED:
            if (event.window.windowID == viewerID) {
                handleViewerMaximized();
            }
            break;

        case SDL_EVENT_WINDOW_MINIMIZED:
            if (event.window.windowID == viewerID && toolbarData && toolbarData->window) {
                setToolbarAlwaysOnTop(false);
                SDL_HideWindow(toolbarData->window);
            }
            break;

        case SDL_EVENT_WINDOW_RESTORED:
            if (event.window.windowID == viewerID) {
                handleViewerRestored();
            } else if (event.window.windowID == toolbarID) {
                updateSnappedWindowPosition();
            }
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (event.window.windowID == viewerID) {
                ensureToolbarVisible();
                setToolbarAlwaysOnTop(true);
            } else if (event.window.windowID == toolbarID) {
                ensureToolbarVisible();
                setToolbarAlwaysOnTop(false);
            }
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (event.window.windowID == viewerID) {
                setToolbarAlwaysOnTop(false);
            }
            break;

        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (event.window.windowID == viewerID) {
                updateWindowData(*findWindow(WindowRole::Viewer));
                updateSnappedWindowPosition();
            } else {
                for (WindowData& data : windowList) {
                    if (data.window && SDL_GetWindowID(data.window) == event.window.windowID) {
                        updateWindowData(data);
                        break;
                    }
                }
            }
            break;

        default:
            break;
    }
}

void UI::beginFrame() {
    for (WindowData& data : windowList) {
        if (!data.renderer) {
            continue;
        }

        if (data.role == WindowRole::Toolbar) {
            SDL_SetRenderDrawColor(data.renderer, 38, 40, 44, 255);
        } else {
            SDL_SetRenderDrawColor(data.renderer, 0, 0, 0, 255);
        }

        SDL_RenderClear(data.renderer);
    }
}

void UI::present() {
    for (WindowData& data : windowList) {
        if (data.renderer) {
            SDL_RenderPresent(data.renderer);
        }
    }
}

void UI::setViewerVisible(bool visible) {
    SDL_Window* viewer = viewerWindow();
    SDL_Window* toolbar = toolbarWindow();

    if (!viewer) {
        return;
    }

    if (visible) {
        SDL_ShowWindow(viewer);
        ensureToolbarVisible();
    } else {
        setToolbarAlwaysOnTop(false);
        SDL_HideWindow(viewer);

        if (toolbar) {
            SDL_ShowWindow(toolbar);
        }
    }
}

void UI::minimizeWindows() {
    for (WindowData& data : windowList) {
        if (!data.window) {
            continue;
        }

        if (data.role == WindowRole::Toolbar) {
            SDL_HideWindow(data.window);
        } else {
            SDL_MinimizeWindow(data.window);
        }
    }
}

bool UI::raiseWindow(SDL_Window* window) {
    return SDL_RaiseWindow(window);
}

void UI::toggleViewerMaximize() {
    SDL_Window* viewer = viewerWindow();
    if (!viewer) {
        return;
    }

    if ((SDL_GetWindowFlags(viewer) & SDL_WINDOW_MAXIMIZED) != 0) {
        SDL_RestoreWindow(viewer);
    } else {
        SDL_MaximizeWindow(viewer);
    }
}

void UI::resizeViewer(int width, int height) {
    SDL_Window* viewer = viewerWindow();
    if (!viewer || width <= 0 || height <= 0) {
        return;
    }

    const SDL_DisplayID display = SDL_GetDisplayForWindow(viewer);
    SDL_Rect displayBounds{};

    if (display == 0 || !SDL_GetDisplayBounds(display, &displayBounds)) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not determine viewer display bounds: %s",
            SDL_GetError()
        );
        return;
    }

    const int maxWidth = std::max(1, static_cast<int>(displayBounds.w * 0.8f));
    const int maxHeight = std::max(1, static_cast<int>(displayBounds.h * 0.8f));

    const float scale = std::min({
        1.0f,
        static_cast<float>(maxWidth) / static_cast<float>(width),
        static_cast<float>(maxHeight) / static_cast<float>(height)
    });

    SDL_SetWindowSize(
        viewer,
        std::max(1, static_cast<int>(width * scale)),
        std::max(1, static_cast<int>(height * scale))
    );

    SDL_SyncWindow(viewer);
    updateWindowData(*findWindow(WindowRole::Viewer));
    updateSnappedWindowPosition();
}

void UI::setToolbarOnRight(bool onRight) {
    toolbarOnRight = onRight;

    SDL_Window* viewer = viewerWindow();
    SDL_Window* toolbar = toolbarWindow();

    if (!windowPositionsInitialized || !viewer || !toolbar) {
        return;
    }

    WindowData* viewerData = findWindow(viewer);
    WindowData* toolbarData = findWindow(toolbar);

    updateWindowData(*viewerData);
    updateWindowData(*toolbarData);

    toolbarX = toolbarOnRight
        ? viewerData->x + viewerData->width
        : viewerData->x - toolbarData->width;

    toolbarY = viewerData->y;
    windowsSnapped = true;
    toolbarDockedToScreen = false;

    setWindowPositionIfChanged(toolbarData->window, toolbarX, toolbarY);
    updateWindowData(*toolbarData);
}

void UI::setWindowPosition(SDL_Window* window, int x, int y) {
    if (!window) {
        return;
    }

    setWindowPositionIfChanged(window, x, y);

    WindowData* data = findWindow(window);
    if (data) {
        updateWindowData(*data);
    }
}

namespace {

struct TitlebarDragSession {
    bool active = false;
    SDL_Window* window = nullptr;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    int windowX = 0;
    int windowY = 0;
    Uint64 lastTick = 0;
};

TitlebarDragSession titlebarDrag;

} // namespace

void UI::moveWindowFromTitlebar(SDL_Window* window, int targetX, int targetY) {
    if (!window) {
        return;
    }

    {
        float globalX = 0.0f;
        float globalY = 0.0f;
        const SDL_MouseButtonFlags buttons = SDL_GetGlobalMouseState(&globalX, &globalY);
        const Uint64 now = SDL_GetTicks();

        if ((buttons & SDL_BUTTON_LMASK) == 0) {
            titlebarDrag.active = false;
        } else {
            if (!titlebarDrag.active
            ||  titlebarDrag.window != window
            ||  now - titlebarDrag.lastTick > 250
            ) {
                int currentX = 0;
                int currentY = 0;
                SDL_GetWindowPosition(window, &currentX, &currentY);

                titlebarDrag.active = true;
                titlebarDrag.window = window;
                titlebarDrag.mouseX = globalX;
                titlebarDrag.mouseY = globalY;
                titlebarDrag.windowX = currentX;
                titlebarDrag.windowY = currentY;
            }

            titlebarDrag.lastTick = now;

            targetX = titlebarDrag.windowX + static_cast<int>(std::lround(globalX - titlebarDrag.mouseX));
            targetY = titlebarDrag.windowY + static_cast<int>(std::lround(globalY - titlebarDrag.mouseY));
        }
    }

    WindowData* movingData = findWindow(window);
    if (!movingData) {
        return;
    }

    WindowData* viewerData = findWindow(WindowRole::Viewer);
    WindowData* toolbarData = findWindow(WindowRole::Toolbar);

    if (!viewerData || !toolbarData) {
        setWindowPositionIfChanged(window, targetX, targetY);
        updateWindowData(*movingData);
        return;
    }

    const bool movingViewer = movingData->role == WindowRole::Viewer;
    const bool movingToolbar = movingData->role == WindowRole::Toolbar;

    if (!movingViewer && !movingToolbar) {
        setWindowPositionIfChanged(window, targetX, targetY);
        updateWindowData(*movingData);
        return;
    }

    if (movingToolbar && windowsSnapped) {
        windowsSnapped = false;
        toolbarDockedToScreen = false;
    }

    if (movingViewer && windowsSnapped) {
        snapSnappedWindowsToScreen(targetX, targetY);

        setWindowPositionIfChanged(viewerData->window, targetX, targetY);

        viewerData->x = viewerX = targetX;
        viewerData->y = viewerY = targetY;

        syncSnappedWindowPosition(targetX, targetY);
        return;
    }

    snapWindowToScreen(window, targetX, targetY);
    setWindowPositionIfChanged(window, targetX, targetY);

    WindowData* otherData = movingViewer ? toolbarData : viewerData;

    movingData->x = targetX;
    movingData->y = targetY;
    SDL_GetWindowSize(movingData->window, &movingData->width, &movingData->height);
    updateWindowData(*otherData);

    if (movingViewer) {
        viewerX = targetX;
        viewerY = targetY;
    } else {
        toolbarX = targetX;
        toolbarY = targetY;
    }

    const bool verticallyAligned =
        std::abs(viewerData->y - toolbarData->y) <= windowSnapDistance;

    if (!verticallyAligned) {
        return;
    }

    if (std::abs(toolbarData->x - (viewerData->x + viewerData->width)) <= windowSnapDistance) {
        windowsSnapped = true;
        toolbarDockedToScreen = false;
        toolbarOnRight = true;

        toolbarX = viewerData->x + viewerData->width;
        toolbarY = viewerData->y;

        setWindowPositionIfChanged(toolbarData->window, toolbarX, toolbarY);
        toolbarData->x = toolbarX;
        toolbarData->y = toolbarY;
        return;
    }

    if (std::abs(viewerData->x - (toolbarData->x + toolbarData->width)) <= windowSnapDistance) {
        windowsSnapped = true;
        toolbarDockedToScreen = false;
        toolbarOnRight = false;

        toolbarX = viewerData->x - toolbarData->width;
        toolbarY = viewerData->y;

        setWindowPositionIfChanged(toolbarData->window, toolbarX, toolbarY);
        toolbarData->x = toolbarX;
        toolbarData->y = toolbarY;
    }
}

void UI::restoreViewerForTitlebarDrag(
    float globalX,
    float globalY,
    float mouseX,
    float mouseY
) {
    SDL_Window* viewer = viewerWindow();
    if (!viewer) {
        return;
    }

    WindowData* viewerData = findWindow(WindowRole::Viewer);
    if (!viewerData) {
        return;
    }

    const int maximizedWidth = viewerData->width;

    if (!SDL_RestoreWindow(viewer) || !SDL_SyncWindow(viewer)) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not restore maximized viewer for dragging: %s",
            SDL_GetError()
        );
        return;
    }

    updateWindowData(*viewerData);

    toolbarDockedToScreen = false;
    titlebarDrag.active = false;

    const float horizontalAnchor = maximizedWidth > 0
        ? mouseX / static_cast<float>(maximizedWidth)
        : 0.5f;

    const int targetX = static_cast<int>(
        std::lround(globalX - horizontalAnchor * viewerData->width)
    );
    const int targetY = static_cast<int>(
        std::lround(globalY - mouseY)
    );

    setWindowPositionIfChanged(viewerData->window, targetX, targetY);
    updateWindowData(*viewerData);

    syncSnappedWindowPosition(targetX, targetY);
    ensureToolbarVisible();
    setToolbarAlwaysOnTop(SDL_GetKeyboardFocus() == viewer);

    updateAllWindowData();
}

void UI::setWindowPositionIfChanged(SDL_Window* window, int x, int y) {
    if (!window || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
        return;
    }

    int currentX = 0;
    int currentY = 0;
    SDL_GetWindowPosition(window, &currentX, &currentY);

    if (currentX != x || currentY != y) {
        SDL_SetWindowPosition(window, x, y);
    }
}

void UI::syncWindowPositions(const SDL_WindowEvent& event) {
    if (!windowPositionsInitialized) {
        return;
    }

    for (WindowData& data : windowList) {
        if (data.window && SDL_GetWindowID(data.window) == event.windowID) {
            updateWindowData(data);
            return;
        }
    }
}

void UI::ensureToolbarVisible() {
    SDL_Window* toolbar = toolbarWindow();
    if (!toolbar) {
        return;
    }

    const SDL_WindowFlags flags = SDL_GetWindowFlags(toolbar);

    if ((flags & SDL_WINDOW_MINIMIZED) != 0) {
        if (!SDL_RestoreWindow(toolbar)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "Could not restore toolbar window: %s",
                SDL_GetError()
            );
        }
    } else if ((flags & SDL_WINDOW_HIDDEN) != 0) {
        if (!SDL_ShowWindow(toolbar)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "Could not show toolbar window: %s",
                SDL_GetError()
            );
        }
    }
}

void UI::setToolbarAlwaysOnTop(bool alwaysOnTop) {
    SDL_Window* toolbar = toolbarWindow();
    if (!toolbar) {
        return;
    }

    const bool currentlyAlwaysOnTop = (SDL_GetWindowFlags(toolbar) & SDL_WINDOW_ALWAYS_ON_TOP) != 0;

    if (currentlyAlwaysOnTop != alwaysOnTop
    &&  !SDL_SetWindowAlwaysOnTop(toolbar, alwaysOnTop)
    ) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not update toolbar always-on-top state: %s",
            SDL_GetError()
        );
    }
}

void UI::syncSnappedWindowPosition(int x, int y) {
    if (!windowsSnapped || toolbarDockedToScreen) {
        return;
    }

    WindowData* viewerData = findWindow(WindowRole::Viewer);
    WindowData* toolbarData = findWindow(WindowRole::Toolbar);

    if (!viewerData || !toolbarData) {
        return;
    }

    SDL_GetWindowSize(viewerData->window, &viewerData->width, &viewerData->height);
    SDL_GetWindowSize(toolbarData->window, &toolbarData->width, &toolbarData->height);

    toolbarX = toolbarOnRight
        ? x + viewerData->width
        : x - toolbarData->width;

    toolbarY = y;

    setWindowPositionIfChanged(toolbarData->window, toolbarX, toolbarY);
    toolbarData->x = toolbarX;
    toolbarData->y = toolbarY;
}

void UI::snapWindowToScreen(SDL_Window* window, int& x, int& y) {
    if (!window) {
        return;
    }

    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    SDL_Rect bounds{};

    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &bounds)) {
        return;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);

    const int leftDistance = std::abs(x - bounds.x);
    const int rightDistance = std::abs(x - (bounds.x + bounds.w - width));
    const int topDistance = std::abs(y - bounds.y);
    const int bottomDistance = std::abs(y - (bounds.y + bounds.h - height));

    if (leftDistance <= windowSnapDistance && leftDistance <= rightDistance) {
        x = bounds.x;
    } else if (rightDistance <= windowSnapDistance) {
        x = bounds.x + bounds.w - width;
    }

    if (topDistance <= windowSnapDistance && topDistance <= bottomDistance) {
        y = bounds.y;
    } else if (bottomDistance <= windowSnapDistance) {
        y = bounds.y + bounds.h - height;
    }
}

void UI::snapSnappedWindowsToScreen(int& x, int& y) {
    WindowData* viewerData = findWindow(WindowRole::Viewer);
    WindowData* toolbarData = findWindow(WindowRole::Toolbar);

    if (!viewerData || !toolbarData) {
        return;
    }

    const SDL_DisplayID display = SDL_GetDisplayForWindow(viewerData->window);
    SDL_Rect bounds{};

    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &bounds)) {
        return;
    }

    updateWindowData(*viewerData);
    updateWindowData(*toolbarData);

    const int groupLeft = toolbarOnRight ? x : x - toolbarData->width;
    const int groupRight = toolbarOnRight
        ? x + viewerData->width + toolbarData->width
        : x + viewerData->width;

    const int groupHeight = std::max(viewerData->height, toolbarData->height);

    const int leftDistance = std::abs(groupLeft - bounds.x);
    const int rightDistance = std::abs(groupRight - (bounds.x + bounds.w));
    const int topDistance = std::abs(y - bounds.y);
    const int bottomDistance = std::abs(y + groupHeight - (bounds.y + bounds.h));

    if (leftDistance <= windowSnapDistance && leftDistance <= rightDistance) {
        x += bounds.x - groupLeft;
    } else if (rightDistance <= windowSnapDistance) {
        x += bounds.x + bounds.w - groupRight;
    }

    if (topDistance <= windowSnapDistance && topDistance <= bottomDistance) {
        y = bounds.y;
    } else if (bottomDistance <= windowSnapDistance) {
        y = bounds.y + bounds.h - groupHeight;
    }
}

void UI::dockToolbarToNearestScreenSide() {
    WindowData* viewerData = findWindow(WindowRole::Viewer);
    WindowData* toolbarData = findWindow(WindowRole::Toolbar);

    if (!viewerData || !toolbarData) {
        return;
    }

    const SDL_DisplayID display = SDL_GetDisplayForWindow(viewerData->window);
    SDL_Rect bounds{};

    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &bounds)) {
        return;
    }

    updateWindowData(*toolbarData);

    const int toolbarCenter = toolbarData->x + toolbarData->width / 2;
    const int distanceLeft = std::abs(toolbarCenter - bounds.x);
    const int distanceRight = std::abs(toolbarCenter - (bounds.x + bounds.w));

    toolbarOnRight = distanceRight < distanceLeft;

    toolbarX = toolbarOnRight
        ? bounds.x + bounds.w - toolbarData->width
        : bounds.x;

    toolbarY = std::clamp(
        toolbarData->y,
        bounds.y,
        std::max(bounds.y, bounds.y + bounds.h - toolbarData->height)
    );

    windowsSnapped = true;
    toolbarDockedToScreen = true;

    setWindowPositionIfChanged(toolbarData->window, toolbarX, toolbarY);
    updateWindowData(*toolbarData);
}

void UI::updateSnappedWindowPosition() {
    if (!windowsSnapped || toolbarDockedToScreen) {
        return;
    }

    SDL_Window* viewer = viewerWindow();
    SDL_Window* toolbar = toolbarWindow();

    if (!viewer || !toolbar) {
        return;
    }

    if ((SDL_GetWindowFlags(viewer) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) != 0
    ||  (SDL_GetWindowFlags(toolbar) & SDL_WINDOW_MINIMIZED) != 0
    ) {
        return;
    }

    WindowData* viewerData = findWindow(WindowRole::Viewer);
    WindowData* toolbarData = findWindow(WindowRole::Toolbar);

    updateWindowData(*viewerData);
    updateWindowData(*toolbarData);

    toolbarX = toolbarOnRight
        ? viewerData->x + viewerData->width
        : viewerData->x - toolbarData->width;

    toolbarY = viewerData->y;

    setWindowPositionIfChanged(toolbarData->window, toolbarX, toolbarY);
    updateWindowData(*toolbarData);
}

void UI::handleViewerMaximized() {
    dockToolbarToNearestScreenSide();
    ensureToolbarVisible();
    setToolbarAlwaysOnTop(SDL_GetKeyboardFocus() == viewerWindow());
    updateAllWindowData();
}

void UI::handleViewerRestored() {
    WindowData* viewerData = findWindow(WindowRole::Viewer);
    WindowData* toolbarData = findWindow(WindowRole::Toolbar);

    if (!viewerData || !toolbarData) {
        return;
    }

    ensureToolbarVisible();

    if (SDL_Window* toolbar = toolbarWindow();
        toolbar && (SDL_GetWindowFlags(toolbar) & SDL_WINDOW_MINIMIZED) != 0
    ) {
        SDL_SyncWindow(toolbar);
    }

    updateWindowData(*viewerData);

    if (windowsSnapped) {
        toolbarDockedToScreen = false;

        updateWindowData(*toolbarData);

        toolbarX = toolbarOnRight
            ? viewerData->x + viewerData->width
            : viewerData->x - toolbarData->width;

        toolbarY = viewerData->y;

        setWindowPositionIfChanged(toolbarData->window, toolbarX, toolbarY);
    }

    ensureToolbarVisible();
    setToolbarAlwaysOnTop(SDL_GetKeyboardFocus() == viewerData->window);
    updateAllWindowData();
}

SDL_HitTestResult SDLCALL UI::viewerHitTest(
    SDL_Window* window,
    const SDL_Point* area,
    void*
) {
    if (!window || !area
    || (SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE) == 0
    || (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) {
        return SDL_HITTEST_NORMAL;
    }

    constexpr int resizeBorder = 4;

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);

    const bool left = area->x < resizeBorder;
    const bool right = area->x >= width - resizeBorder;
    const bool top = area->y < resizeBorder;
    const bool bottom = area->y >= height - resizeBorder;

    if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (top) return SDL_HITTEST_RESIZE_TOP;
    if (right) return SDL_HITTEST_RESIZE_RIGHT;
    if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    if (left) return SDL_HITTEST_RESIZE_LEFT;

    return SDL_HITTEST_NORMAL;
}