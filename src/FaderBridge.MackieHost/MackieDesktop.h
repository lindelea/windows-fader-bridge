#pragma once
#include <Windows.h>
#include <CommCtrl.h>
#include <string>
#include <vector>
#include <filesystem>
#include "MackieSurface.h"
#include "WinMidiPort.h"

class MackieApplication;
// Presentation only. The existing application remains the sole owner of MIDI,
// Core Audio and surface state; the diagnostic dashboard is still available.
class MackieDesktop final
{
public:
    explicit MackieDesktop(MackieApplication& app) : app_(app) {}
    ~MackieDesktop();
    bool Create();
    void Show();
    void Close();
    HWND Window() const { return window_; }
    void Tick();
    void AudioFrameChanged();
    bool Midi(DWORD raw);
    bool Preview(const mackie::Action& action);
    bool Editing() const;
    void Notice(const std::wstring& zh, const std::wstring& en, bool error = false);
private:
    struct Target { int key = -1, axis = -1, gesture = 0; std::wstring label; };
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK FieldProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    static LRESULT CALLBACK ToggleProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    LRESULT Message(UINT, WPARAM, LPARAM);
    void Build();
    void Layout();
    void Navigate(int page);
    void Paint(HDC);
    void DrawItem(DRAWITEMSTRUCT*);
    void Act(int id, int code);
    void Targets();
    void Catalog();
    void Detail();
    void Apply(bool remove = false);
    void Device(bool connect);
    void Ports();
    void CommitPreferences();
    bool Persist();
    std::wstring T(const wchar_t* zh, const wchar_t* en) const;
    std::wstring Binding(const Target&) const;
    HWND Add(const wchar_t*, const std::wstring&, DWORD, int);
    HWND Button(int id, const std::wstring&);
    HWND Toggle(int id,const std::wstring&);
    void DeviceChoices();
    bool SaveDevice();
    void OpenFolder(const std::filesystem::path&);
    HWND List(int id);
    void Place(int id, int x, int y, int width, int height);
    void Set(int id, const std::wstring& text);
    void Text(HDC, const std::wstring&, int x, int y, int width, int height, COLORREF, int font = 0, UINT flags = 0);
    void Panel(HDC, int x, int y, int w, int h, COLORREF fill, COLORREF line);
    int S(int value) const;
    int OverviewRowsPerPage() const;
    int OverviewRowHeight() const;
    MackieApplication& app_;
    HWND window_ = nullptr;
    HFONT fonts_[5]{};
    HFONT iconFont_ = nullptr;
    HICON headerIcon_ = nullptr, aboutIcon_ = nullptr;
    HBRUSH background_ = nullptr, field_ = nullptr;
    int dpi_ = 96, width_ = 1180, height_ = 740, page_ = 0;
    int overviewRowHeight_ = 0;
    bool zh_ = true, building_ = false, listGuard_ = false, capture_ = false;
    bool noticeError_ = false;
    std::uint64_t noticeUntil_ = 0;
    std::wstring notice_, selectedCommand_, inputPreview_, lastSelectedKey_;
    std::vector<Target> targets_;
    std::vector<std::wstring> catalog_, groups_, trackKeys_;
    int target_ = -1;
    std::size_t bindingsSignature_ = 0;
    std::vector<HWND> children_;
    std::vector<MidiPortName> displayedInputs_, displayedOutputs_;
};
