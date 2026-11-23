#pragma once

#include <memory>
#include <string>

namespace app {

// The two lines Discord shows under the application name.
struct PresenceText {
    std::string details;  // first line, e.g. "Playing Stockfish 19"
    std::string state;    // second line, e.g. "Move 12"

    bool operator==(const PresenceText&) const = default;
};

// Discord Rich Presence, talking directly to the Discord client's local RPC connection.
// No Discord library is needed.
//
// Entirely optional: when Discord is not running every call is a harmless no-op. The
// connection is retried in the background, so Discord may be started or closed at any time.
class RichPresence {
public:
    RichPresence();
    ~RichPresence();
    RichPresence(const RichPresence&) = delete;
    RichPresence& operator=(const RichPresence&) = delete;

    // Turning presence off clears the activity and drops the connection.
    void setEnabled(bool enabled);
    // What to show. Sending is rate limited; only the latest text is kept.
    void set(const PresenceText& text);
    // Connects, reads Discord's messages and sends updates. Call once per frame; never blocks.
    void tick();
    // True while a running Discord client has accepted the connection.
    bool connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace app
