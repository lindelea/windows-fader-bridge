#include "MackieApplication.h"
#include "DiagnosticLog.h"
#include <objbase.h>

std::wstring MackieApplication::DeviceLabel(const DeviceContext& device) const
{
    if(!device.settings.deviceName.empty())return device.settings.deviceName;
    if(!device.settings.input.empty())return device.settings.input;
    return workspace_.language==L"en"?L"New device":L"新设备";
}
void MackieApplication::SyncDeviceControls()
{
    if(!window_||!IsSelectedDevice())return;
    const auto fill=[&](int id,const auto& ports,const std::wstring& name){
        SendDlgItemMessageW(window_,id,CB_RESETCONTENT,0,0);int selected=-1;
        auto unique=mackie::UniquePort(ports,name);
        for(int i=0;i<static_cast<int>(ports.size());++i){
            SendDlgItemMessageW(window_,id,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(ports[i].name.c_str()));
            if(unique&&ports[i].index==*unique)selected=i;
        }
        SendDlgItemMessageW(window_,id,CB_SETCURSEL,selected,0);
    };
    fill(101,inputs_,Settings().input);fill(102,outputs_,Settings().output);
    SendDlgItemMessageW(window_,103,CB_SETCURSEL,Settings().profile==L"p1-nano"?1:0,0);
    for(auto pair:{std::pair<int,bool>{106,Settings().touch},{107,Settings().lcd},{108,Settings().meters},{109,Midi().Trace}})
        SendDlgItemMessageW(window_,pair.first,BM_SETCHECK,pair.second?BST_CHECKED:BST_UNCHECKED,0);
    for(int id:{101,102,103,104,106,107,108})EnableWindow(GetDlgItem(window_,id),!Midi().Connected());
    SetDlgItemTextW(window_,105,Midi().Connected()?L"断开设备":L"连接设备");
    FillBindings();
}
bool MackieApplication::SelectDevice(std::size_t index)
{
    if(index>=devices_.size()||encoderWindow_||jogWindow_)return false;
    Learning()=false;selectedDevice_=devices_[index].get();
    workspace_.selected=selectedDevice_->id;
    if(!smoke_)workspace_.Save();
    SyncDeviceControls();renderDue_=0;return true;
}
bool MackieApplication::AddDevice()
{
    if(devices_.size()>=16||smoke_)return false;
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))return false;
    char id[64]{};sprintf_s(id,"device-%08lx-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x",guid.Data1,guid.Data2,guid.Data3,
        guid.Data4[0],guid.Data4[1],guid.Data4[2],guid.Data4[3],guid.Data4[4],guid.Data4[5],guid.Data4[6],guid.Data4[7]);
    auto next=workspace_;next.devices.push_back(id);next.selected=id;
    MackieSettings config;config.storage=next.DevicePath(id);
    if(!config.Save()||!next.Save())return false;
    auto device=std::make_unique<DeviceContext>(*this,id,std::move(config));
    device->media=std::make_unique<MackieMedia>();
    workspace_=std::move(next);devices_.push_back(std::move(device));SelectDevice(devices_.size()-1);
    if(frame_)OnFrame(std::make_unique<AudioFrame>(*frame_));return true;
}
bool MackieApplication::RemoveDevice()
{
    if(!selectedDevice_||Surface().Touched()||smoke_)return false;
    const auto id=selectedDevice_->id;auto next=workspace_;
    next.devices.erase(std::remove(next.devices.begin(),next.devices.end(),id),next.devices.end());next.selected=next.devices.empty()?"":next.devices.front();
    if(!next.Save())return false;
    Disconnect();selectedDevice_=nullptr;
    devices_.erase(std::remove_if(devices_.begin(),devices_.end(),[&](const auto& d){return d->id==id;}),devices_.end());
    workspace_=std::move(next);
    if(!devices_.empty())SelectDevice(0);else SyncDeviceControls();
    // Retain the device file for recovery; removing a device is not file deletion.
    return true;
}
bool MackieApplication::SaveDeviceConfiguration(const std::wstring& name,const std::wstring& input,const std::wstring& output,const std::wstring& profile)
{
    if(!selectedDevice_||name.size()>80||!mackie::SafePair(input,output,profile))return false;
    if(Midi().Connected()&&(input!=Settings().input||output!=Settings().output||profile!=Settings().profile))return false;
    for(auto& device:devices_)if(device.get()!=selectedDevice_&&
        (device->settings.input==input||device->settings.output==output))return false;
    auto next=Settings();next.deviceName=name;next.input=input;next.output=output;next.profile=profile;
    if(!next.Save())return false;
    Settings()=std::move(next);Device().connection.Resume();deviceScanDue_=0;SyncDeviceControls();return true;
}
bool MackieApplication::OpenDevice(UINT input,UINT output)
{
    if(smoke_||closing_||Device().id.empty()||!mackie::SafePair(Settings().input,Settings().output,Settings().profile))return false;
    const auto ins=WinMidiPort::Inputs(false),outs=WinMidiPort::Outputs(false);
    const auto same=[](const auto& ports,UINT index,const std::wstring& name){return std::any_of(ports.begin(),ports.end(),[&](const auto& p){return p.index==index&&p.name==name;});};
    if(!same(ins,input,Settings().input)||!same(outs,output,Settings().output)){Status(L"端口列表已改变，请刷新后重试。");return false;}
    for(auto& other:devices_)if(other.get()!=&Device()&&other->midi.Connected()&&
        (other->inputIndex==input||other->outputIndex==output)){Status(L"MIDI 端口已被另一台设备使用。");return false;}
    if(!Midi().Open(input,output)){Device().connection.Failed(GetTickCount64());Status(Midi().Error());return false;}
    SelectedInput()=input;SelectedOutput()=output;Device().connection.Resume();
    Surface().ResetConnection();CustomPressed().fill(false);CustomFeedback().clear();
    Surface().Feedback(GetTickCount64(),true);SyncDeviceControls();
    FB_TRACE("MACKIE_DEVICE_CONNECTED id=%s input=%u output=%u",Device().id.c_str(),input,output);
    return true;
}
void MackieApplication::ReconcileDevices()
{
    if(smoke_||closing_)return;
    const auto now=GetTickCount64();if(now<deviceScanDue_)return;deviceScanDue_=now+2000;
    // Enumeration is read-only. Reconnect only unique, explicitly saved names.
    inputs_=WinMidiPort::Inputs(false);outputs_=WinMidiPort::Outputs(false);
    for(auto& device:devices_)
    {
        DeviceScope scope(*this,device.get());
        auto input=mackie::UniquePort(inputs_,Settings().input),output=mackie::UniquePort(outputs_,Settings().output);
        if(Midi().Connected()&&(!input||!output||*input!=SelectedInput()||*output!=SelectedOutput()))Disconnect(false);
        bool configured=mackie::SafePair(Settings().input,Settings().output,Settings().profile);
        if(!Midi().Connected()&&input&&output&&device->connection.Due(Settings().autoConnect,configured,now))
            if(!OpenDevice(*input,*output))device->connection.Failed(now);
    }
}
