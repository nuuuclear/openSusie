#include "UI.h"

#include <algorithm>
#include <cmath>

#include "core/FileSystem.h"
#include "core/Paths.h"

UI::~UI() {
    shutdown();
}

bool UI::initialize(const std::string& title, int viewerWidth, int viewerHeight, const FileSystem& files) {
    SDL_SetHint(SDL_HINT_WINDOWS_ENABLE_MESSAGELOOP, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "1");
    SDL_SetHint("SDL_BORDERLESS_RESIZABLE_STYLE", "1");

    viewer.height = ViewerTitleBarHeight;
    viewer.buttonCount = 3;
    viewer.window = SDL_CreateWindow(
        title.c_str(),
        viewerWidth,
        viewerHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS
    );

    if (!viewer.window) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not create viewer window: %s",
            SDL_GetError()
        );

        return false;
    }

    if (!SDL_SetWindowHitTest(viewer.window, UI::viewerHitTest, nullptr)) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not enable viewer window resize hit testing: %s",
            SDL_GetError()
        );
    }

    toolbar.height = ToolbarTitleBarHeight;
    toolbar.buttonCount = 1;
    toolbar.window = SDL_CreateWindow(
        "openSusie toolbar",
        32,
        ToolbarTitleBarHeight + 208,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_UTILITY
    );

    if (!toolbar.window) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not create toolbar window: %s",
            SDL_GetError()
        );

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
        SDL_SetWindowIcon(viewer.window, icon);
        SDL_SetWindowIcon(toolbar.window, icon);
        SDL_DestroySurface(icon);
    }

    viewer.renderer = SDL_CreateRenderer(viewer.window, nullptr);
    if (!viewer.renderer) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not create viewer renderer: %s",
            SDL_GetError()
        );

        shutdown();
        return false;
    }
    
    toolbar.renderer = SDL_CreateRenderer(toolbar.window, nullptr);
    if (!toolbar.renderer) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not create toolbar renderer: %s",
            SDL_GetError()
        );

        shutdown();
        return false;
    }

    SDL_SetRenderVSync(viewer.renderer, 1);
    viewer.closeTexture = loadButtonTexture(
        viewer.renderer,
        files.resolve(Paths::Resources, "close.bmp").string()
    );

    viewer.minimizeTexture = loadButtonTexture(
        viewer.renderer,
        files.resolve(Paths::Resources, "minimize.bmp").string()
    );

    viewer.maximizeTexture = loadButtonTexture(
        viewer.renderer,
        files.resolve(Paths::Resources, "full.bmp").string()
    );

    toolbar.closeTexture = loadButtonTexture(
        toolbar.renderer,
        files.resolve(Paths::Resources, "close.bmp").string()
    );

    SDL_SetWindowPosition(toolbar.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_GetWindowPosition(toolbar.window, &toolbarX, &toolbarY);

    int toolbarWidth = 0;
    SDL_GetWindowSize(toolbar.window, &toolbarWidth, nullptr);

    viewerX = toolbarX + toolbarWidth;
    viewerY = toolbarY;
    SDL_SetWindowPosition(viewer.window, viewerX, viewerY);

    windowPositionsInitialized = true;

    return true;
}

void UI::shutdown() {
    destroyTitlebar(toolbar);
    destroyTitlebar(viewer);
    windowPositionsInitialized = false;
}

SDL_Window* UI::viewerWindow() const {
    return viewer.window;
}

SDL_Renderer* UI::viewerRenderer() const {
    return viewer.renderer;
}

SDL_Window* UI::toolbarWindow() const {
    return toolbar.window;
}

SDL_Renderer* UI::toolbarRenderer() const {
    return toolbar.renderer;
}

