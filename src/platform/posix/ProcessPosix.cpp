#include "platform/Process.h"

// macOS and Linux implementation: fork/exec with two pipes.

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace platform {

namespace {

void closeFd(int& fd) {
    if (fd >= 0) close(fd);
    fd = -1;
}

}  // namespace

struct Process::Impl {
    pid_t pid = -1;
    int stdinWrite = -1;
    int stdoutRead = -1;
    bool exited = false;

    // Collects the exit status if the child has ended. Returns true once it has.
    bool reap(bool block) {
        if (exited || pid <= 0) return true;
        int status = 0;
        const pid_t result = waitpid(pid, &status, block ? 0 : WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD)) exited = true;
        return exited;
    }
};

Process::Process() : impl_(std::make_unique<Impl>()) {}

std::unique_ptr<Process> Process::launch(const std::filesystem::path& executable, std::string* error) {
    const auto failWith = [&](std::string message) -> std::unique_ptr<Process> {
        if (error != nullptr) *error = std::move(message);
        return nullptr;
    };

    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(executable, ec);
    if (ec) absolute = executable;
    if (executable.empty() || !std::filesystem::is_regular_file(absolute, ec))
        return failWith("Engine executable not found: " + executable.string());
    if (access(absolute.c_str(), X_OK) != 0) return failWith("The engine file is not executable: " + absolute.string());

    // Writing to an engine that has died must return an error, not kill the application.
    static const bool sigpipeIgnored = [] {
        std::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)sigpipeIgnored;

    int toChild[2] = {-1, -1};
    int fromChild[2] = {-1, -1};
    if (pipe(toChild) != 0 || pipe(fromChild) != 0) {
        closeFd(toChild[0]);
        closeFd(toChild[1]);
        return failWith(std::string("Could not create pipes: ") + std::strerror(errno));
    }
    // The parent's ends must not leak into this or any later child, or the pipes never report EOF.
    fcntl(toChild[1], F_SETFD, FD_CLOEXEC);
    fcntl(fromChild[0], F_SETFD, FD_CLOEXEC);

    const std::string program = absolute.string();
    const std::string workingDir = absolute.parent_path().string();

    const pid_t pid = fork();
    if (pid < 0) {
        for (int* fd : {&toChild[0], &toChild[1], &fromChild[0], &fromChild[1]}) closeFd(*fd);
        return failWith(std::string("Could not start the engine: ") + std::strerror(errno));
    }
    if (pid == 0) {
        // Child: only async-signal-safe calls from here to exec.
        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        const int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0) dup2(devNull, STDERR_FILENO);
        close(toChild[0]);
        close(fromChild[1]);
        if (!workingDir.empty() && chdir(workingDir.c_str()) != 0) {
            // Not fatal: the engine simply starts in the application's directory.
        }
        char* const arguments[] = {const_cast<char*>(program.c_str()), nullptr};
        execv(program.c_str(), arguments);
        _exit(127);
    }

    closeFd(toChild[0]);
    closeFd(fromChild[1]);
    std::unique_ptr<Process> process(new Process());
    process->impl_->pid = pid;
    process->impl_->stdinWrite = toChild[1];
    process->impl_->stdoutRead = fromChild[0];
    return process;
}

Process::~Process() {
    closeFd(impl_->stdinWrite);
    if (!waitForExit(0)) terminate();
    closeFd(impl_->stdoutRead);
}

bool Process::write(std::string_view data) {
    if (impl_->stdinWrite < 0) return false;
    while (!data.empty()) {
        const ssize_t written = ::write(impl_->stdinWrite, data.data(), data.size());
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data.remove_prefix(static_cast<std::size_t>(written));
    }
    return true;
}

bool Process::read(std::string& out) {
    if (impl_->stdoutRead < 0) return false;
    char buffer[4096];
    for (;;) {
        const ssize_t received = ::read(impl_->stdoutRead, buffer, sizeof(buffer));
        if (received > 0) {
            out.append(buffer, static_cast<std::size_t>(received));
            return true;
        }
        if (received < 0 && errno == EINTR) continue;
        return false;  // end of file or error
    }
}

bool Process::waitForExit(unsigned timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        if (impl_->reap(false)) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void Process::terminate() {
    if (impl_->exited || impl_->pid <= 0) return;
    kill(impl_->pid, SIGKILL);
    impl_->reap(true);
}

}  // namespace platform
