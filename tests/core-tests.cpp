#include "apps.hpp"
#include "chart.hpp"
#include "knit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

namespace {

int g_failures = 0;

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
  }
}

struct Sampled {
  std::uint8_t r = 0, g = 0, b = 0, a = 0;
};

Sampled pixelAt(const hyprknit::Pixels &pixels, int x, int y) {
  const auto index = (static_cast<std::size_t>(y) * pixels.width + x) * 4;
  return {pixels.rgba[index], pixels.rgba[index + 1], pixels.rgba[index + 2],
          pixels.rgba[index + 3]};
}

// The whole frame in one buffer, which is what the decoration splits into
// sides and corners.
hyprknit::Pixels renderFrame(const hyprknit::Band &band) {
  hyprknit::Pixels pixels;
  pixels.width = band.frameWidth;
  pixels.height = band.frameHeight;
  hyprknit::renderBandRegion(band, 0, 0, pixels);
  return pixels;
}

hyprknit::Band sampleBand(int chart, std::uint32_t color) {
  hyprknit::Band band;
  band.frameWidth = 300;
  band.frameHeight = 200;
  band.band = 24;
  band.radius = 40; // 16pt of window rounding plus the band
  band.color = color;
  band.chart = chart;
  return band;
}

void writeChart(const std::filesystem::path &path, int width = 2,
                int height = 2) {
  GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
  gdk_pixbuf_fill(image, 0xff0000ffu);
  gdk_pixbuf_save(image, path.c_str(), "png", nullptr, nullptr);
  g_object_unref(image);
}

void testChartCollection() {
  require(hyprknit::chartIndex("atelier-claude") >= 0,
          "the built-in collection loads without any chart files on disk");

  const auto *notes = hyprknit::chartAt(hyprknit::chartIndex("atelier-notes"));
  require(notes != nullptr, "Notes has a colourway in the collection");
  if (notes) {
    // Its golden rows become a curved cuff. Left in the rectangular chart they
    // would repeat into the inner corner as angular fragments.
    require(notes->cuffColor == 0xffefc852u,
            "Notes lifts its golden rows out of the chart and into the cuff");
    require(std::ranges::none_of(
                notes->px,
                [&](std::uint32_t cell) { return cell == notes->cuffColor; }),
            "no golden cell is left behind in the Notes chart");
  }

  const auto *messages =
      hyprknit::chartAt(hyprknit::chartIndex("atelier-messages"));
  require(messages && messages->roundDots && !messages->sculptedYarn,
          "the polka-dot colourway keeps its curved dot mask");
}

void testAppMatching() {
  // A Wayland app id is not a macOS process name. Reverse-DNS ids have to
  // resolve to the same colourway their app is knitted in.
  const auto *firefox = hyprknit::appRule("org.mozilla.firefox");
  require(firefox && firefox->chart == "atelier-firefox",
          "a reverse-DNS app id finds its colourway");

  const auto *ghostty = hyprknit::appRule("com.mitchellh.ghostty");
  require(ghostty && ghostty->chart == "atelier-ghostty",
          "an app id is also matched on its last segment");

  const auto *helper = hyprknit::appRule("Claude Helper (Renderer)");
  require(helper && helper->chart == "atelier-claude",
          "the rule name matches as a prefix, so helper processes follow suit");

  const auto *teams = hyprknit::appRule("Microsoft Teams");
  require(teams && teams->color == 0xff9283c1u,
          "the longest matching prefix wins over a shorter one");

  require(hyprknit::appRule("no-such-application-4711") == nullptr,
          "an app outside the collection has no rule");
}

void testUserRules(const std::filesystem::path &config) {
  {
    std::ofstream conf(config / "hyprknit" / "apps.conf");
    conf << "# a comment\n"
            "Ghostty = #112233 atelier-finder\n"
            "Broken  = #12345\n"           // five digits
            "Also    = #12345678 zigzag\n" // eight digits
            "Trailing = #445566 zigzag extra\n";
  }
  require(hyprknit::appsLoad() == 1,
          "only the well-formed rule is loaded from apps.conf");

  const auto *ghostty = hyprknit::appRule("com.mitchellh.ghostty");
  require(ghostty && ghostty->color == 0xff112233u &&
              ghostty->chart == "atelier-finder",
          "a personal rule takes priority over the built-in colourway");
}

