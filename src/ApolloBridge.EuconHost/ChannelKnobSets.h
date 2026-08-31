#pragma once
// Private implementation, included after the adapter's checked SDK helpers.
class ChannelKnobSets
{
    struct Array;
    struct Cell
    {
        explicit Cell(EuProcessor &owner) : control(&owner)
        {
        }
        EuControlKnobCell control;
        ChannelCell definition;
        NEuCon::uint32 member = 0;
        bool childAttached = false;
        std::unique_ptr<Array> child;
        NEuCon::uint16 lastDisplayIndex = 0;
        std::string lastDisplay;
    };
    struct Array
    {
        explicit Array(EuProcessor &processor) : owner(processor), control(&processor)
        {
        }
        ~Array()
        {
            for (auto &cell : cells)
            {
                if (cell->childAttached)
                    CleanupResult(control.RemoveChild(cell->member), "Detach channel child");
                if (cell->member)
                    CleanupResult(control.Remove(cell->member), "Remove channel cell");
            }
            if (registered)
                CleanupResult(owner.RemoveControl(control), "Remove channel knob set");
        }
        EuProcessor &owner;
        EuControlKnobCellArray control;
        NEuCon::uint32 id = 0;
        bool registered = false;
        std::vector<std::unique_ptr<Cell>> cells;
    };

