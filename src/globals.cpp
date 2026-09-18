#include "globals.hpp"

#include "apps.hpp"
#include "knit.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace hyprknit {

State state;

void applySettings(bool force) {
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
}

} // namespace hyprknit
