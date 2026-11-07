#include "NativeMenuPlatform.h"

#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

namespace {
std::wstring toWide(const std::string& value) {
    if (value.empty()) return {};

    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.c_str(),
        -1,
        nullptr,
        0
    );
    if (size <= 0) return {};

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, result.data(), size);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

HMENU createMenu(const std::vector<NativeMenuItem>& items) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return nullptr;

    for (const NativeMenuItem& item : items) {
        if (item.separator) {
            MENUITEMINFOW separator{};
            separator.cbSize = sizeof(separator);
            separator.fMask = MIIM_FTYPE;
            separator.fType = MFT_SEPARATOR;
            if (!InsertMenuItemW(menu, static_cast<UINT>(-1), TRUE, &separator)) {
                DestroyMenu(menu);
                return nullptr;
            }
            continue;
        }

        std::wstring label = toWide(item.label);
        MENUITEMINFOW entry{};
        entry.cbSize = sizeof(entry);
        entry.fMask = MIIM_STRING | MIIM_STATE | MIIM_FTYPE;
        entry.dwTypeData = label.data();
        entry.fState = item.enabled ? MFS_ENABLED : MFS_DISABLED;
        entry.fType = item.radio ? MFT_RADIOCHECK : MFT_STRING;
        if (item.checkable && item.checked) {
            entry.fState |= MFS_CHECKED;
        }

        HMENU child = nullptr;
        if (!item.children.empty()) {
            child = createMenu(item.children);
            if (!child) {
                DestroyMenu(menu);
                return nullptr;
            }
            entry.fMask |= MIIM_SUBMENU;
            entry.hSubMenu = child;
        } else {
            entry.fMask |= MIIM_ID;
            entry.wID = static_cast<UINT>(item.command);
        }

        if (!InsertMenuItemW(menu, static_cast<UINT>(-1), TRUE, &entry)) {
            if (child) DestroyMenu(child);
            DestroyMenu(menu);
            return nullptr;
        }
    }
    return menu;
}
}

bool showPlatformMenu(
    SDL_Window* parent,
    int x,
    int y,
    const std::vector<NativeMenuItem>& items,
    const std::function<void(int)>& activate
) {
    const SDL_PropertiesID properties = SDL_GetWindowProperties(parent);
    HWND window = static_cast<HWND>(
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)
    );
    if (!window) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not access the native Windows window handle: %s",
            SDL_GetError()
        );
        return false;
    }

    HMENU menu = createMenu(items);
    if (!menu) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not create native Windows menu (error %lu)",
            GetLastError()
        );
        return false;
    }

    POINT position{ x, y };
    if (!ClientToScreen(window, &position)) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not position native Windows menu (error %lu)",
            GetLastError()
        );
        DestroyMenu(menu);
        return false;
    }

    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        position.x,
        position.y,
        0,
        window,
        nullptr
    );
    DestroyMenu(menu);
    PostMessageW(window, WM_NULL, 0, 0);
    if (command != 0) activate(static_cast<int>(command));
    return true;
}
