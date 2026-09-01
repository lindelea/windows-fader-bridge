#pragma once
#include "Model.h"
#include "DesktopChannelStatus.h"
#include "Preferences.h"
#include <Windows.h>

#include <CommCtrl.h>
#include <functional>
#include <vector>

namespace apollo
{
// Presentation only: this window never opens a transport or owns a EUCON node.
struct DesktopState
{
    Snapshot snapshot;
    bool published = false, conflict = false, preview = false, configExtension = false;
    size_t enabledChannels = 0;
    bool monitorEnabled = false, configEnabled = false;
    double activeCeiling = -20;
    std::wstring notice;
};
struct DesktopActions
{
    std::function<std::wstring(const Preferences &, bool startup, bool activate)> save;
    std::function<void()> connect, disconnect, lock, exit;
};
class ApolloDesktop final
{
  public:
    ApolloDesktop(Preferences preferences, bool startup, DesktopActions actions);
    ~ApolloDesktop();
    bool Create(HINSTANCE instance, bool preview = false);
    void Show(bool settings = false);
    void Update(DesktopState state);
    HWND Window() const
    {
        return window_;
    }
    bool TrayAvailable() const
    {
        return trayAdded_;
    }
    void Hide();

  private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ToggleProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK ComboProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK ButtonProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT Message(UINT, WPARAM, LPARAM);
    void Build();
    void Layout();
    void Fonts();
    void Navigate(int page);
    void Act(int id, int code);
    void Paint(HDC);
    void DrawItem(DRAWITEMSTRUCT *);
    void RefreshChannels();
    void DrawChannel(DRAWITEMSTRUCT *);
    std::wstring ChannelAccessibleText(size_t row) const;
    void Save();
    void AddTray();
    void TrayMenu();
    void OpenFolder();
    void SyncDraft();
    HWND Add(const wchar_t *type, int id, const std::wstring &name, DWORD style);
    HWND Button(int id, const std::wstring &name);
    HWND Toggle(int id, const std::wstring &name, bool checked);
    HWND Combo(int id, const std::vector<std::wstring> &choices, int selected);
    void Place(int id, int x, int y, int w, int h);
    void Panel(HDC, int x, int y, int w, int h, COLORREF fill, COLORREF edge);
    void Text(HDC, const std::wstring &, int x, int y, int w, int h, int font, COLORREF, UINT flags = 0);
    const wchar_t *T(const wchar_t *zh, const wchar_t *en) const;
    int S(int v) const
    {
        return MulDiv(v, dpi_, 96);
    }
    Preferences saved_, draft_;
    bool savedStartup_, draftStartup_, trayAdded_ = false, building_ = false, dirty_ = false;
    DesktopActions actions_;
    DesktopState state_;
    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HICON icon_ = nullptr;
    ULONG_PTR graphicsToken_ = 0;
    HFONT fonts_[6]{};
    HBRUSH fieldBrush_ = nullptr;
    std::vector<HWND> children_;
    std::vector<DesktopChannelStatus> channelRows_;
    int dpi_ = 96, width_ = 1080, height_ = 720, page_ = 0;
    std::wstring notice_;
    UINT taskbarCreated_ = 0;
    ULONGLONG lastPaint_ = 0;
};
} // namespace apollo
