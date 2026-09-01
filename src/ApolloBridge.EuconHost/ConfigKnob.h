#pragma once
// Included after checked SDK helpers. Rotation previews; only LowerSwitch writes.
class ConfigKnob
{
  public:
    void InitializeCell(EuControlKnobCell &cell, const ConfigSetting &setting, int fixed = -1,
                        const std::vector<int> &choices = {})
    {
        auto &p = Primitive(cell, EuControlKnobCell::kID_Knob);
        const bool filtered = fixed < 0 && !choices.empty();
        const size_t count = fixed >= 0 ? 1 : filtered ? choices.size() : setting.options.size() + 1;
        Initialize(p, kTYP_Float, static_cast<NEuCon::uint16>(count));
        for (size_t i = 0; i < count; ++i)
        {
            Check(p.LoadValueAt(static_cast<NEuCon::uint16>(i), static_cast<float>(i)),
                  "Config choice index");
            const int option = fixed >= 0 ? fixed : filtered ? choices[i] : i == 0 ? -1 : int(i - 1);
            if (option >= static_cast<int>(setting.options.size()))
                throw std::runtime_error("Configuration choice index outside setting");
            const auto label = option < 0
                                   ? ConfigDisplayText(setting.value.scalar.empty() ? "NONE"
                                                                                   : setting.value.scalar)
                                   : ConfigDisplayText(setting.options[static_cast<size_t>(option)].label);
            const auto full = setting.kind == ConfigKind::Plugin && option >= 0
                                  ? ConfigDisplayText(setting.options[static_cast<size_t>(option)].value.scalar)
                                  : label;
            const auto wide = Wide(label);
            const auto shorten = [&](size_t length) {
                auto text = wide.substr(0, length);
                if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF)
                    text.pop_back();
                return text;
            };
            Check(p.LoadValueAt(static_cast<NEuCon::uint16>(i), shorten(4), shorten(8),
                                Wide(full.empty() ? label : full)),
                  "Config choice text");
        }
        auto *knob = dynamic_cast<EuPrimitiveKnob *>(&p);
        if (!knob)
            throw std::runtime_error("Missing configuration knob");
        Check(knob->SetPositionRingMode(fixed < 0 ? kRingPoint : kRingOff), "Config choice ring");
        RawSwitch(Primitive(cell, EuControlKnobCell::kID_LowerSwitch));
        auto &led = Primitive(cell, EuControlKnobCell::kID_LowerSwitchLed);
        Initialize(led, kTYP_Int, 4);
        Check(led.LoadValueTableInterpolated(0, 3), "Config confirmation LED");
        RawSwitch(Primitive(cell, EuControlKnobCell::kID_KnobTouchSense));
    }
    bool Dispatch(const Event &event, const ConfigSetting &setting, ConfigController &controller,
                  int fixed = -1, const std::vector<int> &choices = {})
    {
        if (!event.decoded || std::chrono::steady_clock::now() - event.at > std::chrono::milliseconds(500))
            return false;
        if (event.primitive == EuControlKnobCell::kID_Knob && fixed < 0)
        {
            const bool filtered = !choices.empty();
            const size_t count = filtered ? choices.size() : setting.options.size() + 1;
            if (!std::isfinite(event.value) || std::floor(event.value) != event.value || event.value < 0 ||
                event.value >= count)
                return false;
            if (!filtered && event.value == 0)
            {
                staged_.reset();
                return false;
            }
            staged_ = setting;
            choice_ = filtered ? static_cast<size_t>(choices[static_cast<size_t>(event.value)])
                               : static_cast<size_t>(event.value) - 1;
            epoch_ = event.configEpoch;
            at_ = event.at;
            return false;
        }
        if (event.primitive != EuControlKnobCell::kID_LowerSwitch || event.value == 0 || !event.configEpoch ||
            event.configEpoch != controller.Epoch())
            return false;
        if (fixed >= 0)
            return controller.Submit(setting, static_cast<size_t>(fixed), event.configEpoch);
        if (!staged_ || epoch_ != event.configEpoch || !StillValid(setting))
            return false;
        const bool result = controller.Submit(*staged_, choice_, event.configEpoch);
        staged_.reset();
        return result;
    }
    void Apply(EuControlKnobCell &cell, const ConfigSetting &setting, int fixed = -1,
               const std::vector<int> &choices = {})
    {
        if (staged_ && !StillValid(setting))
            staged_.reset();
        size_t index = 0;
        bool active = false;
        for (size_t i = 0; i < setting.options.size(); ++i)
            if (ConfigValueEqual(setting.value, setting.options[i].value))
            {
                if (fixed >= 0)
                    active = static_cast<size_t>(fixed) == i;
                else if (!choices.empty())
                {
                    const auto found = std::find(choices.begin(), choices.end(), static_cast<int>(i));
                    active = found != choices.end();
                    if (active)
                        index = static_cast<size_t>(found - choices.begin());
                }
                else
                    index = i + 1;
                break;
            }
        if (fixed < 0 && staged_)
        {
            if (choices.empty())
                index = choice_ + 1;
            else
            {
                const auto found = std::find(choices.begin(), choices.end(), static_cast<int>(choice_));
                if (found != choices.end())
                    index = static_cast<size_t>(found - choices.begin());
            }
        }
        auto &p = Primitive(cell, EuControlKnobCell::kID_Knob);
        if (fixed < 0 && choices.empty() && current_ != setting.value.scalar)
        {
            UpdateText(p, Wide(ConfigDisplayText(setting.value.scalar.empty() ? "NONE"
                                                                                : setting.value.scalar)),
                       "Config current value");
            current_ = setting.value.scalar;
        }
        Check(p.SetCurrentIndex(static_cast<NEuCon::uint16>(fixed < 0 ? index : 0)),
              "Config choice feedback");
        Check(Primitive(cell, EuControlKnobCell::kID_LowerSwitchLed)
                  .SetCurrentIndex(
                      static_cast<NEuCon::uint16>(active || staged_ ? kLEDStatus_On : kLEDStatus_Off)),
              "Config choice LED");
    }

  private:
    bool StillValid(const ConfigSetting &setting) const
    {
        return staged_ && staged_->context == setting.context && staged_->key == setting.key &&
               ConfigValueEqual(staged_->value, setting.value) &&
               std::chrono::steady_clock::now() - at_ < std::chrono::seconds(10);
    }
    std::optional<ConfigSetting> staged_;
    size_t choice_ = 0;
    uint64_t epoch_ = 0;
    std::string current_;
    std::chrono::steady_clock::time_point at_{};
};
