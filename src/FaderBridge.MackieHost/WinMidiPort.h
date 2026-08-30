#pragma once

#include "MackieProtocol.h"
#include <Windows.h>
#include <mmsystem.h>
#include <functional>
#include <list>
#include <memory>

struct MidiPortName { UINT index; std::wstring name; };
// The UI explicitly chooses ports. No port is ever opened during enumeration.
class WinMidiPort final
{
public:
    using Receiver = std::function<void(DWORD)>;
    explicit WinMidiPort(Receiver receiver) : receiver_(std::move(receiver)) {}
    ~WinMidiPort();
    static std::vector<MidiPortName> Inputs();
    static std::vector<MidiPortName> Outputs();
    bool Open(UINT input, UINT output);
    void Close();
    void Send(const mackie::Bytes& bytes);
    void Collect();
    bool Connected() const { return in_ && out_; }
    bool Faulted() const { return faulted_; }
    const std::wstring& Error() const { return error_; }
    std::uint64_t Received = 0, Sent = 0, Errors = 0;
    bool Trace = false;
private:
    struct InBuffer { MIDIHDR header{}; std::array<char, 1024> bytes{}; bool prepared = false; };
    struct OutBuffer { MIDIHDR header{}; std::vector<char> bytes; };
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Message(UINT, WPARAM, LPARAM);
    bool Check(MMRESULT result, const wchar_t* operation);
    HWND window_ = nullptr;
    HMIDIIN in_ = nullptr;
    HMIDIOUT out_ = nullptr;
    std::array<std::unique_ptr<InBuffer>, 4> inputBuffers_{};
    std::list<std::unique_ptr<OutBuffer>> outputBuffers_;
    Receiver receiver_;
    bool closing_ = false, faulted_ = false;
    std::wstring error_;
};
