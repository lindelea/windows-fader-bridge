#pragma once
// Included inside the adapter namespace after checked SDK helpers.
bool UpperLabelMatches(EuPrimitiveControl &primitive, const UpperLabelText &text)
{
    const std::array<tSTRLEN, 3> widths{kSTRLEN_4, kSTRLEN_8, kSTRLEN_Long};
    for (size_t i = 0; i < widths.size(); ++i)
    {
        tEuString actual;
        Check(primitive.GetValueAt(0, actual, widths[i]), "Upper text variant readback");
        if (actual != text[i])
            return false;
    }
    return true;
}
void UpdateUpperLabel(EuPrimitiveControl &primitive, const UpperLabelText &text)
{
    const auto result = primitive.ChangeText(text[0], text[1], text[2]);
    if (result != kERR_AlreadySet)
        Check(result, "Upper text variants");
    if (!UpperLabelMatches(primitive, text))
    {
        Log("Upper text variants result=" + std::to_string(result) + " readback-matched=0");
        throw std::runtime_error("Upper text variants did not match after update");
    }
    if (result == kERR_AlreadySet)
        Log("Upper text variants result=" + std::to_string(result) + " readback-matched=1");
}
class MonitorKnobSet
{
    struct Cell
    {
        explicit Cell(EuProcessor &owner) : control(&owner)
        {
        }
        EuControlKnobCell control;
        MonitorCell definition;
        NEuCon::uint32 member = 0;
    };

