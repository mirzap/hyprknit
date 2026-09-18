#include "knit.hpp"

#include "chart.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <numbers>

namespace hyprknit {
namespace {

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------

constexpr std::uint32_t kOpaque = 0xff000000u;

constexpr int alphaOf(std::uint32_t argb) {
  return static_cast<int>(argb >> 24);
}
constexpr int redOf(std::uint32_t argb) {
  return static_cast<int>(argb >> 16) & 0xff;
}
constexpr int greenOf(std::uint32_t argb) {
  return static_cast<int>(argb >> 8) & 0xff;
}
constexpr int blueOf(std::uint32_t argb) {
  return static_cast<int>(argb) & 0xff;
}

struct Rgb {
  float r = 0.F;
  float g = 0.F;
  float b = 0.F;
};

Rgb unpack(std::uint32_t argb) {
  return {static_cast<float>(redOf(argb)), static_cast<float>(greenOf(argb)),
          static_cast<float>(blueOf(argb))};
}

std::uint32_t pack(Rgb color) {
  const auto channel = [](float value) {
    return static_cast<std::uint32_t>(std::clamp(value, 0.F, 255.F));
  };
  return kOpaque | (channel(color.r) << 16) | (channel(color.g) << 8) |
         channel(color.b);
}

Rgb operator*(Rgb color, float scale) {
  return {color.r * scale, color.g * scale, color.b * scale};
}

Rgb operator+(Rgb color, float lift) {
  return {color.r + lift, color.g + lift, color.b + lift};
}

/// Move `from` a fraction of the way towards `to`, which is what painting a
/// partly covering strand of yarn over the ground amounts to.
Rgb lerp(Rgb from, Rgb to, float amount) {
  return {from.r + (to.r - from.r) * amount, from.g + (to.g - from.g) * amount,
          from.b + (to.b - from.b) * amount};
}

float peak(Rgb color) { return std::max({color.r, color.g, color.b}); }

// ---------------------------------------------------------------------------
// A 2D buffer
// ---------------------------------------------------------------------------

// Wrapping lookup is what keeps a tile seamless: the ridge blur and the
// gradient the lighting is derived from both reach past the tile's edges.
template <typename T> struct Grid {
  int w = 0;
  int h = 0;
  std::vector<T> cells;

  Grid() = default;
  Grid(int width, int height, T fill = {})
      : w(width), h(height),
        cells(static_cast<std::size_t>(width) * height, fill) {}

  T &operator()(int x, int y) { return cells[index(x, y)]; }
  const T &operator()(int x, int y) const { return cells[index(x, y)]; }

  const T &wrapped(int x, int y) const {
    return cells[index((x % w + w) % w, (y % h + h) % h)];
  }

  bool empty() const { return cells.empty(); }

private:
  std::size_t index(int x, int y) const {
    return static_cast<std::size_t>(y) * w + x;
  }
};

// A seamless repeat of the fabric. Row 0 sits on the outer edge of the band and
// rows grow inward, which is also the direction chart rows are authored in.
using Tile = Grid<std::uint32_t>;

// ---------------------------------------------------------------------------
// Yarn colour
// ---------------------------------------------------------------------------

// Baskets of wool. An app without a curated colourway is assigned a colour by
// hashing its app id, so it keeps that colour for life and neighbours rarely
// collide.
constexpr std::uint32_t kWool[]{
    0xffd1495bu, 0xff4e8098u, 0xffedae49u, 0xff7c9885u, 0xff9b6a8fu,
    0xffe8846bu, 0xff3f7d6eu, 0xffc46a4eu, 0xff6d7ba8u, 0xffb8935fu,
};
constexpr std::uint32_t kSorbet[]{
    0xfff08ca4u, 0xff7fc6c4u, 0xffffc978u, 0xffb08ed4u,
    0xff8fce90u, 0xffff9f7au, 0xff8ab6f0u, 0xffe5a3d0u,
};
constexpr std::uint32_t kForest[]{
    0xff4a6b52u, 0xff7d8f5cu, 0xff3f6b6eu, 0xff8a7248u,
    0xff5c6b8au, 0xff6b5344u, 0xff2f5e4au,
};
constexpr std::uint32_t kMono[]{
    0xff9aa0a6u, 0xff7c8288u, 0xffb4bac0u, 0xff686e74u, 0xff8d939au,
};

// Full-chroma hues held at maximum separation around the wheel, so adjacent
// windows never read as the same colour.
constexpr std::uint32_t kDopamine[]{
    0xffff2d95u, 0xff00d9ffu, 0xffffd400u, 0xff7c3affu,
    0xff00e676u, 0xffff6b00u, 0xffff1744u, 0xff00b8d4u,
};
constexpr std::uint32_t kNeon[]{
    0xfff50057u, 0xff00e5ffu, 0xffc6ff00u,
    0xff651fffu, 0xff1de9b6u, 0xffff9100u,
};
constexpr std::uint32_t kPunch[]{
    0xffe8175du, 0xff0fb9b1u, 0xfffec230u,
    0xff5f27cdu, 0xff10ac84u, 0xffee5a24u,
};

constexpr Basket kBaskets[]{
    {"dopamine", kDopamine}, {"neon", kNeon},     {"punch", kPunch},
    {"wool", kWool},         {"sorbet", kSorbet}, {"forest", kForest},
    {"mono", kMono},
};

constexpr std::string_view kStitchNames[]{"stockinette", "rib", "garter"};

Knitting g_knitting{};

// murmur3 finalizer — spreads sequential hashes across the basket
std::uint32_t knitMix(std::uint32_t h) {
  h ^= h >> 16;
  h *= 0x85ebca6bu;
  h ^= h >> 13;
  h *= 0xc2b2ae35u;
  h ^= h >> 16;
  return h;
}

// deterministic wobble in [0,1), periodic over the tile so it still wraps
float knitNoise(int a, int b) {
  const float s = std::sin(static_cast<float>(a) * 12.9898F +
                           static_cast<float>(b) * 78.233F) *
                  43758.5453F;
  return s - std::floor(s);
}

// a quiet, fixed variation in the fibre, generated only on a cache miss
float grainAt(int x, int y) {
  const std::uint32_t grain =
      knitMix(static_cast<std::uint32_t>(x) * 73856093u ^
              static_cast<std::uint32_t>(y) * 19349663u);
  return static_cast<float>(grain & 255u) / 255.F;
}

// ---------------------------------------------------------------------------
// A small path rasterizer
//
// The reference renders its tiles through CoreGraphics. Everything it needs is
// covered by stroking flattened polylines with round caps and joins, which a
// signed-distance sweep over each segment's bounding box does directly, and
// with the antialiasing the yarn silhouette depends on.
// ---------------------------------------------------------------------------

struct Point {
  float x = 0.F;
  float y = 0.F;
};

using Polyline = std::vector<Point>;
using Strands = std::vector<Polyline>;

void flattenQuad(Polyline &line, Point control, Point end) {
  const Point start = line.back();
  const float span = std::abs(control.x - start.x) +
                     std::abs(control.y - start.y) +
                     std::abs(end.x - control.x) + std::abs(end.y - control.y);
  const int steps =
      std::clamp(static_cast<int>(std::ceil(span * 0.75F)), 3, 24);
  for (int i = 1; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    const float inv = 1.F - t;
    line.push_back(
        {inv * inv * start.x + 2 * inv * t * control.x + t * t * end.x,
         inv * inv * start.y + 2 * inv * t * control.y + t * t * end.y});
  }
}

/// Coverage of `strands` stroked at `width` pixels. Round caps and joins fall
/// out of taking the maximum of the per-segment distance fields.
void strokeCoverage(Grid<float> &out, const Strands &strands, float width) {
  std::ranges::fill(out.cells, 0.F);
  const float radius = std::max(width * 0.5F, 0.35F);
  const float reach = radius + 1.F;

  for (const auto &line : strands) {
    for (std::size_t i = 1; i < line.size(); ++i) {
      const Point a = line[i - 1];
      const Point b = line[i];
      const int minX =
          std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - reach)));
      const int maxX = std::min(
          out.w - 1, static_cast<int>(std::ceil(std::max(a.x, b.x) + reach)));
      const int minY =
          std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - reach)));
      const int maxY = std::min(
          out.h - 1, static_cast<int>(std::ceil(std::max(a.y, b.y) + reach)));
      if (minX > maxX || minY > maxY)
        continue;

      const float dx = b.x - a.x;
      const float dy = b.y - a.y;
      const float lengthSquared = dx * dx + dy * dy;
      for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
          const float px = static_cast<float>(x) + 0.5F - a.x;
          const float py = static_cast<float>(y) + 0.5F - a.y;
          const float t =
              lengthSquared > 0.F
                  ? std::clamp((px * dx + py * dy) / lengthSquared, 0.F, 1.F)
                  : 0.F;
          const float ox = px - t * dx;
          const float oy = py - t * dy;
          const float distance = std::sqrt(ox * ox + oy * oy);
          float &slot = out(x, y);
          slot = std::max(slot, std::clamp(radius + 0.5F - distance, 0.F, 1.F));
        }
      }
    }
  }
}

