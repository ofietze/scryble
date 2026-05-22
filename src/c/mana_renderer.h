#pragma once
#include <pebble.h>

// Draws mana cost symbols right-aligned within `bounds`.
// Returns the total pixel width consumed by the symbols.
int mana_renderer_draw_cost(GContext *ctx, const char *mana_cost,
                             GRect bounds, int symbol_diam);

// Returns pixel width a mana cost string will occupy (for layout calculations).
int mana_renderer_cost_width(const char *mana_cost, int symbol_diam);
