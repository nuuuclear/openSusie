#include "NativeMenuPlatform.h"

#include <gtk/gtk.h>

#include <functional>

namespace {
struct Activation {
    const std::function<void(int)>* callback;
    int command;
};

struct PopupState {
    GMainLoop* loop;
    bool done = false;
};

void activateItem(GtkMenuItem*, gpointer userData) {
    const auto* activation = static_cast<Activation*>(userData);
    (*activation->callback)(activation->command);
}

void destroyActivation(gpointer userData, GClosure*) {
    delete static_cast<Activation*>(userData);
}

void selectionDone(GtkMenuShell*, gpointer userData) {
    auto* state = static_cast<PopupState*>(userData);
    state->done = true;
    if (g_main_loop_is_running(state->loop)) {
        g_main_loop_quit(state->loop);
    }
}

GtkWidget* createMenu(const std::vector<NativeMenuItem>& items,
                      const std::function<void(int)>& callback) {
    GtkWidget* menu = gtk_menu_new();
    GSList* radioGroup = nullptr;

    for (const NativeMenuItem& item : items) {
        if (item.separator) {
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
            radioGroup = nullptr;
            continue;
        }

        GtkWidget* menuItem = nullptr;
        if (!item.children.empty()) {
            menuItem = gtk_menu_item_new_with_label(item.label.c_str());
            gtk_menu_item_set_submenu(
                GTK_MENU_ITEM(menuItem),
                createMenu(item.children, callback)
            );
        } else if (item.radio) {
            menuItem = gtk_radio_menu_item_new_with_label(radioGroup, item.label.c_str());
            radioGroup = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuItem));
            gtk_check_menu_item_set_active(
                GTK_CHECK_MENU_ITEM(menuItem),
                item.checked
            );
        } else if (item.checkable) {
            menuItem = gtk_check_menu_item_new_with_label(item.label.c_str());
            gtk_check_menu_item_set_active(
                GTK_CHECK_MENU_ITEM(menuItem),
                item.checked
            );
            radioGroup = nullptr;
        } else {
            menuItem = gtk_menu_item_new_with_label(item.label.c_str());
            radioGroup = nullptr;
        }

        gtk_widget_set_sensitive(menuItem, item.enabled);
        if (item.command != 0) {
            auto* activation = new Activation{&callback, item.command};
            g_signal_connect_data(
                menuItem,
                "activate",
                G_CALLBACK(activateItem),
                activation,
                destroyActivation,
                GConnectFlags(0)
            );
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuItem);
    }

    return menu;
}
}

bool showPlatformMenu(
    SDL_Window*,
    int,
    int,
    const std::vector<NativeMenuItem>& items,
    const std::function<void(int)>& activate
) {
    static bool initialized = false;
    if (!initialized) {
        if (!gtk_init_check(nullptr, nullptr)) {
            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "Could not initialize GTK for native menus"
            );
            return false;
        }
        initialized = true;
    }

    GtkWidget* menu = createMenu(items, activate);
    PopupState state{g_main_loop_new(nullptr, FALSE)};
    g_signal_connect(menu, "selection-done", G_CALLBACK(selectionDone), &state);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
    if (!state.done) {
        g_main_loop_run(state.loop);
    }

    gtk_widget_destroy(menu);
    g_main_loop_unref(state.loop);
    return true;
}
