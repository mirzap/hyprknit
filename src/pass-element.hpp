#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>

namespace hyprknit {

class KnitDecoration;

class KnitPassElement : public IPassElement {
public:
  explicit KnitPassElement(KnitDecoration *decoration, float alpha);

  std::vector<UP<IPassElement>> draw() override;
  bool needsLiveBlur() override;
  bool needsPrecomputeBlur() override;
  const char *passName() override;
  ePassElementType type() override;
  std::optional<CBox> boundingBox() override;

private:
  KnitDecoration *m_decoration = nullptr;
  float m_alpha = 1.F;
};

} // namespace hyprknit
