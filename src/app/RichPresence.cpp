#include "app/RichPresence.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

#ifdef CHESS_DISCORD
#include <discord_game_sdk.h>

#include "platform/Platform.h"

#include "app/Paths.h"
#endif

namespace app {

#ifdef CHESS_DISCORD

namespace {

constexpr DiscordClientId kApplicationId = 1574346272645845143;
constexpr const char* kLargeImage = "icon";
constexpr const char* kLargeText = "Chess";
constexpr auto kReconnectInterval = std::chrono::seconds(15);
constexpr auto kSendInterval = std::chrono::seconds(2);

using Clock = std::chrono::steady_clock;

template <std::size_t N>
void copyText(char (&target)[N], const std::string& text) {
    // Discord wants at least two characters per line; shorter text is left out.
    if (text.size() < 2) {
        target[0] = '\0';
        return;
    }
    std::size_t length = text.size() < N - 1 ? text.size() : N - 1;
    // Do not cut a UTF-8 character in half.
    while (length > 0 && length < text.size() && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) --length;
    std::memcpy(target, text.data(), length);
    target[length] = '\0';
}

void DISCORD_CALLBACK ignoreResult(void*, EDiscordResult) {}

}  // namespace

struct RichPresence::Impl {
    IDiscordCore* core = nullptr;
    IDiscordActivityManager* activities = nullptr;
    bool enabled = false;
    bool dirty = false;
    PresenceText text;
    std::int64_t startedAt = static_cast<std::int64_t>(std::time(nullptr));
    Clock::time_point lastAttempt = Clock::now() - kReconnectInterval;
    Clock::time_point lastSend = Clock::now() - kSendInterval;

    void disconnect() {
        if (core != nullptr) core->destroy(core);
        core = nullptr;
        activities = nullptr;
    }

    // The SDK library is loaded at run time, so the application also starts without it.
    using CreateFunction = EDiscordResult(DISCORD_API*)(DiscordVersion, DiscordCreateParams*, IDiscordCore**);
    CreateFunction create = nullptr;
    bool loadAttempted = false;

    std::unique_ptr<platform::SharedLibrary> library;

    void loadLibrary() {
        loadAttempted = true;
        library = platform::SharedLibrary::load(executableDir() / platform::sharedLibraryFileName("discord_game_sdk"));
        if (library) create = reinterpret_cast<CreateFunction>(library->symbol("DiscordCreate"));
    }

    void connect() {
        lastAttempt = Clock::now();
        if (!loadAttempted) loadLibrary();
        if (create == nullptr) return;
        DiscordCreateParams params;
        DiscordCreateParamsSetDefault(&params);
        params.client_id = kApplicationId;
        // Without this flag the SDK would close the application to launch Discord.
        params.flags = DiscordCreateFlags_NoRequireDiscord;
        if (create(DISCORD_VERSION, &params, &core) != DiscordResult_Ok || core == nullptr) {
            core = nullptr;
            return;
        }
        activities = core->get_activity_manager(core);
        dirty = true;
    }

    void send() {
        DiscordActivity activity;
        std::memset(&activity, 0, sizeof(activity));
        activity.type = DiscordActivityType_Playing;
        copyText(activity.details, text.details);
        copyText(activity.state, text.state);
        copyText(activity.assets.large_image, kLargeImage);
        copyText(activity.assets.large_text, kLargeText);
        activity.timestamps.start = startedAt;
        activities->update_activity(activities, &activity, nullptr, ignoreResult);
        dirty = false;
        lastSend = Clock::now();
    }
};

RichPresence::RichPresence() : impl_(std::make_unique<Impl>()) {}

RichPresence::~RichPresence() { impl_->disconnect(); }

bool RichPresence::available() { return true; }

bool RichPresence::connected() const { return impl_->core != nullptr; }

void RichPresence::setEnabled(bool enabled) {
    if (impl_->enabled == enabled) return;
    impl_->enabled = enabled;
    if (!enabled) {
        if (impl_->activities != nullptr) {
            impl_->activities->clear_activity(impl_->activities, nullptr, ignoreResult);
            impl_->core->run_callbacks(impl_->core);
        }
        impl_->disconnect();
    } else {
        impl_->lastAttempt = Clock::now() - kReconnectInterval;  // connect on the next tick
    }
}

void RichPresence::set(const PresenceText& text) {
    if (impl_->text == text) return;
    impl_->text = text;
    impl_->dirty = true;
}

void RichPresence::tick() {
    if (!impl_->enabled) return;
    if (impl_->core == nullptr) {
        if (Clock::now() - impl_->lastAttempt < kReconnectInterval) return;
        impl_->connect();
        if (impl_->core == nullptr) return;
    }
    if (impl_->dirty && Clock::now() - impl_->lastSend >= kSendInterval) impl_->send();
    // Fails once Discord has been closed; drop the connection and retry later.
    if (impl_->core->run_callbacks(impl_->core) != DiscordResult_Ok) {
        impl_->disconnect();
        impl_->lastAttempt = Clock::now();
    }
}

#else  // built without the Discord Game SDK

struct RichPresence::Impl {};

RichPresence::RichPresence() : impl_(std::make_unique<Impl>()) {}
RichPresence::~RichPresence() = default;
bool RichPresence::available() { return false; }
bool RichPresence::connected() const { return false; }
void RichPresence::setEnabled(bool) {}
void RichPresence::set(const PresenceText&) {}
void RichPresence::tick() {}

#endif

}  // namespace app
