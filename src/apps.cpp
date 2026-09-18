#include "apps.hpp"

#include "chart.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <span>
#include <string>
#include <system_error>

namespace hyprknit {
namespace {

std::vector<AppRule> g_userRules;
bool g_patternByApp = true;
std::string g_activePattern;

struct BuiltinRule {
  const char *match;
  std::uint32_t color;
  const char *chart;
};

// The collection stays available even when apps.conf is absent or unreadable.
// User rules are evaluated first, so personal colourways always take priority.
//
// Wayland app ids are not macOS process names, so every colourway also lists
// the ids its Linux build reports. The colourways themselves are unchanged.
constexpr BuiltinRule k_collection[] = {
    {"Finder", 0xff258de0u, "atelier-finder"},
    {"Microsoft Teams", 0xff9283c1u, "atelier-teams"},
    {"MSTeams", 0xff9283c1u, "atelier-teams"},
    {"Teams", 0xff9283c1u, "atelier-teams"},
    {"teams-for-linux", 0xff9283c1u, "atelier-teams"},
    {"Claude", 0xffd58561u, "atelier-claude"},
    {"Codex", 0xff0a84ffu, "atelier-chatgpt"},
    {"Spotify", 0xff497641u, "atelier-spotify"},
    {"com.spotify.Client", 0xff497641u, "atelier-spotify"},
    {"Notion", 0xfff5f5f2u, "atelier-notion"},
    {"notion-app", 0xfff5f5f2u, "atelier-notion"},
    {"WhatsApp", 0xff6ea77bu, "atelier-whatsapp"},
    {"Figma", 0xffad7197u, "atelier-figma"},
    {"figma-linux", 0xffad7197u, "atelier-figma"},
    {"Google Chrome", 0xfff4f0e6u, "atelier-chrome"},
    {"Chrome", 0xfff4f0e6u, "atelier-chrome"},
    {"google-chrome", 0xfff4f0e6u, "atelier-chrome"},
    {"Paper", 0xff83ade8u, "atelier-paper"},
    {"Chromium", 0xfff4f0e6u, "atelier-chrome"},
    {"Safari", 0xff268ed8u, "atelier-safari"},
    {"epiphany", 0xff268ed8u, "atelier-safari"},
    {"Firefox", 0xff643a9au, "atelier-firefox"},
    {"org.mozilla.firefox", 0xff643a9au, "atelier-firefox"},
    {"librewolf", 0xff643a9au, "atelier-firefox"},
    {"zen", 0xff643a9au, "atelier-firefox"},
    {"Cursor", 0xff26251eu, "atelier-cursor"},
    {"Slack", 0xff542a52u, "atelier-slack"},
    {"zoom.us", 0xff2877ebu, "atelier-zoom"},
    {"Zoom", 0xff2877ebu, "atelier-zoom"},
    {"us.zoom.Zoom", 0xff2877ebu, "atelier-zoom"},
    {"Telegram", 0xff389eceu, "atelier-telegram"},
    {"org.telegram.desktop", 0xff389eceu, "atelier-telegram"},
    {"Messages", 0xff55af51u, "atelier-messages"},
    {"Mail", 0xff2986ceu, "atelier-mail"},
    {"Notes", 0xfff6f0d9u, "atelier-notes"},
    {"Calendar", 0xfff7f3e9u, "atelier-calendar"},
    {"Reminders", 0xfff6f3ebu, "atelier-reminders"},
    {"Music", 0xffe64e70u, "atelier-music"},
    {"Apple Music", 0xffe64e70u, "atelier-music"},
    {"Photos", 0xfff8f0dbu, "atelier-photos"},
    {"Preview", 0xff597bafu, "atelier-preview"},
    {"Microsoft Word", 0xff2855a1u, "atelier-word"},
    {"Microsoft Excel", 0xff28674fu, "atelier-excel"},
    {"Microsoft PowerPoint", 0xffb9573du, "atelier-powerpoint"},
    {"Microsoft Outlook", 0xff176bb7u, "atelier-outlook"},
    {"Code", 0xff237cafu, "atelier-vscode"},
    {"Visual Studio Code", 0xff237cafu, "atelier-vscode"},
    {"code-oss", 0xff237cafu, "atelier-vscode"},
    {"VSCodium", 0xff237cafu, "atelier-vscode"},
    {"Adobe Photoshop", 0xff182f45u, "atelier-photoshop"},
    {"Adobe Illustrator", 0xff4e3029u, "atelier-illustrator"},
    {"ChatGPT", 0xff0a84ffu, "atelier-chatgpt"},
    {"Grok Bot", 0xff34383bu, "atelier-grok"},
    {"Discord", 0xff5865f2u, "checker"},
    {"vesktop", 0xff5865f2u, "checker"},
    {"WebCord", 0xff5865f2u, "checker"},
    {"Granola", 0xff292e2au, "atelier-granola"},
    {"Terminal", 0xff303c35u, "atelier-terminal"},
    {"ghostty", 0xff2b3350u, "atelier-ghostty"},
    {"com.mitchellh.ghostty", 0xff2b3350u, "atelier-ghostty"},
};

constexpr const char *k_default_conf =
    "# Hyprknit — per-app colourways\n"
    "#\n"
    "# <app id> = #RRGGBB [chart]\n"
    "#\n"
    "# The name is matched as a case-insensitive prefix of the window's "
    "Wayland\n"
    "# app id, so \"Claude\" also covers \"Claude Helper\". A reverse-DNS id "
    "is\n"
    "# also matched on its last segment, so \"ghostty\" covers\n"
    "# \"com.mitchellh.ghostty\". The optional chart name is a built-in chart "
    "or\n"
    "# any file in the charts folder, without .png.\n"
    "# Personal rules take priority over the built-in app collection.\n"
    "#\n"
    "# Run `hyprknitctl reload` to load your changes.\n"
    "\n"
    "# Apps in the collection already have their own yarn and chart; see\n"
    "# the collection list in README.md. Uncomment to customize:\n"
    "# Claude = #D58561 atelier-claude\n";

bool startsWithNoCase(std::string_view haystack, std::string_view needle) {
  if (needle.size() > haystack.size())
    return false;
  for (std::size_t i = 0; i < needle.size(); ++i) {
    const auto a = static_cast<unsigned char>(haystack[i]);
    const auto b = static_cast<unsigned char>(needle[i]);
    if (std::tolower(a) != std::tolower(b))
      return false;
  }
  return true;
}

// A Wayland app id is often reverse-DNS, and its last segment is the app's own
// name, which is what the collection is keyed on. Both spellings are tried.
std::array<std::string_view, 2> spellings(std::string_view appId) {
  const auto dot = appId.rfind('.');
  const bool reverseDns =
      dot != std::string_view::npos && dot + 1 < appId.size();
  return {appId, reverseDns ? appId.substr(dot + 1) : std::string_view{}};
}

/// The longest rule in `rules` whose name is a case-insensitive prefix of the
/// app id, or nullptr. Rules shorter than `best` cannot win, so they are not
/// even compared.
const AppRule *longestMatch(std::span<const AppRule> rules,
                            std::span<const std::string_view> names) {
  const AppRule *best = nullptr;
  std::size_t bestLength = 0;
  for (const auto &rule : rules) {
    if (rule.match.size() <= bestLength)
      continue;
    if (std::ranges::any_of(names, [&rule](std::string_view name) {
          return !name.empty() && startsWithNoCase(name, rule.match);
        })) {
      best = &rule;
      bestLength = rule.match.size();
    }
  }
  return best;
}

std::string_view trim(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())))
    value.remove_prefix(1);
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())))
    value.remove_suffix(1);
  return value;
}

} // namespace