// separable 3-tap blur, wrapping — smooths the stepped ridge into a round one
void blurWrapped(Grid<float> &grid) {
  Grid<float> pass(grid.w, grid.h);
  for (int y = 0; y < grid.h; ++y)
    for (int x = 0; x < grid.w; ++x)
      pass(x, y) =
          (grid.wrapped(x - 1, y) + 2.F * grid(x, y) + grid.wrapped(x + 1, y)) *
          0.25F;
  for (int y = 0; y < grid.h; ++y)
    for (int x = 0; x < grid.w; ++x)
      grid(x, y) =
          (pass.wrapped(x, y - 1) + 2.F * pass(x, y) + pass.wrapped(x, y + 1)) *
          0.25F;
}

// ---------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------

// Internal cache key for single-colour knitted corner and cuff patches.
constexpr int kPatchChart = -2;

// The ridge profile: the same path stroked from wide and low to narrow and
// high, a stepped approximation of a round strand of wool.
constexpr std::array kRidgeWidth{1.00F, 0.80F, 0.60F, 0.40F, 0.20F};
constexpr std::array kRidgeHeight{0.22F, 0.48F, 0.70F, 0.88F, 1.00F};

/// Box-filter the supersampled buffer down to the tile's drawn size, which is
/// what the reference gets from drawing its oversized tile image into the band.
Tile downsample(const Grid<std::uint32_t> &src, int scale) {
  Tile tile(src.w / scale, src.h / scale);
  const auto samples = static_cast<float>(scale * scale);
  for (int y = 0; y < tile.h; ++y)
    for (int x = 0; x < tile.w; ++x) {
      Rgb sum;
      for (int sy = 0; sy < scale; ++sy)
        for (int sx = 0; sx < scale; ++sx) {
          const Rgb sample = unpack(src(x * scale + sx, y * scale + sy));
          sum.r += sample.r;
          sum.g += sample.g;
          sum.b += sample.b;
        }
      tile(x, y) = pack(sum * (1.F / samples));
    }
  return tile;
}

