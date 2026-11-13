#include "Viewer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <ini.h>

#include "app/App.h"
#include "core/Paths.h"
#include "ui/NativeMenu.h"
#include "ui/WindowTitlebar.h"

namespace {
constexpr Sint32 OpenFileEventCode = 0x494D4147;

std::string normalizeExtension(std::string extension) {
    if (!extension.empty() && extension.front() == '.')
        extension.erase(0, 1);
    
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );
    
    return extension;
}

std::string pluginIdentifier(const LoadedPlugin& plugin) {
    return std::filesystem::path(plugin.path).filename().string();
}
} // namespace

Viewer::Viewer(App* app) : Subroutine(app) {
    loadSettings();

    const auto resourcePath = app->getFileSystem().resolve(Paths::Resources, "");

    titlebar.initialize(
        app->ui().viewerRenderer(),
        WindowTitlebar::Config{
            .height = ViewerTitleBarHeight,
            .buttonCount = 3,
            .showTitle = true
        },
        resourcePath.string()
    );
}

Viewer::~Viewer() {
    clearAnimation();
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
}

void Viewer::present(const std::string& imagePath) {
    if (imagePath.empty()) return;

    std::vector<LoadedPlugin*> supportingPlugins = pm.getSupportingPlugins(imagePath);
    bool imageFromArchive = false;
    DecodedImage archiveImage;

    if (supportingPlugins.empty()) {
        std::vector<LoadedPlugin*> archivePlugins = pm.getSupportingArchivePlugins(imagePath);
        if (archivePlugins.empty()) {
            const std::string message
            =   "No installed image or archive plugin supports this file:\n"
            +   imagePath;

            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_WARNING,
                "Unsupported image",
                message.c_str(),
                app->ui().toolbarWindow()
            );

            return;
        }

        LoadedPlugin* archivePlugin = archivePlugins.front();
        if (archivePlugins.size() > 1) {
            std::vector<std::string> labels;
            labels.reserve(archivePlugins.size());
            for (const LoadedPlugin* plugin : archivePlugins) {
                labels.push_back(plugin->name);
            }

            const std::optional<std::size_t> choice
            =   nativeMenu
            ?   nativeMenu->chooseOption(labels)
            :   std::nullopt;

            if (!choice || *choice >= archivePlugins.size()) return;
            archivePlugin = archivePlugins[*choice];
        }

        std::vector<SusieArchiveEntry> archiveEntries;
        if (!pm.getArchiveEntries(archivePlugin, imagePath, archiveEntries)) return;
        const auto imageDecoders = pm.getDeclaredPluginsByExtension();
        std::vector<std::string> imageEntries;
        for (const SusieArchiveEntry& entry : archiveEntries) {
            if (entry.filename.empty()) continue;
            std::string entryName = entry.path;
            if (!entryName.empty() && entryName.back() != '/'
            &&  entryName.back() != '\\'
            ) {
                entryName.push_back('/');
            }

            entryName += entry.filename;
            std::replace(entryName.begin(), entryName.end(), '\\', '/');
            const std::string extension = normalizeExtension(
                std::filesystem::path(entryName).extension().string()
            );

            if (imageDecoders.find(extension) != imageDecoders.end()) {
                imageEntries.push_back(std::move(entryName));
            }
        }
        
        if (imageEntries.empty()) {
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_WARNING,
                "Empty archive",
                "The archive does not contain files supported by an installed image plugin.",
                app->ui().toolbarWindow()
            );

            return;
        }

        std::size_t selectedEntry = 0;
        if (imageEntries.size() > 1) {
            const std::optional<std::size_t> choice
            =   nativeMenu
            ?   nativeMenu->chooseOption(imageEntries)
            :   std::nullopt;

            if (!choice || *choice >= imageEntries.size()) return;
            selectedEntry = *choice;
        }

        if (!pm.decodeArchiveEntry(
                imagePath,
                imageEntries[selectedEntry],
                archiveImage,
                archivePlugin
            )
        ) {
            std::cerr
            <<  "Could not decode archive image: "
            <<  imageEntries[selectedEntry] << " from " << imagePath << "\n";

            return;
        }

        imageFromArchive = true;
    }

    const std::string extension = normalizeExtension(std::filesystem::path(imagePath).extension().string());
    LoadedPlugin* selectedPlugin = nullptr;
    if (!imageFromArchive) {
        const std::string preferredPlugin = decoderPreference(extension);
        if (!preferredPlugin.empty()) {
            const auto preferred = std::find_if(
                supportingPlugins.begin(),
                supportingPlugins.end(),
                [&](const LoadedPlugin* plugin) {
                    return pluginIdentifier(*plugin) == preferredPlugin;
                }
            );

            if (preferred != supportingPlugins.end()) {
                selectedPlugin = *preferred;
            } else {
                setDecoderPreference(extension, "");
            }
        }

        if (!selectedPlugin && supportingPlugins.size() > 1) {
            if (!nativeMenu) {
                SDL_LogError(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Could not choose an image decoder because the application menu is unavailable"
                );
                return;
            }

            std::vector<std::string> labels;
            labels.reserve(supportingPlugins.size());
            for (const LoadedPlugin* plugin : supportingPlugins) {
                std::string label = plugin->name;
                const std::string identifier = pluginIdentifier(*plugin);
                if (std::count_if(
                        supportingPlugins.begin(),
                        supportingPlugins.end(),
                        [&](const LoadedPlugin* candidate) {
                            return candidate->name == plugin->name;
                        }
                    ) > 1) {
                    label += " (" + identifier + ")";
                }
                labels.push_back(std::move(label));
            }

            const std::optional<std::size_t> choice = nativeMenu->chooseOption(labels);
            if (!choice || *choice >= supportingPlugins.size()) return;
            selectedPlugin = supportingPlugins[*choice];
            setDecoderPreference(extension, pluginIdentifier(*selectedPlugin));
        } else if (!selectedPlugin) {
            selectedPlugin = supportingPlugins.front();
        }
    }

    if (!app->ui().viewerRenderer()) {
        std::cerr << "No renderer available.\n";
        return;
    }

    PluginAnimation nextAnimation;
    DecodedImage firstFrame;
    std::uint32_t firstFrameDurationMs = 0;
    bool loaded = imageFromArchive && createTextureFromImage(archiveImage);
    if (!imageFromArchive
    &&  pm.openAnimation(imagePath, nextAnimation, selectedPlugin)
    &&  pm.decodeAnimationFrame(nextAnimation, 0, firstFrame, firstFrameDurationMs)
    &&  createTextureFromImage(firstFrame)
    ) {
        clearAnimation();

        animation = nextAnimation;
        nextAnimation = {};
        animationFrame = 0;
        animationLoop = 0;
        animationFrameDurationMs = std::max(firstFrameDurationMs, 10u);
        animationElapsedMs = 0.0;
        animationPlaying = true;

        currentImageMetadata.clear();
        app->ui().resizeViewer(
            firstFrame.width, firstFrame.height + ViewerTitleBarHeight
        );

        loaded = true;
    }

    pm.closeAnimation(nextAnimation);
    if (!imageFromArchive && !loaded && loadStaticImage(imagePath, selectedPlugin)) {
        clearAnimation();
        loaded = true;
    }

    if (!loaded) {
        std::cerr
        <<  "Could not load image: "
        <<  imagePath
        <<  "\n";

        return;
    }

    if (imageFromArchive) {
        clearAnimation();
        currentImage = std::move(archiveImage);
        currentImageMetadata = currentImage.metadata;

        app->ui().resizeViewer(
            currentImage.width,
            currentImage.height + ViewerTitleBarHeight
        );
    }

    zoom = viewerSettings.resetZoomOnOpen ? 1.0f : zoom;
    rotationDegrees = 0.0;
    imageFlipX = false;
    imageFlipY = false;

    currentImagePath = imagePath;
    rememberFile(imagePath);

    rebuildRect();
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(app->ui().viewerWindow(), &windowWidth, &windowHeight);
    imageX  = (static_cast<float>(windowWidth) - destRect.w) * 0.5f;
    imageY  = ViewerTitleBarHeight
            + (static_cast<float>(std::max(0, windowHeight - ViewerTitleBarHeight))
            - destRect.h) * 0.5f;
    destRect.x = imageX;
    destRect.y = imageY;

    app->ui().setViewerVisible(true);
}