  public:
    static constexpr NEuCon::uint32 Id = 800;
    explicit MonitorKnobSet(EuProcessor &owner) : owner_(owner), array_(&owner)
    {
    }
    ~MonitorKnobSet()
    {
        Clear();
    }
    EuControlKnobCellArray *Page()
    {
        return registered_ ? &array_ : nullptr;
    }
    bool Matches(const std::optional<Monitor> &m) const
    {
        return m ? shape_ && SameMonitorPage(*shape_, *m) : !shape_;
    }
    bool Touched() const
    {
        return touched_.load() != 0;
    }
    void Touch(NEuCon::uint32 member, bool down)
    {
        std::lock_guard<std::mutex> lock(touchMutex_);
        if (down)
            touches_.insert(member);
        else
            touches_.erase(member);
        touched_ = touches_.size();
    }
    void Clear()
    {
        for (auto &cell : cells_)
            if (cell->member)
                CleanupResult(array_.Remove(cell->member), "Remove monitor alias");
        cells_.clear();
        if (registered_)
            CleanupResult(owner_.RemoveControl(array_), "Remove monitor alias page");
        registered_ = false;
        shape_.reset();
    }
    void Rebuild(const std::optional<Monitor> &m)
    {
        Clear(); // caller detached the top-level reference and froze owner
        if (!m)
            return;
        Check(array_.Freeze(), "Monitor alias freeze");
        try
        {
            Check(array_.SetId(Id), "Monitor alias ID");
            Check(array_.SetPersistenceID(L"Apollo.ControlRoom"), "Monitor alias persistence");
            Check(array_.SetUserVisibleName(L"Control Room"), "Monitor alias name");
            Check(array_.SetAttribute2(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_TopLevelKnobSet16),
                  "Monitor alias layout");
            Check(array_.SetAttribute2(kATRIBID_FuncPersID, L"Apollo.ControlRoom"), "Monitor alias function");
            Check(array_.SetKnobCellOrder(EuControlKnobCellArray::kKNOBCELLORDER_Top_Down),
                  "Monitor alias order");
            for (const auto &definition : DescribeMonitor(*m))
            {
                auto cell = std::make_unique<Cell>(owner_);
                cell->definition = definition;
                Check(cell->control.SetPersistenceID(
                          Wide(std::string(FieldName(definition.field)) + definition.source)),
                      "Monitor alias cell identity");
                Label(Primitive(cell->control, EuControlKnobCell::kID_KnobLabelDisplay),
                      Wide(definition.label));
                if (definition.knob)
                {
                    auto &p = Primitive(cell->control, EuControlKnobCell::kID_Knob);
                    const auto &parameter = FieldParameter(*m, definition.field);
                    const auto table = definition.field == MonitorField::DimAmount
                                           ? MonitorDimTable()
                                           : FaderDbTable(static_cast<float>(*parameter->minimum),
                                                          static_cast<float>(*parameter->maximum));
                    Initialize(p, kTYP_Float, static_cast<NEuCon::uint16>(table.size()));
                    Check(p.LoadValueTable(table, 1), "Monitor alias values");
                    RawSwitch(Primitive(cell->control, EuControlKnobCell::kID_KnobTouchSense));
                    auto *knob = dynamic_cast<EuPrimitiveKnob *>(&p);
                    if (!knob)
                        throw std::runtime_error("Missing monitor alias knob");
                    Check(knob->SetPositionRingMode(kRingPoint), "Monitor alias ring");
                }
                else
                {
                    auto &p = Primitive(cell->control, EuControlKnobCell::kID_LowerSwitch);
                    if (definition.field == MonitorField::Talk)
                        RawSwitch(p);
                    else
                        Switch(p);
                    auto &led = Primitive(cell->control, EuControlKnobCell::kID_LowerSwitchLed);
                    Initialize(led, kTYP_Int, 4);
                    Check(led.LoadValueTableInterpolated(0, 3), "Monitor alias LED");
                }
                cells_.push_back(std::move(cell));
                Check(array_.PushBack(&cells_.back()->control, cells_.back()->member), "Monitor alias add");
            }
            Check(owner_.AddControl(array_), "Monitor alias registration");
            registered_ = true;
            shape_ = *m;
        }
        catch (...)
        {
            CleanupResult(array_.Thaw(), "Monitor alias failed thaw");
            Clear();
            throw;
        }
        Check(array_.Thaw(), "Monitor alias thaw");
    }
    void Apply(const Monitor &m)
    {
        if (!shape_ || !SameMonitorPage(*shape_, m))
            return;
        for (const auto &cell : cells_)
        {
            const auto &d = cell->definition;
            const auto &p = FieldParameter(m, d.field);
            if (!p)
                continue;
            if (d.knob)
            {
                bool touching;
                {
                    std::lock_guard<std::mutex> lock(touchMutex_);
                    touching = touches_.count(cell->member) != 0;
                }
                if (!touching)
                    Check(Primitive(cell->control, EuControlKnobCell::kID_Knob)
                              .SetCurrentValue(
                                  static_cast<float>(MonitorKnobValue(d.field, p->value.Number()))),
                          "Monitor alias feedback");
            }
            else
            {
                const bool active =
                    d.field == MonitorField::Source ? p->value.String() == d.source : p->value.Bool();
                Check(Primitive(cell->control, EuControlKnobCell::kID_LowerSwitch)
                          .SetCurrentIndex(active ? 1 : 0),
                      "Monitor alias switch feedback");
                Check(Primitive(cell->control, EuControlKnobCell::kID_LowerSwitchLed)
                          .SetCurrentIndex(
                              static_cast<NEuCon::uint16>(active ? kLEDStatus_On : kLEDStatus_Off)),
                      "Monitor alias LED feedback");
            }
        }
    }
    bool Dispatch(const Event &event, const Monitor &m, MonitorController &controller)
    {
        if (!event.decoded || !event.epoch || event.epoch != controller.Epoch() || !shape_ ||
            !SameMonitorPage(*shape_, m))
            return false;
        if (std::chrono::steady_clock::now() - event.at >= std::chrono::milliseconds(500))
        {
            controller.Disarm();
            return false;
        }
        for (const auto &cell : cells_)
        {
            const auto &d = cell->definition;
            if (cell->member != event.member)
                continue;
            if (d.knob && event.primitive == EuControlKnobCell::kID_Knob)
                return controller.Submit(m.key, d.field,
                                         ControlNumber(MonitorKnobValue(d.field, event.value)), event.epoch);
            if (!d.knob && event.primitive == EuControlKnobCell::kID_LowerSwitch)
            {
                if (d.field == MonitorField::Source)
                {
                    if (event.value == 0)
                        return false;
                    Json value;
                    value.kind = Json::Kind::String;
                    value.scalar = d.source;
                    return controller.Submit(m.key, d.field, value, event.epoch);
                }
                return controller.Submit(m.key, d.field, Json::Parse(event.value != 0 ? "true" : "false"),
                                         event.epoch);
            }
        }
        return false;
    }

  private:
    EuProcessor &owner_;
    EuControlKnobCellArray array_;
    std::vector<std::unique_ptr<Cell>> cells_;
    std::optional<Monitor> shape_;
    bool registered_ = false;
    std::mutex touchMutex_;
    std::set<NEuCon::uint32> touches_;
    std::atomic<size_t> touched_{0};
};