/// A compact fabric shader for colourwork. It shades fine yarn directly into
/// the cached tile, avoiding thousands of paths at first paint. Chart
/// dimensions stay fixed while each profile chooses its stitch density.
Tile makeFabric(std::uint32_t color, const Chart &chart, float sw,
                float rowStep, int tw, int th, int scale) {
  Grid<std::uint32_t> pixels(tw * scale, th * scale);

  // Pattern dimensions stay fixed. Quiet stripes and checks need more readable
  // yarn; intricate motifs keep their finer texture rather than changing.
  const float stitchScale = chart.definedYarn ? 2.F : 1.F;
  const float relief =
      std::min(chart.definedYarn ? 0.30F : 0.25F,
               std::max(0.F, knitting().gauge.relief *
                                 (chart.definedYarn ? 0.32F : 0.20F)));
  const auto cell = [&chart](int column, int row) {
    return chart.px[static_cast<std::size_t>(row) * chart.w +
                    ((column % chart.w) + chart.w) % chart.w];
  };

  for (int y = 0; y < pixels.h; ++y) {
    const float sy =
        (static_cast<float>(y) + 0.5F) / (static_cast<float>(scale) * rowStep);
    const float stitchY = sy / stitchScale;
    const int row = static_cast<int>(std::floor(stitchY));
    const float v = stitchY - static_cast<float>(row);
    for (int x = 0; x < pixels.w; ++x) {
      const float sx =
          (static_cast<float>(x) + 0.5F) / (static_cast<float>(scale) * sw);
      const int column = static_cast<int>(std::floor(sx));
      const float stitchX = sx / stitchScale;
      const int yarnColumn = static_cast<int>(std::floor(stitchX));
      const float u = stitchX - static_cast<float>(yarnColumn);

      // Let the row boundary follow the small stitch head, avoiding a perfectly
      // square pixel-art edge between contrasting yarn colours. Dot masks
      // supply their own curved boundary; warping their row lookup separately
      // would split the dot at its middle stitch seam.
      const float colorWarp =
          chart.roundDots ? 0.F : 0.16F * (1.F - 2.F * std::abs(2.F * u - 1.F));
      int colorRow = static_cast<int>(std::floor(sy + colorWarp));
      colorRow = (colorRow % (chart.h * 2) + chart.h * 2) % (chart.h * 2);
      std::uint32_t yarn = cell(column / 2, colorRow / 2);
      if (chart.roundDots && alphaOf(yarn) >= 128) {
        // Round the 2x2 dot block off into a filled circle, keeping the corner
        // that continues into a neighbouring cell of the same yarn square.
        const int cx = ((column / 2) % chart.w + chart.w) % chart.w;
        const int cy = colorRow / 2;
        const bool left = cell(cx - 1, cy) == yarn;
        const bool above = cell(cx, (cy + chart.h - 1) % chart.h) == yarn;
        const float dx =
            sx * 0.5F - std::floor(sx * 0.5F) + (left ? 1.F : 0.F) - 1.F;
        const float dy =
            sy * 0.5F - std::floor(sy * 0.5F) + (above ? 1.F : 0.F) - 1.F;
        if (dx * dx + dy * dy > 0.94F)
          yarn = color;
      }
      if (alphaOf(yarn) < 128)
        yarn = color;

      const std::uint32_t seed =
          knitMix(static_cast<std::uint32_t>(yarnColumn) * 73856093u ^
                  static_cast<std::uint32_t>(row) * 19349663u);
      const float wobble = (static_cast<float>(seed & 255u) / 255.F - 0.5F) *
                           knitting().gauge.jitter;
      // Rounded, nested stitch legs, with no hard highlight or black crevice.
      const float leg = 0.42F * (1.F - v) + 0.08F * v * (1.F - v);
      const float distance = std::abs(std::abs(u - 0.5F - wobble) - leg);
      const float radius = std::max(0.10F, knitting().gauge.yarn * 0.5F);
      const float ridge =
          std::max(0.F, 1.F - distance * distance / (radius * radius));
      const float fibre = (grainAt(x, y) - 0.5F) * 0.014F;
      const float shade = std::max(
          0.F, knitting().gauge.ground - relief * (1.F - ridge) +
                   (knitting().gauge.ambient - 0.94F) +
                   knitting().gauge.sheen * 0.08F * (0.5F - u) + fibre);

      // Colourwork charts are read from the inner edge outward, the one place
      // the reference takes a chart in the opposite direction to the sculpted
      // yarn. Only the dot masks come through here.
      pixels(x, pixels.h - 1 - y) = pack(unpack(yarn) * shade);
    }
  }
  return downsample(pixels, scale);
}