void testFallbackYarn() {
  const auto once = hyprknit::colorForApp("some-unlisted-app");
  require(once == hyprknit::colorForApp("Some-Unlisted-App"),
          "the fallback yarn ignores case, the way rule matching does");

  const auto &basket = hyprknit::baskets()[hyprknit::knitting().basket];
  require(std::ranges::contains(basket.colors, once),
          "the fallback yarn comes out of the active basket");
}

// The bug this guards: a mitre that reaches too far lets the opposite side
// claim a pixel, and that side reads the chart from the wrong end, so the whole
// band comes out upside down.
void testChartOrientation() {
  const int index = hyprknit::chartIndex("atelier-preview");
  const auto *chart = hyprknit::chartAt(index);
  require(chart != nullptr, "Preview has a colourway in the collection");
  if (!chart)
    return;

  // Its snow yarn widens from one cell in row 2 to five in row 4, so counting
  // snow across the band says which way round the chart was read.
  const std::uint32_t snow = 0xfff2f4efu;
  const auto snowInRow = [&](int row) {
    return std::ranges::count(
        std::ranges::subrange(chart->px.begin() + row * chart->w,
                              chart->px.begin() + (row + 1) * chart->w),
        snow);
  };
  require(snowInRow(2) < snowInRow(4),
          "the Preview chart widens from its row 2 to its row 4");

  // Deliberately a shallow window: the opposite side's mitre only reaches
  // across the frame when the frame is not much deeper than the mitre itself,
  // and that is where reading the chart from the wrong end shows up.
  auto band = sampleBand(index, 0xff597bafu);
  band.frameHeight = 120;
  const auto pixels = renderFrame(band);
  const auto rowHeight = static_cast<double>(band.band) / chart->h;

  const auto snowAcross = [&](int row) {
    const int y = static_cast<int>((row + 0.5) * rowHeight);
    int count = 0;
    for (int x = band.frameWidth / 3; x < band.frameWidth * 2 / 3; ++x) {
      const auto pixel = pixelAt(pixels, x, y);
      if (pixel.r > 200 && pixel.g > 200 && pixel.b > 200)
        ++count;
    }
    return count;
  };
  require(
      snowAcross(2) < snowAcross(4),
      "chart row 0 is knitted at the outer edge of the band, not the inner");
}

void testRingGeometry() {
  const auto band =
      sampleBand(hyprknit::chartIndex("atelier-claude"), 0xffd58561u);
  const auto pixels = renderFrame(band);

  require(pixelAt(pixels, band.frameWidth / 2, band.band / 2).a == 255,
          "the band is opaque along a straight edge");
  require(pixelAt(pixels, band.frameWidth / 2, band.band + 4).a == 0,
          "the window's own area is left untouched");
  require(pixelAt(pixels, 1, 1).a == 0, "the outer corner is rounded away");

  // Every pixel of the ring has to be knitted by exactly one side. A mitre that
  // does not reach far enough leaves bare holes on the corner arcs.
  const double halfW = band.frameWidth * 0.5;
  const double halfH = band.frameHeight * 0.5;
  int holes = 0;
  for (int y = 0; y < band.frameHeight; ++y)
    for (int x = 0; x < band.frameWidth; ++x) {
      // Well inside the ring: at least a pixel clear of both boundaries.
      const double dx = std::abs(x + 0.5 - halfW);
      const double dy = std::abs(y + 0.5 - halfH);
      const double qx = dx - halfW + band.radius;
      const double qy = dy - halfH + band.radius;
      const double outer = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) +
                           std::min(std::max(qx, qy), 0.0) - band.radius;
      if (outer < -1.5 && outer > -(band.band - 1.5) &&
          pixelAt(pixels, x, y).a != 255)
        ++holes;
    }
  require(holes == 0,
          "the mitred sides cover the whole ring, corners included");
}

void testInvalidOutputGeometry() {
  hyprknit::Pixels pixels{.width = -1, .height = 10, .rgba = {1, 2, 3, 4}};
  hyprknit::renderBandRegion(sampleBand(-1, 0xff112233u), 0, 0, pixels);
  require(pixels.rgba.empty(),
          "invalid output geometry is rejected before allocating pixels");
}