class UpperDirectory
{
    struct Binding
    {
        NEuCon::int32 layout;
        const char *identity;
    };
    static Binding BindingFor(UpperFunction function)
    {
        switch (function)
        {
        case UpperFunction::Inserts:
            return {EuLayoutChannel::kNAM_Inserts, kChanFuncID_Inserts};
        case UpperFunction::Input:
            return {EuLayoutChannel::kNAM_Input, kChanFuncID_Input};
        case UpperFunction::Dynamics:
            return {EuLayoutChannel::kNAM_Dynamics, kChanFuncID_Dynamics};
        case UpperFunction::Eq:
            return {EuLayoutChannel::kNAM_Eq, kChanFuncID_EQ};
        case UpperFunction::Aux:
            return {EuLayoutChannel::kNAM_AuxSend, kChanFuncID_AuxSend};
        case UpperFunction::Pan:
            return {EuLayoutChannel::kNAM_Pan, kChanFuncID_Pan};
        case UpperFunction::Group:
            return {EuLayoutChannel::kNAM_Group, kChanFuncID_Group};
        case UpperFunction::Mix:
            return {EuLayoutChannel::kNAM_Mix, kChanFuncID_Mix};
        case UpperFunction::Unison:
            return {EuLayoutChannel::kNAM_TopLevelKnobSet9, "Apollo.UNISON"};
        case UpperFunction::Reserved10:
            return {EuLayoutChannel::kNAM_TopLevelKnobSet10, "Apollo.Reserved10"};
        case UpperFunction::Console:
            return {EuLayoutChannel::kNAM_TopLevelKnobSet11, "Console"};
        case UpperFunction::Reserved12:
            return {EuLayoutChannel::kNAM_TopLevelKnobSet12, "Apollo.Reserved12"};
        case UpperFunction::Reserved13:
            return {EuLayoutChannel::kNAM_TopLevelKnobSet13, "Apollo.Reserved13"};
        case UpperFunction::Reserved14:
            return {EuLayoutChannel::kNAM_TopLevelKnobSet14, "Apollo.Reserved14"};
        case UpperFunction::Reserved15:
            return {EuLayoutChannel::kNAM_TopLevelKnobSet15, "Apollo.Reserved15"};
        case UpperFunction::ControlRoom:
            return {EuLayoutChannel::kNAM_TopLevelKnobSet16, "Apollo.ControlRoom"};
        }
        throw std::invalid_argument("Unknown upper directory binding");
    }
    static bool MatchesBinding(const EuControlKnobCellArray &child, const Binding &binding)
    {
        NEuCon::int32 layout = 0;
        tEuString identity;
        Check(child.GetAttribute(kATRIBID_LayoutName0, layout), "Upper child layout readback");
        Check(child.GetAttribute(kATRIBID_FuncPersID, identity), "Upper child function readback");
        return layout == binding.layout && identity == Wide(binding.identity);
    }
    struct Entry
    {
        explicit Entry(EuProcessor &owner) : control(&owner)
        {
        }
        EuControlKnobCell control;
        NEuCon::uint32 member = 0;
        EuControlKnobCellArray *child = nullptr;
    };