void UI::handleEvent(const SDL_Event& event) {
    const SDL_WindowID viewerID = viewer.window ? SDL_GetWindowID(viewer.window) : 0;
    const SDL_WindowID toolbarID = toolbar.window ? SDL_GetWindowID(toolbar.window) : 0;

    switch (event.type) {
        case SDL_EVENT_WINDOW_MOVED:
            syncWindowPositions(event.window);
            break;

        case SDL_EVENT_WINDOW_MAXIMIZED:
            if (event.window.windowID == viewerID) {
                dockToolbarToNearestScreenSide();
                ensureToolbarVisible();
                setToolbarAlwaysOnTop(SDL_GetKeyboardFocus() == viewer.window);
            }
            break;

        case SDL_EVENT_WINDOW_RESTORED:
            if (event.window.windowID == viewerID) {
                SDL_GetWindowPosition(viewer.window, &viewerX, &viewerY);
                if (windowsSnapped) {
                    int toolbarWidth = 0;
                    SDL_GetWindowSize(toolbar.window, &toolbarWidth, nullptr);

                    int viewerWidth = 0;
                    SDL_GetWindowSize(viewer.window, &viewerWidth, nullptr);

                    toolbarX = toolbarOnRight
                        ? viewerX + viewerWidth
                        : viewerX - toolbarWidth;
                    toolbarY = viewerY;
                    setWindowPositionIfChanged(toolbar.window, toolbarX, toolbarY);
                }

                ensureToolbarVisible();
                setToolbarAlwaysOnTop(SDL_GetKeyboardFocus() == viewer.window);
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
            if (event.window.windowID == viewerID) {
                updateSnappedWindowPosition();
            }
            break;

        default:
            break;
    }

    handleTitlebarEvent(viewer, event);
    handleTitlebarEvent(toolbar, event);
}

void UI::ensureToolbarVisible() {
    if (!toolbar.window) return;

    const SDL_WindowFlags flags = SDL_GetWindowFlags(toolbar.window);
    if ((flags & SDL_WINDOW_MINIMIZED) != 0) {
        if (!SDL_RestoreWindow(toolbar.window)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "Could not restore toolbar window: %s",
                SDL_GetError()
            );
        }
        
    } else if ((flags & SDL_WINDOW_HIDDEN) != 0) {
        if (!SDL_ShowWindow(toolbar.window)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "Could not show toolbar window: %s",
                SDL_GetError()
            );
        }
    }
}

void UI::setToolbarAlwaysOnTop(bool alwaysOnTop) {
    if (!toolbar.window) return;
    const bool currentlyAlwaysOnTop = (SDL_GetWindowFlags(toolbar.window) & SDL_WINDOW_ALWAYS_ON_TOP) != 0;
    if (currentlyAlwaysOnTop != alwaysOnTop
    &&  !SDL_SetWindowAlwaysOnTop(toolbar.window, alwaysOnTop)
    ) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not update toolbar always-on-top state: %s",
            SDL_GetError()
        );
    }
}

UI::Action UI::consumeTitlebarAction(Window window, int* x, int* y) {
    Titlebar& titlebar = window == Window::Viewer ? viewer : toolbar;
    const Action action = titlebar.pendingAction;
    titlebar.pendingAction = Action::None;
    if (x) *x = titlebar.actionX;
    if (y) *y = titlebar.actionY;
    return action;
}

void UI::beginFrame() {
    if (viewer.renderer) {
        SDL_SetRenderDrawColor(viewer.renderer, 0, 0, 0, 255);
        SDL_RenderClear(viewer.renderer);
    }

    if (toolbar.renderer) {
        SDL_SetRenderDrawColor(toolbar.renderer, 38, 40, 44, 255);
        SDL_RenderClear(toolbar.renderer);
    }
}

void UI::present() {
    if (viewer.renderer) SDL_RenderPresent(viewer.renderer);
    if (toolbar.renderer) SDL_RenderPresent(toolbar.renderer);
}

void UI::drawViewerTitlebar(const std::string& title) {
    drawTitlebar(viewer, title);
}

void UI::drawToolbarTitlebar() {
    drawTitlebar(toolbar, {});
}

