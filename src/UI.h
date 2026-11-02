#pragma once

#include <SDL3/SDL.h>

#include <string>

class FileSystem;

inline constexpr int ToolbarTitleBarHeight = 12;
inline constexpr int ViewerTitleBarHeight = 18;

class UI {
public:
    enum class Window {
        Viewer,
        Toolbar
    };

    enum class Action {
        None,
        Close,
        Minimize,
        ToggleMaximize,
        ShowMenu
    };

    UI() = default;
    ~UI();

    UI(const UI&) = delete;
    UI& operator=(const UI&) = delete;

    bool initialize(
        const std::string& title,
        int viewerWidth,
        int viewerHeight,
        const FileSystem& files
    );
    void shutdown();

    SDL_Window* viewerWindow() const;
    SDL_Renderer* viewerRenderer() const;
    SDL_Window* toolbarWindow() const;
    SDL_Renderer* toolbarRenderer() const;

    void handleEvent(const SDL_Event& event);
    Action consumeTitlebarAction(Window window, int* x = nullptr, int* y = nullptr);

    void beginFrame();
    void present();
    void drawViewerTitlebar(const std::string& title);
    void drawToolbarTitlebar();

    void setViewerVisible(bool visible);
    void minimizeWindows();
    void toggleViewerMaximize();
    void resizeViewer(int width, int height);
    void setToolbarOnRight(bool onRight);
    bool isToolbarOnRight() const { return toolbarOnRight; }

private:
    struct Titlebar {
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        SDL_Texture* closeTexture = nullptr;
        SDL_Texture* minimizeTexture = nullptr;
        SDL_Texture* maximizeTexture = nullptr;

        int height = 0;
        int buttonCount = 0;
        int pressedButton = -1;
        Uint8 pressedMouseButton = 0;

        float dragX = 0.0f;
        float dragY = 0.0f;
        float dragStartGlobalX = 0.0f;
        float dragStartGlobalY = 0.0f;
        int dragStartWindowX = 0;
        int dragStartWindowY = 0;
        bool pressDragged = false;

        Action pendingAction = Action::None;
        int actionX = 0;
        int actionY = 0;
    };

    static int buttonAt(const Titlebar& titlebar, float x, float y);
    static SDL_HitTestResult SDLCALL viewerHitTest(
        SDL_Window* window,
        const SDL_Point* area,
        void* data
    );

    void handleTitlebarEvent(Titlebar& titlebar, const SDL_Event& event);
    void handleTitlebarMouseMotion(Titlebar& titlebar, const SDL_MouseMotionEvent& event);
    void handleTitlebarMouseButtonUp(Titlebar& titlebar, const SDL_MouseButtonEvent& event);

    static void resetTitlebarPress(Titlebar& titlebar);
    void restoreViewerForDrag(
        Titlebar& titlebar,
        const SDL_MouseMotionEvent& event,
        float globalX,
        float globalY
    );

    void syncSnappedWindowPosition(const Titlebar& titlebar, int x, int y);
    static void setWindowPositionIfChanged(SDL_Window* window, int x, int y);

    static SDL_Texture* loadButtonTexture(SDL_Renderer* renderer, const std::string& path);

    static void drawTitlebar(Titlebar& titlebar, const std::string& title);
    static void destroyTitlebar(Titlebar& titlebar);

    void ensureToolbarVisible();
    void setToolbarAlwaysOnTop(bool alwaysOnTop);
    void syncWindowPositions(const SDL_WindowEvent& event);

    Titlebar viewer;
    Titlebar toolbar;
    int viewerX = 0;
    int viewerY = 0;
    int toolbarX = 0;
    int toolbarY = 0;
    bool windowPositionsInitialized = false;
    bool windowsSnapped = true;
    bool toolbarOnRight = false;
    static constexpr int windowSnapDistance = 16;
};