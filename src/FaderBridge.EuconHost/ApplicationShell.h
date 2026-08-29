#pragma once

#include "EuconHost.h"

#include <Windows.h>

#include <array>
#include <memory>

class ApplicationShell final
{
public:
    static int Run(HINSTANCE instance, int showCommand, bool startInBackground);

private:
    explicit ApplicationShell(HINSTANCE instance);
    ~ApplicationShell();

    ApplicationShell(const ApplicationShell&) = delete;
    ApplicationShell& operator=(const ApplicationShell&) = delete;

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message,
        WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateMainWindow(int showCommand, bool startInBackground);
    bool OnCreate();
    void OnAudioFrame(AudioFrame& frame);
    void OnSurfaceChange(SurfaceChange& change);
    void Paint();
    void DrawStartupControl(const DRAWITEMSTRUCT& item) const;
    void DrawTrayStatus(const DRAWITEMSTRUCT& item) const;
    void LayoutControls();
    int ChannelRowsPerPage() const;
    void UpdateChannelPagination();
    void ShowMainWindow();
    void HideMainWindow();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayTooltip();
    void ShowTrayMenu(POINT location);
    void ShowAboutDialog() const;
    void RestartApplication();
    void ExitApplication();
    void OpenDiagnosticsFolder() const;
    void CreateFonts();
    void DestroyFonts();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND startupCheck_ = nullptr;
    HWND hideButton_ = nullptr;
    HWND previousPageButton_ = nullptr;
    HWND nextPageButton_ = nullptr;
    HICON largeIcon_ = nullptr;
    HICON smallIcon_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT headingFont_ = nullptr;
    HFONT bodyFont_ = nullptr;
    HFONT smallFont_ = nullptr;
    std::unique_ptr<EuconHost> host_;
    std::array<AudioStripState, EuconHost::MaxChannelCount> strips_{};
    int activeCount_ = 0;
    int channelPage_ = 0;
    int lastChannel_ = -1;
    int lastKind_ = 0;
    float lastValue_ = 0.0F;
    bool lastCommandSent_ = false;
    bool monoAudioEnabled_ = false;
    bool startupEnabled_ = false;
    bool trayAdded_ = false;
    bool exitRequested_ = false;
    UINT taskbarCreatedMessage_ = 0U;
};
