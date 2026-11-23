#include "platform/LocalConnection.h"

// Windows: named pipes.

#include <windows.h>

namespace platform {

struct LocalConnection::Impl {
    HANDLE pipe = INVALID_HANDLE_VALUE;
};

std::unique_ptr<LocalConnection> LocalConnection::open(const std::string& endpoint) {
    // Pipe names are plain ASCII ("\\.\pipe\discord-ipc-0").
    const std::wstring name(endpoint.begin(), endpoint.end());
    const HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return nullptr;

    std::unique_ptr<LocalConnection> connection(new LocalConnection());
    connection->impl_ = std::make_unique<Impl>();
    connection->impl_->pipe = pipe;
    return connection;
}

LocalConnection::~LocalConnection() {
    if (impl_ && impl_->pipe != INVALID_HANDLE_VALUE) CloseHandle(impl_->pipe);
}

bool LocalConnection::write(std::string_view data) {
    while (!data.empty()) {
        DWORD written = 0;
        if (!WriteFile(impl_->pipe, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) || written == 0) {
            return false;
        }
        data.remove_prefix(written);
    }
    return true;
}

bool LocalConnection::readAvailable(std::string& out) {
    for (;;) {
        // Peeking first keeps ReadFile from blocking when nothing has arrived.
        DWORD available = 0;
        if (!PeekNamedPipe(impl_->pipe, nullptr, 0, nullptr, &available, nullptr)) return false;  // broken
        if (available == 0) return true;
        char buffer[4096];
        DWORD received = 0;
        const DWORD wanted = available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
        if (!ReadFile(impl_->pipe, buffer, wanted, &received, nullptr)) return false;
        out.append(buffer, received);
    }
}

std::vector<std::string> discordEndpoints() {
    // Checking whether a pipe exists would itself connect to it, so all ten names are listed;
    // opening one that does not exist fails at once.
    std::vector<std::string> endpoints;
    for (int i = 0; i < 10; ++i) endpoints.push_back("\\\\.\\pipe\\discord-ipc-" + std::to_string(i));
    return endpoints;
}

long currentProcessId() { return static_cast<long>(GetCurrentProcessId()); }

}  // namespace platform