void Viewer::closeImage() {
    clearAnimation();
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }

    currentImage = {};
    currentImagePath.clear();
    currentImageMetadata.clear();
    app->ui().setViewerVisible(false);
}

bool Viewer::hasImage() const {
    return texture != nullptr;
}

void Viewer::saveImageAs() {
	if (currentImage.pixels.empty()) return;

    std::filesystem::path outputPath(currentImagePath);
    outputPath.replace_extension(".png");

	SDL_ShowSaveFileDialog(
		[](void* userdata, const char* const* filelist, int) {
			auto* viewer = static_cast<Viewer*>(userdata);

			if (!filelist || !filelist[0]) return;

			std::filesystem::path path(filelist[0]);

			if (path.extension().empty()) {
				path.replace_extension(".png");
			}

			const DecodedImage& image = viewer->currentImage;

			const int result = stbi_write_png(
				path.string().c_str(),
				image.width,
				image.height,
				4,
				image.pixels.data(),
				image.width * 4
			);

			if (!result) {
				SDL_ShowSimpleMessageBox(
					SDL_MESSAGEBOX_ERROR,
					"Save failed",
					"Could not save the image",
					viewer->app->ui().viewerWindow()
				);
			}
		},
		this,
		app->ui().viewerWindow(),
		nullptr,
		0,
		outputPath.string().c_str()
	);
}