std::filesystem::path appsPath() {
  std::filesystem::path base;
  if (const char *config = std::getenv("XDG_CONFIG_HOME"); config && *config)
    base = config;
  else if (const char *home = std::getenv("HOME"); home && *home)
    base = std::filesystem::path(home) / ".config";
  else
    return {};

  const auto dir = base / "hyprknit";
  std::error_code ignored;
  std::filesystem::create_directories(dir, ignored);
  const auto path = dir / "apps.conf";
  if (!std::filesystem::exists(path, ignored)) {
    if (std::ofstream seed(path); seed) // seed it the first time
      seed << k_default_conf;
  }
  return path;
}

int appsLoad() {
  g_userRules.clear();
  const auto path = appsPath();
  if (path.empty())
    return 0;
  std::ifstream input(path);
  if (!input)
    return 0;

  std::string line;
  while (std::getline(input, line) && g_userRules.size() < 64) {
    auto text = trim(line);
    if (text.empty() || text.front() == '#')
      continue;
    const auto equals = text.find('=');
    if (equals == std::string_view::npos)
      continue;

    const auto name = trim(text.substr(0, equals));
    auto value = trim(text.substr(equals + 1));
    if (name.empty() || name.size() >= 64 || value.size() < 7 ||
        value.front() != '#')
      continue;

    // Require exactly six hex digits followed by whitespace or end of line, so
    // a short or malformed value is rejected rather than half parsed.
    if (!std::all_of(value.begin() + 1, value.begin() + 7, [](char c) {
          return std::isxdigit(static_cast<unsigned char>(c)) != 0;
        }))
      continue;
    if (value.size() > 7 && !std::isspace(static_cast<unsigned char>(value[7])))
      continue;

    const auto rgb =
        std::strtoul(std::string(value.substr(1, 6)).c_str(), nullptr, 16);
    auto tail = trim(value.substr(std::min<std::size_t>(value.size(), 7)));
    std::string chart;
    if (!tail.empty() && tail.front() != '#') {
      const auto end = tail.find_first_of(" \t");
      chart = std::string(tail.substr(0, end));
      if (chart.size() >= 64)
        continue;
      if (end != std::string_view::npos) {
        tail = trim(tail.substr(end));
        if (!tail.empty() && tail.front() != '#')
          continue;
      }
    }

    g_userRules.push_back({std::string(name),
                           0xff000000u | static_cast<std::uint32_t>(rgb),
                           std::move(chart)});
  }
  return static_cast<int>(g_userRules.size());
}

