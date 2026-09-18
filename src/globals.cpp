#include "globals.hpp"

#include "apps.hpp"
#include "knit.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <typeinfo>
#include <utility>

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace hyprknit {

namespace {

std::optional<Config::INTEGER> savedNativeBorder;
bool nativeBorderHidden = false;

std::optional<Config::INTEGER> nativeBorderSize() {
  const auto option = Config::mgr()->getConfigValue("general:border_size");
  if (!option.dataptr || option.type != &typeid(Config::INTEGER) ||
      !*option.dataptr)
    return std::nullopt;
  return *static_cast<const Config::INTEGER *>(*option.dataptr);
}

bool setNativeBorderSize(Config::INTEGER size) {
  const auto result = HyprlandAPI::invokeHyprctlCommand(
      "eval",
      std::format("hl.config({{ general = {{ border_size = {} }} }})", size));
  return result.starts_with("ok");
}

void syncNativeBorder(bool configReloaded) {
  if (!state.enabled || !state.enabled->value()) {
    restoreNativeBorder();
    return;
  }

  const auto current = nativeBorderSize();
  if (!current)
    return;

  // A config reload may install a new user value while knitting is active.
  // Remember that value before hiding it again, so unload restores the latest
  // setting rather than the value seen when the plugin first loaded.
  if (configReloaded || !nativeBorderHidden || *current != 0)
    savedNativeBorder = *current;

  if (*current == 0 || setNativeBorderSize(0))
    nativeBorderHidden = true;
}

} // namespace

State state;

void restoreNativeBorder() noexcept {
  try {
    if (!nativeBorderHidden)
      return;

    const auto current = nativeBorderSize();
    // If something else replaced zero while knitting was active, it already
    // owns the live setting and must not be overwritten with our saved value.
    if (current && *current == 0 && savedNativeBorder &&
        *savedNativeBorder != 0)
      setNativeBorderSize(*savedNativeBorder);
  } catch (...) {
    // Nothing may escape PLUGIN_EXIT into the compositor.
  }

  nativeBorderHidden = false;
  savedNativeBorder.reset();
}

void applySettings(bool force, bool configReloaded) {
  // An unknown pattern name leaves the current selection alone, which is what
  // the validator already rejected it for.
  const auto before = std::pair{patternByApp(), activeChart()};
  patternSelect(state.pattern->value());
  const bool patternMoved = before != std::pair{patternByApp(), activeChart()};

  Knitting next = knitting();
  if (const int stitch = stitchIndex(state.stitch->value()); stitch >= 0)
    next.stitch = static_cast<Stitch>(stitch);
  if (const int basket = basketIndex(state.basket->value()); basket >= 0)
    next.basket = basket;
  const std::string anchor = state.anchor->value();
  next.anchor = anchor == "corner" ? Anchor::Corner : Anchor::Centre;
  // Colourwork needs enough stitch rows to show the whole of its chart, so the
  // configured gauge is a floor rather than an exact count.
  next.gauge.rows =
      std::max(static_cast<float>(state.rows->value()), minimumRows());

  const bool yarnMoved = applyKnitting(next);
  if (yarnMoved || patternMoved || force)
    ++state.generation;

  syncNativeBorder(configReloaded);
}

} // namespace hyprknit
