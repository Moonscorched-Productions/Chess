#include "platform/Process.h"

#include <string>
#include <system_error>
#include <utility>


#include <windows.h>

namespace platform {

namespace {

// Owns a Win32 HANDLE.
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : handle_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.release();
        }
        return *this;
    }

    bool valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return handle_; }
    HANDLE release() {
        const HANDLE handle = handle_;
        handle_ = nullptr;
        return handle;
    }
    void reset() {
        if (valid()) CloseHandle(handle_);
        handle_ = nullptr;
    }

private:
    HANDLE handle_ = nullptr;
};

std::string lastErrorMessage(const char* what) {
    return std::string(what) + " (Windows error " + std::to_string(GetLastError()) + ")";
}

bool createPipe(Handle& readEnd, Handle& writeEnd) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (!CreatePipe(&read, &write, &attributes, 0)) return false;
    readEnd = Handle(read);
    writeEnd = Handle(write);
    return true;
}

}  // namespace

struct Process::Impl {
    Handle process;
    Handle job;
    Handle stdinWrite;
    Handle stdoutRead;
};

Process::Process() : impl_(std::make_unique<Impl>()) {}

std::unique_ptr<Process> Process::launch(const std::filesystem::path& executable, std::string* error) {
    const auto failWith = [&](std::string message) -> std::unique_ptr<Process> {
        if (error != nullptr) *error = std::move(message);
        return nullptr;
    };

    std::error_code ec;
    if (executable.empty() || !std::filesystem::is_regular_file(executable, ec)) {
        const std::u8string shown = executable.u8string();
        return failWith("Engine executable not found: " + std::string(shown.begin(), shown.end()));
    }

    Handle childStdinRead, parentStdinWrite, parentStdoutRead, childStdoutWrite;
    if (!createPipe(childStdinRead, parentStdinWrite) || !createPipe(parentStdoutRead, childStdoutWrite))
        return failWith(lastErrorMessage("Could not create pipes"));

    // The parent's ends must not be inherited, or the pipes never report EOF.
    SetHandleInformation(parentStdinWrite.get(), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(parentStdoutRead.get(), HANDLE_FLAG_INHERIT, 0);

    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;
    Handle nullDevice(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                                  OPEN_EXISTING, 0, nullptr));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childStdinRead.get();
    startup.hStdOutput = childStdoutWrite.get();
    startup.hStdError = nullDevice.valid() ? nullDevice.get() : childStdoutWrite.get();

    // Relative paths would be resolved against the child's working directory, so make it absolute.
    std::filesystem::path absolute = std::filesystem::absolute(executable, ec);
    if (ec) absolute = executable;
    absolute.make_preferred();
    std::wstring commandLine = L"\"" + absolute.wstring() + L"\"";
    const std::wstring workingDir = absolute.parent_path().wstring();

    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                        workingDir.empty() ? nullptr : workingDir.c_str(), &startup, &info);
    if (!created) return failWith(lastErrorMessage("Could not start the engine"));

    std::unique_ptr<Process> process(new Process());
    process->impl_->process = Handle(info.hProcess);
    Handle thread(info.hThread);

    // Tie the child's lifetime to ours, so it cannot outlive a crashed application.
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (job.valid()) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(job.get(), info.hProcess);
        process->impl_->job = std::move(job);
    }
    ResumeThread(thread.get());

    process->impl_->stdinWrite = std::move(parentStdinWrite);
    process->impl_->stdoutRead = std::move(parentStdoutRead);
    return process;
}

Process::~Process() {
    impl_->stdinWrite.reset();
    if (!waitForExit(0)) terminate();
}

bool Process::write(std::string_view data) {
    if (!impl_->stdinWrite.valid()) return false;
    while (!data.empty()) {
        DWORD written = 0;
        if (!WriteFile(impl_->stdinWrite.get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr) ||
            written == 0) {
            return false;
        }
        data.remove_prefix(written);
    }
    return true;
}

bool Process::read(std::string& out) {
    if (!impl_->stdoutRead.valid()) return false;
    char buffer[4096];
    DWORD received = 0;
    if (!ReadFile(impl_->stdoutRead.get(), buffer, sizeof(buffer), &received, nullptr) || received == 0) return false;
    out.append(buffer, received);
    return true;
}

bool Process::waitForExit(unsigned timeoutMs) {
    if (!impl_->process.valid()) return true;
    return WaitForSingleObject(impl_->process.get(), timeoutMs) == WAIT_OBJECT_0;
}

void Process::terminate() {
    if (!impl_->process.valid()) return;
    TerminateProcess(impl_->process.get(), 1);
    WaitForSingleObject(impl_->process.get(), 2000);
}

}  // namespace platform

