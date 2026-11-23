#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <imgui.h>

#include "chess/Types.h"

namespace app {

enum class SoundId { Move, Capture, GameEnd };

// Textures and sounds loaded from the data/ directory.
// Missing files are tolerated: the board falls back to letters and sounds stay silent.
class Assets {
public:
    // Loads everything it can find. Returns a description of what was missing (empty if nothing).
    std::string load(const std::filesystem::path& dataDir);

    bool hasPieces() const { return piecesLoaded_; }
    ImTextureID piecesTexture() const;
    // Texture coordinates of a piece inside the piece sheet.
    void pieceUv(core::Piece piece, ImVec2& uv0, ImVec2& uv1) const;

    // The move-quality badge sheet (six cells in a row, in MoveQuality order), if it was found.
    bool hasBadges() const { return badgesLoaded_; }
    ImTextureID badgesTexture() const;

    // The Moonscorched Productions logo (data/moon-pawn-icon.png), if it was found.
    bool hasLogo() const { return logoLoaded_; }
    ImTextureID logoTexture() const;

    // Plays a sound effect at the given volume (0..100).
    void play(SoundId id, int volume);

    // Replaces a sound effect with the user's own file (ogg, mp3, wav or flac); an empty path
    // brings back the built-in one. Returns false, and uses the built-in sound, when the file
    // cannot be read.
    bool setSound(SoundId id, const std::filesystem::path& custom);

private:
    std::filesystem::path dataDir_;
    sf::Texture pieces_;
    bool piecesLoaded_ = false;
    sf::Texture badges_;
    bool badgesLoaded_ = false;
    sf::Texture logo_;
    bool logoLoaded_ = false;
    std::array<sf::SoundBuffer, 3> buffers_;
    std::array<std::optional<sf::Sound>, 3> sounds_;
};

}  // namespace app
