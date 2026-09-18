#include "pass-element.hpp"

#include "knit-deco.hpp"

#include <hyprland/src/render/Renderer.hpp>

namespace hyprknit {

KnitPassElement::KnitPassElement(KnitDecoration *decoration, float alpha)
    : m_decoration(decoration), m_alpha(alpha) {}

std::vector<UP<IPassElement>> KnitPassElement::draw() {
  m_decoration->renderPass(g_pHyprRenderer->m_renderData.pMonitor.lock(),
                           m_alpha);
  return {};
}

bool KnitPassElement::needsLiveBlur() { return false; }

bool KnitPassElement::needsPrecomputeBlur() { return false; }

const char *KnitPassElement::passName() { return "KnitPassElement"; }

ePassElementType KnitPassElement::type() { return EK_CUSTOM; }

std::optional<CBox> KnitPassElement::boundingBox() {
  return m_decoration->monitorLocalBox();
}

} // namespace hyprknit
