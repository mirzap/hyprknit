#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace hyprknit {

// Knitted window borders.
//
// The band is the ring between the window's rounded rect and that rect grown by
// the border width. Because it is built from the window's real corner radius,
// the knit follows the window's rounding exactly.
//
// The stitch pattern is strictly periodic, so it is rendered once into a small
// seamless tile and then tiled into the ring. That matters: stroking every
// stitch of a full-screen window directly would cost tens of thousands of tiny
// curves on each resize.

enum class Stitch : std::uint8_t { Stockinette = 0, Rib, Garter };

// Where each unpatched side's pattern repeat is anchored.
//
//   Corner  cast on at the mitre corner. Rock steady while the window resizes,
//           because the anchor does not depend on the side's length. The far
//           end takes a partial repeat.
//   Centre  the repeat is centred on each side, so both ends match. Composed,
//           but it slides at half the resize rate.
enum class Anchor : std::uint8_t { Corner = 0, Centre };

struct Gauge {
  float rows = 6.F;         // chart rows across the band (plain: stitch rows)
  float aspect = 1.35F;     // stitch width / row height; real stockinette ~1.35
  float rowOverlap = 0.12F; // how far each row nests into the one above (0..1)
  float yarn = 0.48F;       // yarn thickness, as a fraction of stitch width
  float bow = 0.28F;        // how far the arms of the stitch bow out (0..1)
  float jitter = 0.035F;    // hand-knit wobble, as a fraction of stitch width
  float ground = 0.98F;     // brightness of the wool behind the stitches
  float ambient = 0.94F;    // light everywhere; raise it to keep hues vivid
  float relief = 0.70F;     // how steeply the yarn stands up under the light
  float sheen = 0.10F;      // strength of the directional light on the yarn

  friend bool operator==(const Gauge &, const Gauge &) = default;
};

struct Basket {
  std::string_view name;
  std::span<const std::uint32_t> colors;
};

/// Baskets of wool, for apps without a curated colourway.
std::span<const Basket> baskets();

/// Everything that changes how the yarn itself is knitted. Tiles are cached per
/// (band width, colour, chart), so any change here invalidates all of them;
/// `applyKnitting` is the only way to set it, and it does that flush.
struct Knitting {
  Gauge gauge{};
  Stitch stitch = Stitch::Stockinette;
  int basket = 3; // index into baskets(); 3 is "wool"
  Anchor anchor = Anchor::Corner;

  friend bool operator==(const Knitting &, const Knitting &) = default;
};

const Knitting &knitting();

/// Returns true, and discards every cached tile, when something actually moved.
bool applyKnitting(const Knitting &next);

/// Index into `baskets()`, or -1. Names are those the menu and config use.
int basketIndex(std::string_view name);

/// Index castable to `Stitch`, or -1.
int stitchIndex(std::string_view name);
std::string_view stitchName(Stitch stitch);

/// Stable fallback yarn for an app without a curated colourway. Stable across
/// windows and launches, with no icon decoding or compositor query.
std::uint32_t colorForApp(std::string_view app);

struct Pixels {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba; // premultiplied, row-major
};

struct Band {
  int frameWidth = 0; // outer frame, device pixels
  int frameHeight = 0;
  int band = 0;        // knitted width, device pixels
  double radius = 0.0; // outer corner radius, device pixels
  std::uint32_t color = 0xff000000u;
  int chart = -1; // -1 = plain, no colourwork
  /// How far an unfocused window's band is darkened, 0..1. This darkens rather
  /// than fades: the band stays fully opaque at any value.
  float dim = 0.F;
};

/// Fill `out` (sized by the caller) with the piece of the band covering the
/// frame-local rectangle whose top-left corner is (originX, originY). Pixels
/// outside the ring are left fully transparent.
void renderBandRegion(const Band &band, int originX, int originY, Pixels &out);

/// Discard every cached tile. `applyKnitting` does this for you; call it
/// directly only when the chart collection itself has been rebuilt.
void flushTileCache();

} // namespace hyprknit
