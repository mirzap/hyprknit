#include "chart.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <system_error>
#include <utility>

#include <gdk-pixbuf/gdk-pixbuf.h>

namespace hyprknit {
namespace {

std::vector<Chart> g_charts;
constexpr int kMaxChartDimension = 256;

// Original six-row repeats: clear blocks, small motifs and contrasting yarns.
// A dot keeps the app's main yarn. These are data rather than bundled images,
// so the collection also works on a first run and when the user chart folder is
// unavailable.
struct CollectionChart {
  const char *name;
  const char *rows[12];
  std::uint32_t yarn[6]; // a..f
};

constexpr CollectionChart k_collection[] = {
    {"atelier-finder",
     {"..aaaa....aaaa..", "..aaaa....aaaa..", "..aaaa....aaaa..",
      "aa....aaaa....aa", "aa....aaaa....aa", "b...b......b...b"},
     {0xffb7e4f7u, 0xff24558bu}},
    {"atelier-terminal",
     {"aa....aa", "aa....aa", "aa....aa", "aa....aa", "aa....aa", "aa....aa"},
     {0xffa5bda0u}},
    {"atelier-grok",
     {"aaa.b..b.aaa", "aaa.b..b.aaa", "aaa.b..b.aaa", "aaa.b..b.aaa",
      "aaa.b..b.aaa", "aaa.b..b.aaa"},
     {0xffeeece4u, 0xff868a88u}},
    {"atelier-teams",
     {"a....aa....a", "aa........aa", ".aa......aa.", "..aa....aa..",
      "...aa..aa...", "....aaaa...."},
     {0xfff4efeeu}},
    {"atelier-claude",
     {"............", "..a.....a...", ".aaa...aaa..", "..a.....a...",
      "............", "............"},
     {0xfff7e8c5u}},
    {"atelier-codex",
     {"aa.....b....", "aa.....b....", "aa.....b....", "aa.....b....",
      "aa.....b....", "aa.....b...."},
     {0xffe6e6ceu, 0xffe9bfcbu}},
    {"atelier-spotify",
     {"...aaa...aaa", "...aaa...aaa", "...aaa...aaa", "aaa...aaa...",
      "aaa...aaa...", "aaa...aaa..."},
     {0xffe5d586u}},
    {"atelier-notion",
     {"...aaa...aaa", "...aaa...aaa", "...aaa...aaa", "aaa...aaa...",
      "aaa...aaa...", "aaa...aaa..."},
     {0xff494947u}},
    {"atelier-whatsapp",
     {"aaaa....b...", "aaaa....b...", "aaaa....b...", "aaaa....b...",
      "aaaa....b...", "aaaa....b..."},
     {0xffdde9bcu, 0xffe3a2b8u}},
    {"atelier-figma",
     {"aabb....cc..", "aabb....cc..", "aabb....cc..", "....ddee....",
      "....ddee....", "....ddee...."},
     {0xffeaaf96u, 0xffdcd092u, 0xff8dbccfu, 0xffafc5a1u, 0xffded3e9u}},
    {"atelier-chrome",
     {"....aaaaaaaa........bbbbbbbb........dddddddd........aaaaaaaa........"
      "bbbbbbbb........dddddddd........cccccccc....",
      "....aaaaaaaa........bbbbbbbb........dddddddd........aaaaaaaa........"
      "bbbbbbbb........dddddddd........cccccccc....",
      "....aaaaaaaa........bbbbbbbb........dddddddd........aaaaaaaa........"
      "bbbbbbbb........dddddddd........cccccccc....",
      "aaaa........bbbbbbbb........dddddddd........aaaaaaaa........bbbbbbbb...."
      "....dddddddd........cccccccc........aaaa",
      "aaaa........bbbbbbbb........dddddddd........aaaaaaaa........bbbbbbbb...."
      "....dddddddd........cccccccc........aaaa",
      "aaaa........bbbbbbbb........dddddddd........aaaaaaaa........bbbbbbbb...."
      "....dddddddd........cccccccc........aaaa"},
     {0xffd8675bu, 0xff6ba776u, 0xff4285f4u, 0xffedcc70u}},
    {"atelier-paper",
     {"..aaaa..", ".a...a..", ".a.b.a..", ".aaaaa..", ".aaa....", "........"},
     {0xfff5f5f2u, 0xff5f8bceu}},
    {"atelier-safari",
     {"..a.....a...", ".aaa...aaa..", "aabaa.aabaa.", ".aaa...aaa..",
      "..a.....a...", "............"},
     {0xfff6f2e8u, 0xffed7066u}},
    {"atelier-firefox",
     {"a.....a.....", "aa....aa....", ".aa....aa...", "..bb....bb..",
      "...bb....bb.", "....b.....b."},
     {0xffff873eu, 0xffffc167u}},
    {"atelier-cursor",
     {"a....aa....a", "aa........aa", ".aa......aa.", "..aa....aa..",
      "...aa..aa...", "....aaaa...."},
     {0xffc4c2b9u}},
    {"atelier-slack",
     {".a......b...", "aaa....bbb..", ".a......b...", "....c......d",
      "...ccc....dd", "....c......d"},
     {0xff63c5dfu, 0xff82c5a6u, 0xffedc45eu, 0xffe986a3u}},
    {"atelier-zoom",
     {".aaaa...aaaa", ".abba...abba", ".abba...abba", ".aaaa...aaaa",
      "............", "bbbbbbbbbbbb"},
     {0xfff6f8f4u, 0xff9ec7f5u}},
    {"atelier-telegram",
     {"a.....a.....", "aa....aa....", "aba...aba...", ".aba...aba..",
      "..aa....aa..", "...a.....a.."},
     {0xfff5f5ecu, 0xffaddde8u}},
    {"atelier-messages",
     {"................", "..aa......aa....", "..aa......aa....",
      "................", "......bb......bb", "......bb......bb"},
     {0xfff8f7e8u, 0xffb7db8du}},
    {"atelier-mail",
     {"a.....a.....", ".a...a.a...a", "..a.a...a.a.", "...a.....a..",
      "............", "bbbbbbbbbbbb"},
     {0xfff5f5f0u, 0xffb3d8eeu}},
    {"atelier-notes",
     {"aaaaaaaaaaaa", "aaaaaaaaaaaa", "............", "............",
      "............", "bbbbbbbbbbbb"},
     {0xffefc852u, 0xfffffcf3u}},
    {"atelier-calendar",
     {"aaaaaaaaaaaa", "aaaaaaaaaaaa", "............", "............",
      "............", "............"},
     {0xffe55c52u}},
    {"atelier-reminders",
     {"a.dd..b.dd..", "............", "c.dd..a.dd..", "............",
      "b.dd..c.dd..", "............"},
     {0xff579fdeu, 0xffe5787au, 0xffeba451u, 0xffccc6bau}},
    {"atelier-music",
     {"a.....a.....", "aa....aa....", ".aa....aa...", "..bb....bb..",
      "...bb....bb.", "....b.....b."},
     {0xfff7acc0u, 0xfffff0dbu}},
    {"atelier-photos",
     {".a...c...e..", "aaa.ccc.eee.", ".a...c...e..", "...b...d...f",
      "..bbb.ddd.ff", "...b...d...f"},
     {0xffefa470u, 0xffeccb67u, 0xff95ba78u, 0xff7bbbc9u, 0xff9991c1u,
      0xffd98bacu}},
    {"atelier-preview",
     {"bbbbbbbbbbbb", "b.....b.....", "b..a..b..a..", "b.aaa.b.aaa.",
      "baaaaabaaaaa", "bbbbbbbbbbbb"},
     {0xfff2f4efu, 0xffaacfddu}},
    {"atelier-word",
     {"a....aa....a", "aa........aa", ".aa......aa.", "..bb....bb..",
      "...bb..bb...", "....bbbb...."},
     {0xfff3f1e6u, 0xff82a8deu}},
    {"atelier-excel",
     {"aa...baa...b", "aa...baa...b", "bbbbbbbbbbbb", "..aa.b..aa.b",
      "..aa.b..aa.b", "bbbbbbbbbbbb"},
     {0xff82b99au, 0xffdbead0u}},
    {"atelier-powerpoint",
     {".aaaa...aaaa", ".abba...abba", ".abba...abba", ".aaaa...aaaa",
      "............", "..b.....b..."},
     {0xffeea58au, 0xfff5d9b8u}},
    {"atelier-outlook",
     {"aaa...aaa...", "aba...aba...", "aab...aab...", "...aaa...aaa",
      "...aba...aba", "...aab...aab"},
     {0xff8ac6ecu, 0xfff5f5eau}},
    {"atelier-vscode",
     {"a....ba....b", ".a..ab.a..ab", "..aa.b..aa.b", "..aa.b..aa.b",
      ".a..ab.a..ab", "a....ba....b"},
     {0xffb6e1edu, 0xff17374fu}},
    {"atelier-photoshop",
     {"aaaa..aaaa..", "a..a..a..a..", "aaaa..aaaa..", "...bbb...bbb",
      "...b.b...b.b", "...bbb...bbb"},
     {0xff58b7e9u, 0xff8cbad0u}},
    {"atelier-illustrator",
     {"...baaaab...", "...baaaab...", "...baaaab...", "...baaaab...",
      "...baaaab...", "...baaaab..."},
     {0xfff5a13du, 0xfff8d2a0u}},
    {"atelier-granola",
     {"...aaaaaa...", "...aaaaaa...", "...aaaaaa...", "...aaaaaa...",
      "...aaaaaa...", "...aaaaaa..."},
     {0xff8ba66au}},
    {"atelier-chatgpt",
     {"a....aa....a", "aa........aa", ".aa......aa.", "..aa....aa..",
      "...aa..aa...", "....aaaa...."},
     {0xfff6f0deu}},
    {"atelier-ghostty",
     {"aa..........", "aa..........", "aa..........", "aa..........",
      "aa..........", "aa.........."},
     {0xffecebe4u}},
    {"braid",
     {"abb........a", "a.bb......aa", "...bb....aaa", "....bb..aaa.",
      ".....bbaaa..", "......bba...", ".....aabb...", "....aaa.bb..",
      "...aaa...bb.", "..aaa.....bb", "baaa.......b", "bba........."},
     {0xfff6f0deu, 0xfff078aau}},
    {"blockstripe", {"...aaa"}, {0xffffffffu}},
    {"checker", {"..aa", "..aa", "aa..", "aa.."}, {0xfff2eee4u}},
    {"seedling",
     {"....a.", "....a.", "...a.a", ".a....", ".a....", "a.a..."},
     {0xff96d6a0u}},
    {"trim",
     {"...a....a.", "..........", "bb...bb...", "..........", "c...cc...c",
      "cc...cc..."},
     {0xffffffffu, 0xfff58220u, 0xffd61e6eu}},
    {"picnic",
     {"...aaa...aaa", "...aaa...aaa", "...aaa...aaa", "aaa...aaa...",
      "aaa...aaa...", "aaa...aaa..."},
     {0xfff4e6bfu}},
    {"ribbon",
     {"aa.....b....", "aa.....b....", "aa.....b....", "aa.....b....",
      "aa.....b....", "aa.....b...."},
     {0xfff5e8cfu, 0xffe3a1b3u}},
    {"posy",
     {"............", ".aa.aa......", ".aaaaa......", "..aba.......",
      ".a...a......", "............"},
     {0xfff0d1dbu, 0xfff2dd9du}},
    {"twinkle",
     {"............", "..a.....a...", ".aaa...aaa..", "..a.....a...",
      "............", "............"},
     {0xfff5e6bfu}},
    {"candy-stripe",
     {"aaa...bbb...", "aaa...bbb...", "aaa...bbb...", "aaa...bbb...",
      "aaa...bbb...", "aaa...bbb..."},
     {0xfff5e8cfu, 0xffe3a1b3u}},
    {"zigzag",
     {"a....aa....a", "aa........aa", ".aa......aa.", "..aa....aa..",
      "...aa..aa...", "....aaaa...."},
     {0xfff2d2dcu}},
};

// Solid patches finish patterns that otherwise collide at the mitre.
// A zero colour follows the app's own base yarn, including personal palettes.
constexpr struct {
  const char *chart;
  std::uint32_t color;
} k_corner_styles[] = {
    {"atelier-whatsapp", 0}, // keep its stripe joins quiet
};

void loadCollection() {
  for (const auto &spec : k_collection) {
    const int w = static_cast<int>(std::strlen(spec.rows[0]));
    int h = 0;
    while (h < 12 && spec.rows[h])
      ++h;

    Chart chart;
    chart.name = spec.name;
    chart.w = w;
    chart.h = h;
    chart.px.assign(static_cast<std::size_t>(w) * h, 0u);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const char yarn = spec.rows[y][x];
        if (yarn >= 'a' && yarn <= 'f')
          chart.px[static_cast<std::size_t>(y) * w + x] = spec.yarn[yarn - 'a'];
      }
    }

