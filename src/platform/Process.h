#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace platform {

// A child process with redirected standard input and output.
// The child is terminated when the object is destroyed.
class Process {
public:
    // Starts the executable. Returns nullptr and fills `error` on failure.
    static std::unique_ptr<Process> launch(const std::filesystem::path& executable, std::string* error = nullptr);

    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // Writes to the child's standard input. Returns false if the pipe is closed.
    bool write(std::string_view data);

    // Blocks until the child produces output, then appends it to `out`.
    // Returns false once the child has closed its output (normally: it exited).
    // May be called from a different thread than write().
    bool read(std::string& out);

    // Waits up to `timeoutMs` for the child to exit on its own. Returns true if it did.
    bool waitForExit(unsigned timeoutMs);

    // Forcibly ends the child. Safe to call more than once.
    void terminate();

private:
    Process();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace platform
