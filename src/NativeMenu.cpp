#include "NativeMenu.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "App.h"
#include "NativeMenuPlatform.h"
#include "Paths.h"
#include "Viewer.h"

namespace {
bool checkedAction(
    const std::string& action,
    const Viewer::Settings& settings,
    const Viewer& viewer
) {
    if (action == "interpolation.nearest") {
        return settings.interpolation == Viewer::Interpolation::Nearest;
    }
    if (action == "interpolation.linear") {
        return settings.interpolation == Viewer::Interpolation::Linear;
    }
    if (action == "background.dark") {
        return settings.background == Viewer::Background::Dark;
    }
    if (action == "background.light") {
        return settings.background == Viewer::Background::Light;
    }
    if (action == "background.checkerboard") {
        return settings.background == Viewer::Background::Checkerboard;
    }
    if (action == "toggle.zoom_to_cursor") return settings.zoomToCursor;
    if (action == "toggle.reset_zoom_on_open") return settings.resetZoomOnOpen;
    if (action == "toolbar.left") return !settings.toolbarOnRight;
    if (action == "toolbar.right") return settings.toolbarOnRight;
    constexpr char decoderPrefix[] = "decoder.select:";
    if (action.rfind(decoderPrefix, 0) == 0) {
        const std::string selection = action.substr(sizeof(decoderPrefix) - 1);
        const size_t separator = selection.find(':');
        if (separator == std::string::npos) return false;
        const std::string extension = selection.substr(0, separator);
        const std::string pluginId = selection.substr(separator + 1);
        return viewer.decoderPreference(extension) == pluginId;
    }
    return false;
}

bool requiresImage(const std::string& action) {
    return action == "close"
        || action == "rotate_left"
        || action == "rotate_right"
        || action == "zoom_in"
        || action == "zoom_out"
        || action == "information"
        || action == "fit";
}
}

NativeMenu::NativeMenu(App* parentApp, Viewer* imageViewer)
    : app(parentApp), viewer(imageViewer) {
    loadItems();
}

void NativeMenu::loadItems() {
    const std::string menuPath =
        app->getFileSystem().resolve(Paths::Resources, "menu.json").string();
    size_t fileSize = 0;
    void* fileData = SDL_LoadFile(menuPath.c_str(), &fileSize);
    if (!fileData) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not load menu configuration '%s': %s",
            menuPath.c_str(),
            SDL_GetError()
        );
        return;
    }

    try {
        const auto* data = static_cast<const char*>(fileData);
        const nlohmann::json root = nlohmann::json::parse(data, data + fileSize);
        if (!root.is_object() || !root.contains("items")) {
            throw std::runtime_error("menu configuration requires an 'items' array");
        }

        std::function<std::vector<NativeMenuItem>(const nlohmann::json&)> parseItems;
        parseItems = [&parseItems](const nlohmann::json& entries) {
            if (!entries.is_array()) {
                throw std::runtime_error("'items' must be an array");
            }

            std::vector<NativeMenuItem> result;
            result.reserve(entries.size());
            for (const nlohmann::json& entry : entries) {
                if (!entry.is_object()) {
                    throw std::runtime_error("menu entries must be objects");
                }

                NativeMenuItem item;
                item.label = entry.value("label", std::string{});
                item.action = entry.value("action", std::string{});
                item.enabled = entry.value("enabled", true);
                const std::string type = entry.value("type", std::string{});
                if (type == "separator") {
                    item.separator = true;
                    if (!item.label.empty() || !item.action.empty() || entry.contains("items")) {
                        throw std::runtime_error("separator entries cannot have labels, actions, or children");
                    }
                } else {
                    if (!type.empty() && type != "radio" && type != "checkbox") {
                        throw std::runtime_error("unsupported menu entry type '" + type + "'");
                    }
                    if (item.label.empty()) {
                        throw std::runtime_error("menu entry labels must not be empty");
                    }
                    if (entry.contains("items")) {
                        item.children = parseItems(entry.at("items"));
                    }
                    if (item.action.empty() && item.children.empty()) {
                        throw std::runtime_error("menu entries need an action or child items");
                    }
                    if (!item.action.empty() && !item.children.empty()) {
                        throw std::runtime_error("menu entries cannot have both an action and child items");
                    }
                    if ((type == "radio" || type == "checkbox") && item.action.empty()) {
                        throw std::runtime_error("checkable menu entries need an action");
                    }
                    item.radio = type == "radio";
                    item.checkable = item.radio || type == "checkbox";
                }
                result.push_back(std::move(item));
            }
            return result;
        };

        menuItems = parseItems(root.at("items"));
        if (menuItems.empty()) {
            throw std::runtime_error("menu configuration has no entries");
        }
    } catch (const std::exception& error) {
        menuItems.clear();
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not parse menu configuration '%s': %s",
            menuPath.c_str(),
            error.what()
        );
    }
    SDL_free(fileData);
}