const AppRule *appRule(std::string_view appId) {
  if (appId.empty())
    return nullptr;
  const auto names = spellings(appId);
  // Personal rules are resolved first, so they always win outright.
  if (const AppRule *user = longestMatch(g_userRules, names))
    return user;
  return longestMatch(collectionRules(), names);
}

int patternForApp(std::string_view appId) {
  if (!g_patternByApp)
    return chartIndex(g_activePattern);
  const auto *rule = appRule(appId);
  return rule && !rule->chart.empty() ? chartIndex(rule->chart) : -1;
}

bool patternSelect(std::string_view name) {
  if (name == "by-app") {
    g_patternByApp = true;
    return true;
  }
  if (name == "none") {
    g_activePattern.clear();
    g_patternByApp = false;
    return true;
  }
  const int index = chartIndex(name);
  if (index < 0)
    return false;
  g_activePattern = name;
  g_patternByApp = false;
  return true;
}

bool patternByApp() { return g_patternByApp; }

int activeChart() { return chartIndex(g_activePattern); }

float minimumRows() {
  if (g_patternByApp)
    return 6.F;
  if (const auto *chart = chartAt(activeChart()))
    return static_cast<float>(std::max(6, chart->h));
  return 3.F;
}

const std::vector<AppRule> &userRules() { return g_userRules; }

const std::vector<AppRule> &collectionRules() {
  static const std::vector<AppRule> builtins = [] {
    std::vector<AppRule> rules;
    rules.reserve(std::size(k_collection));
    for (const auto &rule : k_collection)
      rules.push_back({rule.match, rule.color, rule.chart});
    return rules;
  }();
  return builtins;
}

} // namespace hyprknit