void UI::setViewerVisible(bool visible) {
    if (!viewer.window) return;

    if (visible) {
        SDL_ShowWindow(viewer.window);
    } else {
        setToolbarAlwaysOnTop(false);
        SDL_HideWindow(viewer.window);

        if (toolbar.window) {
            SDL_ShowWindow(toolbar.window);
        }
    }
}

void UI::minimizeWindows() {
    if (viewer.window) SDL_MinimizeWindow(viewer.window);
    if (toolbar.window) SDL_MinimizeWindow(toolbar.window);
}

void UI::toggleViewerMaximize() {
    if (!viewer.window) return;
    if ((SDL_GetWindowFlags(viewer.window) & SDL_WINDOW_MAXIMIZED) != 0) {
        SDL_RestoreWindow(viewer.window);
    } else {
        SDL_MaximizeWindow(viewer.window);
    }
}

void UI::resizeViewer(int width, int height) {
    if (!viewer.window) return;

    const SDL_DisplayID display = SDL_GetDisplayForWindow(viewer.window);
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
        static_cast<float>(maxWidth) / width,
        static_cast<float>(maxHeight) / height
    });

    SDL_SetWindowSize(
        viewer.window,
        std::max(1, static_cast<int>(width * scale)),
        std::max(1, static_cast<int>(height * scale))
    );

    SDL_SyncWindow(viewer.window);
    updateSnappedWindowPosition();
}

void UI::setToolbarOnRight(bool onRight) {
    toolbarOnRight = onRight;
    if (!windowPositionsInitialized || !viewer.window || !toolbar.window) return;

    int toolbarWidth = 0;
    int viewerWidth = 0;
    SDL_GetWindowSize(toolbar.window, &toolbarWidth, nullptr);
    SDL_GetWindowSize(viewer.window, &viewerWidth, nullptr);
    SDL_GetWindowPosition(viewer.window, &viewerX, &viewerY);

    toolbarX = toolbarOnRight
        ? viewerX + viewerWidth
        : viewerX - toolbarWidth;
    toolbarY = viewerY;
    windowsSnapped = true;
    setWindowPositionIfChanged(toolbar.window, toolbarX, toolbarY);
}

int UI::buttonAt(const Titlebar& titlebar, float x, float y) {
    if (y < 0 || y >= titlebar.height) return -1;
    int width = 0;

    SDL_GetWindowSize(titlebar.window, &width, nullptr);

    const int left = width - titlebar.buttonCount * 12;
    if (x < left || x >= width) return -1;

    return static_cast<int>(x - left) / 12;
}