void Viewer::zoomIn() {
    zoomBy(zoomFactor);
}

void Viewer::zoomOut() {
    zoomBy(1.0f / zoomFactor);
}

void Viewer::flipX() {
    imageFlipX =! imageFlipX;
}

void Viewer::flipY() {
    imageFlipY =! imageFlipY;
}

void Viewer::fitToWindow() {
    resetView();
}

void Viewer::rotateLeft() {
    if (!texture) return;

    rotationDegrees -= 90.0;
    if (rotationDegrees < 0.0) rotationDegrees += 360.0;
    rebuildRect();
}

void Viewer::rotateRight() {
    if (!texture) return;

    rotationDegrees += 90.0;
    if (rotationDegrees >= 360.0) rotationDegrees -= 360.0;
    rebuildRect();
}

void Viewer::showInformation() const {
    if (!texture) return;

    float width = 0.0f;
    float height = 0.0f;
    SDL_GetTextureSize(texture, &width, &height);

    std::ostringstream message;
    message 
    <<  currentImagePath << "\n"
    <<  static_cast<int>(width) << " x "
    <<  static_cast<int>(height) << " pixels";

    if (!currentImageMetadata.empty()) {
        message << "\n" << currentImageMetadata;
    }

    // TODO: this should be an actual dialog, not a message
    if (!SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "Image information",
        message.str().c_str(),
        app->ui().toolbarWindow()
    )) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not show image information: %s", SDL_GetError());
    }
}

void Viewer::openFileDialog() {
    dialogImageExtensions = pm.getFileDialogExtensions();
    dialogFilters[0] = {
        "Images",
        dialogImageExtensions.empty() ? "*" : dialogImageExtensions.c_str()
    };

    dialogFilters[1] = {"All files", "*"};
    SDL_ShowOpenFileDialog(
        Viewer::openFileCallback,
        this,
        app->ui().viewerWindow(),
        dialogFilters,
        2,
        nullptr,
        false
    );
}

const std::vector<std::string>& Viewer::getRecentFiles() const {
    return recentFiles;
}

