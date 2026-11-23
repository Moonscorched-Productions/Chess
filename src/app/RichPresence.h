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

// Discord Rich Presence through the (legacy) Discord Game SDK.
//
// Entirely optional: when Discord is not running, the SDK library is missing, or the
// application was built without the SDK, every call is a harmless no-op. The connection
// is retried in the background, so Discord may be started or closed at any time.
class RichPresence {
public:
    RichPresence();
    ~RichPresence();
    RichPresence(const RichPresence&) = delete;
    RichPresence& operator=(const RichPresence&) = delete;

    // True when the application was built with the Discord Game SDK.
    static bool available();

    // Turning presence off clears the activity and drops the connection.
    void setEnabled(bool enabled);
    // What to show. Sending is rate limited; only the latest text is kept.
    void set(const PresenceText& text);
    // Pumps the SDK. Call once per frame.
    void tick();
    // True while a connection to a running Discord client is open.
    bool connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace app
