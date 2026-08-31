#pragma once
#include "Model.h"
#include <cstdint>
#include <memory>
#include <string>

namespace apollo
{
class ChannelController;
class MonitorController;
class ApolloEucon
{
  public:
    explicit ApolloEucon(const Snapshot &initial, ChannelController &controller, MonitorController &monitor);
    ~ApolloEucon();
    ApolloEucon(const ApolloEucon &) = delete;
    ApolloEucon &operator=(const ApolloEucon &) = delete;
    void Apply(const Snapshot &state);
    uint64_t SurfaceEvents() const;
    size_t VisibleMeters() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
void Log(const std::string &message);
std::wstring Wide(const std::string &utf8);
// Explicit SDK-only regression; no node registration or Apollo connection.
std::string RunEuconTextTests();
} // namespace apollo
