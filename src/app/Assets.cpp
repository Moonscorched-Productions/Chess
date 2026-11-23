#include "app/Assets.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

#include <SFML/Graphics/Image.hpp>
#include <nanosvg.h>
#include <nanosvgrast.h>

namespace app {

namespace {

// Column of each piece type in the sheet: K Q B N R P, white on the top row.
int sheetColumn(core::PieceType type) {
    switch (type) {
        case core::PieceType::King: return 0;
        case core::PieceType::Queen: return 1;
        case core::PieceType::Bishop: return 2;
        case core::PieceType::Knight: return 3;
        case core::PieceType::Rook: return 4;
        default: return 5;
    }
}

// Rasterizes <dir>/wK.svg ... bP.svg into one 6x2 sheet. Returns false if any file is missing or broken.
bool loadSvgSheet(sf::Texture& texture, const std::filesystem::path& dir) {
    constexpr unsigned kCell = 256;
    constexpr std::array<core::PieceType, 6> kTypes = {core::PieceType::King,   core::PieceType::Queen,
                                                       core::PieceType::Bishop, core::PieceType::Knight,
                                                       core::PieceType::Rook,   core::PieceType::Pawn};
    constexpr char kLetters[] = {'P', 'N', 'B', 'R', 'Q', 'K'};  // indexed by PieceType

    struct RasterizerDeleter {
        void operator()(NSVGrasterizer* r) const { nsvgDeleteRasterizer(r); }
    };
    struct ImageDeleter {
        void operator()(NSVGimage* image) const { nsvgDelete(image); }
    };
    const std::unique_ptr<NSVGrasterizer, RasterizerDeleter> rasterizer(nsvgCreateRasterizer());
    if (!rasterizer) return false;

    sf::Image sheet(sf::Vector2u{kCell * 6, kCell * 2}, sf::Color::Transparent);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(kCell) * kCell * 4);

    for (unsigned row = 0; row < 2; ++row) {
        for (const core::PieceType type : kTypes) {
            const std::string name = std::string(1, row == 0 ? 'w' : 'b') + kLetters[static_cast<int>(type)] + ".svg";
            std::ifstream in(dir / name, std::ios::binary);
            if (!in) return false;
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

            // nsvgParse modifies its input, which must be null terminated; std::string provides both.
            const std::unique_ptr<NSVGimage, ImageDeleter> image(nsvgParse(text.data(), "px", 96.0f));
            if (!image || image->width <= 0.0f || image->height <= 0.0f) return false;

            const float scale = static_cast<float>(kCell) / std::max(image->width, image->height);
            std::fill(pixels.begin(), pixels.end(), static_cast<unsigned char>(0));
            nsvgRasterize(rasterizer.get(), image.get(), 0.0f, 0.0f, scale, pixels.data(), static_cast<int>(kCell),
                          static_cast<int>(kCell), static_cast<int>(kCell * 4));

            const unsigned originX = static_cast<unsigned>(sheetColumn(type)) * kCell;
            const unsigned originY = row * kCell;
            for (unsigned y = 0; y < kCell; ++y) {
                for (unsigned x = 0; x < kCell; ++x) {
                    const unsigned char* p = &pixels[(static_cast<std::size_t>(y) * kCell + x) * 4];
                    sheet.setPixel({originX + x, originY + y}, sf::Color(p[0], p[1], p[2], p[3]));
                }
            }
        }
    }
    return texture.loadFromImage(sheet);
}

// The built-in sound effects in data/sfx, in SoundId order.
constexpr std::array<const char*, 3> kSoundFiles = {"move.ogg", "capture.ogg", "mate.ogg"};

}  // namespace

std::string Assets::load(const std::filesystem::path& dataDir) {
    std::string missing;
    const auto note = [&](const char* what) {
        if (!missing.empty()) missing += ", ";
        missing += what;
    };

    // Prefer the vector piece set; fall back to the low-resolution sheet.
    piecesLoaded_ = !dataDir.empty() && (loadSvgSheet(pieces_, dataDir / "piece_set" / "cburnett") ||
                                         pieces_.loadFromFile(dataDir / "piece_set" / "cburnett.png"));
    if (piecesLoaded_) {
        pieces_.setSmooth(true);
        (void)pieces_.generateMipmap();
    } else {
        note("piece set");
    }

    // Optional: without the sheet the badges are drawn from simple shapes.
    // data/logo.png ships with releases; the large icon source only exists in the repository.
    logoLoaded_ = !dataDir.empty() && (logo_.loadFromFile(dataDir / "logo.png") ||
                                       logo_.loadFromFile(dataDir / "moon-pawn-icon.png"));
    if (logoLoaded_) {
        logo_.setSmooth(true);
        (void)logo_.generateMipmap();  // drawn smaller than its size
    }
    badgesLoaded_ = !dataDir.empty() && badges_.loadFromFile(dataDir / "badges" / "badges.png");
    if (badgesLoaded_) {
        badges_.setSmooth(true);
        (void)badges_.generateMipmap();
    }

    dataDir_ = dataDir;
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
        setSound(static_cast<SoundId>(i), {});
        if (!sounds_[i]) note(kSoundFiles[i]);
    }
    return missing;
}

bool Assets::setSound(SoundId id, const std::filesystem::path& custom) {
    const std::size_t i = static_cast<std::size_t>(id);
    sounds_[i].reset();  // the sound refers to the buffer that is about to change
    const bool customLoaded = !custom.empty() && buffers_[i].loadFromFile(custom);
    if (customLoaded || (!dataDir_.empty() && buffers_[i].loadFromFile(dataDir_ / "sfx" / kSoundFiles[i]))) {
        sounds_[i].emplace(buffers_[i]);
    }
    return custom.empty() || customLoaded;
}

namespace {

// ImGui-SFML expects the OpenGL texture name stored in the ImTextureID.
ImTextureID textureId(const sf::Texture& texture) {
    const unsigned int handle = texture.getNativeHandle();
    ImTextureID id{};
    std::memcpy(&id, &handle, sizeof(handle));
    return id;
}

}  // namespace

ImTextureID Assets::piecesTexture() const { return textureId(pieces_); }

ImTextureID Assets::badgesTexture() const { return textureId(badges_); }
ImTextureID Assets::logoTexture() const { return textureId(logo_); }

void Assets::pieceUv(core::Piece piece, ImVec2& uv0, ImVec2& uv1) const {
    const sf::Vector2u size = pieces_.getSize();
    const float width = size.x > 0 ? static_cast<float>(size.x) : 1.0f;
    const float height = size.y > 0 ? static_cast<float>(size.y) : 1.0f;
    const float cellW = width / 6.0f;
    const float cellH = height / 2.0f;
    const float column = static_cast<float>(sheetColumn(piece.type));
    const float row = piece.color == core::Color::White ? 0.0f : 1.0f;
    // Half a texel of inset keeps neighbouring pieces from bleeding in when filtered.
    uv0 = ImVec2((column * cellW + 0.5f) / width, (row * cellH + 0.5f) / height);
    uv1 = ImVec2(((column + 1.0f) * cellW - 0.5f) / width, ((row + 1.0f) * cellH - 0.5f) / height);
}

void Assets::play(SoundId id, int volume) {
    std::optional<sf::Sound>& sound = sounds_[static_cast<std::size_t>(id)];
    if (!sound) return;
    sound->setVolume(static_cast<float>(std::clamp(volume, 0, 100)));
    sound->play();
}

}  // namespace app