void NativeMenu::show(SDL_Window* parent, int x, int y) {
    if (!parent) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not display the native menu without a parent window"
        );
        return;
    }

    if (menuItems.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Menu configuration has no usable entries");
        return;
    }

    recentFiles = viewer->getRecentFiles();
    commandActions.clear();
    const std::vector<NativeMenuItem> items = buildItems();
    const bool shown = showPlatformMenu(
        parent,
        x,
        y,
        items,
        [this](int id) { activate(id); }
    );
    if (!shown) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not display the native application menu"
        );
    }
}

std::optional<std::size_t> NativeMenu::chooseOption(
    const std::vector<std::string>& labels
) {
    if (labels.empty()) return std::nullopt;
    SDL_Window* parent = app->ui().toolbarWindow();
    if (!parent) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not display the decoder menu without a parent window"
        );
        return std::nullopt;
    }

    std::vector<NativeMenuItem> items;
    items.reserve(labels.size());
    for (std::size_t i = 0; i < labels.size(); ++i) {
        NativeMenuItem item;
        item.label = labels[i];
        item.command = static_cast<int>(i + 1);
        items.push_back(std::move(item));
    }

    std::optional<std::size_t> selected;
    if (!showPlatformMenu(
            parent,
            0,
            0,
            items,
            [&selected](int command) {
                if (command > 0) selected = static_cast<std::size_t>(command - 1);
            }
        )) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not display the decoder selection menu"
        );
    }
    return selected;
}

std::vector<NativeMenuItem> NativeMenu::buildItems() {
    const Viewer::Settings& settings = viewer->settings();
    const bool hasImage = viewer->hasImage();

    std::function<std::vector<NativeMenuItem>(const std::vector<NativeMenuItem>&)> build;
    build = [this, &settings, hasImage, &build](const std::vector<NativeMenuItem>& source) {
        std::vector<NativeMenuItem> result;
        result.reserve(source.size());
        for (const NativeMenuItem& sourceItem : source) {
            NativeMenuItem item = sourceItem;
            item.children.clear();

            if (sourceItem.action == "decoder.preferences") {
                item.children = build(buildDecoderPreferences());
                item.action.clear();
            } else if (sourceItem.action == "recent") {
                if (recentFiles.empty()) {
                    NativeMenuItem empty;
                    empty.label = "(No recent files)";
                    empty.enabled = false;
                    item.children.push_back(std::move(empty));
                } else {
                    for (size_t i = 0; i < recentFiles.size(); ++i) {
                        NativeMenuItem recent;
                        recent.label = std::filesystem::path(recentFiles[i]).filename().string();
                        recent.action = "recent:" + std::to_string(i);
                        item.children.push_back(std::move(recent));
                    }
                }
                item.action.clear();
            } else if (!sourceItem.children.empty()) {
                item.children = build(sourceItem.children);
            }

            if (item.checkable) {
                item.checked = checkedAction(item.action, settings, *viewer);
            }
            if (requiresImage(item.action)) item.enabled = item.enabled && hasImage;
            if (!item.action.empty()) {
                item.command = static_cast<int>(commandActions.size() + 1);
                commandActions.push_back(item.action);
            }
            result.push_back(std::move(item));
        }
        return result;
    };
    return build(menuItems);
}

