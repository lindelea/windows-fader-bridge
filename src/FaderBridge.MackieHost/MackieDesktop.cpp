#include "MackieDesktop.h"
#include "../BridgeProductVersion.h"
#include "MackieApplication.h"
#include "MackieCommandText.h"
#include "DiagnosticLog.h"
#include "Windows80Preset.h"
#include <Dwmapi.h>
#include <UxTheme.h>
#include <windowsx.h>
#include <algorithm>
#include <set>
#include <ShlObj.h>

namespace
{
constexpr COLORREF Bg = RGB(16,18,22), Side = RGB(20,23,28), Card = RGB(25,29,36),
    Edge = RGB(43,49,59), Ink = RGB(235,238,243), Muted = RGB(147,157,173),
    Amber = RGB(237,183,90), Green = RGB(81,204,156), Red = RGB(246,126,126), Selection = RGB(43,39,32);
enum { Nav = 500, Tracks = 600, Prev, Next, Hide, Menu, TargetList = 620, Search, Categories,
    Commands, TargetSearch, Capture, ApplyButton, Remove, DetailText, TargetTitle, InputPreview,
    Sensitivity, Seconds, Layer, Language = 650, TrayClose, Startup, GlobalShortcut, InPort, OutPort, Profile,
    Touch, Lcd, Meters, Refresh, Connect, Preset, Debug, Log, Trace,
    LicenseLink, VersionLink, IssuesLink,
    DevicePicker=700,DeviceList,AddDevice,RemoveDevice,DeviceName,AutoConnect,SaveDeviceButton };
constexpr DWORD Combo = CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP;
std::wstring Read(HWND w)
{ int n = GetWindowTextLengthW(w); std::wstring t(n+1,L'\0'); GetWindowTextW(w,t.data(),n+1); t.resize(n); return t; }
int Choice(HWND w, int id) { return static_cast<int>(SendDlgItemMessageW(w,id,CB_GETCURSEL,0,0)); }
int Row(HWND w, int id) { return ListView_GetNextItem(GetDlgItem(w,id),-1,LVNI_SELECTED); }
std::wstring Percent(float v) { return std::to_wstring(static_cast<int>(std::lround(std::clamp(v,0.F,1.F)*100))) + L"%"; }
void Fill(HWND w, int id, const std::vector<std::wstring>& values, int selected)
{ auto c=GetDlgItem(w,id); SendMessageW(c,CB_RESETCONTENT,0,0); for(const auto& v:values) SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(v.c_str())); SendMessageW(c,CB_SETCURSEL,selected,0); }
void Rows(HWND w, const std::vector<std::wstring>& values, int selected)
{
    SendMessageW(w,WM_SETREDRAW,FALSE,0); ListView_DeleteAllItems(w);
    for(int i=0;i<static_cast<int>(values.size());++i) { LVITEMW item{}; item.mask=LVIF_TEXT; item.iItem=i; item.pszText=const_cast<wchar_t*>(values[i].c_str()); ListView_InsertItem(w,&item); }
    if(selected>=0) ListView_SetItemState(w,selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    SendMessageW(w,WM_SETREDRAW,TRUE,0); InvalidateRect(w,nullptr,FALSE);
}
bool StartupEnabled()
{
    wchar_t value[32768]{}; DWORD size=sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"WindowsFaderBridge.Mackie",RRF_RT_REG_SZ,nullptr,value,&size)==ERROR_SUCCESS;
}
bool SetStartup(bool enabled)
{
    HKEY key{}; if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    LSTATUS result;
    if(enabled) { wchar_t path[32768]{}; auto n=GetModuleFileNameW(nullptr,path,32768); if(!n||n>=32768){RegCloseKey(key);return false;} std::wstring command=L"\""+std::wstring(path)+L"\" --background"; result=RegSetValueExW(key,L"WindowsFaderBridge.Mackie",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t))); }
    else { result=RegDeleteValueW(key,L"WindowsFaderBridge.Mackie"); if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS; }
    RegCloseKey(key); return result==ERROR_SUCCESS;
}
}
MackieDesktop::~MackieDesktop()
{ Close(); for(auto f:fonts_)if(f)DeleteObject(f);if(iconFont_)DeleteObject(iconFont_);if(headerIcon_)DestroyIcon(headerIcon_);if(aboutIcon_)DestroyIcon(aboutIcon_);if(background_)DeleteObject(background_);if(field_)DeleteObject(field_); }
int MackieDesktop::S(int v) const { return MulDiv(v,dpi_,96); }
int MackieDesktop::OverviewRowsPerPage() const
{
    const int mediaTop=height_-32-86,pagerTop=mediaTop-16-32;
    const int listHeight=std::max(110,pagerTop-16-242);
    return std::clamp((listHeight-4)/60,1,5);
}
int MackieDesktop::OverviewRowHeight() const
{
    const int mediaTop=height_-32-86,pagerTop=mediaTop-16-32;
    const int listHeight=std::max(110,pagerTop-16-242);
    const int count=static_cast<int>(app_.Surface().Order().size());
    const int slots=count==4?4:OverviewRowsPerPage();
    return std::max(1,listHeight/std::max(1,slots));
}
std::wstring MackieDesktop::T(const wchar_t* zh,const wchar_t* en) const { return zh_?zh:en; }
bool MackieDesktop::Create()
{
    dpi_=GetDpiForSystem(); zh_=app_.workspace_.language!=L"en";
    background_=CreateSolidBrush(Bg); field_=CreateSolidBrush(Card);
    const int sizes[]={14,12,28,18,11};
    for(int i=0;i<5;++i) fonts_[i]=CreateFontW(-S(sizes[i]),0,0,0,i==2||i==3?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    iconFont_=CreateFontW(-S(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe Fluent Icons");
    headerIcon_=reinterpret_cast<HICON>(LoadImageW(app_.instance_,MAKEINTRESOURCEW(1),IMAGE_ICON,S(40),S(40),LR_DEFAULTCOLOR));
    aboutIcon_=reinterpret_cast<HICON>(LoadImageW(app_.instance_,MAKEINTRESOURCEW(1),IMAGE_ICON,S(64),S(64),LR_DEFAULTCOLOR));
    { auto probe=CreateCompatibleDC(nullptr);auto old=SelectObject(probe,fonts_[2]);wchar_t face[128]{};GetTextFaceW(probe,128,face);FB_TRACE("MACKIE_DESKTOP_FONT face=%ls dpi=%d",face,dpi_);SelectObject(probe,old);DeleteDC(probe); }
    WNDCLASSW wc{}; wc.hInstance=app_.instance_; wc.lpfnWndProc=Proc; wc.hIcon=LoadIconW(app_.instance_,MAKEINTRESOURCEW(1)); wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName=L"WindowsFaderBridge.Mackie.Desktop"; RegisterClassW(&wc);
    RECT bounds{0,0,S(1180),S(740)}, work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    AdjustWindowRectEx(&bounds,WS_OVERLAPPEDWINDOW,FALSE,WS_EX_CONTROLPARENT);
    window_=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Windows Fader Bridge for Mackie Control",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        work.left+std::max(0L,(work.right-work.left-(bounds.right-bounds.left))/2),work.top+std::max(0L,(work.bottom-work.top-(bounds.bottom-bounds.top))/2),
        std::min(bounds.right-bounds.left,work.right-work.left),std::min(bounds.bottom-bounds.top,work.bottom-work.top),nullptr,nullptr,app_.instance_,this);
    if(!window_)return false;
    BOOL dark=TRUE; DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));
    Build(); return true;
}
void MackieDesktop::Show() { if(window_){ShowWindow(window_,SW_RESTORE);SetForegroundWindow(window_);} }
void MackieDesktop::Close() { if(window_){DestroyWindow(window_);window_=nullptr;} }
HWND MackieDesktop::Add(const wchar_t* type,const std::wstring& text,DWORD style,int id)
{
    auto w=CreateWindowExW(0,type,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,1,1,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),app_.instance_,nullptr);
    SendMessageW(w,WM_SETFONT,reinterpret_cast<WPARAM>(fonts_[0]),FALSE);
    if(_wcsicmp(type,WC_COMBOBOXW)==0)
    {
        SetWindowTheme(w,L"",L"");SetWindowSubclass(w,FieldProc,1,reinterpret_cast<DWORD_PTR>(this));
        COMBOBOXINFO info{sizeof(info)};if(GetComboBoxInfo(w,&info))SetWindowTheme(info.hwndList,L"DarkMode_Explorer",nullptr);
    }
    children_.push_back(w);return w;
}
HWND MackieDesktop::Button(int id,const std::wstring& text) { return Add(L"BUTTON",text,BS_OWNERDRAW|WS_TABSTOP,id); }
HWND MackieDesktop::Toggle(int id,const std::wstring& text)
{auto w=Add(L"BUTTON",text,BS_AUTOCHECKBOX|WS_TABSTOP,id);SetWindowTheme(w,L"",L"");SetWindowSubclass(w,ToggleProc,1,reinterpret_cast<DWORD_PTR>(this));return w;}
HWND MackieDesktop::List(int id)
{
    auto w=Add(WC_LISTVIEWW,L"",LVS_REPORT|LVS_NOCOLUMNHEADER|LVS_OWNERDRAWFIXED|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,id);
    SetWindowTheme(w,L"DarkMode_Explorer",nullptr); ListView_SetExtendedListViewStyle(w,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(w,Card); ListView_SetTextColor(w,Ink); ListView_SetTextBkColor(w,Card);
    LVCOLUMNW col{};col.mask=LVCF_WIDTH;col.cx=S(200);ListView_InsertColumn(w,0,&col);
    return w;
}
void MackieDesktop::Place(int id,int x,int y,int w,int h)
{ if(auto c=GetDlgItem(window_,id)){MoveWindow(c,S(x),S(y),S(std::max(1,w)),S(std::max(1,h)),TRUE);if(id==Tracks||id==TargetList||id==Commands){RECT r{};GetClientRect(c,&r);ListView_SetColumnWidth(c,0,std::max(1L,r.right-2));}} }
void MackieDesktop::Set(int id,const std::wstring& text) { if(auto c=GetDlgItem(window_,id);c && Read(c)!=text)SetWindowTextW(c,text.c_str()); }
void MackieDesktop::Navigate(int p)
{
    if(p<0||p>7)return;
    capture_=false; app_.Learning()=false; app_.Surface().ResetJogInput(); app_.Surface().ResetEncoderInput();
    page_=p;target_=-1;selectedCommand_.clear();inputPreview_.clear();notice_.clear();Build();
}
void MackieDesktop::Build()
{
    building_=true;lastSelectedKey_.clear();
    for(auto c:children_)DestroyWindow(c);children_.clear();targets_.clear();catalog_.clear();trackKeys_.clear();
    const wchar_t* cn[]={L"通道总览",L"通用",L"设备",L"按键分配",L"旋钮分配",L"Jog 与方向",L"高级",L"关于"};
    const wchar_t* en[]={L"Overview",L"General",L"Device",L"Button mapping",L"Encoder mapping",L"Jog & directions",L"Advanced",L"About"};
    for(int i=0;i<8;++i)Button(Nav+i,T(cn[i],en[i])); Button(Hide,T(L"后台运行",L"Hide to tray"));
    if(page_==0){List(Tracks);Button(Prev,T(L"上一组",L"Previous bank"));Button(Next,T(L"下一组",L"Next bank"));Button(Connect,T(L"连接设备",L"Connect device"));}
    if(page_==0||(page_>=3&&page_<=5)){Add(WC_COMBOBOXW,L"",Combo,DevicePicker);DeviceChoices();}
    if(page_==1){Add(WC_COMBOBOXW,L"",Combo,Language);Fill(window_,Language,{L"简体中文",L"English"},zh_?0:1);Toggle(TrayClose,T(L"关闭窗口后后台运行",L"Run in background when closed"));Toggle(Startup,T(L"随 Windows 启动",L"Start with Windows"));Button(GlobalShortcut,bridge::ShortcutText({app_.workspace_.shortcutModifiers,app_.workspace_.shortcutKey}));}
    if(page_==2)
    {
        List(DeviceList);Button(AddDevice,T(L"添加设备",L"Add device"));Button(RemoveDevice,T(L"移除设备",L"Remove device"));
        Add(L"EDIT",app_.Settings().deviceName,ES_AUTOHSCROLL|WS_TABSTOP,DeviceName);SendDlgItemMessageW(window_,DeviceName,EM_SETLIMITTEXT,80,0);
        SendDlgItemMessageW(window_,DeviceName,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(T(L"自定义名称",L"Custom name").c_str()));
        Add(WC_COMBOBOXW,L"",Combo,InPort);Add(WC_COMBOBOXW,L"",Combo,OutPort);Add(WC_COMBOBOXW,L"",Combo,Profile);
        Fill(window_,Profile,{T(L"通用 Mackie Control",L"Generic Mackie Control"),L"iCON P1-Nano"},app_.Settings().profile==L"p1-nano"?1:0);
        Toggle(Touch,T(L"触摸保护",L"Touch guard"));Toggle(Lcd,L"LCD");Toggle(Meters,T(L"峰值表",L"Meters"));Toggle(AutoConnect,T(L"自动连接",L"Auto-connect"));
        Button(Refresh,T(L"刷新",L"Refresh"));Button(Connect,L"");Button(SaveDeviceButton,T(L"保存设备",L"Save device"));
        Button(Preset,T(L"应用 Windows 80 键预设",L"Apply Windows 80-key preset"));DeviceChoices();Ports();
    }
    if(page_>=3&&page_<=5)
    {
        Add(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,Search);SendDlgItemMessageW(window_,Search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(T(L"搜索命令 · 中文或英文",L"Search commands in English or Chinese").c_str()));
        SendDlgItemMessageW(window_,Search,EM_SETLIMITTEXT,256,0);
        Add(WC_COMBOBOXW,L"",Combo,Categories);List(TargetList);List(Commands);
        Add(L"STATIC",L"",SS_ENDELLIPSIS,TargetTitle);Add(L"STATIC",L"",0,DetailText);Add(L"STATIC",L"",SS_ENDELLIPSIS,InputPreview);
        Button(ApplyButton,T(L"应用分配",L"Assign command"));Button(Remove,T(L"取消分配",L"Unassign"));
        if(page_==3){Button(Capture,T(L"识别设备按键",L"Detect a button")); Add(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,TargetSearch);SendDlgItemMessageW(window_,TargetSearch,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(T(L"查找已有分配",L"Find a mapping").c_str()));SendDlgItemMessageW(window_,TargetSearch,EM_SETLIMITTEXT,256,0);}
        if(page_==5){Add(WC_COMBOBOXW,L"",Combo,Sensitivity);Add(WC_COMBOBOXW,L"",Combo,Seconds);Add(WC_COMBOBOXW,L"",Combo,Layer);Fill(window_,Seconds,{L"1 s",L"2 s",L"3 s",L"4 s",L"5 s",L"6 s",L"7 s",L"8 s",L"9 s",L"10 s"},app_.Settings().jog.seekSeconds-1);Fill(window_,Layer,{L"Move",L"Zoom"},app_.Surface().CursorZoom()?1:0);}
        Targets();if(target_>=0)selectedCommand_=Binding(targets_[target_]);Catalog();Detail();
    }
    if(page_==6){Button(Log,T(L"打开日志目录",L"Open log folder"));Toggle(Trace,T(L"详细 MIDI 日志",L"Detailed MIDI logging"));}
    if(page_==7){Button(LicenseLink,L"MPL 2.0");Button(VersionLink,BRIDGE_PRODUCT_VERSION_DISPLAY_W);Button(IssuesLink,T(L"报告错误",L"Report an issue"));}
    building_=false;Layout();Tick();
    if(page_>=3&&page_<=5){int row=Row(window_,Commands);if(row>=0)ListView_EnsureVisible(GetDlgItem(window_,Commands),row,FALSE);}
    InvalidateRect(window_,nullptr,FALSE);
}
void MackieDesktop::Layout()
{
    RECT r{};GetClientRect(window_,&r);width_=MulDiv(r.right,96,dpi_);height_=MulDiv(r.bottom,96,dpi_);
    for(int i=0;i<8;++i)Place(Nav+i,16,i==0?138:220+(i-1)*44,192,38);
    Place(Hide,24,height_-66,176,36);
    int x=256,w=width_-288,bottom=height_-72;
    const int mediaTop=height_-32-86,pagerTop=mediaTop-16-32;
    Place(DevicePicker,x,82,std::min(420,w-208),200);
    if(page_==0){Place(Connect,width_-204,48,172,36);Place(Tracks,x,242,w,std::max(110,pagerTop-16-242));Place(Prev,x,pagerTop,122,32);Place(Next,x+132,pagerTop,122,32);}
    if(page_==1){Place(Language,width_-280,164,224,200);Place(TrayClose,width_-112,282,56,30);Place(Startup,width_-112,398,56,30);Place(GlobalShortcut,width_-330,510,274,38);}
    if(page_==2)
    {
        int split=x+264,right=width_-32-split;
        Place(AddDevice,x,82,244,36);Place(DeviceList,x+12,148,220,bottom-220);Place(RemoveDevice,x+20,bottom-56,204,36);
        Place(DeviceName,split+36,172,right-72,26);Place(Profile,split+24,233,right-48,240);
        Place(InPort,split+24,299,right-48,280);Place(OutPort,split+24,365,right-48,280);
        Place(AutoConnect,width_-112,418,56,30);
        int col=(right-48)/3;Place(Touch,split+24,480,52,28);Place(Lcd,split+24+col,480,52,28);Place(Meters,split+24+col*2,480,52,28);
        Place(Preset,split+24,bottom-104,310,36);
        Place(Refresh,width_-136,82,104,36);Place(SaveDeviceButton,split+24,bottom-56,132,36);Place(Connect,width_-220,bottom-56,164,36);
    }
    if(page_>=3&&page_<=5)
    {
        int split=x+296,right=width_-32-split;
        Place(TargetSearch,x+12,188,252,30);Place(Capture,x,132,276,38);
        Place(TargetList,x+12,page_==3?230:196,252,bottom-(page_==3?230:196)-16);
        Place(TargetTitle,split,134,right,26);Place(Search,split+12,180,right-24,30);Place(Categories,split,220,right,260);
        Place(Commands,split+16,278,right-32,bottom-426);
        Place(DetailText,split+20,bottom-142,right-40,60);Place(ApplyButton,width_-232,bottom-56,180,36);Place(Remove,split+20,bottom-56,128,36);
        Place(InputPreview,x,bottom+8,w,22);
        if(page_==5){Place(Sensitivity,x+156,bottom-102,104,140);Place(Seconds,x+156,bottom-56,104,200);Place(Layer,x+156,135,104,160);Place(TargetList,x+12,196,252,bottom-324);}
    }
    if(page_==6){Place(Log,x+24,184,216,36);Place(Trace,width_-112,290,56,30);}
    if(page_==7){Place(LicenseLink,x+112,426,70,32);Place(VersionLink,x+206,426,80,32);Place(IssuesLink,width_-216,426,160,32);}
}
void MackieDesktop::Panel(HDC dc,int x,int y,int w,int h,COLORREF fill,COLORREF line)
{auto b=CreateSolidBrush(fill);auto p=CreatePen(PS_SOLID,1,line);auto ob=SelectObject(dc,b);auto op=SelectObject(dc,p);RoundRect(dc,S(x),S(y),S(x+w),S(y+h),S(12),S(12));SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(b);DeleteObject(p);}
void MackieDesktop::Text(HDC dc,const std::wstring& t,int x,int y,int w,int h,COLORREF color,int font,UINT flags)
{auto old=SelectObject(dc,fonts_[font]);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);RECT r{S(x),S(y),S(x+w),S(y+h)};DrawTextW(dc,t.c_str(),static_cast<int>(t.size()),&r,DT_NOPREFIX|(flags?flags:DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS));SelectObject(dc,old);}
void MackieDesktop::Paint(HDC dc)
{
    RECT rc{};GetClientRect(window_,&rc);FillRect(dc,&rc,background_);RECT side{0,0,S(224),rc.bottom};auto sb=CreateSolidBrush(Side);FillRect(dc,&side,sb);DeleteObject(sb);
    auto line=CreatePen(PS_SOLID,1,Edge);auto old=SelectObject(dc,line);MoveToEx(dc,S(224),0,nullptr);LineTo(dc,S(224),rc.bottom);SelectObject(dc,old);DeleteObject(line);
    if(headerIcon_)DrawIconEx(dc,S(20),S(31),headerIcon_,S(40),S(40),0,nullptr,DI_NORMAL);
    Text(dc,L"Windows Fader Bridge",70,28,146,24,Ink,1);Text(dc,L"for Mackie Control",70,52,146,20,Amber,4);
    Text(dc,T(L"工作区",L"WORKSPACE"),32,104,160,22,Muted,4);Text(dc,T(L"设置",L"SETTINGS"),32,187,160,22,Muted,4);
    const wchar_t* zh[]={L"通道总览",L"通用设置",L"设备",L"按键分配",L"旋钮分配",L"Jog 与方向控制",L"高级",L"关于"};
    const wchar_t* en[]={L"Overview",L"General",L"Devices",L"Button mapping",L"Encoder mapping",L"Jog & directions",L"Advanced",L"About"};
    const int x=256,w=width_-288,bottom=height_-72;
    const int mediaTop=height_-32-86,pagerTop=mediaTop-16-32;
    Text(dc,T(zh[page_],en[page_]),x,36,w-(page_==0?190:0),42,Ink,2);
    if(page_==0)
    {
        Panel(dc,x,128,w,84,Card,Edge);Panel(dc,x+20,152,8,8,app_.Midi().Connected()?Green:Muted,Card);
        Text(dc,app_.Midi().Connected()?T(L"控制器已连接",L"Controller connected"):T(L"等待连接",L"Ready to connect"),x+42,142,w-236,27,Ink,3);
        Text(dc,app_.selectedDevice_?app_.DeviceLabel(app_.Device()):T(L"未添加设备",L"No devices"),x+42,172,w-236,24,Muted,1);
        Text(dc,std::to_wstring(app_.Surface().Order().size()),width_-146,136,86,40,Amber,2,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        Text(dc,T(L"在线通道",L"ONLINE CHANNELS"),width_-220,178,160,20,Muted,4,DT_RIGHT|DT_SINGLELINE);
        Text(dc,T(L"音频通道",L"AUDIO CHANNELS"),x,217,280,24,Muted,4);
        Text(dc,app_.frame_&&app_.frame_->monoAudioEnabled?L"MONO":L"STEREO",width_-174,217,142,24,Amber,4,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        Text(dc,L"CH "+std::to_wstring(app_.Surface().BankStart()+1)+L"–"+std::to_wstring(app_.Surface().BankStart()+8),x+272,pagerTop+2,w-272,28,Muted,1,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        Panel(dc,x,mediaTop,w,86,Card,Edge);
        std::wstring status;auto media=app_.Media()?app_.Media()->State(status):WindowsMediaState{};auto time=PlaybackTime(media.timeline,media.playing);
        Text(dc,media.available?(media.playing?T(L"正在播放",L"NOW PLAYING"):T(L"已暂停",L"PAUSED")):T(L"媒体待机",L"MEDIA IDLE"),x+20,mediaTop+10,w-40,18,Amber,4);
        Text(dc,media.available&&!media.title.empty()?media.title:T(L"等待播放器",L"Waiting for a player"),x+20,mediaTop+30,w-224,25,Ink,0);
        Text(dc,media.available?media.artist:L"",x+20,mediaTop+58,w-224,20,Muted,1);
        Text(dc,time.available?mackie::TimeText(time.elapsedSeconds):mackie::TimeText(app_.Surface().TimeDisplaySeconds),width_-214,mediaTop+27,158,27,Ink,3,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        Text(dc,time.available?(L"/ "+mackie::TimeText(time.durationSeconds)):T(L"系统时间",L"SYSTEM CLOCK"),width_-214,mediaTop+57,158,20,Muted,1,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
    }
    if(page_==1)
    {
        const wchar_t* aZh[]={L"界面语言",L"关闭窗口时保留后台运行",L"随 Windows 启动",L"全局调出快捷键"};
        const wchar_t* aEn[]={L"Interface language",L"Keep running when the window closes",L"Start with Windows",L"Global summon shortcut"};
        for(int i=0;i<4;++i){Panel(dc,x,132+i*116,w,100,Card,Edge);Text(dc,T(aZh[i],aEn[i]),x+24,132+i*116,w-(i==0||i==3?300:140),100,Ink,3);}
    }
    if(page_==2)
    {
        int split=x+264,right=width_-32-split;
        Panel(dc,x,132,244,bottom-132,Card,Edge);Panel(dc,split,132,right,bottom-132,Card,Edge);
        Text(dc,T(L"设备名称",L"Device name"),split+24,143,right-48,22,Muted,1);
        Panel(dc,split+24,167,right-48,36,Card,Edge);
        Text(dc,T(L"设备配置",L"Controller profile"),split+24,209,right-48,22,Muted,1);
        Text(dc,T(L"MIDI 输入",L"MIDI input"),split+24,275,right-48,22,Muted,1);
        Text(dc,T(L"MIDI 输出",L"MIDI output"),split+24,341,right-48,22,Muted,1);
        Text(dc,T(L"自动连接",L"Auto-connect"),split+24,416,right-140,32,Ink);
        int col=(right-48)/3;Text(dc,T(L"触摸保护",L"Touch guard"),split+24,452,col,22,Muted,1);Text(dc,L"LCD",split+24+col,452,col,22,Muted,1);Text(dc,T(L"峰值表",L"Meters"),split+24+col*2,452,col,22,Muted,1);
        if(app_.devices_.empty())Text(dc,T(L"无设备",L"No devices"),x+20,158,204,40,Muted);
    }
    if(page_>=3&&page_<=5)
    {
        int split=x+296,right=width_-32-split;
        Panel(dc,x,180,276,bottom-180,Card,Edge);Panel(dc,split,170,right,44,Card,Edge);Panel(dc,split,262,right,bottom-262,Card,Edge);
        if(page_==3)Panel(dc,x,180,276,46,Card,Edge);
        if(page_==4)Text(dc,T(L"旋钮 1：音量  ·  旋钮 2：Pan",L"Encoder 1: Volume  ·  2: Pan"),x,132,276,40,Muted,1,DT_WORDBREAK);
        if(page_==5)
        {
            Text(dc,T(L"方向层",L"Direction layer"),x+16,138,136,26,Muted,1);
            Text(dc,T(L"命令灵敏度",L"Sensitivity"),x+16,bottom-102,132,30,Muted,1);
            Text(dc,T(L"每格播放进度",L"Seek per tick"),x+16,bottom-56,132,30,Muted,1);
        }
        if(catalog_.empty())Text(dc,T(L"没有匹配的命令\n试试其他关键词或分类。",L"No matching commands\nTry another search or category."),split+24,298,right-48,80,Muted,0,DT_WORDBREAK);
        if(targets_.empty())Text(dc,T(L"还没有按键分配\n点击“识别设备按键”开始。",L"No button mappings yet\nChoose Detect a button to begin."),x+20,250,236,90,Muted,0,DT_WORDBREAK);
    }
    if(page_==6)
    {
        Panel(dc,x,132,w,112,Card,Edge);Text(dc,T(L"日志",L"Logs"),x+24,145,w-48,28,Ink,3);
        Panel(dc,x,264,w,82,Card,Edge);Text(dc,T(L"详细 MIDI 日志",L"Detailed MIDI logging"),x+24,264,w-160,82,Ink,3);
    }
    if(page_==7)
    {
        Panel(dc,x,132,w,360,Card,Edge);if(aboutIcon_)DrawIconEx(dc,S(x+24),S(158),aboutIcon_,S(64),S(64),0,nullptr,DI_NORMAL);
        Text(dc,L"Windows Fader Bridge",x+112,158,w-136,34,Ink,2);Text(dc,L"for Mackie Control",x+112,200,w-136,26,Amber,0);
        Text(dc,L"Windows Core Audio  ↔  Mackie Control",x+24,274,w-48,30,Ink,3);
        Text(dc,T(L"将 Windows 音频混音器映射为稳定的 Mackie Control 通道模型。",L"Maps the Windows audio mixer to a stable Mackie Control channel model."),x+24,310,w-48,28,Muted,0);
        Text(dc,T(L"设备连接、控制反馈与按键分配由本应用独立管理。",L"Device connections, control feedback and assignments are managed by this application."),x+24,341,w-48,28,Muted,0);
        auto footer=CreatePen(PS_SOLID,1,Edge);auto oldFooter=SelectObject(dc,footer);
        MoveToEx(dc,S(x+24),S(414),nullptr);LineTo(dc,S(x+w-24),S(414));
        SelectObject(dc,oldFooter);DeleteObject(footer);
        Text(dc,L"Lindelea",x+24,426,64,32,Muted,1);
        Text(dc,L"·",x+90,426,8,32,Muted,1,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        Text(dc,L"·",x+179,426,8,32,Muted,1,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
    }
    if(!notice_.empty()&&GetTickCount64()<noticeUntil_)Text(dc,notice_,x,height_-40,w,28,noticeError_?Red:Amber,1);
    else if(Editing())Text(dc,T(L"编辑模式 · 输入预览",L"EDIT MODE · INPUT PREVIEW"),x,height_-40,w,28,Muted,1);
}
void MackieDesktop::DrawItem(DRAWITEMSTRUCT* d)
{
    int saved=SaveDC(d->hDC);SetViewportOrgEx(d->hDC,d->rcItem.left,d->rcItem.top,nullptr);
    int w=MulDiv(d->rcItem.right-d->rcItem.left,96,dpi_),h=MulDiv(d->rcItem.bottom-d->rcItem.top,96,dpi_);
    if(d->CtlType==ODT_BUTTON)
    {
        bool nav=d->CtlID>=Nav&&d->CtlID<Nav+8, active=nav&&static_cast<int>(d->CtlID)-Nav==page_;
        bool link=d->CtlID==LicenseLink||d->CtlID==VersionLink||d->CtlID==IssuesLink;
        bool primary=d->CtlID==ApplyButton||d->CtlID==Connect||d->CtlID==AddDevice;
        bool disabled=(d->itemState&ODS_DISABLED)!=0;
        COLORREF fill=active?Selection:primary?(disabled?RGB(52,48,40):Amber):nav?Side:Card;
        auto background=CreateSolidBrush(link?Card:nav?Side:Bg);RECT whole{0,0,S(w),S(h)};FillRect(d->hDC,&whole,background);DeleteObject(background);
        if(d->itemState&ODS_SELECTED)fill=primary?RGB(214,158,65):Edge;
        if(!link)Panel(d->hDC,0,0,w,h,fill,nav?fill:Edge);
        const auto align=d->CtlID==IssuesLink?DT_RIGHT:nav||link?DT_LEFT:DT_CENTER;
        Text(d->hDC,Read(d->hwndItem),link?0:nav?16:10,0,w-(link?0:nav?28:20),h,disabled?Muted:active||link?Amber:primary?Bg:Ink,link?1:0,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|align);
        if(d->CtlID==AddDevice){auto f=SelectObject(d->hDC,iconFont_);SetTextColor(d->hDC,disabled?Muted:Bg);RECT r{S(15),0,S(37),S(h)};DrawTextW(d->hDC,L"\uE710",1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(d->hDC,f);}
        if(d->itemState&ODS_FOCUS){RECT r{S(4),S(4),S(w-4),S(h-4)};DrawFocusRect(d->hDC,&r);}
    }
    else if(d->CtlType==ODT_COMBOBOX)
    {
        auto color=d->itemState&ODS_SELECTED?Selection:Card;auto brush=CreateSolidBrush(color);RECT rect{0,0,S(w),S(h)};FillRect(d->hDC,&rect,brush);DeleteObject(brush);
        std::wstring label;
        if(d->itemID!=static_cast<UINT>(-1)){int n=static_cast<int>(SendMessageW(d->hwndItem,CB_GETLBTEXTLEN,d->itemID,0));if(n>=0&&n<32768){label.resize(n+1);SendMessageW(d->hwndItem,CB_GETLBTEXT,d->itemID,reinterpret_cast<LPARAM>(label.data()));label.resize(n);}}
        Text(d->hDC,label,10,0,w-20,h,(d->itemState&ODS_DISABLED)?Muted:Ink,0);
    }
    else if(d->CtlType==ODT_LISTVIEW&&d->itemID!=static_cast<UINT>(-1))
    {
        bool selected=(d->itemState&ODS_SELECTED)!=0;
        auto b=CreateSolidBrush(selected?Selection:Card);RECT r{0,0,S(w),S(h)};FillRect(d->hDC,&r,b);DeleteObject(b);
        if(selected){RECT mark{0,S(10),S(3),S(h-10)};b=CreateSolidBrush(Amber);FillRect(d->hDC,&mark,b);DeleteObject(b);}
        if(d->CtlID==DeviceList&&d->itemID<app_.devices_.size())
        {
            auto& device=*app_.devices_[d->itemID];
            Text(d->hDC,app_.DeviceLabel(device),12,5,w-24,26,selected?Amber:Ink);
            Text(d->hDC,device.midi.Connected()?T(L"已连接",L"Connected"):(device.settings.input.empty()?T(L"未配置",L"Not configured"):device.connection.paused?T(L"已断开",L"Disconnected"):T(L"未连接",L"Offline")),12,30,w-24,22,device.midi.Connected()?Green:Muted,1);
        }
        else if(d->CtlID==Tracks&&d->itemID<trackKeys_.size())
        {
            auto s=app_.Find(trackKeys_[d->itemID]);
            const int contentTop=std::max(4,(h-53)/2);
            Text(d->hDC,std::to_wstring(d->itemID+1),16,0,30,h,selected?Amber:Muted,1);
            Text(d->hDC,s?s->name:T(L"等待通道更新",L"Waiting for channel"),60,contentTop,w-370,26,Ink);
            if(s)
            {
                std::wstring role=s->role==AudioStripRole::Application?T(L"应用",L"Application"):(s->role==AudioStripRole::MasterOutput?T(L"主输出",L"Master output"):T(L"音频输入",L"Audio input"));
                if(s->isDefault)role+=T(L"  ·  默认设备",L"  ·  Default device");if(s->muted)role+=T(L"  ·  静音",L"  ·  Muted");if(s->soloed)role+=L"  ·  Solo";
                Text(d->hDC,role,60,contentTop+27,w-370,21,Muted,1);
                Text(d->hDC,s->panAvailable?(std::abs(s->pan)<.01F?T(L"居中",L"Center"):(s->pan<0?L"L ":L"R ")+Percent(std::abs(s->pan))):L"—",w-288,0,80,h,Muted,1);
                auto peak=s->meterDb.empty()?s->peakDb:*std::max_element(s->meterDb.begin(),s->meterDb.end());
                float level=std::clamp((peak+60.F)/60.F,0.F,1.F);
                Panel(d->hDC,w-198,h/2-3,98,6,Edge,Edge);
                if(level>0)Panel(d->hDC,w-198,h/2-3,std::max(2,static_cast<int>(98*level)),6,peak>-3?Amber:Green,peak>-3?Amber:Green);
                Text(d->hDC,Percent(s->volume),w-84,0,62,h,s->muted?Muted:Ink,3,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
            }
        }
        else if(d->CtlID==TargetList&&d->itemID<targets_.size())
        {
            const auto& t=targets_[d->itemID];
            const auto name=mackie::ui::Name(Binding(t),zh_);
            Text(d->hDC,page_==3?name:t.label,14,6,w-28,24,selected?Amber:Ink,0);
            Text(d->hDC,page_==3?t.label:name,14,31,w-28,20,Muted,1);
        }
        else if(d->CtlID==Commands&&d->itemID<catalog_.size())
        {
            const auto& id=catalog_[d->itemID];Text(d->hDC,mackie::ui::Name(id,zh_),16,5,w-32,25,selected?Amber:Ink,0);
            auto hint=mackie::ui::Category(mackie::ui::Group(id),zh_);if(zh_)hint+=L"  /  "+mackie::ui::Name(id,false);
            Text(d->hDC,hint,16,30,w-32,20,Muted,1);
        }
        auto p=CreatePen(PS_SOLID,1,Edge);auto old=SelectObject(d->hDC,p);MoveToEx(d->hDC,S(14),S(h-1),nullptr);LineTo(d->hDC,S(w-14),S(h-1));SelectObject(d->hDC,old);DeleteObject(p);
        if((d->itemState&ODS_FOCUS)&&GetFocus()==d->hwndItem){RECT focus{S(4),S(2),S(w-4),S(h-2)};DrawFocusRect(d->hDC,&focus);}
    }
    RestoreDC(d->hDC,saved);
}
std::wstring MackieDesktop::Binding(const Target& t) const
{
    if(page_==3){auto it=app_.Settings().bindings.find(t.key);return it==app_.Settings().bindings.end()?L"":it->second;}
    if(page_==4)return app_.Settings().encoders[t.axis][t.gesture];
    if(page_==5)return app_.Settings().cursorCommands[t.axis][t.gesture];
    return L"";
}
void MackieDesktop::Targets()
{
    listGuard_=true;int selectedKey=target_>=0&&target_<static_cast<int>(targets_.size())?targets_[target_].key:-1;
    int oldIndex=target_;targets_.clear();std::vector<std::wstring> labels;
    if(page_==3)
    {
        auto query=Read(GetDlgItem(window_,TargetSearch));
        for(const auto& [key,id]:app_.Settings().bindings)
        {
            auto label=L"MIDI "+std::to_wstring(key/128+1)+L"  ·  Note "+std::to_wstring(key%128);
            if(!query.empty()&&!mackie::ui::Matches(id,query)&&mackie::ui::Lower(label).find(mackie::ui::Lower(query))==std::wstring::npos)continue;
            targets_.push_back({key,-1,0,label});
        }
        if(selectedKey>=0&&std::none_of(targets_.begin(),targets_.end(),[&](const auto& t){return t.key==selectedKey;}))
            targets_.push_back({selectedKey,-1,0,L"MIDI "+std::to_wstring(selectedKey/128+1)+L"  ·  Note "+std::to_wstring(selectedKey%128)});
        target_=-1;for(int i=0;i<static_cast<int>(targets_.size());++i)if(targets_[i].key==selectedKey)target_=i;
    }
    if(page_==4)
    {for(int e=0;e<6;++e)for(int g=0;g<3;++g)targets_.push_back({e*3+g,e,g,T(L"旋钮 ",L"Encoder ")+std::to_wstring(e+3)+L" · "+(g==0?T(L"左转",L"Turn left"):g==1?T(L"右转",L"Turn right"):T(L"按下",L"Push"))});target_=oldIndex;}
    if(page_==5)
    {
        const wchar_t* names[]={L"Move ↑↓",L"Move ←→",L"Zoom ↑↓",L"Zoom ←→",L"Jog"};
        for(int a:{4,0,1,2,3})for(int g=0;g<2;++g)
        {bool vertical=a<4&&a%2==0;targets_.push_back({a*2+g,a,g,std::wstring(names[a])+L" · "+(vertical?(g?T(L"向下",L"Down"):T(L"向上",L"Up")):(g?T(L"右转",L"Right"):T(L"左转",L"Left")))});}
        target_=oldIndex;
    }
    if(target_<0||target_>=static_cast<int>(targets_.size()))target_=targets_.empty()?-1:0;
    for(auto& t:targets_)labels.push_back(t.label+L" — "+mackie::ui::Name(Binding(t),zh_));
    Rows(GetDlgItem(window_,TargetList),labels,target_);listGuard_=false;
}
void MackieDesktop::Catalog()
{
    listGuard_=true;
    std::wstring filter;int old=Choice(window_,Categories);if(old>0&&old<static_cast<int>(groups_.size()))filter=groups_[old];
    std::vector<std::wstring> all;
    for(const auto& c:MackieCommands)if(page_!=5||!c.special)all.emplace_back(c.id);
    if(page_==4)for(const auto& c:MackieChannelCommands)all.emplace_back(c.id);
    if(page_==5)
    {
        for(const auto& c:mackie::CursorCommands)all.emplace_back(c.id);
        if(target_>=0&&targets_[target_].axis==4){all.emplace_back(L"Jog.SeekBack");all.emplace_back(L"Jog.SeekForward");}
    }
    groups_={L""};std::vector<std::wstring> groupLabels{T(L"全部分类",L"All categories")};
    for(auto& id:all){auto g=mackie::ui::Group(id);if(std::find(groups_.begin(),groups_.end(),g)==groups_.end()){groups_.push_back(g);groupLabels.push_back(mackie::ui::Category(g,zh_));}}
    auto f=std::find(groups_.begin(),groups_.end(),filter);int group=f==groups_.end()?0:static_cast<int>(f-groups_.begin());
    Fill(window_,Categories,groupLabels,group);filter=groups_[group];
    const auto query=Read(GetDlgItem(window_,Search));catalog_.clear();std::vector<std::wstring> rows;
    for(auto& id:all)if((filter.empty()||filter==mackie::ui::Group(id))&&mackie::ui::Matches(id,query))catalog_.push_back(id);
    std::stable_sort(catalog_.begin(),catalog_.end(),[&](const auto& a,const auto& b){return mackie::ui::Name(a,zh_)<mackie::ui::Name(b,zh_);});
    auto it=std::find(catalog_.begin(),catalog_.end(),selectedCommand_);int selected=it==catalog_.end()?-1:static_cast<int>(it-catalog_.begin());
    for(auto& id:catalog_)rows.push_back(mackie::ui::Name(id,zh_)+L" — "+mackie::ui::Category(mackie::ui::Group(id),zh_));
    Rows(GetDlgItem(window_,Commands),rows,selected);if(selected>=0)ListView_EnsureVisible(GetDlgItem(window_,Commands),selected,FALSE);
    listGuard_=false;Detail();InvalidateRect(window_,nullptr,FALSE);
}
void MackieDesktop::Detail()
{
    bool target=app_.selectedDevice_&&target_>=0&&target_<static_cast<int>(targets_.size());
    Set(TargetTitle,target?T(L"分配到：",L"Assign to: ")+targets_[target_].label:T(L"请先选择一个控件",L"Select a control first"));
    auto row=Row(window_,Commands);bool command=row>=0&&row<static_cast<int>(catalog_.size());
    Set(DetailText,command?mackie::ui::Description(catalog_[row],zh_):T(L"搜索或浏览命令；选中后在这里查看说明。",L"Search or browse commands. Select one to see what it does."));
    EnableWindow(GetDlgItem(window_,ApplyButton),target&&command&&!capture_);EnableWindow(GetDlgItem(window_,Remove),target&&!Binding(targets_[target_]).empty()&&!capture_);
    if(page_==5&&target)
    {
        int ticks=app_.Settings().cursorTicks[targets_[target_].axis],index=ticks==8?3:ticks==4?2:ticks==2?1:0;
        std::vector<std::wstring> values;for(int n:{1,2,4,8})values.push_back(std::to_wstring(n)+(zh_?L" 格 / 次":n==1?L" tick":L" ticks"));
        Fill(window_,Sensitivity,values,index);
    }
}
bool MackieDesktop::Editing() const { return app_.IsSelectedDevice()&&window_&&IsWindowVisible(window_)&&page_>=3&&page_<=5; }
void MackieDesktop::Notice(const std::wstring& zh,const std::wstring& en,bool error)
{notice_=zh_?zh:en;noticeError_=error;noticeUntil_=GetTickCount64()+7000;if(window_)InvalidateRect(window_,nullptr,FALSE);}
bool MackieDesktop::Persist()
{if(!app_.selectedDevice_)return false;app_.Settings().trackOrder=app_.Surface().Order();if(!app_.Settings().Save()){Notice(L"无法保存设置，请检查配置目录。",L"Could not save settings. Check the configuration folder.",true);return false;}app_.DirtySettings()=false;return true;}
void MackieDesktop::Apply(bool remove)
{
    if(!app_.selectedDevice_||target_<0||target_>=static_cast<int>(targets_.size())||app_.Surface().Touched()){Notice(L"请先选择控件并松开推子。",L"Select a control and release any touched fader.",true);return;}
    auto row=Row(window_,Commands);if(!remove&&(row<0||row>=static_cast<int>(catalog_.size())))return;
    const auto target=targets_[target_];auto id=remove?L"":catalog_[row];const auto previous=Binding(target);
    if(!remove&&!previous.empty()&&previous!=id)
    {
        auto text=target.label+L"\n\n"+mackie::ui::Name(previous,zh_)+L"  →  "+mackie::ui::Name(id,zh_)+L"\n\n"+T(L"替换这个控件的原有分配？",L"Replace the existing assignment for this control?");
        if(MessageBoxW(window_,text.c_str(),T(L"确认分配",L"Confirm assignment").c_str(),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
    }
    auto next=app_.Settings();
    if(page_==3){if(!MackieSettings::Bindable(target.key/128,target.key%128)||(!remove&&!FindMackieCommand(id)))return;if(remove)next.bindings.erase(target.key);else next.bindings[target.key]=id;}
    if(page_==4){if(!ValidEncoderCommand(id))return;next.encoders[target.axis][target.gesture]=id;}
    if(page_==5){if(!mackie::ValidDirectionCommand(target.axis,id))return;next.cursorCommands[target.axis][target.gesture]=id;}
    next.trackOrder=app_.Surface().Order();if(!next.Save()){Notice(L"保存失败，原分配未改变。",L"Save failed. The original assignment has not changed.",true);return;}
    app_.Settings()=std::move(next);app_.DirtySettings()=false;
    if(page_==3&&app_.Midi().Connected())app_.Midi().Send({static_cast<std::uint8_t>(0x90|target.key/128),static_cast<std::uint8_t>(target.key%128),0});
    app_.CustomFeedback().clear();app_.Surface().InvalidateFeedback();app_.FillBindings();
    for(int i=0;i<2;++i)app_.Surface().JogSeekDirections[i]=mackie::JogSeekDirection(app_.Settings().cursorCommands[4][i]);
    app_.Surface().ResetEncoderInput();app_.Surface().ResetJogInput();Targets();Detail();
    Notice(L"分配已保存。返回通道总览后即可测试。",L"Assignment saved. Return to Overview to test.");
}
bool MackieDesktop::Midi(DWORD raw)
{
    if(!Editing())return false;
    int channel=raw&15,type=raw&0xF0,note=(raw>>8)&127,value=(raw>>16)&127;
    if(channel==0&&((note>=0x68&&note<=0x70)&&(type==0x90||type==0x80)))return false;
    if(channel==0&&type==0xB0&&((note>=0x10&&note<=0x17)||note==0x3C))
    {
        auto delta=mackie::Delta(value);if(delta){inputPreview_=(note==0x3C?L"Jog":T(L"旋钮 ",L"Encoder ")+std::to_wstring(note-0x10+1))+L" · "+(delta>0?T(L"右转",L"Right"):T(L"左转",L"Left"));Set(InputPreview,T(L"检测到：",L"Detected: ")+inputPreview_);}return true;
    }
    if(type!=0x90&&type!=0x80)return false;
    int key=channel*128+note;bool down=type==0x90&&value!=0;
    const bool previous=app_.CustomPressed()[key];
    if(MackieSettings::Bindable(channel,note))app_.CustomPressed()[key]=down;
    if(capture_&&down&&!previous)
    {
        if(!MackieSettings::Bindable(channel,note)){Notice(L"这是保留的核心操作，请按一个可分配的功能键。",L"This is a reserved control. Press an assignable function key.",true);return channel!=0||note<0x60||note>0x65;}
        capture_=false;Set(Capture,T(L"识别设备按键",L"Detect a button"));
        auto it=std::find_if(targets_.begin(),targets_.end(),[&](const auto& t){return t.key==key;});
        if(it==targets_.end()){targets_.push_back({key,-1,0,L"MIDI "+std::to_wstring(channel+1)+L" · Note "+std::to_wstring(note)});target_=static_cast<int>(targets_.size())-1;}else target_=static_cast<int>(it-targets_.begin());
        selectedCommand_=Binding(targets_[target_]);Targets();Catalog();Detail();ListView_EnsureVisible(GetDlgItem(window_,TargetList),target_,FALSE);
        Notice(L"已识别按键。请选择命令，然后点击应用分配。",L"Button detected. Choose a command, then select Assign command.");return true;
    }
    if(MackieSettings::Bindable(channel,note))return true;
    if(channel==0&&note>=0x20&&note<=0x27){if(down)Set(InputPreview,T(L"检测到旋钮按下：",L"Encoder push detected: ")+std::to_wstring(note-0x20+1));return true;}
    return false; // Native touch, Select, banking and Zoom state keep their original path.
}
bool MackieDesktop::Preview(const mackie::Action& a)
{
    if(!Editing())return false;
    if(a.kind==mackie::ActionKind::CursorCommand){Set(InputPreview,T(L"检测到：",L"Detected: ")+std::wstring(mackie::CursorLabels[a.encoder])+(a.value>0?L" +":L" −"));return true;}
    if(a.kind==mackie::ActionKind::Encoder||a.kind==mackie::ActionKind::SeekSeconds)return true;
    return false;
}
void MackieDesktop::Ports()
{
    displayedInputs_=app_.inputs_;displayedOutputs_=app_.outputs_;
    std::vector<std::wstring> ins,outs;
    for(auto& p:displayedInputs_)ins.push_back(p.name+(mackie::ReservedPort(p.name)?T(L"（保留端口）",L" (reserved)"):L""));
    for(auto& p:displayedOutputs_)outs.push_back(p.name+(mackie::ReservedPort(p.name)?T(L"（保留端口）",L" (reserved)"):L""));
    const auto pick=[](const auto& ports,const std::wstring& name){auto unique=mackie::UniquePort(ports,name);if(unique)for(int i=0;i<static_cast<int>(ports.size());++i)if(ports[i].index==*unique)return i;return -1;};
    Fill(window_,InPort,ins,pick(displayedInputs_,app_.Settings().input));Fill(window_,OutPort,outs,pick(displayedOutputs_,app_.Settings().output));
}
void MackieDesktop::Device(bool connect)
{
    if(app_.Midi().Connected()){app_.Disconnect();Notice(L"设备已断开。",L"Device disconnected.");Tick();return;}
    if(page_!=2){Navigate(2);return;}
    int a=Choice(window_,InPort),b=Choice(window_,OutPort);
    if(!SaveDevice())return;
    if(connect&&a>=0&&b>=0&&a<static_cast<int>(displayedInputs_.size())&&b<static_cast<int>(displayedOutputs_.size()))
        app_.OpenDevice(displayedInputs_[a].index,displayedOutputs_[b].index);
    if(!app_.Midi().Connected())Notice(L"未能连接，请检查端口及占用情况。",L"Could not connect. Check the port pair and availability.",true);
    else Notice(L"控制器已连接。",L"Controller connected.");
    Tick();
}
void MackieDesktop::CommitPreferences()
{
    auto previous=app_.workspace_;
    int lang=Choice(window_,Language);if(lang<0||lang>1)return;
    app_.workspace_.language=lang==0?L"zh":L"en";
    if(!app_.workspace_.Save()){app_.workspace_=std::move(previous);return;}
    zh_=lang==0;Build();Notice(L"语言设置已保存。",L"Language preference saved.");
}
void MackieDesktop::DeviceChoices()
{
    std::vector<std::wstring> labels;int selected=-1;
    for(int i=0;i<static_cast<int>(app_.devices_.size());++i){labels.push_back(app_.DeviceLabel(*app_.devices_[i]));if(app_.devices_[i].get()==app_.selectedDevice_)selected=i;}
    if(GetDlgItem(window_,DevicePicker))Fill(window_,DevicePicker,labels,selected);
    if(GetDlgItem(window_,DeviceList))Rows(GetDlgItem(window_,DeviceList),labels,selected);
}
bool MackieDesktop::SaveDevice()
{
    int a=Choice(window_,InPort),b=Choice(window_,OutPort),profile=Choice(window_,Profile);
    if(a<0||b<0||a>=static_cast<int>(displayedInputs_.size())||b>=static_cast<int>(displayedOutputs_.size())||profile<0)
    {Notice(L"请选择 MIDI 输入和输出。",L"Choose a MIDI input and output.",true);return false;}
    if(!app_.SaveDeviceConfiguration(Read(GetDlgItem(window_,DeviceName)),displayedInputs_[a].name,displayedOutputs_[b].name,profile==1?L"p1-nano":L"mcu"))
    {Notice(L"无法保存：端口不匹配、被其他设备使用或写入失败。",L"Could not save: mismatched ports, a port conflict, or a write error.",true);return false;}
    return true;
}
void MackieDesktop::OpenFolder(const std::filesystem::path& path)
{
    HRESULT result=E_INVALIDARG;
    try
    {
        if(!path.empty())
        {
            std::filesystem::create_directories(path);
            const auto folder=std::filesystem::weakly_canonical(path);
            PIDLIST_ABSOLUTE item=nullptr;
            result=SHParseDisplayName(folder.c_str(),nullptr,&item,0,nullptr);
            if(SUCCEEDED(result))
            {
                SHELLEXECUTEINFOW info{sizeof(info)};info.hwnd=window_;info.fMask=SEE_MASK_IDLIST|SEE_MASK_FLAG_NO_UI;
                info.lpVerb=L"open";info.lpIDList=item;info.nShow=SW_SHOWNORMAL;
                result=ShellExecuteExW(&info)?S_OK:HRESULT_FROM_WIN32(GetLastError());CoTaskMemFree(item);
            }
        }
    }catch(const std::exception& e){FB_TRACE("MACKIE_FOLDER_ERROR %s",e.what());result=E_FAIL;}
    if(FAILED(result)){FB_TRACE("MACKIE_FOLDER_OPEN hr=%08X",result);Notice(L"无法打开目录，请查看日志。",L"Could not open the folder. See the log.",true);}
}
LRESULT CALLBACK MackieDesktop::FieldProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data)
{
    auto self=reinterpret_cast<MackieDesktop*>(data);
    if(message==WM_NCDESTROY){RemoveWindowSubclass(window,FieldProc,id);return DefSubclassProc(window,message,w,l);}
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_NCPAINT)
    {
        auto dc=GetWindowDC(window);RECT r{};GetWindowRect(window,&r);OffsetRect(&r,-r.left,-r.top);
        auto brush=CreateSolidBrush(Edge);FrameRect(dc,&r,brush);DeleteObject(brush);ReleaseDC(window,dc);return 0;
    }
    if(message==WM_PAINT||message==WM_PRINTCLIENT)
    {
        PAINTSTRUCT paint{};auto dc=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(w);
        if(!dc)return 0;
        RECT r{};GetClientRect(window,&r);auto brush=CreateSolidBrush(Card);FillRect(dc,&r,brush);DeleteObject(brush);
        int width=MulDiv(r.right,96,self->dpi_),height=MulDiv(r.bottom,96,self->dpi_);
        self->Panel(dc,0,0,width,height,Card,GetFocus()==window?Amber:Edge);
        const int selected=static_cast<int>(SendMessageW(window,CB_GETCURSEL,0,0));std::wstring label;
        if(selected>=0){int n=static_cast<int>(SendMessageW(window,CB_GETLBTEXTLEN,selected,0));if(n>=0&&n<32768){label.resize(n+1);SendMessageW(window,CB_GETLBTEXT,selected,reinterpret_cast<LPARAM>(label.data()));label.resize(n);}}
        self->Text(dc,label,12,0,width-48,height,IsWindowEnabled(window)?Ink:Muted);
        auto font=SelectObject(dc,self->iconFont_);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(window)?Ink:Muted);
        RECT arrow{r.right-self->S(32),0,r.right-self->S(8),r.bottom};DrawTextW(dc,L"\uE70D",1,&arrow,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,font);
        if(message==WM_PAINT)EndPaint(window,&paint);return 0;
    }
    auto result=DefSubclassProc(window,message,w,l);
    if(message==CB_SETCURSEL||message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_ENABLE||message==CB_SHOWDROPDOWN)InvalidateRect(window,nullptr,FALSE);
    return result;
}
LRESULT CALLBACK MackieDesktop::ToggleProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data)
{
    auto self=reinterpret_cast<MackieDesktop*>(data);
    if(message==WM_NCDESTROY){RemoveWindowSubclass(window,ToggleProc,id);return DefSubclassProc(window,message,w,l);}
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT||message==WM_PRINTCLIENT)
    {
        PAINTSTRUCT paint{};auto dc=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(w);if(!dc)return 0;
        RECT r{};GetClientRect(window,&r);auto brush=CreateSolidBrush(Card);FillRect(dc,&r,brush);DeleteObject(brush);
        bool on=SendMessageW(window,BM_GETCHECK,0,0)==BST_CHECKED,enabled=IsWindowEnabled(window)!=FALSE;
        int width=MulDiv(r.right,96,self->dpi_),height=MulDiv(r.bottom,96,self->dpi_);
        auto fill=on?(enabled?Amber:RGB(119,94,53)):Edge;
        auto pen=CreatePen(PS_SOLID,1,GetFocus()==window?Ink:fill);brush=CreateSolidBrush(fill);auto op=SelectObject(dc,pen),ob=SelectObject(dc,brush);
        RoundRect(dc,1,self->S(2),r.right-1,r.bottom-self->S(2),self->S(height),self->S(height));
        SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(pen);DeleteObject(brush);
        int diameter=height-12,left=on?width-diameter-6:6;
        brush=CreateSolidBrush(enabled?(on?Bg:Ink):Muted);ob=SelectObject(dc,brush);op=SelectObject(dc,GetStockObject(NULL_PEN));
        Ellipse(dc,self->S(left),self->S(6),self->S(left+diameter),self->S(6+diameter));SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(brush);
        if(message==WM_PAINT)EndPaint(window,&paint);return 0;
    }
    auto result=DefSubclassProc(window,message,w,l);
    if(message==BM_SETCHECK||message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_ENABLE)InvalidateRect(window,nullptr,FALSE);
    return result;
}
void MackieDesktop::Act(int id,int code)
{
    if(building_)return;
    if(id>=Nav&&id<Nav+8){Navigate(id-Nav);return;}
    switch(id)
    {
    case Hide: if(app_.tray_.hWnd){capture_=false;ShowWindow(window_,SW_HIDE);}break;
    case Prev:app_.Surface().Bank(-8);break;
    case Next:app_.Surface().Bank(8);break;
    case Connect:Device(true);break;
    case Language:if(code==CBN_SELCHANGE)CommitPreferences();break;
    case Profile:if(code==CBN_SELCHANGE)Tick();break;
    case TrayClose:{auto old=app_.workspace_.closeToTray;app_.workspace_.closeToTray=!old;if(!app_.workspace_.Save())app_.workspace_.closeToTray=old;Tick();}break;
    case Startup:if(!SetStartup(!StartupEnabled()))Notice(L"无法更新开机启动设置。",L"Could not update the startup preference.",true);Tick();break;
    case GlobalShortcut:
        {auto next=bridge::Shortcut{app_.workspace_.shortcutModifiers,app_.workspace_.shortcutKey};
        if(bridge::CaptureShortcut(window_,next,next,zh_))
        {auto previous=app_.workspace_;
        if(!app_.globalShortcut_.Apply(app_.window_,1,true,next))Notice(L"快捷键不可用或已被其他程序占用。原快捷键保持有效。",L"The shortcut is unavailable or already in use. The previous shortcut remains active.",true);
        else {app_.workspace_.shortcutModifiers=next.modifiers;app_.workspace_.shortcutKey=next.key;
        if(!app_.workspace_.Save()){app_.workspace_=previous;app_.globalShortcut_.Apply(app_.window_,1,true,{previous.shortcutModifiers,previous.shortcutKey});Notice(L"快捷键无法保存，原快捷键保持有效。",L"The shortcut could not be saved. The previous shortcut remains active.",true);}else {Set(GlobalShortcut,bridge::ShortcutText(next));Notice(L"全局快捷键已更新。",L"Global shortcut updated.");}}}break;}
    case Touch:case Lcd:case Meters:
        if(!app_.Midi().Connected())
        {auto old=app_.Settings();if(id==Touch)app_.Settings().touch=!old.touch;if(id==Lcd)app_.Settings().lcd=!old.lcd;if(id==Meters)app_.Settings().meters=!old.meters;if(!Persist())app_.Settings()=old;
        app_.Surface().RequireTouch=app_.Settings().touch;app_.Surface().LcdEnabled=app_.Settings().lcd;app_.Surface().MetersEnabled=app_.Settings().meters;
        for(auto pair:{std::pair<int,bool>{106,app_.Settings().touch},{107,app_.Settings().lcd},{108,app_.Settings().meters}})SendDlgItemMessageW(app_.window_,pair.first,BM_SETCHECK,pair.second?BST_CHECKED:BST_UNCHECKED,0);Tick();}break;
    case Refresh:app_.inputs_=WinMidiPort::Inputs();app_.outputs_=WinMidiPort::Outputs();Ports();break;
    case DevicePicker:if(code==CBN_SELCHANGE){int i=Choice(window_,DevicePicker);if(i>=0&&app_.SelectDevice(i)){capture_=false;Build();}}break;
    case AddDevice:if(app_.AddDevice())Build();else Notice(L"无法添加设备。",L"Could not add device.",true);break;
    case RemoveDevice:
        if(app_.selectedDevice_&&MessageBoxW(window_,T(L"移除这台设备？分配文件会保留在配置目录。",L"Remove this device? Its mapping file will remain in the settings folder.").c_str(),T(L"移除设备",L"Remove device").c_str(),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)
        {if(app_.RemoveDevice())Build();else Notice(L"无法移除设备，请先松开推子。",L"Could not remove device. Release any touched fader.",true);}break;
    case SaveDeviceButton:if(SaveDevice()){Build();Notice(L"设备已保存。",L"Device saved.");}break;
    case AutoConnect:
        {auto old=app_.Settings().autoConnect;app_.Settings().autoConnect=!old;if(!Persist())app_.Settings().autoConnect=old;
        else if(app_.Settings().autoConnect){app_.Device().connection.Resume();app_.deviceScanDue_=0;}Tick();}break;
    case Search:if(code==EN_CHANGE)Catalog();break;
    case Categories:if(code==CBN_SELCHANGE)Catalog();break;
    case TargetSearch:if(code==EN_CHANGE){Targets();Detail();}break;
    case Capture:
        if(capture_){capture_=false;Set(Capture,T(L"识别设备按键",L"Detect a button"));Detail();break;}
        if(!app_.Midi().Connected()||app_.Surface().Touched()){Notice(L"请先连接设备并松开推子。",L"Connect a device and release all faders first.",true);break;}
        capture_=true;Set(Capture,T(L"取消识别",L"Cancel detection"));Notice(L"请按设备上的目标按键；此时不会执行命令。",L"Press the desired hardware button. No command will run.");Detail();break;
    case ApplyButton:Apply(false);break;
    case Remove:Apply(true);break;
    case Sensitivity:case Seconds:
        if(code==CBN_SELCHANGE&&target_>=0)
        {auto old=app_.Settings();if(id==Seconds){int i=Choice(window_,Seconds);if(i>=0&&i<10)app_.Settings().jog.seekSeconds=i+1;}else{int i=Choice(window_,Sensitivity);if(i>=0&&i<4)app_.Settings().cursorTicks[targets_[target_].axis]=1<<i;}
        if(!Persist())app_.Settings()=old;app_.Surface().Jog.settings=app_.Settings().jog;app_.Surface().CursorGestures.ticks=app_.Settings().cursorTicks;app_.Surface().ResetJogInput();Detail();}break;
    case Layer:if(code==CBN_SELCHANGE){int i=Choice(window_,Layer);if(i==0||i==1)app_.Surface().SetCursorZoom(i!=0);}break;
    case Log:OpenFolder(std::filesystem::path(DiagnosticLog::Instance().Path()).parent_path());break;
    case Trace:{bool on=std::any_of(app_.devices_.begin(),app_.devices_.end(),[](const auto& d){return d->midi.Trace.load();});for(auto& d:app_.devices_)d->midi.Trace.store(!on);Tick();}break;
    case Preset:
        if(app_.Midi().Connected()){Notice(L"请先断开设备再应用预设。",L"Disconnect the device before applying a preset.",true);break;}
        if(page_!=2||Choice(window_,Profile)!=1||!SaveDevice())break;
        app_.ApplyWindowsPreset();
        {bool complete=true;for(int note=0;note<static_cast<int>(Windows80Commands.size());++note){auto it=app_.Settings().bindings.find(15*128+note);if(it==app_.Settings().bindings.end()||it->second!=Windows80Commands[note])complete=false;}
        if(complete)Notice(L"80 键预设已就绪，可在按键分配页查看。",L"The 80-key preset is ready. Review it in Button mapping.");
        else Notice(L"预设未应用：存在冲突分配或保存失败。原分配保持不变。",L"Preset not applied: conflicting mappings or a save error. Existing mappings are unchanged.",true);}break;
    case LicenseLink:ShellExecuteW(window_,L"open",L"https://github.com/lindelea/windows-fader-bridge/blob/main/LICENSE",nullptr,nullptr,SW_SHOWNORMAL);break;
    case VersionLink:ShellExecuteW(window_,L"open",L"https://github.com/lindelea/windows-fader-bridge",nullptr,nullptr,SW_SHOWNORMAL);break;
    case IssuesLink:ShellExecuteW(window_,L"open",L"https://github.com/lindelea/windows-fader-bridge/issues",nullptr,nullptr,SW_SHOWNORMAL);break;
    }
}
void MackieDesktop::Tick()
{
    if(!window_||!IsWindowVisible(window_)||building_)return;
    Set(Connect,app_.Midi().Connected()?T(L"断开设备",L"Disconnect"):T(L"连接设备",L"Connect device"));
    const auto check=[&](int id,bool value){auto c=GetDlgItem(window_,id);if(c){SendMessageW(c,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);InvalidateRect(c,nullptr,FALSE);}};
    if(page_==1){check(TrayClose,app_.workspace_.closeToTray);check(Startup,StartupEnabled());}
    if(page_==2)
    {
        check(Touch,app_.Settings().touch);check(Lcd,app_.Settings().lcd);check(Meters,app_.Settings().meters);check(AutoConnect,app_.Settings().autoConnect);
        for(int id:{InPort,OutPort,Profile,Touch,Lcd,Meters})EnableWindow(GetDlgItem(window_,id),app_.selectedDevice_&&!app_.Midi().Connected());
        for(int id:{DeviceName,AutoConnect,Connect,SaveDeviceButton,RemoveDevice})EnableWindow(GetDlgItem(window_,id),app_.selectedDevice_!=nullptr);
        EnableWindow(GetDlgItem(window_,AddDevice),app_.devices_.size()<16);InvalidateRect(GetDlgItem(window_,DeviceList),nullptr,FALSE);
        const bool p1Nano=app_.selectedDevice_&&Choice(window_,Profile)==1;
        ShowWindow(GetDlgItem(window_,Preset),p1Nano?SW_SHOWNA:SW_HIDE);
        EnableWindow(GetDlgItem(window_,Preset),p1Nano&&!app_.Midi().Connected());
    }
    if(page_==6)check(Trace,std::any_of(app_.devices_.begin(),app_.devices_.end(),[](const auto& d){return d->midi.Trace.load();}));
    if(page_==5&&Choice(window_,Layer)!=(app_.Surface().CursorZoom()?1:0))SendDlgItemMessageW(window_,Layer,CB_SETCURSEL,app_.Surface().CursorZoom()?1:0,0);
    if(page_==0)
    {
        auto list=GetDlgItem(window_,Tracks);const auto& order=app_.Surface().Order();auto selected=app_.Surface().Selected();listGuard_=true;
        const int desiredRowHeight=OverviewRowHeight();
        if(overviewRowHeight_!=0&&desiredRowHeight!=overviewRowHeight_){listGuard_=false;Build();return;}
        EnableWindow(GetDlgItem(window_,Prev),app_.Surface().BankStart()>0);EnableWindow(GetDlgItem(window_,Next),app_.Surface().BankStart()+8<static_cast<int>(order.size()));
        if(order!=trackKeys_){trackKeys_=order;std::vector<std::wstring> labels;for(auto& key:order){auto s=app_.Find(key);labels.push_back(s?s->name:key);}Rows(list,labels,-1);}
        for(int i=0;i<static_cast<int>(trackKeys_.size());++i)
        {
            auto strip=app_.Find(trackKeys_[i]);auto label=strip?strip->name+L" · "+Percent(strip->volume):T(L"等待通道更新",L"Waiting for channel");
            wchar_t old[2048]{};ListView_GetItemText(list,i,0,old,2048);if(label!=old)ListView_SetItemText(list,i,0,label.data());
            ListView_SetItemState(list,i,selected&&selected->key==trackKeys_[i]?LVIS_SELECTED:0,LVIS_SELECTED);
            if(selected&&selected->key==trackKeys_[i]&&selected->key!=lastSelectedKey_){ListView_EnsureVisible(list,i,FALSE);lastSelectedKey_=selected->key;}
        }
        RECT client{};GetClientRect(list,&client);ListView_SetColumnWidth(list,0,std::max(1L,client.right-2));
        InvalidateRect(list,nullptr,FALSE);listGuard_=false;
    }
    InvalidateRect(window_,nullptr,FALSE);
}
void MackieDesktop::AudioFrameChanged()
{
    if(!window_||!IsWindowVisible(window_)||building_||page_!=0)return;
    if(auto list=GetDlgItem(window_,Tracks))InvalidateRect(list,nullptr,FALSE);
}
LRESULT CALLBACK MackieDesktop::Proc(HWND w,UINT m,WPARAM a,LPARAM b)
{
    auto self=reinterpret_cast<MackieDesktop*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(m==WM_NCCREATE){self=static_cast<MackieDesktop*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);self->window_=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    return self?self->Message(m,a,b):DefWindowProcW(w,m,a,b);
}
LRESULT MackieDesktop::Message(UINT m,WPARAM a,LPARAM b)
{
    switch(m)
    {
    case WM_PAINT:{PAINTSTRUCT p{};auto dc=BeginPaint(window_,&p);auto memory=CreateCompatibleDC(dc);RECT r{};GetClientRect(window_,&r);auto bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(memory,bitmap);Paint(memory);BitBlt(dc,0,0,r.right,r.bottom,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);EndPaint(window_,&p);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_DRAWITEM:DrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(b));return TRUE;
    case WM_MEASUREITEM:{auto d=reinterpret_cast<MEASUREITEMSTRUCT*>(b);if(d->CtlID==Tracks){overviewRowHeight_=OverviewRowHeight();d->itemHeight=S(overviewRowHeight_);}else d->itemHeight=S(d->CtlType==ODT_COMBOBOX?30:56);return TRUE;}
    case WM_SIZE:if(!building_&&!children_.empty()){Layout();InvalidateRect(window_,nullptr,FALSE);}return 0;
    case WM_GETMINMAXINFO:{auto p=reinterpret_cast<MINMAXINFO*>(b);p->ptMinTrackSize={S(1080),S(690)};return 0;}
    case WM_COMMAND:Act(LOWORD(a),HIWORD(a));return 0;
    case WM_NOTIFY:
        if(!building_&&!listGuard_)
        {
            auto hdr=reinterpret_cast<NMHDR*>(b);
            if(hdr->code==LVN_ITEMCHANGED&&(reinterpret_cast<NMLISTVIEW*>(b)->uNewState&LVIS_SELECTED))
            {
                if(hdr->idFrom==DeviceList){int i=Row(window_,DeviceList);if(i>=0&&app_.SelectDevice(i)){capture_=false;Build();}}
                if(hdr->idFrom==Tracks){int i=Row(window_,Tracks);if(i>=0&&i<static_cast<int>(trackKeys_.size()))app_.Surface().Select(trackKeys_[i]);}
                if(hdr->idFrom==TargetList){int i=Row(window_,TargetList);if(i>=0&&i<static_cast<int>(targets_.size())){target_=i;selectedCommand_=Binding(targets_[i]);Catalog();Detail();}}
                if(hdr->idFrom==Commands){int i=Row(window_,Commands);if(i>=0&&i<static_cast<int>(catalog_.size()))selectedCommand_=catalog_[i];Detail();}
            }
        }return 0;
    case WM_CTLCOLORSTATIC:{bool detail=GetDlgCtrlID(reinterpret_cast<HWND>(b))==DetailText;SetTextColor(reinterpret_cast<HDC>(a),Ink);SetBkColor(reinterpret_cast<HDC>(a),detail?Card:Bg);return reinterpret_cast<LRESULT>(detail?field_:background_);}
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor(reinterpret_cast<HDC>(a),Ink);SetBkColor(reinterpret_cast<HDC>(a),Card);return reinterpret_cast<LRESULT>(field_);
    case WM_CLOSE:capture_=false;if(app_.workspace_.closeToTray&&app_.tray_.hWnd)ShowWindow(window_,SW_HIDE);else PostMessageW(app_.window_,WM_COMMAND,124,0);return 0;
    case WM_DESTROY:return 0;
    }
    return DefWindowProcW(window_,m,a,b);
}