    for (const auto &corner : k_corner_styles) {
      if (chart.name == corner.chart) {
        chart.solidCorners = true;
        chart.cornerColor = corner.color;
        break;
      }
    }
    if (chart.name == "atelier-notes" || chart.name == "atelier-calendar") {
      // Move the outer cuff out of the rectangular chart: its coloured rows
      // otherwise repeat into the inner corner and make angular fragments.
      chart.cuffColor = spec.yarn[0];
      for (auto &cell : chart.px)
        if (cell == chart.cuffColor)
          cell = 0;
    }
    chart.roundDots = chart.name == "atelier-messages";
    chart.fittedRepeat =
        chart.name == "atelier-finder" || chart.name == "atelier-terminal" ||
        chart.name == "atelier-grok" || chart.name == "atelier-granola" ||
        chart.name == "atelier-illustrator" || chart.name == "atelier-chrome";
    chart.sculptedYarn = !chart.roundDots;
    chart.definedYarn = true;
    chart.builtin = true;
    g_charts.push_back(std::move(chart));
  }
}

bool loadOne(const std::filesystem::path &path, const std::string &name,
             Chart &out) {
  int fileWidth = 0;
  int fileHeight = 0;
  if (!gdk_pixbuf_get_file_info(path.c_str(), &fileWidth, &fileHeight) ||
      fileWidth < 1 || fileHeight < 1 || fileWidth > kMaxChartDimension ||
      fileHeight > kMaxChartDimension)
    return false;

  GError *error = nullptr;
  GdkPixbuf *image = gdk_pixbuf_new_from_file(path.c_str(), &error);
  if (!image) {
    if (error)
      g_error_free(error);
    return false;
  }

  const int w = gdk_pixbuf_get_width(image);
  const int h = gdk_pixbuf_get_height(image);
  const int channels = gdk_pixbuf_get_n_channels(image);
  const int stride = gdk_pixbuf_get_rowstride(image);
  const bool hasAlpha = gdk_pixbuf_get_has_alpha(image) != 0;
  const guchar *pixels = gdk_pixbuf_get_pixels(image);
  if (w != fileWidth || h != fileHeight || channels < 3) {
    g_object_unref(image);
    return false;
  }

  out.px.assign(static_cast<std::size_t>(w) * h, 0u);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const guchar *pixel = pixels + static_cast<std::size_t>(y) * stride +
                            static_cast<std::size_t>(x) * channels;
      const std::uint32_t alpha = hasAlpha ? pixel[3] : 255u;
      out.px[static_cast<std::size_t>(y) * w + x] =
          (alpha << 24) | (static_cast<std::uint32_t>(pixel[0]) << 16) |
          (static_cast<std::uint32_t>(pixel[1]) << 8) |
          static_cast<std::uint32_t>(pixel[2]);
    }
  }
  g_object_unref(image);

  out.name = name;
  out.w = w;
  out.h = h;
  out.sculptedYarn = true;
  out.definedYarn = true;
  return true;
}

} // namespace