/// Lay `ncols` x `nrows` stitches, plus a two-cell margin so the pattern wraps.
/// Each row is one continuous strand, which is both faster to stroke and how
/// knitting actually works.
void knitLay(Strands &strands, int ncols, int nrows, float sw, float sh,
             float rowStep) {
  const float bx = sw * 0.5F * knitting().gauge.bow;
  const float jit = sw * knitting().gauge.jitter;

  switch (knitting().stitch) {
  case Stitch::Rib: {
    // k1p1: vertical columns of wool, every other one sitting proud
    const float bottom = static_cast<float>(nrows + 4) * rowStep;
    for (int i = -2; i <= ncols + 1; ++i) {
      const float x = static_cast<float>(i) * sw + sw * 0.5F;
      const float purl = (i & 1) ? sw * 0.16F : 0.F; // purl columns pull inward
      strands.push_back({{x + purl, -2 * rowStep}, {x + purl, bottom}});
    }
    return;
  }
  case Stitch::Garter:
    // horizontal ridges: every row a gentle wave, alternating phase
    for (int j = -2; j <= nrows + 1; ++j) {
      const float y = static_cast<float>(j) * rowStep;
      const float phase = (j & 1) ? sw * 0.5F : 0.F;
      Polyline line{{-2 * sw, y}};
      for (int i = -2; i <= ncols + 1; ++i) {
        const float x = static_cast<float>(i) * sw + phase;
        flattenQuad(line, {x + sw * 0.25F, y - sh * 0.30F}, {x + sw * 0.5F, y});
        flattenQuad(line, {x + sw * 0.75F, y + sh * 0.30F}, {x + sw, y});
      }
      strands.push_back(std::move(line));
    }
    return;
  case Stitch::Stockinette:
    break;
  }

  for (int j = -2; j <= nrows + 1; ++j) {
    const float y = static_cast<float>(j) * rowStep;
    const float jy = (knitNoise((j + nrows) % nrows, 3) - 0.5F) * jit * 2.F;
    Polyline line{{-2 * sw, y + jy}};
    for (int i = -2; i <= ncols + 1; ++i) {
      const float x = static_cast<float>(i) * sw;
      const float jx =
          (knitNoise((i + ncols) % ncols, (j + nrows) % nrows) - 0.5F) * jit *
          2.F;
      flattenQuad(line, {x + bx + jx, y + sh * 0.55F + jy},
                  {x + sw * 0.5F + jx, y + sh + jy});
      flattenQuad(line, {x + sw - bx + jx, y + sh * 0.55F + jy},
                  {x + sw + jx, y + jy});
    }
    strands.push_back(std::move(line));
  }
}

/// One stitch, as a V whose point lies a row further into the band.
Polyline stitchAt(float x, float y, float sw, float sh, float bow) {
  Polyline line{{x, y}};
  flattenQuad(line, {x + bow, y + sh * 0.55F}, {x + sw * 0.5F, y + sh});
  flattenQuad(line, {x + sw - bow, y + sh * 0.55F}, {x + sw, y});
  return line;
}

/// The sculpted stitches, grouped by yarn colour so each group can be painted
/// over the last, the way stranded colourwork actually sits.
void layColourwork(std::vector<std::uint32_t> &yarns,
                   std::vector<Strands> &groups, const Chart *chart,
                   std::uint32_t color, int ncols, int nrows, float sw,
                   float sh, float rowStep, float bow) {
  constexpr std::size_t kMaxYarns = 16;
  for (int j = -2; j <= nrows + 1; ++j)
    for (int i = -2; i <= ncols + 1; ++i) {
      std::uint32_t cell = 0;
      if (chart)
        cell = chart->px[static_cast<std::size_t>((j % chart->h + chart->h) %
                                                  chart->h) *
                             chart->w +
                         (i % chart->w + chart->w) % chart->w];
      const std::uint32_t yarn = alphaOf(cell) < 128 ? color : (cell | kOpaque);

      auto slot = std::ranges::find(yarns, yarn);
      if (slot == yarns.end()) {
        if (yarns.size() == kMaxYarns)
          continue;
        yarns.push_back(yarn);
        groups.emplace_back();
        slot = yarns.end() - 1;
      }
      groups[static_cast<std::size_t>(slot - yarns.begin())].push_back(
          stitchAt(static_cast<float>(i) * sw, static_cast<float>(j) * rowStep,
                   sw, sh, bow));
    }
}

