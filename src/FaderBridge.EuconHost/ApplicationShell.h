#pragma once

#include "EuconHost.h"
#include "../BridgeGlobalShortcut.h"

#include <Windows.h>

#include <array>
#include <memory>

class WindowsMediaObserver;

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
    static LRESULT CALLBACK LanguageProcedure(HWND window, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR data);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateMainWindow(int showCommand, bool startInBackground);
    bool OnCreate();
    void InitializeHost();
    void OnAudioFrame(AudioFrame& frame);
    void OnSurfaceChange(SurfaceChange& change);
    void Paint();
    void DrawStartupControl(const DRAWITEMSTRUCT& item) const;
    void DrawCheckboxControl(const DRAWITEMSTRUCT& item, const wchar_t* caption,
        bool checked) const;
    void DrawButtonControl(const DRAWITEMSTRUCT& item) const;
    void DrawLanguageControl(const DRAWITEMSTRUCT& item) const;
    void DrawTrayStatus(const DRAWITEMSTRUCT& item) const;
    void LayoutControls();
    int ChannelListHeight() const;
    int ChannelRowsPerPage() const;
    int ChannelRowHeight(int pageSize) const;
    void UpdateChannelPagination();
    void ShowMainWindow();
    void HideMainWindow();
    void SummonForEucon();
    bool ApplyShortcut(bool enabled, const bridge::Shortcut& shortcut, bool showError);
    void LoadShortcutSettings();
    bool SaveShortcutSettings() const;
    void UpdateLanguage();
    const wchar_t* T(const wchar_t* simplifiedChinese, const wchar_t* english) const;
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
    HWND shortcutEnabledCheck_ = nullptr;
    HWND shortcutBackgroundCheck_ = nullptr;
    HWND shortcutButton_ = nullptr;
    HWND languageCombo_ = nullptr;
    HWND licenseLink_ = nullptr;
    HWND versionLink_ = nullptr;
    HWND issuesLink_ = nullptr;
    std::array<HWND, 3> navigationButtons_{};
    HICON largeIcon_ = nullptr;
    HICON smallIcon_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT headingFont_ = nullptr;
    HFONT bodyFont_ = nullptr;
    HFONT smallFont_ = nullptr;
    HFONT microFont_ = nullptr;
    HFONT iconFont_ = nullptr;
    std::unique_ptr<EuconHost> host_;
    std::unique_ptr<WindowsMediaObserver> mediaObserver_;
    std::array<AudioStripState, EuconHost::MaxChannelCount> strips_{};
    int activeCount_ = 0;
    int page_ = 0;
    int channelPage_ = 0;
    int lastChannel_ = -1;
    int lastKind_ = 0;
    float lastValue_ = 0.0F;
    std::wstring selectedTrackKey_;
    bool lastCommandSent_ = false;
    bool monoAudioEnabled_ = false;
    bool startupEnabled_ = false;
    bool shortcutEnabled_ = true;
    bool shortcutReturnToBackground_ = true;
    bool chinese_ = true;
    bool trayAdded_ = false;
    bool exitRequested_ = false;
    int hostInitializationError_ = 0;
    UINT taskbarCreatedMessage_ = 0U;
    bridge::Shortcut shortcut_{bridge::ShortcutControl | bridge::ShortcutAlt |
        bridge::ShortcutShift, 'W'};
    bridge::GlobalShortcutRegistration shortcutRegistration_;
};
