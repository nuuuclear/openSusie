#pragma once

#include <SDL3/SDL.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/Subroutine.h"
#include "plugin/PluginManager.h"
#include "ui/WindowTitlebar.h"

class App;
class PluginManager;
class NativeMenu;

class Viewer : public Subroutine {
public:
    enum class Interpolation {
        Nearest,
        Linear
    };

    enum class Background {
        Dark,
        Light,
        Checkerboard
    };

    struct Settings {
        Interpolation interpolation = Interpolation::Linear;
        Background background = Background::Dark;
        bool zoomToCursor = true;
        bool resetZoomOnOpen = true;
        bool toolbarOnRight = false;
    };

    explicit Viewer(App* app);
    ~Viewer();

    void event(const SDL_Event& event);
    void update(double deltaTime);
    void draw();
    Sint32 millisecondsUntilUpdate() const override;

    void loadPlugins(const std::string& dirPath);
    void present(const std::string& imagePath);
    void closeImage();
    bool hasImage() const;
    void saveImageAs();

    void zoomIn();
    void zoomOut();
    void fitToWindow();
    void rotateLeft();
    void rotateRight();
    void flipX();
    void flipY();

    void showInformation() const;
    void openFileDialog();

    const std::vector<std::string>& getRecentFiles() const;

    void setNativeMenu(NativeMenu* menu);

    std::map<std::string, std::vector<LoadedPlugin*>> getDecoderOptions();
    std::string decoderPreference(const std::string& extension) const;
    void setDecoderPreference(const std::string& extension, const std::string& pluginId);

    const Settings& settings() const;

    void setInterpolation(Interpolation interpolation);
    void setBackground(Background background);

    void setZoomToCursor(bool enabled);
    void setResetZoomOnOpen(bool enabled);
    void setToolbarOnRight(bool enabled);

    void rebuildRect();
private:
    bool loadStaticImage(const std::string& imagePath, LoadedPlugin* plugin);
    void clearAnimation();
    void loadSettings();
    void saveSettings() const;

    bool createTextureFromImage(DecodedImage& image);

    void resetView();
    void handleZoom(const SDL_Event& event);
    void zoomBy(float factor);
private:
    SDL_Texture* texture = nullptr;

    WindowTitlebar titlebar;
    void handleTitlebarAction(const SDL_Event& event);

    PluginManager pm;
    
    PluginAnimation animation;
    std::uint32_t animationFrame = 0;
    std::uint32_t animationLoop = 0;
    std::uint32_t animationFrameDurationMs = 100;
    double animationElapsedMs = 0.0;
    bool animationPlaying = false;

    std::string dialogImageExtensions;
    SDL_DialogFileFilter dialogFilters[2]{};

    SDL_FRect destRect{};

    DecodedImage currentImage;
    std::string currentImagePath;
    std::string currentImageMetadata;

    float imageX = 0.0f;
    float imageY = 0.0f;

    float mouseX = 0.0f;
    float mouseY = 0.0f;

    // zoom
    float fitScale = 1.0f;

    float zoom = 1.0f;

    static constexpr float minZoom = 0.05f;
    static constexpr float maxZoom = 20.0f;
    static constexpr float zoomFactor = 1.1f;

    // pan
    bool dragging = false;

    float dragStartMouseX = 0.0f;
    float dragStartMouseY = 0.0f;

    float dragStartImageX = 0.0f;
    float dragStartImageY = 0.0f;

    // flip
    
    bool imageFlipX = false;
    bool imageFlipY = false;
    
    // rotation
    double rotationDegrees = 0.0;
    
    NativeMenu* nativeMenu = nullptr;
    std::vector<std::string> recentFiles;
    std::map<std::string, std::string> decoderPreferences;
    Settings viewerSettings;
    std::filesystem::path settingsPath;

    static void SDLCALL openFileCallback(
        void* userdata,
        const char* const* filelist,
        int filter
    );
    void rememberFile(const std::string& imagePath);
};