SDL_HitTestResult SDLCALL UI::viewerHitTest(SDL_Window* window, const SDL_Point* area, void*) {
    if (!window || !area
    || (SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE) == 0
    || (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0
    ) {
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

void UI::handleTitlebarEvent(Titlebar& titlebar, const SDL_Event& event) {
    if (!titlebar.window) return;
    const SDL_WindowID windowID = SDL_GetWindowID(titlebar.window);

    switch (event.type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == windowID) {
                titlebar.pendingAction = Action::Close;
            }
            return;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (event.window.windowID == windowID) {
                resetTitlebarPress(titlebar);
            }
            return;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            resetTitlebarPress(titlebar);
            if (event.button.windowID != windowID
            ||  event.button.y < 0
            ||  event.button.y >= titlebar.height
            ) {
                return;
            }

            titlebar.pressedMouseButton = event.button.button;
            SDL_GetGlobalMouseState(
                &titlebar.dragStartGlobalX,
                &titlebar.dragStartGlobalY
            );

            SDL_GetWindowPosition(
                titlebar.window,
                &titlebar.dragStartWindowX,
                &titlebar.dragStartWindowY
            );

            if (event.button.button == SDL_BUTTON_LEFT) {
                titlebar.pressedButton = buttonAt(titlebar, event.button.x, event.button.y);
            }

            return;

        case SDL_EVENT_MOUSE_MOTION:
            handleTitlebarMouseMotion(titlebar, event.motion);
            return;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            handleTitlebarMouseButtonUp(titlebar, event.button);
            return;

        default:
            return;
    }
}

void UI::handleTitlebarMouseMotion(Titlebar& titlebar, const SDL_MouseMotionEvent& event) {
    if (event.windowID != SDL_GetWindowID(titlebar.window)
    ||  titlebar.pressedMouseButton == 0
    ) {
        return;
    }

    constexpr float dragThreshold = 4.0f;

    float globalX = 0.0f;
    float globalY = 0.0f;
    SDL_GetGlobalMouseState(&globalX, &globalY);

    titlebar.dragX = globalX - titlebar.dragStartGlobalX;
    titlebar.dragY = globalY - titlebar.dragStartGlobalY;

    if (!titlebar.pressDragged) {
        const float distanceSquared =
            titlebar.dragX * titlebar.dragX +
            titlebar.dragY * titlebar.dragY;

        if (distanceSquared <= dragThreshold * dragThreshold) {
            return;
        }

        titlebar.pressDragged = true;
    }

    if (titlebar.pressedMouseButton != SDL_BUTTON_LEFT
    ||  titlebar.pressedButton >= 0
    ) {
        return;
    }

    const bool isViewer = titlebar.window == viewer.window;

    const bool viewerMaximized = isViewer 
    &&  (SDL_GetWindowFlags(viewer.window) & SDL_WINDOW_MAXIMIZED) != 0;

    if (viewerMaximized) {
        restoreViewerForDrag(titlebar, event, globalX, globalY);
        return;
    }

    if (!isViewer && windowsSnapped) {
        windowsSnapped = false;
    }

    int targetX = titlebar.dragStartWindowX + static_cast<int>(std::lround(titlebar.dragX));
    int targetY = titlebar.dragStartWindowY + static_cast<int>(std::lround(titlebar.dragY));

    if (isViewer && windowsSnapped) {
        snapWindowToScreen(viewer.window, targetX, targetY);

        viewerX = targetX;
        viewerY = targetY;

        setWindowPositionIfChanged(viewer.window, targetX, targetY);
        syncSnappedWindowPosition(titlebar, targetX, targetY);
        return;
    }

    snapSnappedWindowsToScreen(targetX, targetY);
    setWindowPositionIfChanged(titlebar.window, targetX, targetY);

    if (isViewer) {
        viewerX = targetX;
        viewerY = targetY;
    } else {
        toolbarX = targetX;
        toolbarY = targetY;
    }

    int viewerWidth = 0;
    int toolbarWidth = 0;

    SDL_GetWindowSize(viewer.window, &viewerWidth, nullptr);
    SDL_GetWindowSize(toolbar.window, &toolbarWidth, nullptr);

    const bool verticallyAligned = std::abs(viewerY - toolbarY) <= windowSnapDistance;

    if (!verticallyAligned) {
        return;
    }

    // right side
    if (std::abs(toolbarX - (viewerX + viewerWidth)) <= windowSnapDistance) {
        windowsSnapped = true;
        toolbarOnRight = true;

        toolbarX = viewerX + viewerWidth;
        toolbarY = viewerY;

        setWindowPositionIfChanged(toolbar.window, toolbarX, toolbarY);
        return;
    }

    // left side
    if (std::abs(viewerX - (toolbarX + toolbarWidth)) <= windowSnapDistance) {
        windowsSnapped = true;
        toolbarOnRight = false;

        toolbarX = viewerX - toolbarWidth;
        toolbarY = viewerY;

        setWindowPositionIfChanged(toolbar.window, toolbarX, toolbarY);
    }
}

void UI::restoreViewerForDrag(
    Titlebar& titlebar,
    const SDL_MouseMotionEvent& event,
    float globalX,
    float globalY
) {
    int maximizedWidth = 0;
    SDL_GetWindowSize(viewer.window, &maximizedWidth, nullptr);
    if (!SDL_RestoreWindow(viewer.window) || !SDL_SyncWindow(viewer.window)) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not restore maximized viewer for dragging: %s",
            SDL_GetError()
        );
        titlebar.pressedMouseButton = 0;
        return;
    }

    int restoredWidth = 0;
    SDL_GetWindowSize(viewer.window, &restoredWidth, nullptr);

    const float horizontalAnchor
    =   maximizedWidth > 0
    ?   event.x / static_cast<float>(maximizedWidth)
    :   0.5f;

    const int targetX = static_cast<int>(std::lround(globalX - horizontalAnchor * restoredWidth));
    const int targetY = static_cast<int>(std::lround(globalY - event.y));

    setWindowPositionIfChanged(viewer.window, targetX, targetY);
    viewerX = targetX;
    viewerY = targetY;

    syncSnappedWindowPosition(titlebar, targetX, targetY);
    ensureToolbarVisible();
    setToolbarAlwaysOnTop(SDL_GetKeyboardFocus() == viewer.window);

    titlebar.dragStartGlobalX = globalX;
    titlebar.dragStartGlobalY = globalY;
    titlebar.dragStartWindowX = targetX;
    titlebar.dragStartWindowY = targetY;
    titlebar.dragX = 0.0f;
    titlebar.dragY = 0.0f;
}