std::filesystem::path chartsDir() {
  std::filesystem::path base;
  if (const char *config = std::getenv("XDG_CONFIG_HOME"); config && *config)
    base = config;
  else if (const char *home = std::getenv("HOME"); home && *home)
    base = std::filesystem::path(home) / ".config";
  else
    return {};

  const auto dir = base / "hyprknit" / "charts";
  std::error_code ignored;
  std::filesystem::create_directories(dir, ignored);
  return dir;
}

int chartsLoad(const std::filesystem::path &dir) {
  g_charts.clear();
  loadCollection();

  // A stable order avoids changing an active chart just because the filesystem
  // returned its directory in a different order.
  std::vector<std::filesystem::path> files;
  std::error_code ignored;
  for (const auto &entry : std::filesystem::directory_iterator(
           dir, std::filesystem::directory_options::skip_permission_denied,
           ignored)) {
    auto extension = entry.path().extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return std::tolower(c); });
    if (extension == ".png" && entry.is_regular_file(ignored))
      files.push_back(entry.path());
  }
  std::ranges::sort(files);

  for (const auto &file : files) {
    const auto name = file.stem().string();
    if (name.empty() || name.size() >= 64 ||
        !std::ranges::all_of(name, [](unsigned char c) {
          return std::isalnum(c) || c == '.' || c == '_' || c == '-';
        }))
      continue;
    Chart chart;
    if (!loadOne(file, name, chart))
      continue;
    if (const int target = chartIndex(name); target >= 0) {
      // A user chart replaces its built-in's cells but keeps the collection's
      // corner, cuff and yarn treatment, which the image cannot express.
      chart.solidCorners = g_charts[target].solidCorners;
      chart.cornerColor = g_charts[target].cornerColor;
      chart.cuffColor = g_charts[target].cuffColor;
      chart.roundDots = g_charts[target].roundDots;
      chart.fittedRepeat = g_charts[target].fittedRepeat;
      chart.sculptedYarn = g_charts[target].sculptedYarn;
      chart.definedYarn = g_charts[target].definedYarn;
      chart.builtin = g_charts[target].builtin;
      g_charts[target] = std::move(chart);
    } else {
      g_charts.push_back(std::move(chart));
    }
  }
  return static_cast<int>(g_charts.size());
}