/// Render one seamless tile.
///
/// Rather than faking depth with an offset dark copy, this builds a height
/// field for the yarn, derives surface normals from it, and lights it. Because
/// the tile is built once and cached it costs nothing per frame.
Tile makeTile(float band, std::uint32_t color, int chart) {
  const Chart *ch = chart == kPatchChart ? nullptr : chartAt(chart);
  const bool sculpted = chart == kPatchChart || (ch && ch->sculptedYarn);

  Gauge material = knitting().gauge;
  if (sculpted) {
    // Raised yarn material. Geometry stays independent of the colour chart.
    material.rowOverlap = 0.05F;
    material.yarn = 0.40F;
    material.ground = 0.94F;
    material.ambient = 0.84F;
    material.relief = 1.65F;
    material.sheen = 0.28F;
  }

  float rowStep = band / std::max(material.rows, 1.5F);
  float sw = rowStep * material.aspect;

  // Enough variation to feel hand-knit; even counts preserve rib and garter.
  int ncols = 8;
  int nrows = 8;
  if (ch) {
    // One chart is already a complete repeat. Each authored colour cell spans
    // 2x2 sampling units, so the motif keeps its size while fabric detail no
    // longer dictates the dot size.
    const int density = sculpted ? 1 : 2;
    sw /= static_cast<float>(density);
    rowStep /= static_cast<float>(density);
    ncols = ch->w * density;
    nrows = ch->h * density;
    // Very small user charts still need enough pixels for the ridge filter.
    ncols *= static_cast<int>(
        std::max(1.F, std::ceil(8.F / (sw * static_cast<float>(ncols)))));
    nrows *= static_cast<int>(
        std::max(1.F, std::ceil(8.F / (rowStep * static_cast<float>(nrows)))));
  }

  const int tw = std::max(
      8, static_cast<int>(std::lround(sw * static_cast<float>(ncols))));
  const int th = std::max(
      8, static_cast<int>(std::lround(rowStep * static_cast<float>(nrows))));
  sw = static_cast<float>(tw) / static_cast<float>(ncols);
  rowStep = static_cast<float>(th) / static_cast<float>(nrows);
  const float sh = rowStep / (1.F - material.rowOverlap);

  float yarn = std::max(sw * material.yarn, sculpted ? 0.8F : 0.35F);
  // Colourwork always uses stockinette, regardless of the last plain choice.
  if (!ch && !sculpted && knitting().stitch == Stitch::Rib)
    yarn = sw * 0.55F;

  const int scale = rowStep < 4.F ? 4 : 2;
  if (ch && !sculpted)
    return makeFabric(color, *ch, sw, rowStep, tw, th, scale);

  // Paths are laid straight into the supersampled grid, so curve flattening and
  // stroke widths are both measured in the pixels they are rasterized to.
  const auto step = static_cast<float>(scale);
  std::vector<std::uint32_t> yarns;
  std::vector<Strands> groups;
  if (sculpted)
    layColourwork(yarns, groups, ch, color, ncols, nrows, sw * step, sh * step,
                  rowStep * step, sw * step * 0.5F * material.bow);
  else {
    yarns.push_back(color);
    knitLay(groups.emplace_back(), ncols, nrows, sw * step, sh * step,
            rowStep * step);
  }

  const int W = tw * scale;
  const int H = th * scale;
  Grid<float> coverage(W, H);
  Grid<float> height(W, H);
  Grid<Rgb> flat(W, H, unpack(color) * material.ground);

  for (std::size_t k = 0; k < groups.size(); ++k) {
    strokeCoverage(coverage, groups[k], yarn * step);
    const Rgb strand = unpack(yarns[k]);
    for (std::size_t i = 0; i < flat.cells.size(); ++i)
      if (const float c = coverage.cells[i]; c > 0.F)
        flat.cells[i] = lerp(flat.cells[i], strand, c);

    for (std::size_t layer = 0; layer < kRidgeWidth.size(); ++layer) {
      strokeCoverage(coverage, groups[k], yarn * step * kRidgeWidth[layer]);
      for (std::size_t i = 0; i < height.cells.size(); ++i)
        if (const float c = coverage.cells[i]; c > 0.F)
          height.cells[i] += (kRidgeHeight[layer] - height.cells[i]) * c;
    }
  }

  blurWrapped(height);
  blurWrapped(height);

  // Light it from the upper left, the convention that reads as raised rather
  // than sunken. The gradient is per-pixel, so the relief scales with the
  // supersampling.
  constexpr float lx = -0.45F;
  constexpr float ly = -0.55F;
  constexpr float lz = 0.70F;
  const float relief = step * material.relief;

  Grid<std::uint32_t> lit(W, H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const float nx =
          -(height.wrapped(x + 1, y) - height.wrapped(x - 1, y)) * relief;
      const float ny =
          -(height.wrapped(x, y + 1) - height.wrapped(x, y - 1)) * relief;
      const float inv = 1.F / std::sqrt(nx * nx + ny * ny + 1.F);
      const float ndl = std::max(0.F, (nx * lx + ny * ly + lz) * inv);

      // ambient, diffuse, and a little occlusion in the gaps between strands
      const float ridge = height(x, y);
      const float occ =
          (sculpted ? 0.74F : 0.86F) + (sculpted ? 0.26F : 0.14F) * ridge;
      const float fibre = sculpted ? 1.F : 0.993F + grainAt(x, y) * 0.014F;
      const float shade =
          (material.ambient + material.sheen * ndl) * occ * fibre;

      // Very dark wool still needs a visible strand highlight. Multiplication
      // alone loses almost all relief when a yarn is close to black.
      const float lift =
          sculpted ? std::max(0.F, 1.F - peak(flat(x, y)) / 48.F) * ridge * 10.F
                   : 0.F;
      lit(x, y) = pack(flat(x, y) * shade + lift);
    }
  return downsample(lit, scale);
}