void UI::syncSnappedWindowPosition(const Titlebar& titlebar, int x, int y) {
    if (!windowsSnapped
    ||  titlebar.window != viewer.window
    ) {
        return;
    }

    int viewerWidth = 0;
    int toolbarWidth = 0;

    SDL_GetWindowSize(viewer.window, &viewerWidth, nullptr);
    SDL_GetWindowSize(toolbar.window, &toolbarWidth, nullptr);

    toolbarX = toolbarOnRight
        ? x + viewerWidth
        : x - toolbarWidth;

    toolbarY = y;

    setWindowPositionIfChanged(toolbar.window, toolbarX, toolbarY);
}

void UI::resetTitlebarPress(Titlebar& titlebar) {
    titlebar.pressedButton = -1;
    titlebar.pressedMouseButton = 0;
    titlebar.dragX = 0.0f;
    titlebar.dragY = 0.0f;
    titlebar.pressDragged = false;
}

void UI::handleTitlebarMouseButtonUp(Titlebar& titlebar, const SDL_MouseButtonEvent& event) {
    const bool targetWindow = event.windowID == SDL_GetWindowID(titlebar.window);
    if (!targetWindow || titlebar.pressDragged) {
        resetTitlebarPress(titlebar);
        return;
    }

    switch (event.button) {
        case SDL_BUTTON_RIGHT:
            if (titlebar.pressedMouseButton != SDL_BUTTON_RIGHT
            ||  event.y < 0
            ||  event.y >= titlebar.height
            ) {
                break;
            }
            titlebar.pendingAction = Action::ShowMenu;
            titlebar.actionX = static_cast<int>(event.x);
            titlebar.actionY = static_cast<int>(event.y);
            break;
        case SDL_BUTTON_LEFT: {
            const int releasedButton = buttonAt(titlebar, event.x, event.y);
            if (releasedButton < 0 || releasedButton != titlebar.pressedButton) {
                break;
            }
            switch (releasedButton) {
                case 0:
                    titlebar.pendingAction = titlebar.buttonCount == 1
                        ? Action::Close
                        : Action::Minimize;
                    break;
                case 1:
                    titlebar.pendingAction = Action::ToggleMaximize;
                    break;
                case 2:
                    titlebar.pendingAction = Action::Close;
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }

    resetTitlebarPress(titlebar);
}

void UI::setWindowPositionIfChanged(SDL_Window* window, int x, int y) {
    int currentX = 0;
    int currentY = 0;
    SDL_GetWindowPosition(window, &currentX, &currentY);
    if (currentX != x || currentY != y) {
        SDL_SetWindowPosition(window, x, y);
    }
}

SDL_Texture* UI::loadButtonTexture(SDL_Renderer* renderer, const std::string& path) {
    SDL_Surface* surface = SDL_LoadBMP(path.c_str());
    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load titlebar button %s: %s", path.c_str(), SDL_GetError());
        return nullptr;
    }
    if (!SDL_SetSurfaceColorKey(surface, true, SDL_MapSurfaceRGB(surface, 0, 0, 0))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not set titlebar button transparency: %s", SDL_GetError());
        SDL_DestroySurface(surface);
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create titlebar button texture: %s", SDL_GetError());
    }
    SDL_DestroySurface(surface);
    return texture;
}

void UI::drawTitlebar(Titlebar& titlebar, const std::string& title) {
    if (!titlebar.renderer || !titlebar.window) return;

    int width = 0;
    SDL_GetWindowSize(titlebar.window, &width, nullptr);
    const SDL_FRect bar{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(titlebar.height)};
    SDL_SetRenderDrawColor(titlebar.renderer, 48, 50, 54, 255);
    SDL_RenderFillRect(titlebar.renderer, &bar);

    if (!title.empty()) {
        SDL_SetRenderDrawColor(titlebar.renderer, 230, 231, 233, 255);
        const SDL_Rect textArea{
            0, 0, std::max(0, width - titlebar.buttonCount * 12 - 4), titlebar.height
        };
        SDL_SetRenderClipRect(titlebar.renderer, &textArea);
        SDL_RenderDebugText(titlebar.renderer, 4.0f, 5.0f, title.c_str());
        SDL_SetRenderClipRect(titlebar.renderer, nullptr);
    }

    const float y = static_cast<float>((titlebar.height - 10) / 2);
    if (titlebar.buttonCount == 3) {
        const SDL_FRect minimize{static_cast<float>(width - 35), y, 10.0f, 10.0f};
        const SDL_FRect maximize{static_cast<float>(width - 23), y, 10.0f, 10.0f};
        if (titlebar.minimizeTexture) {
            SDL_RenderTexture(titlebar.renderer, titlebar.minimizeTexture, nullptr, &minimize);
        }
        if (titlebar.maximizeTexture) {
            SDL_RenderTexture(titlebar.renderer, titlebar.maximizeTexture, nullptr, &maximize);
        }
    }
    const SDL_FRect close{static_cast<float>(width - 11), y, 10.0f, 10.0f};
    if (titlebar.closeTexture) {
        SDL_RenderTexture(titlebar.renderer, titlebar.closeTexture, nullptr, &close);
    }
}

void UI::destroyTitlebar(Titlebar& titlebar) {
    SDL_DestroyTexture(titlebar.closeTexture);
    SDL_DestroyTexture(titlebar.minimizeTexture);
    SDL_DestroyTexture(titlebar.maximizeTexture);
    SDL_DestroyRenderer(titlebar.renderer);
    SDL_DestroyWindow(titlebar.window);
    titlebar = {};
}

void UI::syncWindowPositions(const SDL_WindowEvent& event) {
	if (!windowPositionsInitialized) {
		return;
	}

	const SDL_WindowID viewerID = SDL_GetWindowID(viewer.window);
	const SDL_WindowID toolbarID = SDL_GetWindowID(toolbar.window);

	if (event.windowID == viewerID) {
		SDL_GetWindowPosition(viewer.window, &viewerX, &viewerY);
		return;
	}

	if (event.windowID == toolbarID) {
		SDL_GetWindowPosition(toolbar.window, &toolbarX, &toolbarY);
	}
}

void UI::snapWindowToScreen(SDL_Window* window, int& x, int& y) {
    if (!window) return;

    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    SDL_Rect bounds{};

    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &bounds)) {
        return;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);

    const int leftDistance = std::abs(x - bounds.x);
    const int rightDistance = std::abs(
        x - (bounds.x + bounds.w - width)
    );

    const int topDistance = std::abs(y - bounds.y);
    const int bottomDistance = std::abs(
        y - (bounds.y + bounds.h - height)
    );

    if (leftDistance <= windowSnapDistance
    &&  leftDistance <= rightDistance) {
        x = bounds.x;
    } else if (rightDistance <= windowSnapDistance) {
        x = bounds.x + bounds.w - width;
    }

    if (topDistance <= windowSnapDistance
    &&  topDistance <= bottomDistance) {
        y = bounds.y;
    } else if (bottomDistance <= windowSnapDistance) {
        y = bounds.y + bounds.h - height;
    }
}

