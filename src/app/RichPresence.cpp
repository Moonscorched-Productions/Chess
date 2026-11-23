#include "app/RichPresence.h"

// Discord Rich Presence over the Discord client's local RPC connection (a Unix socket or a
// named pipe called discord-ipc-N). Messages are frames of [opcode:int32 LE][length:int32 LE]
// followed by that many bytes of JSON. After a HANDSHAKE with the application id, Discord
// answers with a READY event; SET_ACTIVITY commands then change what it shows.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>

#include "platform/LocalConnection.h"

namespace app {

namespace {

constexpr const char* kApplicationId = "1574346272645845143";
constexpr const char* kLargeImage = "icon";
constexpr const char* kLargeText = "Chess";
constexpr auto kReconnectInterval = std::chrono::seconds(15);
constexpr auto kHandshakeTimeout = std::chrono::seconds(10);
constexpr auto kSendInterval = std::chrono::seconds(2);
constexpr std::size_t kMaxTextBytes = 128;          // Discord's limit for details and state
constexpr std::uint32_t kMaxFrameBytes = 64 * 1024;  // anything larger is not Discord talking

enum Opcode : std::uint32_t { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

using Clock = std::chrono::steady_clock;

std::string frame(std::uint32_t opcode, std::string_view json) {
    std::string out;
    out.reserve(8 + json.size());
    const auto put = [&out](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>((value >> shift) & 0xFF));
    };
    put(opcode);
    put(static_cast<std::uint32_t>(json.size()));
    out.append(json);
    return out;
}

std::uint32_t readUint32(std::string_view bytes) {
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; --i) value = (value << 8) | static_cast<unsigned char>(bytes[static_cast<std::size_t>(i)]);
    return value;
}

// A JSON string literal, quotes included.
std::string jsonQuoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
                    out += escaped;
                } else {
                    out += c;  // UTF-8 passes through
                }
        }
    }
    return out + "\"";
}

// Discord rejects lines shorter than two characters and cuts long ones; keep within its limit
// without splitting a UTF-8 character.
std::string fitText(const std::string& text) {
    if (text.size() < 2) return {};
    std::size_t length = text.size() < kMaxTextBytes ? text.size() : kMaxTextBytes;
    while (length > 0 && length < text.size() && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) --length;
    return text.substr(0, length);
}

// The string value of `"key": "value"` in a flat search over `json`; empty if missing.
std::string_view stringField(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    std::size_t at = json.find(needle);
    if (at == std::string_view::npos) return {};
    at += needle.size();
    while (at < json.size() && (json[at] == ' ' || json[at] == ':')) ++at;
    if (at >= json.size() || json[at] != '"') return {};
    const std::size_t end = json.find('"', at + 1);
    return end == std::string_view::npos ? std::string_view{} : json.substr(at + 1, end - at - 1);
}

}  // namespace

struct RichPresence::Impl {
    std::unique_ptr<platform::LocalConnection> connection;
    bool ready = false;  // the handshake was answered
    std::string received;
    bool enabled = false;
    bool dirty = false;
    PresenceText text;
    std::int64_t startedAt = static_cast<std::int64_t>(std::time(nullptr));
    unsigned nonce = 0;
    Clock::time_point lastAttempt = Clock::now() - kReconnectInterval;
    Clock::time_point handshakeAt{};
    Clock::time_point lastSend = Clock::now() - kSendInterval;

    void disconnect() {
        connection.reset();
        ready = false;
        received.clear();
        lastAttempt = Clock::now();
    }

    void connect() {
        lastAttempt = Clock::now();
        for (const std::string& endpoint : platform::discordEndpoints()) {
            connection = platform::LocalConnection::open(endpoint);
            if (!connection) continue;
            const std::string hello = std::string("{\"v\":1,\"client_id\":") + jsonQuoted(kApplicationId) + "}";
            if (connection->write(frame(Handshake, hello))) {
                handshakeAt = Clock::now();
                return;
            }
            connection.reset();
        }
    }

    bool sendCommand(const std::string& json) {
        if (connection->write(frame(Frame, json))) return true;
        disconnect();
        return false;
    }

    // `withActivity` false clears the presence.
    void sendActivity(bool withActivity) {
        std::string activity = "null";
        if (withActivity) {
            activity = "{";
            const std::string details = fitText(text.details);
            const std::string state = fitText(text.state);
            if (!details.empty()) activity += "\"details\":" + jsonQuoted(details) + ",";
            if (!state.empty()) activity += "\"state\":" + jsonQuoted(state) + ",";
            activity += "\"timestamps\":{\"start\":" + std::to_string(startedAt) + "},";
            activity += "\"assets\":{\"large_image\":" + jsonQuoted(kLargeImage) + ",\"large_text\":" + jsonQuoted(kLargeText) + "}}";
        }
        const std::string command = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
                                    std::to_string(platform::currentProcessId()) + ",\"activity\":" + activity +
                                    "},\"nonce\":" + jsonQuoted(std::to_string(++nonce)) + "}";
        if (sendCommand(command)) {
            dirty = false;
            lastSend = Clock::now();
        }
    }

    // Handles every complete message received so far.
    void processMessages() {
        while (connection && received.size() >= 8) {
            const std::uint32_t opcode = readUint32(std::string_view(received).substr(0, 4));
            const std::uint32_t length = readUint32(std::string_view(received).substr(4, 4));
            if (length > kMaxFrameBytes) {
                disconnect();
                return;
            }
            if (received.size() < 8 + static_cast<std::size_t>(length)) return;  // the rest is still on its way
            const std::string payload = received.substr(8, length);
            received.erase(0, 8 + static_cast<std::size_t>(length));

            switch (opcode) {
                case Frame:
                    if (stringField(payload, "evt") == "READY") {
                        ready = true;
                        dirty = true;  // show the current text right away
                    }
                    break;
                case Ping:
                    if (!connection->write(frame(Pong, payload))) disconnect();
                    break;
                case Close:  // e.g. Discord closing, or an unknown application id
                    disconnect();
                    break;
                default:
                    break;
            }
        }
    }
};

RichPresence::RichPresence() : impl_(std::make_unique<Impl>()) {}

RichPresence::~RichPresence() {
    // Discord clears the activity itself when the connection closes.
    impl_->connection.reset();
}

bool RichPresence::connected() const { return impl_->ready; }

void RichPresence::setEnabled(bool enabled) {
    if (impl_->enabled == enabled) return;
    impl_->enabled = enabled;
    if (!enabled) {
        if (impl_->ready) impl_->sendActivity(false);
        impl_->disconnect();
    } else {
        impl_->lastAttempt = Clock::now() - kReconnectInterval;  // connect on the next tick
        impl_->dirty = true;
    }
}

void RichPresence::set(const PresenceText& text) {
    if (impl_->text == text) return;
    impl_->text = text;
    impl_->dirty = true;
}

void RichPresence::tick() {
    Impl& d = *impl_;
    if (!d.enabled) return;
    if (!d.connection) {
        if (Clock::now() - d.lastAttempt < kReconnectInterval) return;
        d.connect();
        if (!d.connection) return;
    }
    if (!d.connection->readAvailable(d.received)) {  // Discord was closed
        d.disconnect();
        return;
    }
    d.processMessages();
    if (!d.connection) return;
    if (!d.ready) {
        if (Clock::now() - d.handshakeAt > kHandshakeTimeout) d.disconnect();
        return;
    }
    if (d.dirty && Clock::now() - d.lastSend >= kSendInterval) d.sendActivity(true);
}

}  // namespace app
