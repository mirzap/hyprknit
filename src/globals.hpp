#pragma once

#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/signal/Listener.hpp>

namespace hyprknit {

struct State {
  HANDLE handle = nullptr;
  SP<Config::Values::CBoolValue> enabled;
  SP<Config::Values::CIntValue> width;
  SP<Config::Values::CIntValue> rows;
  SP<Config::Values::CIntValue> rounding;
  SP<Config::Values::CFloatValue> dim;
  SP<Config::Values::CStringValue> pattern;
  SP<Config::Values::CStringValue> stitch;
  SP<Config::Values::CStringValue> basket;
  SP<Config::Values::CStringValue> anchor;
  /// Bumped whenever the gauge, stitch, basket or pattern changes, so every
  /// decoration knows its cached knitting is stale.
  unsigned generation = 0;

  /// Compositor events the plugin listens to. These are not tied to the
  /// plugin's handle, so Hyprland does not drop them on unload: PLUGIN_EXIT
  /// must, or they fire into a plugin whose settings no longer exist.
  Hyprutils::Signal::CHyprSignalListener windowOpened;
  Hyprutils::Signal::CHyprSignalListener configReloaded;

  /// False from the moment PLUGIN_EXIT starts. Anything still reaching the
  /// plugin after that, such as work Hyprland deferred before the unload,
  /// returns at once.
  bool alive = false;
};

extern State state;

/// Re-read the settings that live outside the per-window geometry, flush the
/// tile cache if any of them moved, and bump `generation` when they did. Pass
/// `force` after the chart collection has been rebuilt, when a setting that has
/// not itself changed may now resolve differently. `configReloaded` preserves
/// the newly loaded native border value before hiding it again.
void applySettings(bool force = false, bool configReloaded = false);

/// Put Hyprland's native border back if Hyprknit temporarily hid it.
void restoreNativeBorder() noexcept;

} // namespace hyprknit