void testPatternSelection() {
  require(hyprknit::patternSelect("zigzag"), "a global chart can be selected");
  require(hyprknit::patternForApp("com.mitchellh.ghostty") ==
              hyprknit::chartIndex("zigzag"),
          "a global pattern overrides each app's own chart");
  require(hyprknit::minimumRows() >= 6.F,
          "colourwork asks for enough stitch rows to show the whole chart");

  require(!hyprknit::patternSelect("not-a-chart"),
          "an unknown pattern name is rejected");
  require(hyprknit::patternForApp("com.mitchellh.ghostty") ==
              hyprknit::chartIndex("zigzag"),
          "a rejected name leaves the current selection alone");

  require(hyprknit::patternSelect("by-app"), "By App can be selected back");
  require(hyprknit::patternForApp("com.mitchellh.ghostty") ==
              hyprknit::chartIndex("atelier-ghostty"),
          "By App gives each app its own chart again");
}

// The Pattern menu offers what the Mac app's does: its six featured patterns,
// then the user's own charts, and never the charts By App gives each app.
void testMenuPatterns(const std::filesystem::path &charts) {
  // A user's own chart: a 2x2 PNG, one red cell.
  std::filesystem::create_directories(charts);
  writeChart(charts / "my-hearts.png");
  writeChart(charts / "bad name.png");
  writeChart(charts / "too-wide.png", 257, 1);
  hyprknit::chartsLoad(charts);

  std::vector<std::string> names;
  for (const auto &pattern : hyprknit::menuPatterns())
    names.push_back(pattern.name);
  const std::vector<std::string> expected{"zigzag",   "picnic",  "ribbon",
                                          "posy",     "twinkle", "candy-stripe",
                                          "my-hearts"};
  require(names == expected,
          "the menu lists the six featured patterns, then the user's charts");
  require(hyprknit::menuPatterns().front().title == "Zigzag" &&
              hyprknit::menuPatterns().back().title == "My Hearts",
          "featured patterns keep their names; a user chart is titled from its "
          "file");
  require(hyprknit::chartIndex("atelier-claude") >= 0,
          "the app colourways' charts are still loaded for By App");
  require(hyprknit::chartIndex("bad name") < 0,
          "chart names use the identifier syntax accepted by every client");
  require(hyprknit::chartIndex("too-wide") < 0,
          "oversized chart dimensions are rejected");
}

void testPatternSurvivesCollectionChanges(const std::filesystem::path &charts) {
  writeChart(charts / "a.png");
  writeChart(charts / "b.png");
  hyprknit::chartsLoad(charts);
  require(hyprknit::patternSelect("b"), "a custom chart can be selected");

  std::filesystem::remove(charts / "a.png");
  hyprknit::chartsLoad(charts);
  require(hyprknit::patternForApp("anything") == hyprknit::chartIndex("b"),
          "a selected chart follows its name when collection indices move");

  std::filesystem::remove(charts / "b.png");
  hyprknit::chartsLoad(charts);
  require(hyprknit::patternForApp("anything") == -1,
          "deleting the selected chart falls back to plain knitting");
}

} // namespace

int main() {
  // Keep the test off the developer's own collection and settings.
  const auto config = std::filesystem::temp_directory_path() /
                      ("hyprknit-tests-" + std::to_string(::getpid()));
  std::filesystem::create_directories(config / "hyprknit");
  setenv("XDG_CONFIG_HOME", config.c_str(), 1);

  hyprknit::chartsLoad(config / "hyprknit" / "charts");
  hyprknit::appsLoad();

  testChartCollection();
  testAppMatching();
  testFallbackYarn();
  testChartOrientation();
  testRingGeometry();
  testInvalidOutputGeometry();
  testPatternSelection();
  testUserRules(config);
  testMenuPatterns(config / "hyprknit" / "charts");
  testPatternSurvivesCollectionChanges(config / "hyprknit" / "charts");

  std::error_code ignored;
  std::filesystem::remove_all(config, ignored);
  if (g_failures == 0)
    std::cout << "all checks passed\n";
  return g_failures == 0 ? 0 : 1;
}
