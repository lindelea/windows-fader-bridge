#pragma once
#include "Model.h"

inline apollo::NodeMap ChannelFeatureFixture(const std::string &path)
{
    using apollo::Json;
    apollo::NodeMap n;
    // Invented data, deliberately different slots/names from the real interface.
    n[path] = Json::Parse(R"({"properties":{
      "Active":{"type":"bool","value":true},"Name":{"type":"string","value":"Feature fixture"},
      "IOType":{"type":"string","value":"Mic","values":["Mic","Line","Dante"]},
      "Stereo":{"type":"bool","value":false},"FaderLevel":{"type":"float","min":-144,"max":12,"value":-20},
      "Mute":{"type":"bool","value":false},"Solo":{"type":"bool","value":false},
      "Pan":{"type":"float","min":-1,"max":1,"value":0},
      "OutputDestination":{"type":"string","value":"Monitor","values":[{"value":"Monitor","enabled":true},{"value":"Line 1-2","enabled":true},{"value":"Unavailable","enabled":false}]},
      "PGADisabledInLineMode":{"type":"bool","value":false}},
      "children":{"meters":{},"sends":{},"preamps":{},"effects":{}}})");
    n[path + "/sends"] = Json::Parse(R"({"children":{"5":{},"9":{}}})");
    for (const auto slot : {"5", "9"})
    {
        auto send = Json::Parse(R"({"properties":{"Type":{"type":"string","value":"send"},
          "Name":{"type":"string","value":"Fixture send"},"ID":{"type":"string","value":"test-destination"},
          "Gain":{"type":"float","min":-144,"max":12,"value":-30},"Bypass":{"type":"bool","value":false},
          "Pan":{"type":"float","min":-1,"max":1,"value":0.25},"PostFaderOverride":{"type":"int","min":-1,"max":1,"value":-1}}})");
        send.object["properties"].object["ID"].object["value"].scalar += slot;
        send.object["properties"].object["Name"].object["value"].scalar += slot;
        n[path + "/sends/" + slot] = send;
    }
    n[path + "/preamps"] = Json::Parse(R"({"children":{"0":{}}})");
    n[path + "/preamps/0"] = Json::Parse(R"({"properties":{"Type":{"type":"string","value":"preamp"},
      "Gain":{"type":"float","min":-144,"max":65,"value":20},"LowCut":{"type":"bool","value":false},
      "Phase":{"type":"bool","value":false},"Pad":{"type":"bool","value":false},
      "HiZ":{"type":"bool","value":false},"48V":{"type":"bool","value":false}},"children":{"effects":{}}})");
    n[path + "/preamps/0/effects"] = Json::Parse(R"({"children":{}})");
    n[path + "/effects"] = Json::Parse(R"({"children":{"0":{},"2":{}}})");
    n[path + "/effects/0"] = Json::Parse(
        R"({"properties":{"Type":{"type":"string","value":"effect"},"EffectInstance":{"type":"pointer","value":0},"EffectName":{"type":"string","value":""}}})");
    n[path + "/effects/2"] = Json::Parse(R"({"properties":{"Type":{"type":"string","value":"effect"},
      "EffectInstance":{"type":"pointer","readonly":true,"value":987654321},"EffectName":{"type":"string","value":"Fixture Processor"},
      "Power":{"type":"bool","value":true}},"children":{"parameters":{}}})");
    n[path + "/effects/2/parameters"] = Json::Parse(R"({"children":{"0":{},"1":{},"2":{}}})");
    n[path + "/effects/2/parameters/0"] = Json::Parse(
        R"({"properties":{"Type":{"type":"string","value":"parameter"},"Name":{"type":"string","value":"Time"},"StringValue":{"type":"string","value":"3.2 ms"},"NormalizedValue":{"type":"float","min":0,"max":1,"value":0.25}}})");
    n[path + "/effects/2/parameters/1"] = Json::Parse(
        R"({"properties":{"Type":{"type":"string","value":"parameter"},"Name":{"type":"string","value":"Mode"},"StringValue":{"type":"string","value":"Slow"},"NormalizedValue":{"type":"float","min":0,"max":1,"value":0},"StepValue":{"type":"string","values":["Slow","Fast"],"value":"Slow"}}})");
    n[path + "/effects/2/parameters/2"] = Json::Parse(
        R"({"properties":{"Type":{"type":"string","value":"parameter"},"Name":{"type":"string","value":"Meter"},"NormalizedValue":{"type":"float","readonly":true,"min":0,"max":1,"value":0.5}}})");
    return n;
}

