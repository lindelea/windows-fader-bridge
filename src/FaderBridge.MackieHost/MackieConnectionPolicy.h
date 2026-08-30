#pragma once
#include "MackieSettings.h"
#include <optional>
#include <cwctype>

namespace mackie
{
inline bool ReservedPort(std::wstring name)
{
    if(IconDawPort(name)==4)return true;
    std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return std::towlower(c);});
    return name.find(L"eumidi")!=std::wstring::npos||name.find(L"imap")!=std::wstring::npos;
}
inline bool SafePair(const std::wstring& input,const std::wstring& output,const std::wstring& profile)
{
    if(input.empty()||output.empty()||ReservedPort(input)||ReservedPort(output)||IconDawPort(input)==4||IconDawPort(output)==4)return false;
    int a=IconDawPort(input),b=IconDawPort(output);
    return (!(a||b)||a==b) && (profile!=L"p1-nano"||(a&&b));
}
template<class Ports> std::optional<unsigned> UniquePort(const Ports& ports,const std::wstring& name)
{
    std::optional<unsigned> result;
    for(auto& p:ports)if(p.name==name){if(result)return {};result=p.index;}
    return result;
}
struct ConnectionPolicy
{
    bool paused=false;
    std::uint64_t retryAt=0;
    bool Due(bool automatic,bool configured,std::uint64_t now) const
    {return automatic&&configured&&!paused&&now>=retryAt;}
    void Failed(std::uint64_t now){retryAt=now+5000;}
    void ManualDisconnect(){paused=true;}
    void Resume(){paused=false;retryAt=0;}
};
}
