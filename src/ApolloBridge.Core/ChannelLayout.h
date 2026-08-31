#pragma once
#include "ChannelControl.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace apollo
{
// A protocol-independent description of the documented EUCON knob-set model.
// Tests can verify semantics without initializing the SDK or touching hardware.
enum class ChannelFunction
{
    Aux,
    Mix,
    Input,
    Inserts,
    Console,
    Unison
};
struct ChannelCell
{
    std::string key, label;
    std::optional<ChannelAddress> knob, lower;
    bool lowerInverted = false;
    std::optional<std::string> lowerChoice;
    std::vector<ChannelCell> children;
};
struct ChannelKnobSet
{
    ChannelFunction function;
    std::vector<ChannelCell> cells;
};
inline bool Available(const Channel &c, ChannelAddress a)
{
    const auto &p = FieldParameter(c, a);
    if (!p || !p->enabled || p->reportedReadOnly)
        return false;
    try
    {
        if (ChoiceField(a))
        {
            for (const auto &choice : p->choices)
            {
                Json value;
                value.kind = Json::Kind::String;
                value.scalar = choice;
                try
                {
                    if (!ChannelCommand(c, a, value).empty())
                        return true;
                }
                catch (const std::invalid_argument &)
                {
                }
            }
            return false;
        }
        return !ChannelCommand(c, a, p->value).empty();
    }
    catch (...)
    {
        return false;
    }
}
inline ChannelCell ChannelKnob(const std::string &key, const std::string &label, ChannelAddress a)
{
    ChannelCell cell;
    cell.key = key;
    cell.label = label;
    cell.knob = std::move(a);
    return cell;
}
inline ChannelCell ChannelButton(const std::string &key, const std::string &label, ChannelAddress a)
{
    ChannelCell cell;
    cell.key = key;
    cell.label = label;
    cell.lower = std::move(a);
    return cell;
}
inline std::vector<ChannelKnobSet> DescribeChannel(const Channel &c, size_t pluginLimit = 0)
{
    if (!ControlEligible(c))
        return {};
    std::vector<ChannelKnobSet> result;
    ChannelKnobSet aux{ChannelFunction::Aux, {}};
    for (const auto &send : c.sends)
    {
        ChannelCell cell;
        cell.key = "Send." + send.slot + "." + StableToken(send.id);
        cell.label = send.name;
        const ChannelAddress gain{ChannelField::SendLevel, send.slot},
            mute{ChannelField::SendBypass, send.slot}, pan{ChannelField::SendPan, send.slot};
        if (Available(c, gain))
            cell.knob = gain;
        if (Available(c, mute))
        {
            cell.lower = mute;
            cell.lowerInverted = true;
        }
        if (Available(c, pan))
            cell.children.push_back(ChannelKnob("Pan", "Pan", pan));
        if (cell.knob || cell.lower || !cell.children.empty())
            aux.cells.push_back(std::move(cell));
    }
    if (!aux.cells.empty())
        result.push_back(std::move(aux));
    ChannelKnobSet mix{ChannelFunction::Mix, {}};
    if (Available(c, ChannelField::Output))
        for (const auto &choice : c.output->choices)
        {
            auto cell = ChannelButton("Output." + StableToken(choice), choice, ChannelField::Output);
            Json selected;
            selected.kind = Json::Kind::String;
            selected.scalar = choice;
            try
            {
                ChannelCommand(c, ChannelField::Output, selected);
            }
            catch (const std::invalid_argument &)
            {
                continue;
            }
            cell.lowerChoice = choice; // Routing is a deliberate press, never encoder rotation.
            mix.cells.push_back(std::move(cell));
        }
    if (!mix.cells.empty())
        result.push_back(std::move(mix));
    ChannelKnobSet input{ChannelFunction::Input, {}};
    for (const auto &preamp : c.preamps)
    {
        const auto suffix = c.preamps.size() > 1 ? " " + std::to_string(std::stoul(preamp.slot) + 1) : "";
        // Applicable parts of the guide's Input order. No blank fake controls.
        for (const auto &entry :
             std::vector<std::pair<ChannelField, std::string>>{{ChannelField::Phantom, "48V"},
                                                               {ChannelField::LowCut, "HPF"},
                                                               {ChannelField::PreampGain, "Gain"},
                                                               {ChannelField::Phase, "Phase"},
                                                               {ChannelField::Pad, "Pad"}})
        {
            const ChannelAddress address{entry.first, preamp.slot};
            if (!Available(c, address))
                continue;
            const auto key = "Preamp." + preamp.slot + "." + FieldName(address);
            input.cells.push_back(BooleanField(address) ? ChannelButton(key, entry.second + suffix, address)
                                                        : ChannelKnob(key, entry.second + suffix, address));
        }
    }
    if (Available(c, ChannelField::Input))
    {
        auto selector = ChannelCell{};
        selector.key = "Input";
        selector.label = "Input";
        // Selection is an explicit lower-switch action on the standard child
        // page; the physical Hi-Z jack overrides it and therefore hides it.
        for (const auto &choice : c.input->choices)
            if (choice == "Mic" || choice == "Line")
            {
                auto cell = ChannelButton("Input." + choice, choice, ChannelField::Input);
                cell.lowerChoice = choice;
                selector.children.push_back(std::move(cell));
            }
        if (selector.children.size() > 1)
            input.cells.push_back(std::move(selector));
    }
    if (Available(c, ChannelField::Reference))
    {
        input.cells.push_back(ChannelButton("Reference", "+4 dBu", ChannelField::Reference));
        auto low = ChannelButton("Reference.Low", "-10 dBV", ChannelField::Reference);
        low.lowerInverted = true;
        input.cells.push_back(std::move(low));
    }
    if (Available(c, ChannelField::SampleRateConvert))
        input.cells.push_back(ChannelButton("SRC", "SRC", ChannelField::SampleRateConvert));
    // User-selected, channel-type-specific Input settings. These are bus-wide
    // controls on an AUX return, never independent pre/post switches on sends.
    if (Available(c, ChannelField::SendPostFader))
    {
        auto pre = ChannelButton("Bus.Pre", "PRE", ChannelField::SendPostFader);
        pre.lowerInverted = true;
        input.cells.push_back(std::move(pre));
        input.cells.push_back(ChannelButton("Bus.Post", "POST", ChannelField::SendPostFader));
    }
    for (const auto &item :
         std::vector<std::pair<ChannelField, std::string>>{{ChannelField::Mono, "MONO"},
                                                           {ChannelField::Talk, "TALK"},
                                                           {ChannelField::TalkToMonitor, "TB to Mon"}})
        if (Available(c, item.first))
            input.cells.push_back(ChannelButton(FieldName(item.first), item.second, item.first));
    if (!input.cells.empty())
        result.push_back(std::move(input));
    ChannelKnobSet inserts{ChannelFunction::Inserts, {}};
    for (const auto &insert : c.inserts)
    {
        ChannelCell cell;
        cell.key = "Insert." + insert.slot + "." + StableToken(insert.name);
        cell.label = std::to_string(std::stoul(insert.slot) + 1) + " " + insert.name;
        const ChannelAddress power{ChannelField::InsertPower, insert.slot};
        if (Available(c, power))
            cell.lower = power;
        for (const auto &parameter : insert.parameters)
        {
            if (pluginLimit && cell.children.size() >= pluginLimit)
                break;
            const ChannelAddress step{ChannelField::InsertStep, insert.slot, parameter.slot};
            const ChannelAddress normalized{ChannelField::InsertValue, insert.slot, parameter.slot};
            if (Available(c, step))
                cell.children.push_back(ChannelKnob("Parameter." + parameter.slot, parameter.name, step));
            else if (!parameter.step && Available(c, normalized))
                cell.children.push_back(
                    ChannelKnob("Parameter." + parameter.slot, parameter.name, normalized));
        }
        inserts.cells.push_back(std::move(cell));
    }
    if (!inserts.cells.empty() || !c.insertSlots.empty())
        result.push_back(std::move(inserts));
    if (!result.empty() && result.back().function == ChannelFunction::Inserts && !c.insertSlots.empty())
    {
        // Keep real empty slots in place; a plug-in in slot 4 must not silently
        // become insert 1. No loading/configuration actions are invented.
        auto &set = result.back();
        std::vector<ChannelCell> ordered;
        for (const auto &slot : c.insertSlots)
        {
            const auto found = std::find_if(set.cells.begin(), set.cells.end(), [&](const auto &cell) {
                return cell.key.rfind("Insert." + slot + ".", 0) == 0;
            });
            if (found != set.cells.end())
                ordered.push_back(*found);
            else
            {
                ChannelCell empty;
                empty.key = "EmptyInsert." + slot;
                empty.label = std::to_string(std::stoul(slot) + 1) + " None";
                ordered.push_back(std::move(empty));
            }
        }
        if (set.function == ChannelFunction::Inserts)
            set.cells = std::move(ordered);
    }
    ChannelKnobSet console{ChannelFunction::Console, {}};
    if (Available(c, ChannelField::RecordPreEffects))
    {
        auto rec = ChannelButton("UAD.REC", "UAD REC", ChannelField::RecordPreEffects);
        rec.lowerInverted = true; // false = effects are printed to the DAW
        console.cells.push_back(std::move(rec));
        console.cells.push_back(ChannelButton("UAD.MON", "UAD MON", ChannelField::RecordPreEffects));
    }
    if (!console.cells.empty())
        result.push_back(std::move(console));
    ChannelKnobSet unison{ChannelFunction::Unison, {}};
    for (const auto &preamp : c.preamps)
    {
        const auto prefix = c.preamps.size() > 1 ? std::to_string(std::stoul(preamp.slot) + 1) + " " : "";
        if (preamp.unison.empty())
        {
            ChannelCell empty;
            empty.key = "Unison." + preamp.slot + ".None";
            empty.label = prefix + "None";
            unison.cells.push_back(std::move(empty));
        }
        for (const auto &insert : preamp.unison)
        {
            ChannelCell cell;
            cell.key = "Unison." + preamp.slot + "." + insert.slot + "." + insert.identity;
            cell.label = prefix + insert.name;
            const ChannelAddress power{ChannelField::UnisonPower, insert.slot, "", preamp.slot};
            if (Available(c, power))
                cell.lower = power;
            for (const auto &parameter : insert.parameters)
            {
                if (pluginLimit && cell.children.size() >= pluginLimit)
                    break;
                const ChannelAddress step{ChannelField::UnisonStep, insert.slot, parameter.slot, preamp.slot};
                const ChannelAddress value{ChannelField::UnisonValue, insert.slot, parameter.slot,
                                           preamp.slot};
                if (Available(c, step))
                    cell.children.push_back(ChannelKnob("Parameter." + parameter.slot, parameter.name, step));
                else if (!parameter.step && Available(c, value))
                    cell.children.push_back(
                        ChannelKnob("Parameter." + parameter.slot, parameter.name, value));
            }
            unison.cells.push_back(std::move(cell));
        }
    }
    if (!unison.cells.empty())
        result.push_back(std::move(unison));
    return result;
}
inline double SurfaceScale(ChannelAddress address)
{
    return address.kind == ChannelField::SendPan ? 100.0 : 1.0;
}
inline Json DecodeChannelKnob(const Channel &c, ChannelAddress address, double value)
{
    if (!std::isfinite(value))
        throw std::invalid_argument("Invalid knob value");
    const auto &p = FieldParameter(c, address);
    if (!p)
        throw std::invalid_argument("Missing knob parameter");
    if (ChoiceField(address))
    {
        if (value < 0 || std::floor(value) != value || value >= p->choices.size())
            throw std::invalid_argument("Invalid choice index");
        Json choice;
        choice.kind = Json::Kind::String;
        choice.scalar = p->choices[static_cast<size_t>(value)];
        return choice;
    }
    return ControlNumber(value / SurfaceScale(address));
}
} // namespace apollo