// ---------------------------------------------------------------------------
// Tile cache
// ---------------------------------------------------------------------------

struct TileKey {
  float band = 0.F;
  std::uint32_t color = 0;
  int chart = 0;

  friend bool operator==(const TileKey &, const TileKey &) = default;
};

constexpr std::size_t kTileCacheSize = 64;
std::vector<std::pair<TileKey, std::shared_ptr<const Tile>>> g_cache;
std::mutex g_cacheLock;

/// Shared ownership, so a caller holding a tile is unaffected by an eviction
/// or a flush on another thread.
std::shared_ptr<const Tile> getTile(float band, std::uint32_t color,
                                    int chart) {
  const TileKey key{std::round(band * 2.F) / 2.F, color, chart};
  const std::scoped_lock lock(g_cacheLock);
  for (const auto &[cached, tile] : g_cache)
    if (cached == key)
      return tile;

  auto tile = std::make_shared<const Tile>(makeTile(key.band, color, chart));
  if (g_cache.size() == kTileCacheSize) // full: drop the oldest
    g_cache.erase(g_cache.begin());
  g_cache.emplace_back(key, tile);
  return tile;
}

// ---------------------------------------------------------------------------
// The band
// ---------------------------------------------------------------------------

/// Signed distance to a rounded rectangle centred on the origin: negative
/// inside, and the ring between two of them is the band.
double roundedRectDistance(double px, double py, double halfWidth,
                           double halfHeight, double radius) {
  radius = std::clamp(radius, 0.0, std::min(halfWidth, halfHeight));
  const double qx = std::abs(px) - halfWidth + radius;
  const double qy = std::abs(py) - halfHeight + radius;
  return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) +
         std::min(std::max(qx, qy), 0.0) - radius;
}

double wrap(double value, double period) {
  const double folded = std::fmod(value, period);
  return folded < 0.0 ? folded + period : folded;
}

/// `along` is measured in the drawn repeat's own width, which the fitted and
/// solid-corner profiles stretch away from the tile's natural size.
int tileColumn(const Tile &tile, double along, double repeatWidth) {
  const double u = wrap(along, repeatWidth) / repeatWidth * tile.w;
  return std::clamp(static_cast<int>(u), 0, tile.w - 1);
}

int tileRow(const Tile &tile, double across) {
  return std::clamp(static_cast<int>(wrap(across, tile.h)), 0, tile.h - 1);
}

/// Everything about a side's repeat depends only on that side's length. There
/// are two of these per window: one for the pair that runs the frame's width,
/// one for the pair that runs its height.
struct SideRepeat {
  double length = 0.0;
  double cap = 0.0;       // solid corner patches, measured in from each end
  double capRepeat = 0.0; // repeat width between the two caps
  double repeat = 0.0;    // repeat width for an unpatched side
  double anchor = 0.0;    // where the repeat is cast on
};

/// Which stitch a pixel lands on depends only on how far along the edge and how
/// far into the band it sits, so both are worked out once per wale (a column of
/// stitches) and per course (a row) rather than once per pixel.
struct Wale {
  int column = 0;
  bool patched = false; // knitted from the solid corner patch
};

} // namespace

std::span<const Basket> baskets() { return kBaskets; }

const Knitting &knitting() { return g_knitting; }

bool applyKnitting(const Knitting &next) {
  if (g_knitting == next)
    return false;
  g_knitting = next;
  flushTileCache();
  return true;
}