inline void AddUnisonFixture(apollo::NodeMap &nodes, const std::string &channel)
{
    const auto fixture = ChannelFeatureFixture(channel);
    const auto destination = channel + "/preamps/0/effects/0";
    const auto source = channel + "/effects/2";
    nodes[channel + "/preamps/0/effects"] = apollo::Json::Parse(R"({"children":{"0":{}}})");
    for (const auto &entry : fixture)
        if (entry.first == source || entry.first.rfind(source + "/", 0) == 0)
            nodes[destination + entry.first.substr(source.size())] = entry.second;
    nodes[destination].object["properties"].object["EffectName"].object["value"].scalar = "Fixture UNISON";
    nodes[destination].object["properties"].object["EffectInstance"].object["value"].scalar = "76543210";
}
inline void AddMonitorFixture(apollo::NodeMap &nodes, const std::string &device = "/devices/3")
{
    using apollo::Json;
    auto &p = nodes[device].object["properties"].object;
    p["SurroundMonitorMode"] = Json::Parse(R"({"type":"string","value":"STEREO"})");
    p["AltMonSelection"] = Json::Parse(R"({"type":"int","min":0,"max":2,"value":0})");
    p["DimAttenuation"] = Json::Parse(R"({"type":"int","min":0,"max":60,"value":17})");
    p["Enable24dBMode"] = Json::Parse(R"({"type":"bool","value":false})");
    nodes[device + "/outputs/40"] = Json::Parse(R"({"properties":{
      "IOType":{"type":"string","value":"Monitor"},"Stereo":{"type":"bool","value":true},
      "Name":{"type":"string","value":"Fixture monitoring"},
      "MixInSource":{"type":"string","value":"mon","values":["mon","cue1","cue2","cue3","cue4"]},
      "CRMonitorLevel":{"type":"float","min":-96,"max":0,"value":-30},
      "Mute":{"type":"bool","value":false},"DimOn":{"type":"bool","value":false},
      "MixToMono":{"type":"bool","value":false}}})");
}

inline void AddConfigurationFixture(apollo::NodeMap &nodes, const std::string &device = "/devices/3")
{
    using apollo::Json;
    auto &root = nodes["/"].object["properties"].object;
    root["ClipHold"] = Json::Parse(R"({"type":"string","value":"NONE","values":["NONE","2 SEC","5 SEC"]})");
    root["PostFaderMetering"] = Json::Parse(R"({"type":"bool","value":false})");
    root["SampleRate"] = Json::Parse(R"({"type":"int","value":48000,"values":[{"value":48000,"string":"48 kHz"},{"value":96000,"string":"96 kHz"}]})");
    auto &outputs = nodes[device + "/outputs"].object["children"].object;
    outputs["44"] = Json::Parse("{}");
    outputs["46"] = Json::Parse("{}");
    nodes[device + "/outputs/44"] = Json::Parse(R"({"properties":{"IOType":{"value":"Cue"},"Name":{"value":"CUE 2"},"Stereo":{"value":true},"Active":{"value":true},"MixToMono":{"type":"bool","value":false},"MixInSource":{"type":"string","value":"mon","values":["mon","cue"]},"OutputDestination":{"type":"string","value":"None","values":["None","Line 9-10"]}}})");
    nodes[device + "/outputs/46"] = Json::Parse(R"({"properties":{"IOType":{"value":"Headphone"},"Name":{"value":"HP 2"},"Stereo":{"value":true},"Active":{"value":false},"MixInSource":{"type":"string","value":"cue2","values":["none","cue2","cue3"]}}})");
    nodes["/plugins"] = Json::Parse(R"({"children":{"7":{},"8":{}}})");
    nodes["/plugins/7"] = Json::Parse(R"({"properties":{"Type":{"value":"plugin"},"Name":{"value":"Fixture Processor"},"Unison":{"type":"bool","value":true},"Authorized":{"value":true},"Preset":{"values":[{"value":"Neutral","type":"file"},{"value":"Wide","type":"file"},{"value":"Folder","type":"folder"}]}}})");
    nodes["/plugins/8"] = Json::Parse(R"({"properties":{"Type":{"value":"plugin"},"Name":{"value":"Not Licensed"},"Unison":{"type":"bool","value":true},"Authorized":{"value":false}}})");
    nodes[device + "/inputs/0/effects/2"].object["properties"].object["Preset"] =
        Json::Parse(R"({"type":"string","value":"Neutral"})");
    const auto unison = device + "/inputs/0/preamps/0/effects/0";
    nodes[device + "/inputs/0/preamps/0/effects"] = Json::Parse(R"({"children":{"0":{}}})");
    nodes[unison] = Json::Parse(
        R"({"properties":{"Type":{"type":"string","value":"effect"},"EffectInstance":{"type":"pointer","readonly":true,"value":0},"EffectName":{"type":"string","value":""},"Preset":{"type":"string","value":""}}})");
}

