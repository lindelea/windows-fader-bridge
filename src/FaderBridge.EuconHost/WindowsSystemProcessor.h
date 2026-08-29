#pragma once

#include "EuControlSwitch.h"
#include "EuProcessor.h"

#include <functional>

class WindowsSystemProcessor final : public EuProcessor
{
public:
    using ClearSoloHandler = std::function<void()>;

    explicit WindowsSystemProcessor(ClearSoloHandler clearSoloHandler);
    ~WindowsSystemProcessor() override;

    void SetSoloActive(bool active);

    void OnPrimitiveCallback(tEVT eventType,
        NEuCon::uint32 eventFlags,
        NEuCon::uint32 controlId,
        NEuCon::uint32 arrayMemberControlId,
        NEuCon::uint32 primitiveId,
        EuPrimitiveControl* affectedPrimitive,
        NEuCon::uint16 newValueIndex,
        void* callbackEventData = nullptr) override;

private:
    static constexpr NEuCon::uint32 ClearSoloId = 1U;

    ClearSoloHandler clearSoloHandler_;
    EuControlSwitch clearSolo_;
    bool soloActive_ = false;
};
