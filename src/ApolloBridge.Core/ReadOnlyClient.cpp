#include <winsock2.h>
#include <ws2tcpip.h>
#include "ReadOnlyClient.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace apollo
{
namespace
{
using Clock = std::chrono::steady_clock;
}
ReadOnlyClient::ReadOnlyClient(const std::atomic<bool> &stop, uint16_t port) : stop_(stop), port_(port)
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        throw std::runtime_error("Windows socket initialization failed");
    winsock_ = true;
}
ReadOnlyClient::~ReadOnlyClient()
{
    Close();
    if (winsock_)
        WSACleanup();
}
void ReadOnlyClient::Close() noexcept
{
    if (socket_ != INVALID_SOCKET)
    {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    decoder_.Reset();
    inbox_.clear();
    partialSince_ = {};
}
void ReadOnlyClient::CheckStop() const
{
    if (stop_)
        throw std::runtime_error("Stopped");
}
bool ReadOnlyClient::Wait(bool writing, int milliseconds)
{
    CheckStop();
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket_, &set);
    timeval timeout{milliseconds / 1000, (milliseconds % 1000) * 1000};
    const int status = select(0, writing ? nullptr : &set, writing ? &set : nullptr, nullptr, &timeout);
    if (status == SOCKET_ERROR)
        throw std::runtime_error("Apollo socket wait failed");
    return status > 0;
}
void ReadOnlyClient::Connect()
{
    Close();
    CheckStop();
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET)
        throw std::runtime_error("Cannot create Apollo socket");
    u_long nonblocking = 1;
    if (ioctlsocket(socket_, FIONBIO, &nonblocking) != 0)
        throw std::runtime_error("Cannot configure Apollo socket");
    const int nodelay = 1;
    setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port_);
    if (connect(socket_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
            throw std::runtime_error("UA Mixer Engine is not available");
        const auto deadline = Clock::now() + std::chrono::milliseconds(1500);
        while (!Wait(true, 50))
            if (Clock::now() >= deadline)
                throw std::runtime_error("Apollo connection timed out");
        int error = 0, length = sizeof(error);
        if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &length) != 0 || error)
            throw std::runtime_error("UA Mixer Engine connection failed");
    }
}
void ReadOnlyClient::SendRead(std::string_view verb, const std::string &path)
{
    SendBytes(ReadCommand(verb, path));
    ++reads_;
}
void ReadOnlyClient::SendBytes(const std::string &command)
{
    size_t sent = 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(1500);
    while (sent < command.size())
    {
        if (Clock::now() >= deadline)
            throw std::runtime_error("Apollo send deadline exceeded");
        if (!Wait(true, 50))
            continue;
        const int count = send(socket_, command.data() + sent, static_cast<int>(command.size() - sent), 0);
        if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
            continue;
        if (count <= 0)
            throw std::runtime_error("Apollo send failed");
        sent += static_cast<size_t>(count);
    }
}
std::optional<Json> ReadOnlyClient::Poll(int milliseconds)
{
    CheckStop();
    if (inbox_.empty())
    {
        if (decoder_.Partial() && Clock::now() - partialSince_ > std::chrono::seconds(3))
            throw std::runtime_error("Apollo partial frame deadline exceeded");
        if (!Wait(false, std::clamp(milliseconds, 0, 100)))
            return {};
        std::array<char, 16384> buffer{};
        const int count = recv(socket_, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
            return {};
        if (count <= 0)
            throw std::runtime_error("Apollo connection closed");
        const bool wasPartial = decoder_.Partial();
        auto frames = decoder_.Feed(std::string_view(buffer.data(), static_cast<size_t>(count)));
        if (decoder_.Partial() && (!wasPartial || !frames.empty()))
            partialSince_ = Clock::now();
        for (const auto &frame : frames)
        {
            if (inbox_.size() >= 4096)
                throw std::runtime_error("Apollo response queue limit exceeded");
            auto parsed = Json::Parse(frame);
            if (parsed.kind != Json::Kind::Object || !IsPath(parsed.At("path").String()))
                throw std::runtime_error("Invalid Apollo response envelope");
            inbox_.push_back(std::move(parsed));
            ++frames_;
        }
    }
    if (inbox_.empty())
        return {};
    auto result = std::move(inbox_.front());
    inbox_.pop_front();
    return result;
}
Json ReadOnlyClient::Get(const std::string &path, const std::function<void(const Json &)> &update)
{
    SendRead("get", path);
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < deadline)
    {
        auto response = Poll(50);
        if (!response)
            continue;
        if (response->At("path").String() == path)
        {
            if (response->Has("error") || !response->Has("data"))
                throw std::runtime_error("Apollo rejected read");
            return response->At("data");
        }
        if (update)
            update(*response);
    }
    throw std::runtime_error("Apollo read timed out");
}
void ReadOnlyClient::Subscribe(const std::string &path)
{
    SendRead("subscribe", path);
}
} // namespace apollo