int basketIndex(std::string_view name) {
  const auto *const found = std::ranges::find(kBaskets, name, &Basket::name);
  return found == std::end(kBaskets)
             ? -1
             : static_cast<int>(found - std::begin(kBaskets));
}

int stitchIndex(std::string_view name) {
  const auto *const found = std::ranges::find(kStitchNames, name);
  return found == std::end(kStitchNames)
             ? -1
             : static_cast<int>(found - std::begin(kStitchNames));
}

std::string_view stitchName(Stitch stitch) {
  return kStitchNames[static_cast<std::size_t>(stitch)];
}

std::uint32_t colorForApp(std::string_view app) {
  // ASCII case folding matches the collection's case-insensitive matching.
  std::uint32_t hash = 2166136261u;
  for (const unsigned char raw : app) {
    const unsigned char c = raw >= 'A' && raw <= 'Z'
                                ? static_cast<unsigned char>(raw + ('a' - 'A'))
                                : raw;
    hash = (hash ^ c) * 16777619u;
  }
  const auto colors = kBaskets[g_knitting.basket].colors;
  return colors[knitMix(hash) % colors.size()];
}

void flushTileCache() {
  const std::scoped_lock lock(g_cacheLock);
  g_cache.clear();
}

void renderBandRegion(const Band &band, int originX, int originY, Pixels &out) {
  out.rgba.clear();
  if (out.width <= 0 || out.height <= 0 || band.band <= 0 ||
      band.frameWidth <= 2 * band.band || band.frameHeight <= 2 * band.band)
    return;
  const auto outputWidth = static_cast<std::size_t>(out.width);
  const auto outputHeight = static_cast<std::size_t>(out.height);
  if (outputWidth > out.rgba.max_size() / 4 / outputHeight)
    return;
  out.rgba.assign(outputWidth * outputHeight * 4, 0u);

  const Chart *chart = chartAt(band.chart);
  const bool solidCorners = chart && chart->solidCorners;
  const std::uint32_t cornerColor =
      solidCorners && chart->cornerColor ? chart->cornerColor : band.color;

  const auto width = static_cast<double>(band.band);
  const auto tile = getTile(static_cast<float>(width), band.color, band.chart);
  if (tile->empty())
    return;
  const auto patch = solidCorners ? getTile(static_cast<float>(width),
                                            cornerColor, kPatchChart)
                                  : nullptr;
  const auto cuff =
      chart && chart->cuffColor
          ? getTile(static_cast<float>(width), chart->cuffColor, kPatchChart)
          : nullptr;
  const double cuffWidth = width / 3.0;

  // The app collection starts with a cuff at the outside edge. Its phase
  // depends only on the yarn gauge, never on the changing window size.
  const bool atelier = chart && chart->name.starts_with("atelier-");
  const double acrossOffset = atelier ? 0.0 : width * 0.5 - tile->h * 0.5;

  const double frameW = band.frameWidth;
  const double frameH = band.frameHeight;
  const double halfW = frameW * 0.5;
  const double halfH = frameH * 0.5;
  const double outerRadius =
      std::clamp(band.radius, 0.0, std::min(halfW, halfH));
  const double innerRadius = std::max(0.0, outerRadius - width);
  const double keep = 1.0 - std::clamp<double>(band.dim, 0.0, 1.0);

  // How deep each side's mitre reaches. At 45 degrees the inner rounded arc
  // reaches further in than the straight edge does, so the mitre has to cover
  // that arc; without the bound the opposite side would claim the same pixels
  // and read its pattern upside down.
  const double depth =
      std::min({width + 1.0 + innerRadius * (1.0 - std::numbers::sqrt2 / 2.0),
                halfW, halfH});

  std::array<SideRepeat, 2> sides{};
  std::array<std::vector<Wale>, 2> wales;
  std::array<std::vector<int>, 2> cuffWales;
  for (int i = 0; i < 2; ++i) {
    SideRepeat &side = sides[i];
    side.length = i == 0 ? frameW : frameH;
    side.repeat = tile->w;
    if (patch) {
      side.cap = std::min(outerRadius, side.length * 0.5);
      const double available = std::max(0.0, side.length - 2.0 * side.cap);
      side.capRepeat =
          available / std::max(1.0, std::round(available / tile->w));
    } else if (chart && chart->fittedRepeat) {
      // Symmetric stripe and check charts meet at both mitres when each edge
      // contains whole repeats.
      side.repeat =
          side.length / std::max(1.0, std::round(side.length / tile->w));
    } else if (knitting().anchor == Anchor::Centre) {
      side.anchor = side.length * 0.5 - tile->w * 0.5;
    }

    // Pixel centres are on the half, so the integer part of each coordinate
    // indexes these tables directly.
    const auto count = static_cast<std::size_t>(std::ceil(side.length)) + 2;
    wales[i].resize(count);
    if (cuff)
      cuffWales[i].resize(count);
    for (std::size_t j = 0; j < count; ++j) {
      const double u = static_cast<double>(j) + 0.5;
      // The curved arcs stay in the base yarn when a chart asks for a solid
      // corner patch; the stripe starts after the tangent, with whole repeats
      // fitted between the two ends rather than a remainder left at a corner.
      if (patch && (u < side.cap || u >= side.length - side.cap))
        wales[i][j] = {tileColumn(*patch, u, patch->w), true};
      else if (patch)
        wales[i][j] = {tileColumn(*tile, u - side.cap, side.capRepeat), false};
      else
        wales[i][j] = {tileColumn(*tile, u - side.anchor, side.repeat), false};
      if (cuff)
        cuffWales[i][j] = tileColumn(*cuff, u, cuff->w);
    }
  }

  const auto courses = static_cast<std::size_t>(std::ceil(depth)) + 2;
  std::vector<int> tileCourse(courses);
  std::vector<int> patchCourse(patch ? courses : 0);
  std::vector<int> cuffCourse(cuff ? courses : 0);
  for (std::size_t k = 0; k < courses; ++k) {
    const double v = static_cast<double>(k) + 0.5;
    tileCourse[k] = tileRow(*tile, v - acrossOffset);
    if (patch)
      patchCourse[k] = tileRow(*patch, v - acrossOffset);
    if (cuff)
      cuffCourse[k] = tileRow(*cuff, v);
  }

  // Away from the arcs the ring is a plain rectangle, so the rounded-rect
  // distance is only worth computing near a corner.
  const double straight = outerRadius + 1.0;

  for (int y = 0; y < out.height; ++y) {
    const double fy = originY + y + 0.5;
    for (int x = 0; x < out.width; ++x) {
      const double fx = originX + x + 0.5;

      // Four separate pieces of knitting, mitred at the corners, the way a
      // frame would actually be made. Each side has its own local space where u
      // runs along that edge and v runs inward, so the stitches follow the
      // edge. A side owns the pixels its 45-degree mitre reaches, and where two
      // sides meet the later one covers the seam.
      const double along[4]{fx, fy, frameW - fx, frameH - fy};
      const double into[4]{fy, frameW - fx, frameH - fy, fx};

      int owner = -1;
      for (int i = 0; i < 4; ++i)
        if (const double v = into[i]; v >= 0.0 && v <= depth && v <= along[i] &&
                                      v <= sides[i & 1].length - along[i])
          owner = i;
      if (owner < 0)
        continue;

      const double u = along[owner];
      const double v = into[owner];

      double alpha = 1.0;
      double fromOuter = v; // depth below the outer boundary, for the cuff
      if (u >= straight && u <= sides[owner & 1].length - straight) {
        // The straight run: the ring clip is just the two parallel edges.
        alpha = std::min(std::clamp(v + 0.5, 0.0, 1.0),
                         std::clamp(width - v + 0.5, 0.0, 1.0));
      } else {
        // The ring clip is what gives correct rounded corners for free.
        const double cx = fx - halfW;
        const double cy = fy - halfH;
        const double outer =
            roundedRectDistance(cx, cy, halfW, halfH, outerRadius);
        const double inner = roundedRectDistance(cx, cy, halfW - width,
                                                 halfH - width, innerRadius);
        alpha = std::clamp(0.5 - outer, 0.0, 1.0) *
                std::clamp(0.5 + inner, 0.0, 1.0);
        fromOuter = -outer;
      }
      if (alpha <= 0.0)
        continue;

      const auto column = static_cast<std::size_t>(u);
      const auto course = static_cast<std::size_t>(v);
      const Wale &wale = wales[owner & 1][column];
      std::uint32_t argb = wale.patched
                               ? (*patch)(wale.column, patchCourse[course])
                               : (*tile)(wale.column, tileCourse[course]);

      // The cuff follows the outer arc rather than the straight edge, so it
      // keeps its width all the way through the corner.
      if (cuff && fromOuter < cuffWidth)
        argb = (*cuff)(cuffWales[owner & 1][column], cuffCourse[course]);

      // Unfocused windows are darkened, not faded, so the desktop never shows
      // through the band.
      const double scale = keep * alpha;
      // Both factors are non-negative, so adding a half and truncating rounds
      // correctly. std::lround is the general spelling but costs a libm call
      // per channel, and this is the hottest loop in the plugin.
      // NOLINTNEXTLINE(bugprone-incorrect-roundings)
      const auto channel = [](double value) {
        return static_cast<std::uint8_t>(value + 0.5);
      };
      const auto index = (static_cast<std::size_t>(y) * out.width + x) * 4;
      out.rgba[index] = channel(redOf(argb) * scale);
      out.rgba[index + 1] = channel(greenOf(argb) * scale);
      out.rgba[index + 2] = channel(blueOf(argb) * scale);
      out.rgba[index + 3] = channel(255.0 * alpha);
    }
  }
}

} // namespace hyprknit
