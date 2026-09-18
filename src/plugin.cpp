#define WLR_USE_UNSTABLE

#include "apps.hpp"
#include "chart.hpp"
#include "globals.hpp"
#include "knit-deco.hpp"
#include "knit.hpp"

#include <algorithm>
#include <expected>
#include <format>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/render/Renderer.hpp>

using namespace hyprknit;

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

namespace {

void attachDecoration(PHLWINDOW window) {
  if (!window || std::ranges::any_of(
                     window->m_windowDecorations, [](const auto &decoration) {
                       return decoration->getDisplayName() == "Hyprknit";
                     }))
    return;
  HyprlandAPI::addWindowDecoration(state.handle, window,
                                   makeUnique<KnitDecoration>(window));
}

void updateDecorations() {
  for (const auto &window : Desktop::windowState()->windows()) {
    for (const auto &decoration : window->m_windowDecorations) {
      if (decoration->getDisplayName() == "Hyprknit")
        static_cast<KnitDecoration *>(decoration.get())->onConfigReloaded();
    }
  }
}

/// Re-read the user's charts and apps.conf, and the settings built on them.
void reloadCollection() {
  chartsLoad(chartsDir());
  appsLoad();
  // A rebuilt collection moves the chart indices tiles are keyed on.
  flushTileCache();
  applySettings(true);
}

/// Run a callback Hyprland invoked. Nothing may throw back into Hyprland: an
/// exception escaping a signal or a hyprctl command terminates the whole
/// compositor, not just the plugin. Returns false if it did not complete.
template <typename Fn> bool safely(const char *what, Fn &&fn) noexcept {
  if (!state.alive)
    return false;
  try {
    fn();
    return true;
  } catch (const std::exception &error) {
    std::cerr << "[hyprknit] " << what << " failed: " << error.what() << '\n';
  } catch (...) {
    std::cerr << "[hyprknit] " << what << " failed\n";
  }
  return false;
}

/// Create a config value and register it in one step, so a new setting cannot
/// be declared and then quietly left unregistered.
template <typename Value, typename... Args> SP<Value> declare(Args &&...args) {
  auto value = makeShared<Value>(std::forward<Args>(args)...);
  HyprlandAPI::addConfigValueV2(state.handle, value);
  return value;
}

using Bool = Config::Values::CBoolValue;
using Int = Config::Values::CIntValue;
using Float = Config::Values::CFloatValue;
using Text = Config::Values::CStringValue;

/// The basket names, listed from the table itself so the two cannot drift.
std::string basketNames() {
  std::string names;
  for (const auto &basket : baskets())
    names += (names.empty() ? "" : ", ") + std::string(basket.name);
  return names;
}

/// A validator that accepts anything `resolve` maps to a non-negative index.
auto named(int (*resolve)(std::string_view), std::string expected) {
  return [resolve, expected = std::move(expected)](
             const Config::STRING &value) -> std::expected<void, std::string> {
    if (resolve(value) >= 0)
      return {};
    return std::unexpected("expected " + expected);
  };
}

} // namespace

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  state.handle = handle;
  state.alive = true;

  const std::string runtimeHash = __hyprland_api_get_hash();
  const std::string clientHash = __hyprland_api_get_client_hash();
  if (runtimeHash != clientHash) {
    HyprlandAPI::addNotification(handle,
                                 "[hyprknit] Hyprland/header version mismatch",
                                 CHyprColor{1.0, 0.2, 0.2, 1.0}, 6000);
    throw std::runtime_error(
        std::format("hyprknit: version mismatch (runtime {}, headers {})",
                    runtimeHash, clientHash));
  }

  chartsLoad(chartsDir());
  appsLoad();

  state.enabled = declare<Bool>("plugin:hyprknit:enabled",
                                "Knit a sweater onto every window", true);
  state.width =
      declare<Int>("plugin:hyprknit:width", "Border width in logical pixels",
                   12, Config::Values::SIntValueOptions{.min = 2, .max = 48});
  state.rows =
      declare<Int>("plugin:hyprknit:rows", "Stitch rows across the band", 6,
                   Config::Values::SIntValueOptions{.min = 2, .max = 24});
  state.rounding = declare<Int>(
      "plugin:hyprknit:rounding",
      "Window corner radius the knit follows, or -1 to use the window's own",
      -1, Config::Values::SIntValueOptions{.min = -1, .max = 64});
  state.dim = declare<Float>(
      "plugin:hyprknit:dim", "How far an unfocused window's band is darkened",
      0.3F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
  state.pattern = declare<Text>(
      "plugin:hyprknit:pattern",
      "by-app, none, or the name of a chart every window shares", "by-app",
      Config::Values::SStringValueOptions{
          .validator = [](const Config::STRING &value)
              -> std::expected<void, std::string> {
            if (value == "by-app" || value == "none" || chartIndex(value) >= 0)
              return {};
            return std::unexpected(
                "expected by-app, none, or a chart in the collection");
          }});
  state.stitch = declare<Text>(
      "plugin:hyprknit:stitch", "Plain knitting stitch", "stockinette",
      Config::Values::SStringValueOptions{
          .validator = named(stitchIndex, "stockinette, rib or garter")});
  state.basket =
      declare<Text>("plugin:hyprknit:basket",
                    "Basket of wool for apps without a colourway", "wool",
                    Config::Values::SStringValueOptions{
                        .validator = named(basketIndex, basketNames())});
  state.anchor = declare<Text>(
      "plugin:hyprknit:anchor", "Where each side's pattern repeat is cast on",
      "corner",
      Config::Values::SStringValueOptions{
          .validator = [](const Config::STRING &value)
              -> std::expected<void, std::string> {
            if (value == "corner" || value == "centre" || value == "center")
              return {};
            return std::unexpected("expected corner or centre");
          }});

  if (!HyprlandAPI::registerHyprCtlCommand(
          handle, SHyprCtlCommand{.name = "hyprknit-reload",
                                  .exact = true,
                                  .fn = [](eHyprCtlOutputFormat, std::string) {
                                    const bool ok = safely("reload", [] {
                                      reloadCollection();
                                      updateDecorations();
                                    });
                                    return std::string{ok ? "ok" : "error"};
                                  }}))
    throw std::runtime_error("hyprknit: could not register reload command");

  // The shared patterns the menu offers, as the Mac app's menu does: its
  // featured six, then the user's own charts. One "name<TAB>title" per line,
  // or a JSON array with -j.
  if (!HyprlandAPI::registerHyprCtlCommand(
          handle,
          SHyprCtlCommand{
              .name = "hyprknit-patterns",
              .exact = true,
              .fn = [](eHyprCtlOutputFormat format, std::string) {
                std::string out;
                safely("patterns", [&] {
                  const bool json = format == eHyprCtlOutputFormat::FORMAT_JSON;
                  for (const auto &[name, title] : menuPatterns()) {
                    if (json)
                      out += std::format(R"({}{{"name":"{}","title":"{}"}})",
                                         out.empty() ? "[" : ",", name, title);
                    else
                      out += (out.empty() ? "" : "\n") + name + "\t" + title;
                  }
                  if (json)
                    out = out.empty() ? "[]" : out + "]";
                });
                return out;
              }}))
    throw std::runtime_error("hyprknit: could not register patterns command");

  HyprlandAPI::reloadConfig();

  state.windowOpened =
      Event::bus()->m_events.window.open.listen([](PHLWINDOW window) {
        safely("window open", [&] { attachDecoration(window); });
      });
  state.configReloaded = Event::bus()->m_events.config.reloaded.listen([]() {
    safely("config reload", [] {
      applySettings(false, true);
      updateDecorations();
    });
  });

  for (const auto &window : Desktop::windowState()->windows()) {
    if (!window->isHidden() && window->m_isMapped)
      attachDecoration(window);
  }

  applySettings();
  HyprlandAPI::addNotification(
      handle, std::format("[hyprknit] {} sweaters cast on", charts().size()),
      CHyprColor{0.35, 0.85, 0.65, 1.0}, 4000);
  return {"hyprknit", "Knitted window borders", "Mirza", "0.2.4"};
}

APICALL EXPORT void PLUGIN_EXIT() {
  // Stop listening first. Unloading removes the plugin's config options and
  // reloads the config, and a listener that survived would read options that
  // no longer exist. Hyprland drops the decorations, config values and hyprctl
  // commands registered through the handle on its own.
  state.alive = false;
  state.windowOpened.reset();
  state.configReloaded.reset();
  restoreNativeBorder();
  g_pHyprRenderer->m_renderPass.removeAllOfType("KnitPassElement");
  flushTileCache();
}