void Viewer::setNativeMenu(NativeMenu* menu) {
    nativeMenu = menu;
}

std::map<std::string, std::vector<LoadedPlugin*>> Viewer::getDecoderOptions() {
    return pm.getDeclaredPluginsByExtension();
}

std::string Viewer::decoderPreference(const std::string& extension) const {
    const auto preference = decoderPreferences.find(normalizeExtension(extension));

    return preference == decoderPreferences.end() ? std::string{} : preference->second;
}

void Viewer::setDecoderPreference(const std::string& extension, const std::string& pluginId) {
    const std::string normalized = normalizeExtension(extension);
    if (normalized.empty()) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Cannot save a decoder preference without a file extension"
        );

        return;
    }

    if (pluginId.empty()) {
        decoderPreferences.erase(normalized);
    } else {
        decoderPreferences[normalized] = pluginId;
    }

    saveSettings();
}

const Viewer::Settings& Viewer::settings() const {
    return viewerSettings;
}

void Viewer::setInterpolation(Interpolation interpolation) {
    if (viewerSettings.interpolation == interpolation) return;
    viewerSettings.interpolation = interpolation;
    if (texture) {
        const SDL_ScaleMode mode
        =   interpolation == Interpolation::Nearest
        ?   SDL_SCALEMODE_NEAREST
        :   SDL_SCALEMODE_LINEAR;
        
        if (!SDL_SetTextureScaleMode(texture, mode)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "Could not update image interpolation: %s",
                SDL_GetError()
            );
        }
    }

    saveSettings();
}

void Viewer::setBackground(Background background) {
    if (viewerSettings.background == background) return;
    viewerSettings.background = background;
    saveSettings();
}

void Viewer::setZoomToCursor(bool enabled) {
    if (viewerSettings.zoomToCursor == enabled) return;
    viewerSettings.zoomToCursor = enabled;
    saveSettings();
}

void Viewer::setResetZoomOnOpen(bool enabled) {
    if (viewerSettings.resetZoomOnOpen == enabled) return;
    viewerSettings.resetZoomOnOpen = enabled;
    saveSettings();
}

void Viewer::setToolbarOnRight(bool enabled) {
    if (viewerSettings.toolbarOnRight == enabled) {
        app->ui().setToolbarOnRight(enabled);
        return;
    }

    viewerSettings.toolbarOnRight = enabled;
    app->ui().setToolbarOnRight(enabled);
    saveSettings();
}

void Viewer::loadSettings() {
    char* preferencePath = SDL_GetPrefPath("openSusie", "openSusie");
    if (!preferencePath) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not locate settings directory: %s",
            SDL_GetError()
        );
        return;
    }

    settingsPath = std::filesystem::path(preferencePath) / "settings.ini";
    SDL_free(preferencePath);

    std::filesystem::path settingsFileToLoad = settingsPath;
    std::error_code fileError;
    bool settingsExist = std::filesystem::exists(settingsFileToLoad, fileError);

    if (fileError) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not check settings file: %s",
            fileError.message().c_str()
        );
        return;
    }

    if (!settingsExist) {
        return;
    }

    try {
        const inih::INIReader config(settingsFileToLoad.string());
        const std::string interpolation = config.Get("viewer", "interpolation", std::string("linear"));
        if (interpolation == "nearest") {
            viewerSettings.interpolation = Interpolation::Nearest;
        } else if (interpolation == "linear") {
            viewerSettings.interpolation = Interpolation::Linear;
        } else {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Unknown image interpolation setting '%s'; using linear",
                interpolation.c_str()
            );
        }

        const std::string background = config.Get("viewer", "background", std::string("dark"));
        if (background == "light") {
            viewerSettings.background = Background::Light;
        } else if (background == "checkerboard") {
            viewerSettings.background = Background::Checkerboard;
        } else if (background != "dark") {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Unknown image background setting '%s'; using dark",
                background.c_str()
            );
        }

        viewerSettings.zoomToCursor
        = config.Get<bool>("viewer", "zoom_to_cursor", true);

        viewerSettings.resetZoomOnOpen
        = config.Get<bool>("viewer", "reset_zoom_on_open", true);

        viewerSettings.toolbarOnRight
        = config.Get<bool>("viewer", "toolbar_on_right", false);

        if (config.Sections().count("decoders") != 0) {
            for (const auto& [extension, pluginId] : config.Get("decoders")) {
                const std::string normalized = normalizeExtension(extension);

                if (!normalized.empty() && !pluginId.empty()) {
                    decoderPreferences[normalized] = pluginId;
                }
            }
        }

    } catch (const std::exception& error) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not load settings from %s: %s",
            settingsFileToLoad.string().c_str(),
            error.what()
        );
    }

    app->ui().setToolbarOnRight(viewerSettings.toolbarOnRight);
}

