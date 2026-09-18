// Renders Hyprknit's picture book: the collection plates, the By App and Zigzag
// comparison, and a hero image. Every border is drawn by the plugin's own
// renderer; cairo only lays out the paper, the windows and the text.
//
//   hyprknit-catalogue [output-dir]
//
// The layout follows Window Sweaters' own plates, with Omarchy's windows:
// square corners and no title bar, so each window is simply an empty pane in
// its sweater.

#include "apps.hpp"
#include "chart.hpp"
#include "knit.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cmath>

#include <cairo.h>
#include <pango/pangocairo.h>

namespace {

// Everything is laid out in points and drawn at 2x, like a HiDPI screen.
constexpr double kScale = 2.0;

struct Entry {
  std::string_view name;    // as the collection names it
  std::string_view appId;   // what the colourway is matched on
  std::string_view palette; // the yarns, in words
  std::string_view pattern; // the design, in words
};

// Window Sweaters' collection, in its catalogue order.
constexpr Entry kCollection[]{
    {"Finder", "Finder", "Finder blue, ice blue, navy",
     "Blue checks with aligned knitted corners"},
    {"Microsoft Teams", "Microsoft Teams", "Violet, porcelain",
     "Soft porcelain zigzags on violet knit"},
    {"Claude", "Claude", "Apricot, vanilla",
     "Cream stars on apricot wool, no inner trim"},
    {"Codex", "Codex", "Blue, cream",
     "Cream chevrons on blue knit, patterned corners"},
    {"Spotify", "Spotify", "Leaf green, butter yellow",
     "Raised knitted checks, patterned corners"},
    {"Notion", "Notion", "Paper white, charcoal",
     "Raised ivory and charcoal knitted checks"},
    {"WhatsApp", "WhatsApp", "Meadow, pistachio, pink",
     "Broad stripes with pink accents, green corners"},
    {"Figma", "Figma", "Mauve and pastel accents", "Playful colour blocks"},
    {"Google Chrome", "Google Chrome", "Ivory, red, green, yellow, blue",
     "Ivory knit with colourful checks"},
    {"Paper", "Paper", "Paper blue, white, denim", "Layered sheet motifs"},
    {"ChatGPT", "ChatGPT", "Blue, cream",
     "Cream chevrons on blue knit, patterned corners"},
    {"Grok Bot", "Grok Bot", "Charcoal, oatmeal, silver",
     "Framed stripes, matching knitted corners"},
    {"Discord", "Discord", "Blurple, warm white", "Mini checkerboard"},
    {"Granola", "Granola", "Deep grey, leaf green",
     "Grey and green stripes, patterned corners"},
    {"Ghostty", "com.mitchellh.ghostty", "Midnight, soft ivory",
     "Quiet ivory stripes"},
    {"Safari", "Safari", "Sky blue, porcelain, coral", "Compass diamonds"},
    {"Firefox", "org.mozilla.firefox", "Violet, tangerine, golden peach",
     "Flame chevrons"},
    {"Slack", "Slack", "Aubergine, blue, mint, gold, pink", "Confetti crosses"},
    {"Zoom", "Zoom", "Cobalt, ice blue, white", "Linked little windows"},
    {"Telegram", "org.telegram.desktop", "Sky blue, white, glacier",
     "Folded diagonal ribbons"},
    {"Messages", "Messages", "Apple green, white, pale lime",
     "Scattered polka dots"},
    {"Mail", "Mail", "Azure, white, powder blue", "Envelope scallops"},
    {"Notes", "Notes", "Vanilla, golden yellow",
     "Ivory knit with a continuous curved golden cuff"},
    {"Calendar", "Calendar", "Paper white, tomato",
     "White knit with a continuous curved red cuff"},
    {"Reminders", "Reminders", "Paper, blue, coral, orange, grey",
     "Tiny task stitches"},
    {"Apple Music", "Apple Music", "Watermelon, ballet pink, cream",
     "Syncopated rose zigzags"},
    {"Photos", "Photos", "Cream and six petal colours", "Rainbow posies"},
    {"Preview", "Preview", "Denim, glacier, snow", "Alpine photo frames"},
    {"Microsoft Word", "Microsoft Word", "Book blue, cornflower, paper",
     "Classic two-tone chevrons"},
    {"Microsoft Excel", "Microsoft Excel", "Forest, sage, pale mint",
     "Workbook windowpane"},
    {"Microsoft PowerPoint", "Microsoft PowerPoint",
     "Terracotta, salmon, peach", "Nested presentation tiles"},
    {"Microsoft Outlook", "Microsoft Outlook", "Outlook blue, sky, white",
     "Overlapping envelope checks"},
    {"VS Code", "code", "Code blue, deep ink, ice", "Angular linked ribbons"},
    {"Adobe Photoshop", "Adobe Photoshop", "Midnight, cyan, soft blue",
     "Pixel-frame checks"},
    {"Adobe Illustrator", "Adobe Illustrator", "Espresso, orange, apricot",
     "Warm rugby stripes"},
    {"Terminal", "Terminal", "Charcoal green, soft sage",
     "Sage stripes with matching knitted corners"},
    {"Cursor", "Cursor", "Warm charcoal, stone",
     "Soft stone zigzags on charcoal knit"},
};

struct Rgb {
  double r, g, b;
};

constexpr Rgb hex(std::uint32_t rgb) {
  return {((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0,
          (rgb & 0xff) / 255.0};
}

constexpr Rgb kPaper = hex(0xf9f7f3);
constexpr Rgb kInk = hex(0x3b3a36);
constexpr Rgb kQuiet = hex(0x6e6c66);
constexpr Rgb kPane = hex(0xffffff);
constexpr Rgb kCream = hex(0xfeedc2);

// A page: a cairo surface drawn in points at kScale.
class Page {
public:
  Page(double width, double height)
      : m_surface(cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, static_cast<int>(width * kScale),
            static_cast<int>(height * kScale))),
        m_cr(cairo_create(m_surface)) {
    cairo_scale(m_cr, kScale, kScale);
  }
  ~Page() {
    cairo_destroy(m_cr);
    cairo_surface_destroy(m_surface);
  }
  Page(const Page &) = delete;
  Page &operator=(const Page &) = delete;

  cairo_t *cr() const { return m_cr; }

  void fill(Rgb color) const {
    cairo_set_source_rgb(m_cr, color.r, color.g, color.b);
    cairo_paint(m_cr);
  }

  void rect(double x, double y, double w, double h, Rgb color,
            double alpha = 1.0) const {
    cairo_set_source_rgba(m_cr, color.r, color.g, color.b, alpha);
    cairo_rectangle(m_cr, x, y, w, h);
    cairo_fill(m_cr);
  }

  void roundRect(double x, double y, double w, double h, double radius,
                 Rgb color, double alpha) const {
    cairo_new_sub_path(m_cr);
    cairo_arc(m_cr, x + w - radius, y + radius, radius, -M_PI / 2, 0);
    cairo_arc(m_cr, x + w - radius, y + h - radius, radius, 0, M_PI / 2);
    cairo_arc(m_cr, x + radius, y + h - radius, radius, M_PI / 2, M_PI);
    cairo_arc(m_cr, x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
    cairo_close_path(m_cr);
    cairo_set_source_rgba(m_cr, color.r, color.g, color.b, alpha);
    cairo_fill(m_cr);
  }

  /// Returns the height the text took, in points.
  double text(double x, double y, std::string_view words, std::string_view font,
              Rgb color, double width = -1) const {
    PangoLayout *layout = pango_cairo_create_layout(m_cr);
    PangoFontDescription *description =
        pango_font_description_from_string(std::string(font).c_str());
    pango_layout_set_font_description(layout, description);
    pango_layout_set_text(layout, words.data(), static_cast<int>(words.size()));
    if (width > 0)
      pango_layout_set_width(layout, static_cast<int>(width * PANGO_SCALE));
    cairo_set_source_rgb(m_cr, color.r, color.g, color.b);
    cairo_move_to(m_cr, x, y);
    pango_cairo_show_layout(m_cr, layout);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    pango_font_description_free(description);
    g_object_unref(layout);
    return h;
  }

  /// Paint the renderer's premultiplied RGBA at device-pixel position.
  void pixels(double x, double y, const hyprknit::Pixels &source) const {
    cairo_surface_t *image = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, source.width, source.height);
    cairo_surface_flush(image);
    auto *data = cairo_image_surface_get_data(image);
    const int stride = cairo_image_surface_get_stride(image);
    for (int row = 0; row < source.height; ++row) {
      auto *out = reinterpret_cast<std::uint32_t *>(data + row * stride);
      for (int column = 0; column < source.width; ++column) {
        const auto *in =
            &source
                 .rgba[(static_cast<std::size_t>(row) * source.width + column) *
                       4];
        out[column] = (std::uint32_t{in[3]} << 24) |
                      (std::uint32_t{in[0]} << 16) |
                      (std::uint32_t{in[1]} << 8) | std::uint32_t{in[2]};
      }
    }
    cairo_surface_mark_dirty(image);
    cairo_save(m_cr);
    cairo_scale(m_cr, 1.0 / kScale, 1.0 / kScale);
    cairo_set_source_surface(m_cr, image, x * kScale, y * kScale);
    cairo_paint(m_cr);
    cairo_restore(m_cr);
    cairo_surface_destroy(image);
  }

  bool save(const std::filesystem::path &path) const {
    return cairo_surface_write_to_png(m_surface, path.c_str()) ==
           CAIRO_STATUS_SUCCESS;
  }

private:
  cairo_surface_t *m_surface;
  cairo_t *m_cr;
};

struct Yarn {
  std::uint32_t color;
  int chart;
};

/// The colour and chart an app wears under the current pattern setting.
Yarn yarnFor(std::string_view appId) {
  const auto *rule = hyprknit::appRule(appId);
  return {rule ? rule->color : hyprknit::colorForApp(appId),
          hyprknit::patternForApp(appId)};
}

/// An Omarchy window in its sweater: a square pane, its knitted frame outside
/// it, the frame's outer corner rounded by the band as Hyprknit draws it.
/// `x, y, w, h` are the frame's outer bounds in points.
void window(const Page &page, double x, double y, double w, double h,
            double band, Yarn yarn, Rgb pane = kPane) {
  page.rect(x + band, y + band, w - 2 * band, h - 2 * band, pane);
  hyprknit::Band knit{
      .frameWidth = static_cast<int>(w * kScale),
      .frameHeight = static_cast<int>(h * kScale),
      .band = static_cast<int>(band * kScale),
      .radius = band * kScale, // window rounding 0, Omarchy's default
      .color = yarn.color,
      .chart = yarn.chart,
  };
  hyprknit::Pixels pixels{
      .width = knit.frameWidth, .height = knit.frameHeight, .rgba = {}};
  hyprknit::renderBandRegion(knit, 0, 0, pixels);
  page.pixels(x, y, pixels);
}

/// A stretch of straight band, knitted at a larger size to show the stitches.
void yarnDetail(const Page &page, double x, double y, double w, double h,
                Yarn yarn) {
  const int band = static_cast<int>(h * kScale);
  const int length = static_cast<int>(w * kScale);
  hyprknit::Band knit{
      .frameWidth = length + band * 4,
      .frameHeight = band * 6,
      .band = band,
      .radius = static_cast<double>(band),
      .color = yarn.color,
      .chart = yarn.chart,
  };
  hyprknit::Pixels pixels{.width = length, .height = band, .rgba = {}};
  hyprknit::renderBandRegion(knit, band * 2, 0, pixels);
  page.pixels(x, y, pixels);
}

/// One app: its name, its yarns, its window, and its stitches up close.
double card(const Page &page, double x, double y, double width,
            const Entry &entry, double band, bool describe) {
  double at = y;
  at += page.text(x, at, entry.name, "Noto Sans 17", kInk);
  if (describe)
    at += 2 + page.text(x, at, entry.palette, "Noto Sans 10", kQuiet);
  at += 10;
  const Yarn yarn = yarnFor(entry.appId);
  const double height = describe ? 104 : 84;
  window(page, x, at, width, height, band, yarn);
  if (describe)
    page.text(x + band + 18, at + height / 2 - 8, entry.pattern, "Noto Sans 11",
              kInk, width - 2 * band - 36);
  at += height + 16;
  yarnDetail(page, x, at, width, band * 2.4, yarn);
  return at + band * 2.4 - y;
}

bool plate(const std::filesystem::path &out, std::string_view title,
           std::span<const Entry> entries) {
  constexpr double kWidth = 1120, kMargin = 56, kGap = 48;
  const double column = (kWidth - 2 * kMargin - kGap) / 2;
  const int rows = static_cast<int>((entries.size() + 1) / 2);
  constexpr double kRow = 236;
  const double height = 160 + rows * kRow + 50;

  Page page(kWidth, height);
  page.fill(kPaper);
  page.text(kMargin, 40, title, "Noto Sans 27", kInk);
  page.text(kMargin, 94,
            "Hyprknit / App-inspired colours, a little whimsy, and real "
            "knitted stitches.",
            "Noto Sans 11", kInk);
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const double x = kMargin + static_cast<double>(i % 2) * (column + kGap);
    const double y = 146 + static_cast<double>(i / 2) * kRow;
    card(page, x, y, column, entries[i], 10, true);
  }
  page.text(kMargin, height - 44,
            "10 pt window borders on Omarchy's square windows / enlarged yarn "
            "details / drawn by Hyprknit's own renderer at 2x",
            "Noto Sans 9", kQuiet);
  return page.save(out);
}

bool comparison(const std::filesystem::path &out) {
  constexpr double kWidth = 2240, kMargin = 56, kGap = 40, kSplit = 96;
  const double half = (kWidth - 2 * kMargin - kSplit) / 2;
  const double column = (half - kGap) / 2;
  constexpr double kRow = 196;
  const std::span featured(kCollection, 8);
  const double height = 206 + 4 * kRow + 40;

  Page page(kWidth, height);
  page.fill(kPaper);
  page.text(kMargin, 32, "Hyprknit", "Noto Sans 27", kInk);
  page.text(kMargin, 86, "Your favourite apps, two ways to wear them.",
            "Noto Sans 11", kInk);

  const std::pair<std::string_view, std::string_view> sides[]{
      {"By App", "by-app"}, {"Zigzag", "zigzag"}};
  for (int side = 0; side < 2; ++side) {
    hyprknit::patternSelect(sides[side].second);
    const double left = kMargin + side * (half + kSplit);
    page.text(left, 128, sides[side].first, "Noto Sans 19", kInk);
    for (std::size_t i = 0; i < featured.size(); ++i) {
      const double x = left + static_cast<double>(i % 2) * (column + kGap);
      const double y = 186 + static_cast<double>(i / 2) * kRow;
      card(page, x, y, column, featured[i], 12, false);
    }
  }
  hyprknit::patternSelect("by-app");

  page.rect(kMargin + half + kSplit / 2, 128, 0.8, height - 200, kQuiet, 0.35);
  page.text(kMargin, height - 36,
            "12 pt borders / enlarged yarn details / drawn by Hyprknit's own "
            "renderer",
            "Noto Sans 9", kQuiet);
  page.text(kMargin + half + kSplit, height - 36,
            "Zigzag keeps each app's base yarn and adds a soft pink contrast.",
            "Noto Sans 9", kQuiet);
  return page.save(out);
}

// Window corners cascading down the page, as the Mac app's hero has them, but
// every stitch here comes from the renderer.
bool hero(const std::filesystem::path &out) {
  constexpr double kWidth = 1536, kHeight = 1024, kBand = 56;
  Page page(kWidth, kHeight);
  page.fill(kCream);

  const std::string_view apps[]{"Spotify", "Codex", "Finder", "Claude"};
  constexpr double kStepX = 190, kStepY = 170;
  for (std::size_t i = 0; i < std::size(apps); ++i) {
    const double x = 160 + static_cast<double>(i) * kStepX;
    const double y = 200 + static_cast<double>(i) * kStepY;
    const double w = kWidth + 400, h = kHeight + 400;
    // a soft shadow the shape of the knitted frame, so each window sits on
    // the one behind it
    for (int layer = 14; layer > 0; --layer) {
      const double spread = layer * 2.0;
      page.roundRect(x - spread + 10, y - spread + 18, w + 2 * spread,
                     h + 2 * spread, kBand + spread, hex(0x5a4a2a), 0.013);
    }
    window(page, x, y, w, h, kBand, yarnFor(apps[i]), hex(0xf6f5f1));
  }
  return page.save(out);
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path out = argc > 1 ? argv[1] : "assets";
  std::filesystem::create_directories(out);

  // The built-in collection only: personal charts and rules stay out of it.
  hyprknit::chartsLoad({});
  hyprknit::patternSelect("by-app");

  bool ok = hero(out / "hero.png") && comparison(out / "styles-comparison.png");
  const std::span collection(kCollection);
  for (std::size_t first = 0, page = 1; first < collection.size();
       first += 8, ++page) {
    const auto entries = collection.subspan(
        first, std::min<std::size_t>(8, collection.size() - first));
    ok = ok && plate(out / std::format("collection-{}.png", page),
                     std::format("The collection / {:02}", page), entries);
  }
  if (!ok) {
    std::cerr << "hyprknit-catalogue: could not write to " << out << '\n';
    return 1;
  }
  std::cout << "catalogue written to " << out << '\n';
  return 0;
}
