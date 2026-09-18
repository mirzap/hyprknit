#include "knit-deco.hpp"

#include "apps.hpp"
#include "globals.hpp"
#include "knit.hpp"
#include "pass-element.hpp"

#include <algorithm>
#include <cmath>
#include <drm_fourcc.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/gl/GLTexture.hpp>
#include <hyprutils/memory/Casts.hpp>

using namespace Hyprutils::Memory;
using namespace Render::GL;

namespace hyprknit {
namespace {

std::string appIdOf(const PHLWINDOW &window) {
  return window->m_class.empty() ? window->m_initialClass : window->m_class;
}

} // namespace

KnitDecoration::KnitDecoration(PHLWINDOW window)
    : IHyprWindowDecoration(window), m_window(window) {
  m_appId = appIdOf(window);
  pickYarn();
}

KnitDecoration::~KnitDecoration() { damageEntire(); }

// A per-app rule wins over the colour this window would otherwise be handed,
// and may carry its own pattern.
void KnitDecoration::pickYarn() {
  const auto *rule = appRule(m_appId);
  m_yarn = rule ? rule->color : colorForApp(m_appId);
  m_chart = patternForApp(m_appId);
}

SDecorationPositioningInfo KnitDecoration::getPositioningInfo() {
  const bool enabled = state.enabled && state.enabled->value();
  const int width =
      enabled ? std::max<Config::INTEGER>(state.width->value(), 1) : 0;

  SDecorationPositioningInfo info;
  info.policy = DECORATION_POSITION_STICKY;
  info.reserved = true;
  info.priority = 9980;
  info.edges = DECORATION_EDGE_TOP | DECORATION_EDGE_RIGHT |
               DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT;
  info.desiredExtents = {{width, width}, {width, width}};
  return info;
}

void KnitDecoration::onPositioningReply(
    const SDecorationPositioningReply &reply) {
  if (reply.assignedGeometry.size() != m_assignedGeometry.size())
    m_knitted = {};
  m_assignedGeometry = reply.assignedGeometry;
}

void KnitDecoration::draw(PHLMONITOR, const float &alpha) {
  const bool enabled = state.enabled && state.enabled->value();
  if (enabled != m_lastEnabled) {
    m_lastEnabled = enabled;
    g_pDecorationPositioner->repositionDeco(this);
    damageEntire();
  }
  if (!enabled || !validMapped(m_window))
    return;

  const auto window = m_window.lock();
  if (!window->m_ruleApplicator->decorate().valueOrDefault())
    return;

  g_pHyprRenderer->m_renderPass.add(makeUnique<KnitPassElement>(this, alpha));
}

void KnitDecoration::renderPass(PHLMONITOR monitor, float alpha) {
  if (!monitor || !validMapped(m_window))
    return;

  const auto window = m_window.lock();
  if (const auto appId = appIdOf(window); appId != m_appId) {
    m_appId = appId;
    pickYarn();
    m_knitted = {};
  }

  auto frame = assignedBoxGlobal();
  frame.translate(-monitor->m_position).scale(monitor->m_scale).round();
  if (frame.width < 1 || frame.height < 1)
    return;

  const int width = std::max(
      1, static_cast<int>(std::round(state.width->value() * monitor->m_scale)));
  if (frame.width <= width * 2 || frame.height <= width * 2)
    return;

  // The knit follows the window's own rounding: the band is the ring between
  // the window's rounded rect and that rect grown by the border width.
  const auto configured = state.rounding->value();
  const double windowRadius = configured >= 0 ? configured : window->rounding();

  const Signature wanted{
      .frameWidth = static_cast<int>(frame.width),
      .frameHeight = static_cast<int>(frame.height),
      .width = width,
      .radius = windowRadius * monitor->m_scale + width,
      .dim = Desktop::focusState()->isWindowActive(window)
                 ? 0.F
                 : std::clamp<float>(state.dim->value(), 0.F, 1.F),
      .generation = state.generation,
  };
  rebuildTextures(wanted);

  g_pHyprOpenGL->scissor(nullptr);
  CHyprOpenGLImpl::STextureRenderData renderData{};
  renderData.a = alpha;
  renderData.noAA = true;
  for (const auto &piece : m_pieces) {
    if (!piece.texture || !piece.texture->ok())
      continue;
    CBox box = piece.box;
    box.translate({frame.x, frame.y});
    g_pHyprOpenGL->renderTexture(piece.texture, box, renderData);
  }
}

void KnitDecoration::rebuildTextures(const Signature &wanted) {
  if (wanted == m_knitted)
    return;

  const Band band{
      .frameWidth = wanted.frameWidth,
      .frameHeight = wanted.frameHeight,
      .band = wanted.width,
      .radius = wanted.radius,
      .color = m_yarn,
      .chart = m_chart,
      .dim = wanted.dim,
  };

  // The corner squares hold the mitre, so the sides only cover what is left
  // between them. A square corner still needs a full border's worth of room.
  const double corner =
      std::clamp(std::ceil(std::max(wanted.radius, double(wanted.width))), 0.0,
                 std::min(wanted.frameWidth, wanted.frameHeight) / 2.0);
  const double thick = wanted.width;
  const double right = wanted.frameWidth - corner;
  const double bottom = wanted.frameHeight - corner;
  const double runX = wanted.frameWidth - corner * 2;
  const double runY = wanted.frameHeight - corner * 2;

  const std::array<CBox, 8> layout{
      CBox{corner, 0.0, runX, thick},                        // top
      CBox{wanted.frameWidth - thick, corner, thick, runY},  // right
      CBox{corner, wanted.frameHeight - thick, runX, thick}, // bottom
      CBox{0.0, corner, thick, runY},                        // left
      CBox{0.0, 0.0, corner, corner},                        // top left, then
      CBox{right, 0.0, corner, corner},                      // clockwise
      CBox{right, bottom, corner, corner},
      CBox{0.0, bottom, corner, corner},
  };

  Pixels pixels;
  for (std::size_t index = 0; index < layout.size(); ++index) {
    const auto &box = layout[index];
    m_pieces[index].box = box;
    if (box.width < 1 || box.height < 1) {
      m_pieces[index].texture.reset();
      continue;
    }

    pixels.width = static_cast<int>(box.width);
    pixels.height = static_cast<int>(box.height);
    renderBandRegion(band, static_cast<int>(box.x), static_cast<int>(box.y),
                     pixels);

    m_pieces[index].texture = makeShared<CGLTexture>(
        DRM_FORMAT_ABGR8888, pixels.rgba.data(), pixels.width * 4,
        Vector2D{double(pixels.width), double(pixels.height)}, false, false);
    m_pieces[index].texture->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_pieces[index].texture->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  }

  m_knitted = wanted;
}

eDecorationType KnitDecoration::getDecorationType() {
  return DECORATION_CUSTOM;
}

void KnitDecoration::updateWindow(PHLWINDOW) { damageEntire(); }

void KnitDecoration::damageEntire() {
  if (m_lastGlobalBox.width > 0 && m_lastGlobalBox.height > 0)
    g_pHyprRenderer->damageBox(m_lastGlobalBox.copy().expand(2));
}

uint64_t KnitDecoration::getDecorationFlags() {
  return DECORATION_PART_OF_MAIN_WINDOW;
}

eDecorationLayer KnitDecoration::getDecorationLayer() {
  return DECORATION_LAYER_OVER;
}

std::string KnitDecoration::getDisplayName() { return "Hyprknit"; }

CBox KnitDecoration::assignedBoxGlobal() const {
  if (!validMapped(m_window))
    return {};
  CBox box = m_assignedGeometry;
  box.translate(g_pDecorationPositioner->getEdgeDefinedPoint(
      DECORATION_EDGE_TOP | DECORATION_EDGE_RIGHT | DECORATION_EDGE_BOTTOM |
          DECORATION_EDGE_LEFT,
      m_window));
  const auto window = m_window.lock();
  const auto workspace = window->m_workspace;
  const auto workspaceOffset = workspace && !window->m_pinned
                                   ? workspace->m_renderOffset->value()
                                   : Vector2D();
  box.translate(window->m_floatingOffset + workspaceOffset);
  const_cast<KnitDecoration *>(this)->m_lastGlobalBox = box;
  return box;
}

CBox KnitDecoration::monitorLocalBox() const {
  if (!validMapped(m_window))
    return {};
  const auto window = m_window.lock();
  const auto monitor = window->m_monitor.lock();
  auto box = assignedBoxGlobal();
  if (monitor)
    box.translate(-monitor->m_position);
  return box;
}

void KnitDecoration::onConfigReloaded() {
  pickYarn();
  m_knitted = {};
  g_pDecorationPositioner->repositionDeco(this);
  damageEntire();
}

} // namespace hyprknit