int chartIndex(std::string_view name) {
  for (std::size_t i = 0; i < g_charts.size(); ++i)
    if (g_charts[i].name == name)
      return static_cast<int>(i);
  return -1;
}

const std::vector<Chart> &charts() { return g_charts; }

std::vector<MenuPattern> menuPatterns() {
  // The Mac app's own menu names for its featured patterns.
  constexpr std::pair<std::string_view, std::string_view> kFeatured[]{
      {"zigzag", "Zigzag"},         {"picnic", "Picnic Checks"},
      {"ribbon", "Ribbon Stripes"}, {"posy", "Little Bows"},
      {"twinkle", "Tiny Stars"},    {"candy-stripe", "Candy Stripes"},
  };

  std::vector<MenuPattern> patterns;
  for (const auto &[name, title] : kFeatured)
    if (chartIndex(name) >= 0)
      patterns.push_back({std::string(name), std::string(title)});
  for (const auto &chart : g_charts)
    if (!chart.builtin)
      patterns.push_back({chart.name, chartTitle(chart.name)});
  return patterns;
}

std::string chartTitle(std::string_view name) {
  std::string title(name);
  bool wordStart = true;
  for (char &c : title) {
    if (c == '-' || c == '_') {
      c = ' ';
      wordStart = true;
    } else {
      c = static_cast<char>(wordStart
                                ? std::toupper(static_cast<unsigned char>(c))
                                : std::tolower(static_cast<unsigned char>(c)));
      wordStart = false;
    }
  }
  return title;
}

const Chart *chartAt(int index) {
  if (index < 0 || std::cmp_greater_equal(index, g_charts.size()))
    return nullptr;
  return &g_charts[static_cast<std::size_t>(index)];
}

} // namespace hyprknit
