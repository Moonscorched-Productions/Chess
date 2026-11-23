#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace platform {

// A connection to another program on this computer: a Unix domain socket on macOS and Linux,
// a named pipe on Windows. Used to talk to the Discord client.
class LocalConnection {
public:
    // Connects to `endpoint`, a socket path or a pipe name (\\.\pipe\...).
    // Returns nullptr when nothing listens there.
    static std::unique_ptr<LocalConnection> open(const std::string& endpoint);

    ~LocalConnection();
    LocalConnection(const LocalConnection&) = delete;
    LocalConnection& operator=(const LocalConnection&) = delete;

    // Sends all of `data`. Returns false once the connection is broken.
    bool write(std::string_view data);
    // Appends whatever has arrived to `out` without waiting.
    // Returns false once the other side has closed the connection.
    bool readAvailable(std::string& out);

private:
    LocalConnection() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Where a running Discord client may be listening for Rich Presence, most likely first.
// On macOS and Linux only sockets that exist are listed; on Windows all ten pipe names.
std::vector<std::string> discordEndpoints();

// Id of the running process.
long currentProcessId();

}  // namespace platform