void Viewer::saveSettings() const {
    if (settingsPath.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Settings file path is unavailable");
        return;
    }

    try {
        inih::INIReader config;
        config.InsertEntry(
            "viewer",
            "interpolation",
            viewerSettings.interpolation == Interpolation::Nearest
                ? "nearest"
                : "linear"
        );

        const char* background = "dark";
        if (viewerSettings.background == Background::Light) {
            background = "light";
        } else if (viewerSettings.background == Background::Checkerboard) {
            background = "checkerboard";
        }

        config.InsertEntry("viewer", "background", background);
        config.InsertEntry("viewer", "zoom_to_cursor", viewerSettings.zoomToCursor);
        config.InsertEntry(
            "viewer",
            "reset_zoom_on_open",
            viewerSettings.resetZoomOnOpen
        );

        config.InsertEntry("viewer", "toolbar_on_right", viewerSettings.toolbarOnRight);
        for (const auto& [extension, pluginId] : decoderPreferences) {
            config.InsertEntry("decoders", extension, pluginId);
        }

        inih::INIWriter::write(settingsPath.string(), config, true);

    } catch (const std::exception& error) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not save settings to %s: %s",
            settingsPath.string().c_str(),
            error.what()
        );
    }
}

void SDLCALL Viewer::openFileCallback(void* userdata, const char* const* filelist, int) {
    auto* viewer = static_cast<Viewer*>(userdata);
    if (!filelist) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Open file dialog failed: %s", SDL_GetError());
        return;
    }

    if (!filelist[0]) return;

    SDL_Event event{};
    event.type = SDL_EVENT_USER;
    event.user.windowID = SDL_GetWindowID(viewer->app->ui().viewerWindow());
    event.user.code = OpenFileEventCode;
    event.user.data1 = new std::string(filelist[0]);
    if (!SDL_PushEvent(&event)) {
        delete static_cast<std::string*>(event.user.data1);

        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not queue selected image: %s", SDL_GetError());
    }
}

void Viewer::rememberFile(const std::string& imagePath) {
    // TODO: save these to the disk

    recentFiles.erase(
        std::remove(recentFiles.begin(), recentFiles.end(), imagePath),
        recentFiles.end()
    );

    recentFiles.insert(recentFiles.begin(), imagePath);
    if (recentFiles.size() > 10) recentFiles.resize(10);
}

bool Viewer::loadStaticImage(const std::string& imagePath, LoadedPlugin* plugin) {
    DecodedImage image;
    if (!pm.decodeImage(imagePath, image, plugin)) return false;
    if (!createTextureFromImage(image)) return false;

    currentImage = std::move(image);
	currentImageMetadata = currentImage.metadata;

	app->ui().resizeViewer(currentImage.width, currentImage.height + ViewerTitleBarHeight);

    return true;
}

void Viewer::clearAnimation() {
    pm.closeAnimation(animation);
    animationFrame = 0;
    animationLoop = 0;
    animationFrameDurationMs = 100;
    animationElapsedMs = 0.0;
    animationPlaying = false;
}

