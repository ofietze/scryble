#include <pebble.h>
#include "mana_renderer.h"
#include <string.h>

#define MAX_SYMBOLS  16
#define SYMBOL_GAP    2
#define RIGHT_MARGIN  6

// Parse "{1}{U}{W}" -> ["1","U","W"], returns count
static int parse_mana_cost(const char *cost, char syms[MAX_SYMBOLS][8]) {
  int count = 0;
  const char *p = cost;
  while (*p && count < MAX_SYMBOLS) {
    if (*p == '{') {
      p++;
      int j = 0;
      while (*p && *p != '}' && j < 7) syms[count][j++] = *p++;
      syms[count][j] = '\0';
      if (*p == '}') p++;
      count++;
    } else {
      p++;
    }
  }
  return count;
}

static GColor fill_for_symbol(const char *sym) {
  if (sym[0] == 'W' && sym[1] == '\0') return GColorWhite;
  if (sym[0] == 'U' && sym[1] == '\0') return GColorVividCerulean;
  if (sym[0] == 'B' && sym[1] == '\0') return GColorBlack;
  if (sym[0] == 'R' && sym[1] == '\0') return GColorRed;
  if (sym[0] == 'G' && sym[1] == '\0') return GColorIslamicGreen;
  return GColorLightGray; // numbers, X, C, hybrid, etc.
}

static GColor text_for_fill(GColor fill) {
  // Light backgrounds need dark text
  if (gcolor_equal(fill, GColorWhite) || gcolor_equal(fill, GColorLightGray))
    return GColorBlack;
  return GColorWhite;
}

// Draw a single mana symbol centered at `center` with radius `r`
static void draw_symbol(GContext *ctx, const char *sym, GPoint center, int r) {
  GColor fill = fill_for_symbol(sym);
  GColor text = text_for_fill(fill);

  // Filled circle
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, center, r);

  // Border — slightly darker ring for contrast
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_circle(ctx, center, r);

  // Label inside the circle
  // Use the symbol text but abbreviate long hybrid (e.g. "W/U" -> show "W")
  char label[4] = {0};
  if (sym[0] != '\0' && (sym[1] == '/' || sym[1] == '\0')) {
    label[0] = sym[0];
  } else {
    // Generic number — use as-is (up to 2 chars so "10","11"... fit)
    strncpy(label, sym, 3);
  }

  graphics_context_set_text_color(ctx, text);
  int font_size = (r >= 7) ? 14 : 9;
  GFont font = fonts_get_system_font(
      font_size >= 14 ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_09);

  // Vertically center the glyph inside the circle
  int text_h = font_size + 2;
  GRect text_rect = GRect(center.x - r, center.y - text_h / 2, r * 2, text_h);
  graphics_draw_text(ctx, label, font, text_rect,
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

int mana_renderer_cost_width(const char *mana_cost, int symbol_diam) {
  if (!mana_cost || mana_cost[0] == '\0') return 0;
  char syms[MAX_SYMBOLS][8];
  int n = parse_mana_cost(mana_cost, syms);
  if (n == 0) return 0;
  return n * symbol_diam + (n - 1) * SYMBOL_GAP;
}

int mana_renderer_draw_cost(GContext *ctx, const char *mana_cost,
                             GRect bounds, int symbol_diam) {
  if (!mana_cost || mana_cost[0] == '\0') return 0;

  char syms[MAX_SYMBOLS][8];
  int n = parse_mana_cost(mana_cost, syms);
  if (n == 0) return 0;

  int total_w = n * symbol_diam + (n - 1) * SYMBOL_GAP;
  int r       = symbol_diam / 2;
  int y_center = bounds.origin.y + bounds.size.h / 2;

  // Start x: right edge of bounds minus margin, moving left
  int x = bounds.origin.x + bounds.size.w - r - RIGHT_MARGIN;

  for (int i = n - 1; i >= 0; i--) {
    draw_symbol(ctx, syms[i], GPoint(x, y_center), r);
    x -= (symbol_diam + SYMBOL_GAP);
  }

  return total_w + RIGHT_MARGIN;
}