std::vector<NativeMenuItem> NativeMenu::buildDecoderPreferences() {
    std::vector<NativeMenuItem> extensions;
    const auto pluginsByExtension = viewer->getDecoderOptions();
    for (const auto& [extension, plugins] : pluginsByExtension) {
        NativeMenuItem format;
        format.label = "." + extension;

        NativeMenuItem askEveryTime;
        askEveryTime.label = "Ask each time";
        askEveryTime.action = "decoder.select:" + extension + ":";
        askEveryTime.checkable = true;
        askEveryTime.radio = true;
        askEveryTime.checked = viewer->decoderPreference(extension).empty();
        format.children.push_back(std::move(askEveryTime));

        for (const LoadedPlugin* plugin : plugins) {
            NativeMenuItem option;
            option.label = plugin->name;
            const std::string pluginId =
                std::filesystem::path(plugin->path).filename().string();
            option.action = "decoder.select:" + extension + ":" + pluginId;
            option.checkable = true;
            option.radio = true;
            if (std::count_if(
                plugins.begin(),
                plugins.end(),
                [&](const LoadedPlugin* candidate) {
                    return candidate->name == plugin->name;
                }
            ) > 1) {
                option.label += " (" + pluginId + ")";
            }
            format.children.push_back(std::move(option));
        }
        extensions.push_back(std::move(format));
    }

    if (extensions.empty()) {
        NativeMenuItem unavailable;
        unavailable.label = "(No decoder plugins available)";
        unavailable.enabled = false;
        extensions.push_back(std::move(unavailable));
    }
    return extensions;
}

void NativeMenu::activate(int id) {
    if (id <= 0 || static_cast<size_t>(id) > commandActions.size()) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Unsupported native menu command %d",
            id
        );
        return;
    }

    const std::string& action = commandActions[static_cast<size_t>(id - 1)];
    constexpr char decoderPrefix[] = "decoder.select:";
    if (action.rfind(decoderPrefix, 0) == 0) {
        const std::string selection = action.substr(sizeof(decoderPrefix) - 1);
        const size_t separator = selection.find(':');
        if (separator == std::string::npos) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "Invalid decoder preference menu action '%s'",
                action.c_str()
            );
            return;
        }
        viewer->setDecoderPreference(
            selection.substr(0, separator),
            selection.substr(separator + 1)
        );
        return;
    }

    if (action.rfind("recent:", 0) == 0) {
        const size_t index = static_cast<size_t>(std::stoul(action.substr(7)));
        if (index < recentFiles.size()) {
            viewer->present(recentFiles[index]);
        }
        return;
    }

    if (action == "open") {
            viewer->openFileDialog();
    } else if (action == "close") {
            viewer->closeImage();
    } else if (action == "rotate_left") {
            viewer->rotateLeft();
    } else if (action == "rotate_right") {
            viewer->rotateRight();
    } else if (action == "zoom_in") {
            viewer->zoomIn();
    } else if (action == "zoom_out") {
            viewer->zoomOut();
    } else if (action == "information") {
            viewer->showInformation();
    } else if (action == "fit") {
            viewer->fitToWindow();
    } else if (action == "quit") {
            app->quit();
    } else if (action == "interpolation.nearest") {
            viewer->setInterpolation(Viewer::Interpolation::Nearest);
    } else if (action == "interpolation.linear") {
            viewer->setInterpolation(Viewer::Interpolation::Linear);
    } else if (action == "background.dark") {
            viewer->setBackground(Viewer::Background::Dark);
    } else if (action == "background.light") {
            viewer->setBackground(Viewer::Background::Light);
    } else if (action == "background.checkerboard") {
            viewer->setBackground(Viewer::Background::Checkerboard);
    } else if (action == "toggle.zoom_to_cursor") {
            viewer->setZoomToCursor(!viewer->settings().zoomToCursor);
    } else if (action == "toggle.reset_zoom_on_open") {
            viewer->setResetZoomOnOpen(!viewer->settings().resetZoomOnOpen);
    } else if (action == "toolbar.left") {
            viewer->setToolbarOnRight(false);
    } else if (action == "toolbar.right") {
            viewer->setToolbarOnRight(true);
    } else {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Unsupported menu action '%s'",
            action.c_str()
        );
    }
}
