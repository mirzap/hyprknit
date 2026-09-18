#pragma once

#define WLR_USE_UNSTABLE

#include <array>
#include <cstdint>
#include <string>

#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>

namespace hyprknit {

class KnitDecoration : public IHyprWindowDecoration {
public:
  explicit KnitDecoration(PHLWINDOW window);
  ~KnitDecoration() override;

  SDecorationPositioningInfo getPositioningInfo() override;
  void onPositioningReply(const SDecorationPositioningReply &reply) override;
  void draw(PHLMONITOR monitor, const float &alpha) override;
  eDecorationType getDecorationType() override;
  void updateWindow(PHLWINDOW window) override;
  void damageEntire() override;
  uint64_t getDecorationFlags() override;
  eDecorationLayer getDecorationLayer() override;
  std::string getDisplayName() override;

  void renderPass(PHLMONITOR monitor, float alpha);
  CBox monitorLocalBox() const;
  void onConfigReloaded();

private:
  // The eight pieces a knitted frame is made of: four sides, four mitred
  // corners. Anything the ring does not cover stays transparent.
  struct Piece {
    CBox box; // frame-local, device pixels
    SP<Render::ITexture> texture;
  };

  // Everything the knitting depends on. The textures are rebuilt when, and only
  // when, this changes.
  struct Signature {
    int frameWidth = -1;
    int frameHeight = -1;
    int width = -1;
    double radius = -1.0;
    float dim = -1.F;
    unsigned generation = ~0u;

    friend bool operator==(const Signature &, const Signature &) = default;
  };

  void pickYarn();
  void rebuildTextures(const Signature &wanted);
  CBox assignedBoxGlobal() const;

  PHLWINDOWREF m_window;
  CBox m_assignedGeometry;
  CBox m_lastGlobalBox;
  std::array<Piece, 8> m_pieces;
  std::string m_appId;
  std::uint32_t m_yarn = 0xff000000u;
  int m_chart = -1;
  Signature m_knitted;
  bool m_lastEnabled = false;
};

} // namespace hyprknit