  public:
    static constexpr NEuCon::uint32 Id = 900;
    UpperDirectory(EuProcessor &owner, int tag) : owner_(owner), array_(&owner), tag_(tag)
    {
    }
    ~UpperDirectory()
    {
        Clear();
    }
    void InitializeDirectory()
    {
        Check(array_.Freeze(), "Upper directory freeze");
        try
        {
            Check(array_.SetId(Id), "Upper directory ID");
            Check(array_.SetPersistenceID(L"Apollo.ChannelFunctions"), "Upper directory persistence");
            Check(array_.SetAttribute2(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_TopLevelKnobset),
                  "Upper directory layout");
            Check(array_.SetKnobCellOrder(EuControlKnobCellArray::kKNOBCELLORDER_Top_Down),
                  "Upper directory order");
            for (const auto &definition : UpperDirectoryEntries)
            {
                const auto binding = BindingFor(definition.function);
                auto entry = std::make_unique<Entry>(owner_);
                Check(entry->control.SetPersistenceID(Wide(binding.identity)), "Upper cell persistence");
                Check(entry->control.SetAttribute2(kATRIBID_FuncPersID, Wide(binding.identity)),
                      "Upper cell function");
                Label(Primitive(entry->control, EuControlKnobCell::kID_KnobLabelDisplay), L"");
                auto *navigationKnob =
                    dynamic_cast<EuPrimitiveKnob *>(&Primitive(entry->control, EuControlKnobCell::kID_Knob));
                if (!navigationKnob)
                    throw std::runtime_error("Missing upper navigation knob");
                Check(navigationKnob->SetPositionRingMode(kRingOff), "Upper navigation ring off");
                RawSwitch(Primitive(entry->control, EuControlKnobCell::kID_KnobTopSwitch));
                entries_.push_back(std::move(entry));
                Check(array_.PushBack(&entries_.back()->control, entries_.back()->member), "Upper cell add");
            }
            Check(owner_.AddControl(array_), "Upper directory registration");
            registered_ = true;
        }
        catch (...)
        {
            CleanupResult(array_.Thaw(), "Upper directory failed thaw");
            Clear();
            throw;
        }
        Check(array_.Thaw(), "Upper directory thaw");
    }
    void Link(UpperFunction function, EuControlKnobCellArray *child)
    {
        const auto index = UpperDirectoryIndex(function);
        const auto binding = BindingFor(function);
        auto &entry = *entries_.at(index);
        // Reject a semantic mismatch before detaching or changing any controls.
        if (child && !MatchesBinding(*child, binding))
            throw std::invalid_argument("Upper child does not match its native function");
        if (entry.child == child)
            return;
        Check(entry.control.Freeze(), "Upper link freeze");
        try
        {
            if (entry.child)
            {
                Check(array_.RemoveChild(entry.member), "Upper unlink");
                entry.child = nullptr;
            }
            if (child)
            {
                Check(array_.AddChild(entry.member, *child), "Upper link");
                entry.child = child;
            }
            UpdateUpperLabel(Primitive(entry.control, EuControlKnobCell::kID_KnobLabelDisplay),
                             UpperDirectoryLabels(function, child != nullptr));
        }
        catch (...)
        {
            CleanupResult(entry.control.Thaw(), "Upper link failed thaw");
            throw;
        }
        Check(entry.control.Thaw(), "Upper link thaw");
        NEuCon::uint32 childId = 0;
        if (child)
            Check(child->GetId(childId), "Upper child ID readback");
        Log("upper-binding tag=" + std::to_string(tag_) + " directory=" + std::to_string(Id) +
            " position=" + std::to_string(index + 1) + " member=" + std::to_string(entry.member) +
            " function=" + binding.identity + " layout=" + std::to_string(binding.layout) +
            " child=" + std::to_string(childId));
    }
    void UnlinkPage(EuControlKnobCellArray *page)
    {
        if (!page)
            return;
        for (size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i]->child == page)
                Link(UpperDirectoryEntries[i].function, nullptr);
    }
    void Clear()
    {
        for (auto &entry : entries_)
        {
            if (entry->child)
                CleanupResult(array_.RemoveChild(entry->member), "Detach upper page");
            if (entry->member)
                CleanupResult(array_.Remove(entry->member), "Remove upper entry");
        }
        entries_.clear();
        if (registered_)
            CleanupResult(owner_.RemoveControl(array_), "Remove upper directory");
        registered_ = false;
    }
    EuControlKnobCellArray *ChildForTest(UpperFunction function) const
    {
        const auto &entry = entries_.at(UpperDirectoryIndex(function));
        if (!entry->child)
            return nullptr;
        EuControlKnobCellArray *actual = nullptr;
        Check(array_.GetChild(entry->member, actual), "Upper child readback");
        return actual;
    }
    bool BindingsForTest() const
    {
        if (entries_.size() != UpperDirectoryEntries.size())
            return false;
        for (size_t i = 0; i < entries_.size(); ++i)
        {
            auto &entry = *entries_[i];
            const auto binding = BindingFor(UpperDirectoryEntries[i].function);
            EuControl *actual = nullptr;
            Check(array_.GetContainedControlByIndex(static_cast<NEuCon::uint32>(i), &actual),
                  "Upper position readback");
            tEuString identity;
            Check(entry.control.GetAttribute(kATRIBID_FuncPersID, identity), "Upper identity readback");
            if (actual != &entry.control || identity != Wide(binding.identity) ||
                !UpperLabelMatches(
                    Primitive(entry.control, EuControlKnobCell::kID_KnobLabelDisplay),
                    UpperDirectoryLabels(UpperDirectoryEntries[i].function, entry.child != nullptr)) ||
                (entry.child && !MatchesBinding(*entry.child, binding)))
                return false;
        }
        return true;
    }

  private:
    EuProcessor &owner_;
    mutable EuControlKnobCellArray array_;
    int tag_;
    std::vector<std::unique_ptr<Entry>> entries_;
    bool registered_ = false;
};
