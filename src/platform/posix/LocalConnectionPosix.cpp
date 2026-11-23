#include "platform/LocalConnection.h"

// macOS and Linux: Unix domain sockets.

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace platform {

namespace {

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;  // Linux: no SIGPIPE when the other side is gone
#else
constexpr int kSendFlags = 0;  // macOS uses the SO_NOSIGPIPE socket option instead
#endif

constexpr int kWriteTimeoutMs = 200;

}  // namespace

struct LocalConnection::Impl {
    int fd = -1;
};

std::unique_ptr<LocalConnection> LocalConnection::open(const std::string& endpoint) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (endpoint.empty() || endpoint.size() >= sizeof(address.sun_path)) return nullptr;
    std::memcpy(address.sun_path, endpoint.c_str(), endpoint.size() + 1);

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return nullptr;
#ifdef SO_NOSIGPIPE
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return nullptr;
    }
    // Reads must never block the frame loop.
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    std::unique_ptr<LocalConnection> connection(new LocalConnection());
    connection->impl_ = std::make_unique<Impl>();
    connection->impl_->fd = fd;
    return connection;
}

LocalConnection::~LocalConnection() {
    if (impl_ && impl_->fd >= 0) ::close(impl_->fd);
}

bool LocalConnection::write(std::string_view data) {
    while (!data.empty()) {
        const ssize_t sent = ::send(impl_->fd, data.data(), data.size(), kSendFlags);
        if (sent > 0) {
            data.remove_prefix(static_cast<std::size_t>(sent));
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // The socket is non-blocking; wait briefly for room instead of giving up.
            pollfd waiting{impl_->fd, POLLOUT, 0};
            if (::poll(&waiting, 1, kWriteTimeoutMs) > 0) continue;
        }
        return false;
    }
    return true;
}

bool LocalConnection::readAvailable(std::string& out) {
    char buffer[4096];
    for (;;) {
        const ssize_t received = ::recv(impl_->fd, buffer, sizeof(buffer), 0);
        if (received > 0) {
            out.append(buffer, static_cast<std::size_t>(received));
            continue;
        }
        if (received == 0) return false;  // closed by the other side
        if (errno == EINTR) continue;
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }
}

std::vector<std::string> discordEndpoints() {
    // Discord creates discord-ipc-0..9 in the first temporary directory it finds; Flatpak and
    // Snap installs put them in a subfolder.
    std::vector<std::string> folders;
    for (const char* variable : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        if (const char* value = std::getenv(variable); value != nullptr && *value != '\0') folders.emplace_back(value);
    }
    folders.emplace_back("/tmp");

    std::vector<std::string> endpoints;
    std::error_code ec;
    for (const std::string& folder : folders) {
        for (const char* subfolder : {"", "app/com.discordapp.Discord/", "snap.discord/"}) {
            for (int i = 0; i < 10; ++i) {
                std::string path = folder;
                if (!path.empty() && path.back() != '/') path += '/';
                path += subfolder;
                path += "discord-ipc-" + std::to_string(i);
                if (!std::filesystem::is_socket(path, ec)) continue;
                bool known = false;
                for (const std::string& existing : endpoints) known = known || existing == path;
                if (!known) endpoints.push_back(std::move(path));
            }
        }
    }
    return endpoints;
}

long currentProcessId() { return static_cast<long>(::getpid()); }

}  // namespace platform
