#pragma once
#include <GfxRenderer.h>

#include "components/themes/BaseTheme.h"

namespace SemanticAppIcons {
// Uses the shipped preprocessed sizes; no runtime decoding or allocation.
void draw(const GfxRenderer& renderer, UIIcon icon, int x, int y, int size, int levels, bool inverted);
}  // namespace SemanticAppIcons
