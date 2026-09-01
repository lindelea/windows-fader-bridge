#pragma once
// Private implementation, included after the adapter's checked SDK helpers.
tRING ChannelRingMode(ChannelAddress address)
{
    if (address.kind == ChannelField::SendPan)
        return kRingCenterAnchored;
    if (address.kind == ChannelField::SendLevel)
        return kRingThermometerLeft;
    // Console does not report plug-in polarity/shape metadata. Point is the
    // neutral Avid representation for choices, preamp gain and unknown
    // plug-in parameters; do not infer bipolar gain, Q or width from names.
    return kRingPoint;
}
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
        ConfigKnob configKnob;
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
    explicit ChannelKnobSets(EuProcessor &owner, EuNode &node, bool config = false)
        : owner_(owner), node_(node), config_(config)
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
        return ready_ && ControlEligible(shape_) == ControlEligible(c) && SameExtensionShape(shape_, c) &&
               (!config_ || SameConfiguration(shape_.configuration, c.configuration));
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
        for (const auto &set : DescribeConfiguredChannel(c, limit > 0 ? static_cast<size_t>(limit) : 0, config_))
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
                a.lowerInverted != b.lowerInverted || a.lowerChoice != b.lowerChoice ||
                a.configStart != b.configStart || a.readOnlyGlobal != b.readOnlyGlobal)
                return false;
            if (a.configKey != b.configKey || a.configChoice != b.configChoice ||
                a.configChoices != b.configChoices ||
                (!a.configKey.empty() && !SameConfigShape(shape_.configuration, channel.configuration, a.configKey))) return false;
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
        if (!definition.readOnlyGlobal.empty())
            return false;
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
    bool IsReadOnlyConfig(NEuCon::uint32 control, NEuCon::uint32 member) const
    {
        const auto found = bindings_.find({control, member});
        return found != bindings_.end() && !found->second->definition.readOnlyGlobal.empty();
    }
    bool IsConfig(NEuCon::uint32 control, NEuCon::uint32 member) const
    {
        const auto it = bindings_.find({control, member});
        return it != bindings_.end() && !it->second->definition.configKey.empty();
    }
    bool DispatchConfig(const Event &event, const Channel &c, ConfigController &controller)
    {
        const auto it = bindings_.find({event.control, event.member});
        if (it == bindings_.end() || !Matches(c)) return false;
        auto &cell = *it->second;
        const auto *s = FindConfig(c.configuration, cell.definition.configKey);
        return s && cell.configKnob.Dispatch(event, *s, controller, cell.definition.configChoice,
                                             cell.definition.configChoices);
    }
    bool GlobalConfigReadbackForTest(const std::string &key, const std::wstring &expected)
    {
        for (const auto &binding : bindings_)
            if (binding.second->definition.readOnlyGlobal == key)
            {
                tEuString text;
                auto &knob = Primitive(binding.second->control, EuControlKnobCell::kID_Knob);
                NEuCon::uint16 size = 0;
                Check(knob.GetValueTableSize(size), "Preview table size");
                Check(knob.GetValueAt(0, text, kSTRLEN_Long), "Preview readback");
                return size == 1 && text == expected &&
                       !binding.second->definition.knob && !binding.second->definition.lower;
            }
        return false;
    }
    void ApplyGlobalConfig(const std::map<std::string, std::string> &values, bool connected)
    {
        for (auto &binding : bindings_)
        {
            auto &cell = *binding.second;
            const auto &key = cell.definition.readOnlyGlobal;
            if (key.empty())
                continue;
            const auto found = values.find(key);
            const auto text = !connected ? "OFFLINE" :
                              key == "@hint" ? "CONFIG" : found == values.end() ? "N/A" : found->second;
            if (cell.lastDisplay != text)
            {
                UpdateText(Primitive(cell.control, EuControlKnobCell::kID_Knob), Wide(text),
                           "Global Config preview");
                cell.lastDisplay = text;
            }
        }
    }
    bool ConfigMarkersForTest() const
    {
        const std::function<bool(Array &)> verify = [&](Array &array) {
            std::vector<NEuCon::uint32> expected, actual, normal;
            for (const auto &cell : array.cells)
            {
                if (cell->definition.configStart)
                    expected.push_back(cell->member);
                if (cell->child && !verify(*cell->child)) return false;
            }
            Check(array.control.GetConfigPages(actual), "Config marker test");
            Check(array.control.GetPages(normal), "Normal page marker test");
            if (expected != actual)
                return false;
            for (const auto id : expected)
                if (std::find(normal.begin(), normal.end(), id) != normal.end())
                    return false;
            return true;
        };
        for (const auto &root : roots_) if (!verify(*root.second)) return false;
        return true;
    }
    bool ConfigValueForTest(const std::string &key, const std::wstring &expected) const
    {
        for (const auto &entry : bindings_)
            if (entry.second->definition.configKey == key)
            {
                tEuString text;
                Check(Primitive(entry.second->control, EuControlKnobCell::kID_Knob).GetCurrentValue(text),
                      "Configuration current value readback");
                return text == expected;
            }
        return false;
    }
    bool RingModesForTest() const
    {
        for (const auto &binding : bindings_)
        {
            const auto &definition = binding.second->definition;
            auto &primitive = Primitive(binding.second->control, EuControlKnobCell::kID_Knob);
            auto *knob = dynamic_cast<EuPrimitiveKnob *>(&primitive);
            if (!knob)
                return false;
            tRING actual = kRingModeInvalid;
            Check(knob->GetPositionRingMode(actual), "Channel knob ring readback");
            const auto expected = !definition.configKey.empty()
                                      ? (definition.configChoice < 0 ? kRingPoint : kRingOff)
                                  : !definition.readOnlyGlobal.empty() || !definition.knob
                                      ? kRingOff
                                  : ChannelRingMode(*definition.knob);
            if (actual != expected)
                return false;
        }
        return true;
    }
    void Apply(const Channel &c)
    {
        if (!Matches(c))
            return;
        for (const auto &binding : bindings_)
        {
            auto &cell = *binding.second;
            const auto &definition = cell.definition;
            if (const auto *s = FindConfig(c.configuration, definition.configKey))
            {
                cell.configKnob.Apply(cell.control, *s, definition.configChoice, definition.configChoices);
                continue;
            }
            if (definition.knob)
            {
                const auto &a = *definition.knob;
                const auto &p = FieldParameter(c, a);
                if (p)
                {
                    auto &knob = Primitive(cell.control, EuControlKnobCell::kID_Knob);
                    const bool touched = IsTouched(binding.first);
                    if (ChoiceField(a))
                    {
                        const auto selected =
                            std::find(p->choices.begin(), p->choices.end(), p->value.String());
                        if (!touched && selected != p->choices.end())
                            Check(knob.SetCurrentIndex(
                                      static_cast<NEuCon::uint16>(selected - p->choices.begin())),
                                  "Choice feedback");
                    }
                    else
                    {
                        // Touch owns the physical position, but it must not
                        // suppress UAD's engineering-unit text for that
                        // position. Only position feedback is gated here.
                        if (!touched)
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
                                // NormalizedValue is transport data, never a
                                // professional display unit. Clear cached text
                                // for old/new indices and publish only UAD's
                                // StringValue (dB, Hz, ms, ratio, mode, etc.).
                                UpdateText(knob, L"", "Clear old plug-in text", cell.lastDisplayIndex);
                                UpdateText(knob, display.empty() ? L"" : Wide(display),
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
        if (const auto *s = FindConfig(channel.configuration, cell.definition.configKey))
        {
            cell.configKnob.InitializeCell(cell.control, *s, cell.definition.configChoice,
                                           cell.definition.configChoices);
            return;
        }
        auto &primitive = Primitive(cell.control, EuControlKnobCell::kID_Knob);
        auto *knob = dynamic_cast<EuPrimitiveKnob *>(&primitive);
        if (!knob)
            throw std::runtime_error("Missing channel knob primitive");
        if (!cell.definition.readOnlyGlobal.empty())
        {
            // One immutable numeric entry: rotation cannot select a candidate.
            Initialize(primitive, kTYP_Float, 1);
            Check(primitive.LoadValueAt(0, 0.0F), "Config preview value");
            UpdateText(primitive, L"N/A", "Initial Config preview");
            Check(knob->SetPositionRingMode(kRingOff), "Config preview ring off");
            return;
        }
        if (!cell.definition.knob)
        {
            // The S3 reserves an encoder-position region in every knob cell,
            // even for directory and switch-only entries.  Match Avid's
            // official switch-only Input example: leave the knob primitive
            // uninitialized and explicitly disable its position ring.
            Check(knob->SetPositionRingMode(kRingOff), "Unused channel knob ring off");
            return;
        }
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
            if (address.kind == ChannelField::SendLevel || address.kind == ChannelField::PreampGain)
            {
                const auto minimum = static_cast<float>(*p->minimum * SurfaceScale(address));
                LoadValueText(primitive, static_cast<NEuCon::uint16>(i), DbValueText(values[i], minimum),
                              "Channel dB display table");
                continue;
            }
            if (ChoiceField(address))
                text = Wide(p->choices[i]);
            else if (address.kind == ChannelField::SendPan)
            {
                const int n = static_cast<int>(std::lround(values[i]));
                text = n == 0 ? L"Center" : std::to_wstring(std::abs(n)) + (n < 0 ? L" L" : L" R");
            }
            else if (address.kind == ChannelField::InsertValue || address.kind == ChannelField::UnisonValue)
            {
                // Do not expose the normalized 0..1 transport as a percentage.
                // The live UAD StringValue supplies the authoritative unit.
                CheckTextResult(primitive,
                                primitive.LoadValueAt(static_cast<NEuCon::uint16>(i), L"", L"", L""),
                                static_cast<NEuCon::uint16>(i), L"", "Blank plug-in value table");
                continue;
            }
            if (!text.empty())
                Check(primitive.LoadValueAt(static_cast<NEuCon::uint16>(i), text),
                      "Channel knob display table");
        }
        Check(knob->SetPositionRingMode(ChannelRingMode(address)),
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
            // Follow the SDK lifecycle: register the frozen parent array with
            // its processor before adding members or attaching child arrays.
            // Registering only after the hierarchy is complete can leave a
            // deeper child undiscoverable on physical surfaces.
            Check(owner_.AddControl(array->control), "Register channel knob set");
            array->registered = true;
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
                    const auto pluginPower = definition.lower->kind == ChannelField::InsertPower ||
                                             definition.lower->kind == ChannelField::UnisonPower;
                    if (pluginPower)
                        LatchSwitch(Primitive(cell->control, EuControlKnobCell::kID_LowerSwitch));
                    else
                        Switch(Primitive(cell->control, EuControlKnobCell::kID_LowerSwitch));
                    auto &led = Primitive(cell->control, EuControlKnobCell::kID_LowerSwitchLed);
                    Initialize(led, kTYP_Int, 4);
                    Check(led.LoadValueTableInterpolated(0, 3), "Channel switch LED table");
                }
                // kID_KnobTopSwitch is owned by EUCON hierarchy navigation once
                // AddChild is present. Do not initialize it as an application
                // switch: encoder press enters the child page. SEL is the
                // distinct kID_UpperSwitch and is reserved for a real peer.
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
            for (const auto &cell : array->cells)
                if (cell->definition.configStart)
                    MarkConfigPage(array->control, cell->member);
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
    const bool config_;
    Channel shape_;
    bool ready_ = false;
    NEuCon::uint32 nextId_ = FirstId;
    std::map<ChannelFunction, std::unique_ptr<Array>> roots_;
    std::map<std::pair<NEuCon::uint32, NEuCon::uint32>, Cell *> bindings_;
    std::mutex touchMutex_;
    std::set<std::pair<NEuCon::uint32, NEuCon::uint32>> touches_;
    std::atomic<size_t> touchCount_ = 0;
};
