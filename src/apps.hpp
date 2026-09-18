#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hyprknit {

// Per-app colourways.
//
// Some apps deserve their own yarn regardless of which colour the window would
// otherwise be handed. Rules live in a plain text file the user owns:
//
//   Claude   = #C68468 atelier-claude
//   Spotify  = #3B6450 atelier-spotify
//
// The name on the left is matched as a case-insensitive prefix of the window's
// app id, so "Claude" also covers "Claude Helper". An optional chart name after
// the colour gives that app its own pattern too.
struct AppRule {
  std::string match;
  std::uint32_t color = 0;
  std::string chart; // empty = plain in By App mode
};

/// Path to apps.conf, created and seeded with defaults on first run.
std::filesystem::path appsPath();

/// (Re)read apps.conf. Returns the number of user rules loaded. Built-in app
/// colourways remain available independently of this file.
int appsLoad();

/// Longest-prefix user match, then built-in app match, or nullptr. `appId` is a
/// Wayland app id (the window class); reverse-DNS ids are also matched on their
/// final segment, so "org.mozilla.firefox" finds the Firefox colourway.
const AppRule *appRule(std::string_view appId);

/// Resolve the effective chart (-1 = plain), independently of the app's yarn.
/// By App uses the profile; a global selection overrides it.
int patternForApp(std::string_view appId);

/// Select "by-app", "none" (global plain), or a loaded chart (global pattern).
/// Returns false for an unknown name without changing the current selection.
bool patternSelect(std::string_view name);

bool patternByApp();
int activeChart();

/// Lowest stitch-row count that still shows the whole of the active pattern.
float minimumRows();

const std::vector<AppRule> &userRules();

/// The built-in colourways, in catalogue order. Several app ids share one
/// colourway, so entries repeat by design.
const std::vector<AppRule> &collectionRules();

} // namespace hyprknit