bool Viewer::createTextureFromImage(DecodedImage& image) {
    if (image.pixels.empty()
    ||  image.width <= 0
    ||  image.height <= 0
    ||  image.width > std::numeric_limits<int>::max() / 4
    ) {
        return false;
    }

    SDL_Renderer* renderer = app->ui().viewerRenderer();

    if (!renderer) {
        return false;
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        image.width,
        image.height,
        SDL_PIXELFORMAT_RGBA32,
        image.pixels.data(),
        image.width * 4
    );

    if (!surface) {
        std::cerr
        <<  "Could not create SDL surface: "
        <<  SDL_GetError()
        <<  "\n";

        return false;
    }

    SDL_Texture* newTexture = SDL_CreateTextureFromSurface(
        renderer,
        surface
    );

    SDL_DestroySurface(surface);

    if (!newTexture) {
        std::cerr
        <<  "Could not create SDL texture: "
        <<  SDL_GetError()
        <<  "\n";

        return false;
    }

    const SDL_ScaleMode scaleMode =
        viewerSettings.interpolation == Interpolation::Nearest
            ? SDL_SCALEMODE_NEAREST
            : SDL_SCALEMODE_LINEAR;
    if (!SDL_SetTextureScaleMode(newTexture, scaleMode)) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not set image interpolation: %s",
            SDL_GetError()
        );
        
        SDL_DestroyTexture(newTexture);
        return false;
    }

    if (texture) SDL_DestroyTexture(texture);
    
    texture = newTexture;

    return true;
}

void Viewer::event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_USER && event.user.code == OpenFileEventCode) {
        std::unique_ptr<std::string> path(static_cast<std::string*>(event.user.data1));
        present(*path);
        return;
    }

    titlebar.handleEvent(app->ui().viewerWindow(), event, app->ui());

    int titlebarX = 0;
    int titlebarY = 0;
    switch (titlebar.consumeAction(&titlebarX, &titlebarY)) {
        case WindowTitlebar::Action::Close:
            closeImage();

            app->ui().raiseWindow(app->ui().toolbarWindow());
            return;

        case WindowTitlebar::Action::Minimize:
            app->ui().minimizeWindows();
            return;

        case WindowTitlebar::Action::ToggleMaximize:
            app->ui().toggleViewerMaximize();
            return;

        case WindowTitlebar::Action::ShowMenu:
            nativeMenu->show(app->ui().viewerWindow(), mouseX, mouseY);
            return;

        case WindowTitlebar::Action::None:
            break;
    }

    if ((event.type == SDL_EVENT_WINDOW_RESIZED
    ||  event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    &&  event.window.windowID != SDL_GetWindowID(app->ui().viewerWindow())
    ) return;
    
    if ((event.type == SDL_EVENT_MOUSE_MOTION
    ||  event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
    ||  event.type == SDL_EVENT_MOUSE_BUTTON_UP
    ||  event.type == SDL_EVENT_MOUSE_WHEEL
    ||  event.type == SDL_EVENT_DROP_BEGIN
    ||  event.type == SDL_EVENT_DROP_FILE
    ||  event.type == SDL_EVENT_DROP_COMPLETE
    )   && event.motion.windowID != SDL_GetWindowID(app->ui().viewerWindow())
    ) return;

    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            mouseX = event.motion.x;
            mouseY = event.motion.y;

            if (dragging) {
                float dx = mouseX - dragStartMouseX;
                float dy = mouseY - dragStartMouseY;

                imageX = dragStartImageX + dx;
                imageY = dragStartImageY + dy;

                destRect.x = imageX;
                destRect.y = imageY;
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                if (event.button.y < ViewerTitleBarHeight) break;

                dragging = true;

                dragStartMouseX = event.button.x;
                dragStartMouseY = event.button.y;

                dragStartImageX = imageX;

                dragStartImageY = imageY;
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                dragging = false;
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            handleZoom(event);
            break;

        case SDL_EVENT_DROP_BEGIN:

            // show feedback for dragndrop
            
            break;

        case SDL_EVENT_DROP_FILE: 
            if (event.drop.data != NULL) {
                std::string dropped_file(event.drop.data);

                present(dropped_file);
            }
            break;

        case SDL_EVENT_DROP_COMPLETE:

            break;

        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            rebuildRect();

            break;
    }
}

