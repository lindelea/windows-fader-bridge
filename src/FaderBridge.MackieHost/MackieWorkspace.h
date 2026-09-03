#pragma once
#include "MackieSettings.h"
#include <fstream>
#include <iomanip>
#include <set>
#include <algorithm>
#include <sstream>
#include <cstdint>

// Only project-owned IDs may name device files. No paths are read from the manifest.
struct MackieWorkspace
{
    std::wstring language = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? L"zh" : L"en";
    bool closeToTray = true;
    std::uint32_t shortcutModifiers = 7, shortcutKey = 'M';
    std::vector<std::string> devices;
    std::string selected;
    std::filesystem::path root;
    static bool ValidId(const std::string& id)
    {
        return !id.empty() && id.size() <= 64 && std::all_of(id.begin(),id.end(),[](char c){
            return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'; });
    }
    std::filesystem::path DevicePath(const std::string& id) const
    { return !ValidId(id) ? std::filesystem::path{} : id=="primary" ? root/L"settings.txt" : root/L"devices"/(id+".txt"); }
    static MackieWorkspace Load(std::filesystem::path root = MackieSettings::Path().parent_path())
    {
        MackieWorkspace result;result.root=std::move(root);
        std::ifstream in(result.root/L"workspace.txt",std::ios::binary);
        if(!in)
        {
            auto legacy=MackieSettings::Load(result.root/L"settings.txt");
            result.language=legacy.language;result.closeToTray=legacy.closeToTray;
            result.devices={"primary"};result.selected="primary";return result;
        }
        std::string line;std::set<std::string> seen;
        for(int count=0;count<256&&std::getline(in,line);++count)
        {
            if(line.size()>1024)continue;
            std::istringstream row(line);std::string key,value;row>>key;
            if(key=="device"&&row>>std::quoted(value)&&ValidId(value)&&result.devices.size()<16&&seen.insert(value).second)result.devices.push_back(value);
            else if(key=="selected"&&row>>std::quoted(value)&&ValidId(value))result.selected=value;
            else if(key=="language"&&row>>std::quoted(value)&&(value=="zh"||value=="en"))result.language=value=="en"?L"en":L"zh";
            else if(key=="closeToTray"){int n=-1;if(row>>n&&(n==0||n==1))result.closeToTray=n!=0;}
            else if(key=="shortcutModifiers"){unsigned n=0;if(row>>n&&n&&!(n&~15U))result.shortcutModifiers=n;}
            else if(key=="shortcutKey"){unsigned n=0;if(row>>n&&n<=0xFE)result.shortcutKey=n;}
        }
        if(std::find(result.devices.begin(),result.devices.end(),result.selected)==result.devices.end())
            result.selected=result.devices.empty()?"":result.devices.front();
        return result;
    }
    bool Save() const
    {
        try
        {
            if(root.empty()||devices.size()>16||!shortcutModifiers||
               (shortcutModifiers&~15U)||!shortcutKey||shortcutKey>0xFE)return false;
            std::set<std::string> seen;
            for(auto& id:devices)if(!ValidId(id)||!seen.insert(id).second)return false;
            std::filesystem::create_directories(root);
            auto path=root/L"workspace.txt",temp=root/L"workspace.txt.new";
            {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)return false;
            out<<"version 1\nlanguage "<<std::quoted(language==L"en"?"en":"zh")
               <<"\ncloseToTray "<<closeToTray<<"\nshortcutModifiers "<<shortcutModifiers
               <<"\nshortcutKey "<<shortcutKey<<"\nselected "<<std::quoted(selected)<<'\n';
            for(auto& id:devices)out<<"device "<<std::quoted(id)<<'\n';out.flush();if(!out)return false;}
            return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
        }catch(...){return false;}
    }
};