void UI::dockToolbarToNearestScreenSide() {
    if (!viewer.window || !toolbar.window) return;

    const SDL_DisplayID display = SDL_GetDisplayForWindow(viewer.window);
    SDL_Rect bounds{};

    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &bounds)) {
        return;
    }

    int toolbarWidth = 0;
    int toolbarHeight = 0;

    SDL_GetWindowSize(toolbar.window, &toolbarWidth, &toolbarHeight);
    SDL_GetWindowPosition(toolbar.window, &toolbarX, &toolbarY);

    const int toolbarCenter = toolbarX + toolbarWidth / 2;

    const int distanceLeft = std::abs(toolbarCenter - bounds.x);
    const int distanceRight = std::abs(
        toolbarCenter - (bounds.x + bounds.w)
    );

    toolbarOnRight = distanceRight < distanceLeft;

    toolbarX = toolbarOnRight
        ? bounds.x + bounds.w - toolbarWidth
        : bounds.x;

    toolbarY = std::clamp(
        toolbarY,
        bounds.y,
        std::max(bounds.y, bounds.y + bounds.h - toolbarHeight)
    );

    // treat the toolbar as temporarily attached to the maximized viewer
    windowsSnapped = true;

    setWindowPositionIfChanged(toolbar.window, toolbarX, toolbarY);
}