void Viewer::update(double deltaTime) {
    if (viewerSettings.toolbarOnRight != app->ui().isToolbarOnRight()) {
        viewerSettings.toolbarOnRight = app->ui().isToolbarOnRight();
        saveSettings();
    }

    if (!animationPlaying || !animation.handle || deltaTime <= 0.0) return;

    animationElapsedMs += deltaTime * 1000.0;
    DecodedImage nextFrame;
    std::uint32_t nextDurationMs = 0;
    std::uint32_t steps = 0;
    bool frameChanged = false;
    while (animationElapsedMs >= animationFrameDurationMs && steps < 10000) {
        animationElapsedMs -= animationFrameDurationMs;
        ++steps;

        std::uint32_t nextIndex = animationFrame + 1;
        if (nextIndex >= animation.frameCount) {
            ++animationLoop;
            if (animation.loopCount != 0 && animationLoop >= animation.loopCount) {
                animationPlaying = false;
                animationElapsedMs = 0.0;
                break;
            }
            nextIndex = 0;
        }

        if (!pm.decodeAnimationFrame(
                animation, nextIndex, nextFrame, nextDurationMs
            )) {
            clearAnimation();
            return;
        }
        animationFrame = nextIndex;
        animationFrameDurationMs = std::max(nextDurationMs, 10u);
        frameChanged = true;
    }

    if (frameChanged && !createTextureFromImage(nextFrame)) {
        clearAnimation();
    }
}

Sint32 Viewer::millisecondsUntilUpdate() const {
    if (!animationPlaying || !animation.handle) return -1;

    const double remainingMs = animationFrameDurationMs - animationElapsedMs;
    const double roundedUpMs = std::ceil(std::max(remainingMs, 0.0));
    return static_cast<Sint32>(std::min(
        roundedUpMs,
        static_cast<double>(std::numeric_limits<Sint32>::max())
    ));
}

void Viewer::draw() {
    SDL_Renderer* renderer = app->ui().viewerRenderer();
    if (!renderer) return;

    std::string title;
    if (texture) {
        const std::filesystem::path imagePath(currentImagePath);
        float width = 0.0f;
        float height = 0.0f;
        SDL_GetTextureSize(texture, &width, &height);

        title 
        =   imagePath.filename().string() + "  "
        +   std::to_string(static_cast<int>(width)) 
        + " x " // by
        +   std::to_string(static_cast<int>(height));
    }

    if (texture) {
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(app->ui().viewerWindow(), &windowWidth, &windowHeight);
        const SDL_Rect imageArea{
            0,
            ViewerTitleBarHeight,
            windowWidth,
            std::max(0, windowHeight - ViewerTitleBarHeight)
        };

        SDL_SetRenderClipRect(renderer, &imageArea);
        if (viewerSettings.background == Background::Light) {
            SDL_SetRenderDrawColor(renderer, 224, 224, 224, 255);
            SDL_RenderFillRect(renderer, nullptr);

        } else if (viewerSettings.background == Background::Checkerboard) {
            constexpr int checkerSize = 16;
            for (int y = imageArea.y; y < imageArea.y + imageArea.h; y += checkerSize) {
                for (int x = 0; x < imageArea.w; x += checkerSize) {
                    const bool lightSquare = ((x / checkerSize) + (y / checkerSize)) % 2 == 0;
                    SDL_SetRenderDrawColor(
                        renderer,
                        lightSquare ? 112 : 72,
                        lightSquare ? 112 : 72,
                        lightSquare ? 112 : 72,
                        255
                    );

                    const SDL_FRect checker{
                        static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(checkerSize),
                        static_cast<float>(checkerSize)
                    };

                    SDL_RenderFillRect(renderer, &checker);
                }
            }
        } else {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(renderer, nullptr);
        }

        SDL_FlipMode flipmode = SDL_FLIP_NONE;

        if (imageFlipX && imageFlipY) {
            flipmode = SDL_FLIP_HORIZONTAL_AND_VERTICAL;
        } else if (imageFlipX) {
            flipmode = SDL_FLIP_HORIZONTAL;
        } else if (imageFlipY) {
            flipmode = SDL_FLIP_VERTICAL;
        }

        SDL_RenderTextureRotated(
            renderer,
            texture,
            nullptr,
            &destRect,
            rotationDegrees,
            nullptr,
            flipmode
        );

        SDL_SetRenderClipRect(renderer, nullptr);
    }
    
    titlebar.draw(
        app->ui().viewerWindow(),
        app->ui().viewerRenderer(),
        title
    );
}

