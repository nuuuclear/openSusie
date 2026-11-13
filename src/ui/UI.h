#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

class FileSystem;

inline constexpr int ToolbarTitleBarHeight = 12;
inline constexpr int ViewerTitleBarHeight = 18;

class UI {
public:
	enum class WindowRole {
		Viewer,
		Toolbar,
		Player,
		Other
	};

	struct WindowData {
		WindowRole role = WindowRole::Other;
		SDL_Window* window = nullptr;
		SDL_Renderer* renderer = nullptr;

		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;

		bool ownsWindow = true;
		bool ownsRenderer = true;
		bool participatesInSnapping = false;
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
	SDL_Window* playerWindow() const;
	SDL_Renderer* playerRenderer() const;

	SDL_Window* window(WindowRole role) const;
	SDL_Renderer* renderer(WindowRole role) const;

	bool registerWindow(
		WindowRole role,
		SDL_Window* window,
		SDL_Renderer* renderer,
		bool ownsWindow = false,
		bool ownsRenderer = false
	);
	void unregisterWindow(SDL_Window* window);

	const std::vector<WindowData>& windows() const { return windowList; }

	void handleEvent(const SDL_Event& event);

	void beginFrame();
	void present();

	void setViewerVisible(bool visible);
	void minimizeWindows();
	void toggleViewerMaximize();
	void resizeViewer(int width, int height);

	void setToolbarOnRight(bool onRight);
	bool isToolbarOnRight() const { return toolbarOnRight; }

	void moveWindowFromTitlebar(SDL_Window* window, int targetX, int targetY);
	void restoreViewerForTitlebarDrag(
		float globalX,
		float globalY,
		float mouseX,
		float mouseY
	);

	void setWindowPosition(SDL_Window* window, int x, int y);
	void refreshWindowData(SDL_Window* window);

	static SDL_HitTestResult SDLCALL viewerHitTest(
		SDL_Window* window,
		const SDL_Point* area,
		void* data
	);

private:
	std::vector<WindowData> windowList;

	int viewerX = 0;
	int viewerY = 0;
    
	int toolbarX = 0;
	int toolbarY = 0;

	int playerX = 0;
	int playerY = 0;

	bool windowPositionsInitialized = false;
	bool windowsSnapped = true;
	bool toolbarOnRight = false;
	bool toolbarDockedToScreen = false;

	static constexpr int windowSnapDistance = 8;

	WindowData* findWindow(SDL_Window* window);
	const WindowData* findWindow(SDL_Window* window) const;
	WindowData* findWindow(WindowRole role);
	const WindowData* findWindow(WindowRole role) const;

	bool createManagedWindow(
		WindowRole role,
		const std::string& title,
		int width,
		int height,
		SDL_WindowFlags flags
	);

	void destroyManagedWindows();
	void updateWindowData(WindowData& data);
	void updateAllWindowData();

	void syncWindowPositions(const SDL_WindowEvent& event);
	void ensureToolbarVisible();
	void setToolbarAlwaysOnTop(bool alwaysOnTop);

	void setWindowPositionIfChanged(SDL_Window* window, int x, int y);
	void syncSnappedWindowPosition(int viewerX, int viewerY);
	void updateSnappedWindowPosition();

	void snapWindowToScreen(SDL_Window* window, int& x, int& y);
	void snapSnappedWindowsToScreen(int& viewerX, int& viewerY);
	void dockToolbarToNearestScreenSide();

	void handleViewerMaximized();
	void handleViewerRestored();
};