  public:
    static constexpr NEuCon::uint32 FirstId = 1000;
    EuControlKnobCellArray *Page(ChannelFunction function) const
    {
        const auto it = roots_.find(function);
        return it == roots_.end() ? nullptr : &it->second->control;
    }
    NEuCon::uint32 PageIdForTest(ChannelFunction function) const
    {
        const auto it = roots_.find(function);
        return it == roots_.end() ? 0 : it->second->id;
    }
    explicit ChannelKnobSets(EuProcessor &owner, EuNode &node) : owner_(owner), node_(node)
    {
    }
    std::function<void(EuControlKnobCellArray *)> beforeRemove;
    void Clear()
    {
        if (beforeRemove)
            for (const auto &root : roots_)
                beforeRemove(&root.second->control);
        bindings_.clear();
        roots_.clear();
    }
    bool Matches(const Channel &c) const
    {
        return ready_ && ControlEligible(shape_) == ControlEligible(c) && SameExtensionShape(shape_, c);
    }
    bool Touched() const
    {
        return touchCount_.load() != 0;
    }
    void Touch(NEuCon::uint32 control, NEuCon::uint32 member, bool down)
    {
        std::lock_guard<std::mutex> lock(touchMutex_);
        const auto key = std::make_pair(control, member);
        if (down && touches_.size() < 4096)
            touches_.insert(key);
        else if (!down)
            touches_.erase(key);
        touchCount_ = touches_.size();
    }
    void Rebuild(const Channel &c)
    {
        NEuCon::int32 limit = 0;
        const auto limitResult = node_.GetAttribute(kATRIBID_LimitKnobEngage, limit);
        if (limitResult != kERR_OK)
            limit = 0; // The attribute is optional.
        Log("channel-knob-limit result=" + std::to_string(limitResult) +
            " effective=" + std::to_string(limit));
        std::set<ChannelFunction> desired;
        for (const auto &set : DescribeChannel(c, limit > 0 ? static_cast<size_t>(limit) : 0))
        {
            desired.insert(set.function);
            const auto old = roots_.find(set.function);
            if (old != roots_.end())
            {
                if (SamePage(old->second->cells, set.cells, c))
                    continue;
                Forget(*old->second);
                if (beforeRemove)
                    beforeRemove(&old->second->control);
                roots_.erase(old);
            }
            int layout = EuLayoutChannel::kNAM_Null;
            std::string name;
            switch (set.function)
            {
            case ChannelFunction::Aux:
                layout = EuLayoutChannel::kNAM_AuxSend;
                name = "Aux";
                break;
            case ChannelFunction::Mix:
                layout = EuLayoutChannel::kNAM_Mix;
                name = "Mix";
                break;
            case ChannelFunction::Input:
                layout = EuLayoutChannel::kNAM_Input;
                name = "Input";
                break;
            case ChannelFunction::Inserts:
                layout = EuLayoutChannel::kNAM_Inserts;
                name = "Inserts";
                break;
            case ChannelFunction::Console:
                layout = EuLayoutChannel::kNAM_TopLevelKnobSet11;
                name = "Console";
                break;
            case ChannelFunction::Unison:
                layout = EuLayoutChannel::kNAM_TopLevelKnobSet9;
                name = "UNISON";
                break;
            }
            roots_[set.function] = Build(c, set.cells, name, layout);
        }
        for (auto it = roots_.begin(); it != roots_.end();)
            if (!desired.count(it->first))
            {
                Forget(*it->second);
                if (beforeRemove)
                    beforeRemove(&it->second->control);
                it = roots_.erase(it);
            }
            else
                ++it;
        shape_ = c;
        ready_ = true;
    }
    void Forget(Array &array)
    {
        for (auto &cell : array.cells)
        {
            bindings_.erase({array.id, cell->member});
            if (cell->child)
                Forget(*cell->child);
        }
    }
    bool SamePage(const std::vector<std::unique_ptr<Cell>> &old, const std::vector<ChannelCell> &next,
                  const Channel &channel) const
    {
        if (old.size() != next.size())
            return false;
        for (size_t i = 0; i < next.size(); ++i)
        {
            const auto &a = old[i]->definition, &b = next[i];
            if (a.key != b.key || a.label != b.label || a.knob != b.knob || a.lower != b.lower ||
                a.lowerInverted != b.lowerInverted || a.lowerChoice != b.lowerChoice)
                return false;
            for (const auto &address : {a.knob, a.lower})
                if (address && !SameFieldShape(shape_, channel, *address))
                    return false;
            if (old[i]->child ? !SamePage(old[i]->child->cells, b.children, channel) : !b.children.empty())
                return false;
        }
        return true;
    }
    bool Dispatch(const Event &event, const Channel &c, ChannelController &controller)
    {
        const auto found = bindings_.find({event.control, event.member});
        if (found == bindings_.end() || !Matches(c))
            return false;
        const auto &definition = found->second->definition;
        std::optional<ChannelAddress> address;
        Json value;
        if (event.primitive == EuControlKnobCell::kID_Knob && definition.knob)
        {
            address = definition.knob;
            value = DecodeChannelKnob(c, *address, event.value);
        }
        else if (event.primitive == EuControlKnobCell::kID_LowerSwitch && definition.lower)
        {
            address = definition.lower;
            if (definition.lowerChoice)
            {
                // A second press of the current route is a no-op, not "unroute".
                if (event.value == 0)
                    return false;
                value.kind = Json::Kind::String;
                value.scalar = *definition.lowerChoice;
            }
            else
                value = Json::Parse((event.value != 0) != definition.lowerInverted ? "true" : "false");
        }
        if (!address)
            return false; // Navigation, touch and unused switches never write.
        return controller.Submit(c.key, *address, value, event.epoch);
    }
    void Apply(const Channel &c)
    {
        if (!Matches(c))
            return;
        for (const auto &binding : bindings_)
        {
            auto &cell = *binding.second;
            const auto &definition = cell.definition;
            if (definition.knob)
            {
                const auto &a = *definition.knob;
                const auto &p = FieldParameter(c, a);
                if (p && !IsTouched(binding.first))
                {
                    auto &knob = Primitive(cell.control, EuControlKnobCell::kID_Knob);
                    if (ChoiceField(a))
                    {
                        const auto selected =
                            std::find(p->choices.begin(), p->choices.end(), p->value.String());
                        if (selected != p->choices.end())
                            Check(knob.SetCurrentIndex(
                                      static_cast<NEuCon::uint16>(selected - p->choices.begin())),
                                  "Choice feedback");
                    }
                    else
                    {
                        Check(knob.SetCurrentValue(static_cast<float>(p->value.Number() * SurfaceScale(a))),
                              "Channel knob feedback");
                        if (a.kind == ChannelField::InsertValue || a.kind == ChannelField::UnisonValue)
                        {
                            std::string display;
                            for (const auto &insert : c.inserts)
                                if (a.kind == ChannelField::InsertValue && insert.slot == a.slot)
                                    for (const auto &parameter : insert.parameters)
                                        if (parameter.slot == a.parameter)
                                            display = parameter.display;
                            for (const auto &preamp : c.preamps)
                                if (a.kind == ChannelField::UnisonValue && preamp.slot == a.preamp)
                                    for (const auto &insert : preamp.unison)
                                        if (insert.slot == a.slot)
                                            for (const auto &parameter : insert.parameters)
                                                if (parameter.slot == a.parameter)
                                                    display = parameter.display;
                            NEuCon::uint16 index = 0;
                            Check(knob.GetCurrentIndex(index), "Plug-in display index");
                            if (display != cell.lastDisplay || index != cell.lastDisplayIndex)
                            {
                                // Clear the previous override: a stale dB/Hz label
                                // must never be shown for another normalized value.
                                float previousValue = 0, currentValue = 0;
                                Check(knob.GetValueAt(cell.lastDisplayIndex, previousValue),
                                      "Previous plug-in value");
                                Check(knob.GetValueAt(index, currentValue), "Current plug-in value");
                                UpdateText(knob, std::to_wstring(std::lround(previousValue * 100)) + L"%",
                                           "Clear old plug-in text", cell.lastDisplayIndex);
                                UpdateText(knob,
                                           display.empty()
                                               ? std::to_wstring(std::lround(currentValue * 100)) + L"%"
                                               : Wide(display),
                                           "Plug-in value text", index);
                                cell.lastDisplay = display;
                                cell.lastDisplayIndex = index;
                            }
                        }
                    }
                }
            }
            if (definition.lower)
            {
                const auto &p = FieldParameter(c, *definition.lower);
                if (p)
                {
                    const bool active = definition.lowerChoice ? p->value.String() == *definition.lowerChoice
                                                               : p->value.Bool() != definition.lowerInverted;
                    Check(Primitive(cell.control, EuControlKnobCell::kID_LowerSwitch)
                              .SetCurrentIndex(active ? 1 : 0),
                          "Channel switch feedback");
                    Check(Primitive(cell.control, EuControlKnobCell::kID_LowerSwitchLed)
                              .SetCurrentIndex(
                                  static_cast<NEuCon::uint16>(active ? kLEDStatus_On : kLEDStatus_Off)),
                          "Channel switch LED");
                }
            }
        }
    }