void Viewer::loadPlugins(const std::string& dirPath) {
    pm.loadPluginsFromFolder(dirPath);
    const std::vector<std::string>& errors = pm.getPluginLoadErrors();
    if (errors.empty()) return;

    std::ostringstream message;
    message << "Some image plugins could not be loaded:\n\n";
    for (const std::string& error : errors) {
        message << "- " << error << '\n';
    }

    if (!SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_WARNING,
            "Plugin loading warning",
            message.str().c_str(),
            app->ui().toolbarWindow()
        )
    ) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not show plugin loading warning: %s",
            SDL_GetError()
        );
    }
}

void Viewer::rebuildRect() {
    if (!texture) return;

    float imgWidth = 0.0f;
    float imgHeight = 0.0f;
    SDL_GetTextureSize(
        texture,
        &imgWidth,
        &imgHeight
    );

    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(
        app->ui().viewerWindow(),
        &windowWidth,
        &windowHeight
    );

    const bool quarterTurn = static_cast<int>(rotationDegrees) % 180 != 0;
    const float displayWidth = quarterTurn ? imgHeight : imgWidth;
    const float displayHeight = quarterTurn ? imgWidth : imgHeight;

    float scaleX = static_cast<float>(windowWidth) / displayWidth;
    const int imageAreaHeight = std::max(0, windowHeight - ViewerTitleBarHeight);
    float scaleY = static_cast<float>(imageAreaHeight) / displayHeight;

    fitScale = std::min(scaleX, scaleY);

    float scale = fitScale * zoom;

    destRect.w = imgWidth * scale;
    destRect.h = imgHeight * scale;

    if (zoom == 1.0f) {
        imageX = (static_cast<float>(windowWidth) - destRect.w) * 0.5f;
        imageY = ViewerTitleBarHeight
            + (static_cast<float>(imageAreaHeight) - destRect.h) * 0.5f;
    }

    destRect.x = imageX;
    destRect.y = imageY;
}

void Viewer::handleZoom(const SDL_Event& event) {
    if (!texture) return;

    if (event.wheel.y > 0) {
        zoomBy(zoomFactor);
    } else if (event.wheel.y < 0) {
        zoomBy(1.0f / zoomFactor);
    }
}

void Viewer::zoomBy(float factor) {
    if (!texture) return;

    float oldZoom = zoom;
    zoom = std::clamp(zoom * factor, minZoom, maxZoom);

    if (zoom == oldZoom) return;

    float anchorX = mouseX;
    float anchorY = mouseY;
    if (!viewerSettings.zoomToCursor
    ||  SDL_GetMouseFocus() != app->ui().viewerWindow()
    ) {
        anchorX = destRect.x + destRect.w * 0.5f;
        anchorY = destRect.y + destRect.h * 0.5f;
    }

    float relativeX = (anchorX - destRect.x) / destRect.w;
    float relativeY = (anchorY - destRect.y) / destRect.h;

    rebuildRect();

    imageX = anchorX - relativeX * destRect.w;
    imageY = anchorY - relativeY * destRect.h;

    destRect.x = imageX;
    destRect.y = imageY;
}

void Viewer::resetView() {
    zoom = 1.0f;

    rebuildRect();
}