inline apollo::NodeMap ConsoleFixture(const std::string &device = "/devices/3")
{
    using apollo::Json;
    apollo::NodeMap n;
    const auto id = device.substr(9);
    n["/"] = Json::Parse(
        R"({"properties":{"TalkbackOn":{"type":"bool","value":false},"TalkbackMaster":{"type":"int","value":0},"TalkbackInPhysicalCR":{"type":"bool","value":false},"TalkbackMicSelect":{"type":"int","value":0}}})");
    n["/"].object["properties"].object["TalkbackMaster"].object["value"] = Json::Parse(id);
    n["/devices"] = Json::Parse("{\"children\":{\"" + id + "\":{}}}");
    n[device] = Json::Parse(
        R"({"properties":{"DeviceOnline":{"type":"bool","value":true},"DeviceHwID":{"type":"int64","value":7654321},"DeviceName":{"type":"string","value":"Console fixture"},"TalkbackMaster":{"type":"int","value":0}}})");
    n[device].object["properties"].object["TalkbackMaster"].object["value"] = Json::Parse(id);
    n[device + "/inputs"] = Json::Parse(R"({"children":{"0":{},"4":{},"8":{},"12":{},"16":{}}})");
    for (const auto &e : ChannelFeatureFixture(device + "/inputs/0"))
        n[e.first] = e.second;
    n[device + "/inputs/0/meters"] = Json::Parse(R"({"children":{"0":{}}})");
    n[device + "/inputs/0/meters/0"] =
        Json::Parse(R"({"properties":{"MeterLevel":{"type":"float","min":-77,"max":0,"value":-25}}})");
    n[device + "/inputs/0"].object["properties"].object["RecordPreEffects"] =
        Json::Parse(R"({"type":"bool","value":true})");
    for (const auto &entry : std::vector<std::pair<std::string, std::string>>{
             {"4", "Line"}, {"8", "S/PDIF"}, {"12", "TalkbackMic"}, {"16", "Virtual"}})
    {
        const auto path = device + "/inputs/" + entry.first;
        auto channel = Json::Parse(
            R"({"properties":{"Active":{"type":"bool","value":true},"Name":{"type":"string","value":"Synthetic channel"},"IOType":{"type":"string","value":"Line"},"Stereo":{"type":"bool","value":false},"FaderLevel":{"type":"float","min":-144,"max":12,"value":-35},"Mute":{"type":"bool","value":false},"RecordPreEffects":{"type":"bool","value":true},"OutputDestination":{"type":"string","value":"Monitor","values":["Monitor","Line 1-2","Line 1","S/PDIF"]}},"children":{"effects":{}}})");
        auto &p = channel.object["properties"].object;
        p["IOType"].object["value"].scalar = entry.second;
        if (entry.second == "Line")
            p["Pad"] = Json::Parse(R"({"type":"bool","value":false})");
        if (entry.second == "S/PDIF")
            p["SRConvert"] = Json::Parse(R"({"type":"bool","value":false})");
        if (entry.second == "TalkbackMic")
            p["TalkbackEnabled"] = Json::Parse(R"({"type":"bool","value":true})");
        n[path] = channel;
        n[path + "/effects"] = Json::Parse(R"({"children":{"0":{},"2":{}}})");
        n[path + "/effects/0"] = n[path + "/effects/2"] = n[device + "/inputs/0/effects/0"];
    }
    n[device + "/auxs"] = Json::Parse(R"({"children":{"0":{}}})");
    n[device + "/auxs/0"] = Json::Parse(
        R"({"properties":{"Active":{"type":"bool","value":true},"Name":{"type":"string","value":"AUX fixture"},"FaderLevel":{"type":"float","min":-144,"max":12,"value":-35},"Mute":{"type":"bool","value":false},"MixToMono":{"type":"bool","value":false},"SendPostFader":{"type":"bool","value":true}}})");
    n[device + "/outputs"] = Json::Parse(R"({"children":{"28":{},"29":{},"40":{},"48":{},"49":{}}})");
    for (const auto &entry : std::vector<std::pair<std::string, std::string>>{
             {"28", "ANALOG 1"}, {"29", "ANALOG 2"}, {"48", "S/PDIF L"}, {"49", "S/PDIF R"}})
    {
        auto output = Json::Parse(
            R"({"properties":{"Active":{"type":"bool","value":true},"Name":{"type":"string","value":""},"IOType":{"type":"string","value":"Line"},"Stereo":{"type":"bool","value":false}}})");
        output.object["properties"].object["Name"].object["value"].scalar = entry.second;
        if (entry.first == "48" || entry.first == "49")
            output.object["properties"].object["IOType"].object["value"].scalar = "S/PDIF";
        n[device + "/outputs/" + entry.first] = output;
    }
    n[device + "/outputs/40"] = Json::Parse(
        R"({"properties":{"Name":{"type":"string","value":"Monitor"},"IOType":{"type":"string","value":"Monitor"},"Stereo":{"type":"bool","value":true}}})");
    return n;
}
