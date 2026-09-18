#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hyprknit {

// A colourwork chart: one cell = one colour cell, like a paper knitting chart.
// Each cell renders as 2x2 fine stitches, or one defined stitch.
//
//   fully transparent cell -> the window's own yarn colour
//   opaque cell            -> that literal colour, knitted as a contrast yarn
//
// The chart's width and height are the pattern repeat, so an 8x8 chart is an
// 8-stitch by 8-row repeat that tiles around the whole frame. Row 0 is the
// outer edge of the band.
struct Chart {
  std::string name;
  int w = 0;
  int h = 0;
  std::vector<std::uint32_t> px; // ARGB, row 0 = top of the chart as authored
  bool solidCorners = false;     // use a solid yarn patch on the rounded arcs
  std::uint32_t cornerColor = 0; // zero follows the base yarn, else opaque ARGB
  std::uint32_t cuffColor =
      0;                  // optional curved outer cuff, one third of the band
  bool roundDots = false; // soften the 2x2 dot blocks into filled circles
  bool fittedRepeat = false; // symmetric charts fit complete repeats per edge
  bool sculptedYarn = false; // raised, path-rendered stitches, cached per tile
  bool definedYarn = false;  // larger, clearer stitches for flat colour blocks
  bool builtin = false;      // part of the collection, not a user's own PNG
};

/// The folder for user-authored PNG charts. Built-ins require no files.
std::filesystem::path chartsDir();

/// Reload the built-in collection, then sorted .png files in `dir`. A user PNG
/// with the same name replaces its built-in chart. Returns the total, even when
/// `dir` cannot be read.
int chartsLoad(const std::filesystem::path &dir);

/// Look up a chart by name (a PNG file name without .png). Returns index or -1.
int chartIndex(std::string_view name);

const std::vector<Chart> &charts();

/// Nullptr for an out-of-range index, so callers can pass -1 for plain
/// knitting.
const Chart *chartAt(int index);

/// A pattern the menu offers as one every window shares.
struct MenuPattern {
  std::string name;
  std::string title;
};

/// What the Pattern menu lists besides By App and plain knitting, as the Mac
/// app's does: its six featured patterns, then the user's own charts. The app
/// colourways' charts stay out of it: By App already gives each app its own,
/// and forcing one app's chart onto every window is not a pattern anyone means
/// to pick.
std::vector<MenuPattern> menuPatterns();

/// "candy-stripe" -> "Candy Stripe", as the menu titles a user's chart.
std::string chartTitle(std::string_view name);

} // namespace hyprknit