  private:
    bool IsTouched(const std::pair<NEuCon::uint32, NEuCon::uint32> &key)
    {
        std::lock_guard<std::mutex> lock(touchMutex_);
        return touches_.count(key) != 0; // Lock released before every SDK call.
    }
    void InitKnob(Cell &cell, const Channel &channel)
    {
        auto &primitive = Primitive(cell.control, EuControlKnobCell::kID_Knob);
        auto *knob = dynamic_cast<EuPrimitiveKnob *>(&primitive);
        if (!knob)
            throw std::runtime_error("Missing channel knob primitive");
        if (!cell.definition.knob)
            return;
        const auto address = *cell.definition.knob;
        const auto &p = FieldParameter(channel, address);
        if (!p)
            throw std::runtime_error("Missing channel parameter definition");
        std::vector<float> values;
        if (ChoiceField(address))
        {
            for (size_t i = 0; i < p->choices.size(); ++i)
                values.push_back(static_cast<float>(i));
        }
        else if (address.kind == ChannelField::SendLevel)
            values = FaderDbTable(static_cast<float>(*p->minimum), static_cast<float>(*p->maximum));
        else
        {
            const size_t count = address.kind == ChannelField::SendPan ? 201
                                 : address.kind == ChannelField::PreampGain
                                     ? static_cast<size_t>(*p->maximum - *p->minimum) + 1
                                     : 4097;
            for (size_t i = 0; i < count; ++i)
                values.push_back(static_cast<float>(
                    (*p->minimum + (*p->maximum - *p->minimum) * i / (count - 1)) * SurfaceScale(address)));
        }
        Initialize(primitive, kTYP_Float, static_cast<NEuCon::uint16>(values.size()));
        Check(primitive.LoadValueTable(values, address.kind == ChannelField::PreampGain ? 0 : 1),
              "Channel knob values");
        for (size_t i = 0; i < values.size(); ++i)
        {
            std::wstring text;
            if (ChoiceField(address))
                text = Wide(p->choices[i]);
            else if (address.kind == ChannelField::SendPan)
            {
                const int n = static_cast<int>(std::lround(values[i]));
                text = n == 0 ? L"Center" : std::to_wstring(std::abs(n)) + (n < 0 ? L" L" : L" R");
            }
            else if (address.kind == ChannelField::InsertValue || address.kind == ChannelField::UnisonValue)
                text = std::to_wstring(std::lround(values[i] * 100)) + L"%";
            if (!text.empty())
                Check(primitive.LoadValueAt(static_cast<NEuCon::uint16>(i), text),
                      "Channel knob display table");
        }
        Check(knob->SetPositionRingMode(address.kind == ChannelField::SendPan ? kRingCenterAnchored
                                                                              : kRingPoint),
              "Channel knob ring");
        Check(primitive.SetAttribute2(kATRIBID_ParameterPersID, Wide(cell.definition.key)),
              "Channel parameter persistence");
        RawSwitch(Primitive(cell.control, EuControlKnobCell::kID_KnobTouchSense));
    }
    std::unique_ptr<Array> Build(const Channel &channel, const std::vector<ChannelCell> &definitions,
                                 const std::string &key, int layout)
    {
        auto array = std::make_unique<Array>(owner_);
        array->id = nextId_++;
        Check(array->control.Freeze(), "Channel knob-set freeze");
        try
        {
            Check(array->control.SetId(array->id), "Channel knob-set ID");
            Check(array->control.SetPersistenceID(Wide(key)), "Channel knob-set persistence");
            Check(array->control.SetUserVisibleName(Wide(key)), "Channel knob-set name");
            Check(array->control.SetAttribute2(kATRIBID_LayoutName0, layout), "Channel knob-set layout");
            const char *function = layout == EuLayoutChannel::kNAM_AuxSend             ? kChanFuncID_AuxSend
                                   : layout == EuLayoutChannel::kNAM_Mix               ? kChanFuncID_Mix
                                   : layout == EuLayoutChannel::kNAM_Input             ? kChanFuncID_Input
                                   : layout == EuLayoutChannel::kNAM_Inserts           ? kChanFuncID_Inserts
                                   : layout == EuLayoutChannel::kNAM_TopLevelKnobSet11 ? "Console"
                                   : layout == EuLayoutChannel::kNAM_TopLevelKnobSet9  ? "Apollo.UNISON"
                                                                                       : nullptr;
            if (function)
                Check(array->control.SetAttribute2(kATRIBID_FuncPersID, Wide(function)),
                      "Standard channel function persistence");
            Check(array->control.SetKnobCellOrder(EuControlKnobCellArray::kKNOBCELLORDER_Top_Down),
                  "Channel knob order");
            for (const auto &definition : definitions)
            {
                auto cell = std::make_unique<Cell>(owner_);
                cell->definition = definition;
                Check(cell->control.SetPersistenceID(Wide(definition.key)), "Channel cell persistence");
                Label(Primitive(cell->control, EuControlKnobCell::kID_KnobLabelDisplay),
                      Wide(definition.label));
                InitKnob(*cell, channel);
                if (definition.lower)
                {
                    Switch(Primitive(cell->control, EuControlKnobCell::kID_LowerSwitch));
                    auto &led = Primitive(cell->control, EuControlKnobCell::kID_LowerSwitchLed);
                    Initialize(led, kTYP_Int, 4);
                    Check(led.LoadValueTableInterpolated(0, 3), "Channel switch LED table");
                }
                if (!definition.children.empty())
                    RawSwitch(Primitive(cell->control, EuControlKnobCell::kID_KnobTopSwitch));
                array->cells.push_back(std::move(cell));
                auto &saved = *array->cells.back();
                Check(array->control.PushBack(&saved.control, saved.member), "Add channel knob cell");
                bindings_[{array->id, saved.member}] = &saved;
                if (!definition.children.empty())
                {
                    saved.child = Build(channel, definition.children, key + "." + definition.key,
                                        EuLayoutChannel::kNAM_Null);
                    Check(saved.control.Freeze(), "Channel child owner freeze");
                    const auto added = array->control.AddChild(saved.member, saved.child->control);
                    saved.childAttached = added == kERR_OK;
                    const auto thawed = saved.control.Thaw();
                    if (added != kERR_OK)
                        CleanupResult(thawed, "Channel child owner thaw after error");
                    Check(added, "Attach channel child knob set");
                    Check(thawed, "Channel child owner thaw");
                }
            }
            Check(owner_.AddControl(array->control), "Register channel knob set");
            array->registered = true;
        }
        catch (...)
        {
            CleanupResult(array->control.Thaw(), "Failed channel knob-set thaw");
            throw;
        }
        Check(array->control.Thaw(), "Channel knob-set thaw");
        return array;
    }
    EuProcessor &owner_;
    EuNode &node_;
    Channel shape_;
    bool ready_ = false;
    NEuCon::uint32 nextId_ = FirstId;
    std::map<ChannelFunction, std::unique_ptr<Array>> roots_;
    std::map<std::pair<NEuCon::uint32, NEuCon::uint32>, Cell *> bindings_;
    std::mutex touchMutex_;
    std::set<std::pair<NEuCon::uint32, NEuCon::uint32>> touches_;
    std::atomic<size_t> touchCount_ = 0;
};
