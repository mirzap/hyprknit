// Renders the collection the way the compositor does, straight to PNG files, so
// a change to the knitting can be looked at without a running Hyprland.
//
//   hyprknit-swatch [output-dir] [--width N] [--rows N] [--rounding N]
//                   [--size WxH] [--pattern name] [--stitch name] [--app id]

#include "apps.hpp"
#include "chart.hpp"
#include "knit.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>

namespace {

struct Options {
  std::filesystem::path out = "swatches";
  int width = 12;
  int rows = 6;
  int rounding = 10;
  int frameWidth = 420;
  int frameHeight = 260;
  std::string pattern = "by-app";
  std::string stitch = "stockinette";
  std::string app;
};

bool toInt(std::string_view text, int &value) {
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

std::string fileNameFor(std::string_view app) {
  std::string name;
  name.reserve(app.size());
  for (const unsigned char c : app) {
    if (std::isalnum(c) || c == '.' || c == '_' || c == '-')
      name.push_back(static_cast<char>(std::tolower(c)));
    else
      name.push_back('-');
  }
  if (name.empty() || name == "." || name == "..")
    return "swatch";
  return name;
}

// One window's worth of band, composited into a single straight-alpha image.
std::vector<std::uint8_t> renderWindow(const hyprknit::Band &band) {
  std::vector<std::uint8_t> image(
      static_cast<std::size_t>(band.frameWidth) * band.frameHeight * 4, 0u);
  hyprknit::Pixels pixels;
  pixels.width = band.frameWidth;
  pixels.height = band.frameHeight;
  hyprknit::renderBandRegion(band, 0, 0, pixels);

  for (std::size_t i = 0; i < image.size(); i += 4) {
    const auto alpha = pixels.rgba[i + 3];
    for (int channel = 0; channel < 3; ++channel)
      image[i + channel] = alpha ? static_cast<std::uint8_t>(
                                       pixels.rgba[i + channel] * 255 / alpha)
                                 : 0u;
    image[i + 3] = alpha;
  }
  return image;
}

bool writePng(const std::filesystem::path &path,
              const std::vector<std::uint8_t> &rgba, int width, int height) {
  GdkPixbuf *image =
      gdk_pixbuf_new_from_data(rgba.data(), GDK_COLORSPACE_RGB, TRUE, 8, width,
                               height, width * 4, nullptr, nullptr);
  if (!image)
    return false;
  GError *error = nullptr;
  const bool ok = gdk_pixbuf_save(image, path.c_str(), "png", &error, nullptr);
  if (error)
    g_error_free(error);
  g_object_unref(image);
  return ok;
}

} // namespace

int main(int argc, char **argv) {
  Options options;
  bool valid = true;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const auto next = [&]() -> std::string_view {
      return i + 1 < argc ? argv[++i] : "";
    };
    if (arg == "--width")
      valid = toInt(next(), options.width) && valid;
    else if (arg == "--rows")
      valid = toInt(next(), options.rows) && valid;
    else if (arg == "--rounding")
      valid = toInt(next(), options.rounding) && valid;
    else if (arg == "--pattern")
      options.pattern = next();
    else if (arg == "--stitch")
      options.stitch = next();
    else if (arg == "--app")
      options.app = next();
    else if (arg == "--size") {
      const std::string_view size = next();
      const auto cross = size.find('x');
      valid = cross != std::string_view::npos &&
              toInt(size.substr(0, cross), options.frameWidth) && valid;
      if (cross != std::string_view::npos)
        valid = toInt(size.substr(cross + 1), options.frameHeight) && valid;
    } else if (!arg.starts_with("--"))
      options.out = arg;
    else
      valid = false;
  }

  constexpr int kMaxFrameDimension = 8192;
  constexpr std::size_t kMaxPixels = 32u * 1024u * 1024u;
  const bool dimensionsValid =
      options.width > 0 && options.width <= 256 && options.rows > 0 &&
      options.rows <= 256 && options.rounding >= 0 &&
      options.rounding <= kMaxFrameDimension && options.frameWidth > 0 &&
      options.frameWidth <= kMaxFrameDimension && options.frameHeight > 0 &&
      options.frameHeight <= kMaxFrameDimension &&
      static_cast<std::size_t>(options.frameWidth) <=
          kMaxPixels / static_cast<std::size_t>(options.frameHeight);
  if (!valid || !dimensionsValid) {
    std::cerr << "invalid arguments; expected positive bounded dimensions and "
                 "--size WxH\n";
    return 2;
  }

  hyprknit::chartsLoad(hyprknit::chartsDir());
  hyprknit::appsLoad();
  if (!hyprknit::patternSelect(options.pattern)) {
    std::cerr << "unknown pattern: " << options.pattern << '\n';
    return 1;
  }
  hyprknit::Knitting knitting = hyprknit::knitting();
  if (const int stitch = hyprknit::stitchIndex(options.stitch); stitch >= 0)
    knitting.stitch = static_cast<hyprknit::Stitch>(stitch);
  else {
    std::cerr << "unknown stitch: " << options.stitch << '\n';
    return 1;
  }
  knitting.gauge.rows =
      std::max(static_cast<float>(options.rows), hyprknit::minimumRows());
  hyprknit::applyKnitting(knitting);

  std::error_code ignored;
  std::filesystem::create_directories(options.out, ignored);

  // Every app in the collection, or just the one the caller named. One
  // colourway can answer to several app ids, so it is rendered once, under the
  // first id that names it.
  std::vector<std::string> apps;
  if (!options.app.empty())
    apps.push_back(options.app);
  else {
    std::vector<std::size_t> seen;
    for (const auto &rule : hyprknit::collectionRules()) {
      const auto key = rule.color ^ std::hash<std::string>{}(rule.chart);
      if (std::ranges::find(seen, key) != seen.end())
        continue;
      seen.push_back(key);
      apps.push_back(rule.match);
    }
  }

  int written = 0;
  for (const auto &app : apps) {
    hyprknit::Band band;
    band.frameWidth = options.frameWidth;
    band.frameHeight = options.frameHeight;
    band.band = options.width;
    band.radius = options.rounding + options.width;
    const auto *rule = hyprknit::appRule(app);
    band.color = rule ? rule->color : hyprknit::colorForApp(app);
    band.chart = hyprknit::patternForApp(app);

    const std::string file = fileNameFor(app);
    const auto image = renderWindow(band);
    const auto path = options.out / (file + ".png");
    if (!writePng(path, image, band.frameWidth, band.frameHeight)) {
      std::cerr << "could not write " << path << '\n';
      return 1;
    }
    ++written;
  }
  std::cout << written << " swatches in " << options.out << '\n';
  return 0;
}