void UI::snapSnappedWindowsToScreen(int& x, int& y) {
    if (!viewer.window || !toolbar.window) return;

    const SDL_DisplayID display = SDL_GetDisplayForWindow(viewer.window);
    SDL_Rect bounds{};

    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &bounds)) {
        return;
    }

    int viewerWidth = 0;
    int viewerHeight = 0;
    int toolbarWidth = 0;
    int toolbarHeight = 0;

    SDL_GetWindowSize(viewer.window, &viewerWidth, &viewerHeight);
    SDL_GetWindowSize(toolbar.window, &toolbarWidth, &toolbarHeight);

    const int groupLeft = toolbarOnRight ? x : x - toolbarWidth;
    const int groupRight = toolbarOnRight
        ? x + viewerWidth + toolbarWidth
        : x + viewerWidth;

    const int groupHeight = std::max(viewerHeight, toolbarHeight);

    const int leftDistance = std::abs(groupLeft - bounds.x);
    const int rightDistance = std::abs(groupRight - (bounds.x + bounds.w));
    const int topDistance = std::abs(y - bounds.y);
    const int bottomDistance = std::abs(y + groupHeight - (bounds.y + bounds.h));

    if (leftDistance <= windowSnapDistance
    &&  leftDistance <= rightDistance) {
        x += bounds.x - groupLeft;
    } else if (rightDistance <= windowSnapDistance) {
        x += bounds.x + bounds.w - groupRight;
    }

    if (topDistance <= windowSnapDistance
    &&  topDistance <= bottomDistance) {
        y = bounds.y;
    } else if (bottomDistance <= windowSnapDistance) {
        y = bounds.y + bounds.h - groupHeight;
    }
}

void UI::updateSnappedWindowPosition() {
    if (!windowsSnapped || !viewer.window || !toolbar.window) {
        return;
    }

    if ((SDL_GetWindowFlags(viewer.window) & SDL_WINDOW_MAXIMIZED) != 0) {
        return;
    }

    SDL_GetWindowPosition(viewer.window, &viewerX, &viewerY);

    int viewerWidth = 0;
    int toolbarWidth = 0;

    SDL_GetWindowSize(viewer.window, &viewerWidth, nullptr);
    SDL_GetWindowSize(toolbar.window, &toolbarWidth, nullptr);

    toolbarX = toolbarOnRight
    ?   viewerX + viewerWidth
    :   viewerX - toolbarWidth;

    toolbarY = viewerY;

    setWindowPositionIfChanged(toolbar.window, toolbarX, toolbarY);
}