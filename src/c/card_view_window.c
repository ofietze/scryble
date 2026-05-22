#include <pebble.h>
#include "card_view_window.h"
#include "mana_renderer.h"
#include "comm.h"
#include "state.h"

#define MANA_DIAM  18
#define PT_W       46
#define PT_H       22
#define ART_W      200
#define ART_H       75

// Computed dynamically in window_load from the actual screen size
static int s_w, s_h;
static int s_header_h, s_art_h, s_type_h, s_scroll_y, s_scroll_h;

static Window      *s_window;
static Layer       *s_header_layer;
static Layer       *s_art_layer;
static TextLayer   *s_type_layer;
static ScrollLayer *s_scroll_layer;
static TextLayer   *s_text_layer;
static Layer       *s_pt_layer;
static TextLayer   *s_loading_layer;

static bool      s_loaded     = false;
static bool      s_art_loaded = false;
static GBitmap  *s_art_bmp   = NULL;

// ── Color helpers ─────────────────────────────────────────────────────────────
static GColor card_bg_color(void) {
  const char *c = g_state.card.colors;
  int len = (int)strlen(c);
  if (len == 0) return GColorLightGray;
  if (len > 1)  return GColorChromeYellow;
  switch (c[0]) {
    case 'W': return GColorWhite;
    case 'U': return GColorVividCerulean;
    case 'B': return GColorBlack;
    case 'R': return GColorRed;
    case 'G': return GColorIslamicGreen;
    default:  return GColorLightGray;
  }
}

static GColor card_text_color(GColor bg) {
  if (gcolor_equal(bg, GColorWhite) ||
      gcolor_equal(bg, GColorChromeYellow) ||
      gcolor_equal(bg, GColorLightGray))
    return GColorBlack;
  return GColorWhite;
}

// ── Header layer ──────────────────────────────────────────────────────────────
static void header_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = card_bg_color();
  GColor fg = card_text_color(bg);

  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (!s_loaded) return;

  int mana_w = mana_renderer_draw_cost(ctx, g_state.card.mana_cost, b, MANA_DIAM);
  int name_w = b.size.w - mana_w - (mana_w > 0 ? 6 : 4) - 4;
  GRect name_rect = GRect(4, (b.size.h - 26) / 2, name_w, 26);
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, g_state.card.name,
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     name_rect, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
}

// ── Art layer ─────────────────────────────────────────────────────────────────
static void art_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);

  if (s_art_loaded && s_art_bmp) {
    graphics_draw_bitmap_in_rect(ctx, s_art_bmp, b);
    return;
  }

  // Placeholder while image is loading (or on iOS where Canvas is unavailable)
  GColor bg = card_bg_color();
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  graphics_context_set_stroke_color(ctx,
      gcolor_equal(bg, GColorBlack) ? GColorDarkGray : GColorBlack);
  for (int x = -b.size.h; x < b.size.w; x += 12) {
    graphics_draw_line(ctx, GPoint(x, 0), GPoint(x + b.size.h, b.size.h));
  }

  graphics_context_set_text_color(ctx, card_text_color(bg));
  graphics_draw_text(ctx, "(art)", fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     b, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// ── Image chunk callback ──────────────────────────────────────────────────────
static void on_image_chunk(uint32_t offset, const uint8_t *data,
                            uint16_t len, bool done) {
  if (!s_art_bmp) {
    s_art_bmp = gbitmap_create_blank(GSize(ART_W, ART_H), GBitmapFormat8Bit);
    if (!s_art_bmp) return;
  }

  uint16_t stride = gbitmap_get_bytes_per_row(s_art_bmp);
  uint8_t *bmp    = gbitmap_get_data(s_art_bmp);

  if (stride == ART_W) {
    // Fast path: no row padding
    if (offset + len <= (uint32_t)(ART_W * ART_H))
      memcpy(bmp + offset, data, len);
  } else {
    for (uint16_t i = 0; i < len; i++) {
      uint32_t idx = offset + i;
      int y = idx / ART_W, x = idx % ART_W;
      if (y < ART_H) bmp[y * stride + x] = data[i];
    }
  }

  if (done) {
    s_art_loaded = true;
    layer_mark_dirty(s_art_layer);
  }
}

// ── P/T overlay ───────────────────────────────────────────────────────────────
static void pt_draw(Layer *layer, GContext *ctx) {
  if (!s_loaded || g_state.card.pt[0] == '\0') return;

  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 4, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_round_rect(ctx, b, 4);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, g_state.card.pt,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(0, 1, b.size.w, b.size.h),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// ── Data callbacks ────────────────────────────────────────────────────────────
void card_view_window_on_card(void) {
  s_loaded = true;
  // Reset art for the new card
  s_art_loaded = false;
  if (s_art_bmp) { gbitmap_destroy(s_art_bmp); s_art_bmp = NULL; }
  layer_mark_dirty(s_art_layer);
  layer_set_hidden(text_layer_get_layer(s_loading_layer), true);

  static char oracle_buf[530];
  bool is_creature = strstr(g_state.card.type_line, "Creature") != NULL;
  snprintf(oracle_buf, sizeof(oracle_buf), "%s%s",
           g_state.card.oracle_text,
           is_creature ? "\n\n\n\n" : "\n\n");
  text_layer_set_text(s_text_layer, oracle_buf);

  // Expand height before measuring so content_size isn't capped
  layer_set_frame(text_layer_get_layer(s_text_layer), GRect(4, 2, s_w - 8, 4000));
  GSize text_size = text_layer_get_content_size(s_text_layer);
  GRect text_frame = GRect(4, 2, s_w - 8, text_size.h + 4);
  layer_set_frame(text_layer_get_layer(s_text_layer), text_frame);
  // Content height must include the text layer's y offset
  scroll_layer_set_content_size(s_scroll_layer,
                                GSize(s_w, text_frame.origin.y + text_frame.size.h));

  text_layer_set_text(s_type_layer, g_state.card.type_line);
  layer_set_hidden(s_pt_layer, g_state.card.pt[0] == '\0');

  layer_mark_dirty(s_header_layer);
  layer_mark_dirty(s_art_layer);
}

void card_view_window_on_error(const char *msg) {
  s_loaded = false;
  text_layer_set_text(s_loading_layer, msg);
  layer_set_hidden(text_layer_get_layer(s_loading_layer), false);
}

static void window_appear(Window *w) {
  comm_set_card_callback(card_view_window_on_card);
  comm_set_error_callback(card_view_window_on_error);
  comm_set_image_callback(on_image_chunk);
}

// ── Window lifecycle ──────────────────────────────────────────────────────────
static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  s_w = bounds.size.w;
  s_h = bounds.size.h;
  s_loaded = false;

  // Proportional section heights relative to actual screen
  s_header_h = s_h * 15 / 100;   // ~15%
  s_art_h    = s_h * 33 / 100;   // ~33%
  s_type_h   = s_h * 11 / 100;   // ~11%
  s_scroll_y = s_header_h + s_art_h + s_type_h;
  s_scroll_h = s_h - s_scroll_y;

  s_header_layer = layer_create(GRect(0, 0, s_w, s_header_h));
  layer_set_update_proc(s_header_layer, header_draw);
  layer_add_child(root, s_header_layer);

  s_art_layer = layer_create(GRect(0, s_header_h, s_w, s_art_h));
  layer_set_update_proc(s_art_layer, art_draw);
  layer_add_child(root, s_art_layer);

  s_type_layer = text_layer_create(GRect(4, s_header_h + s_art_h, s_w - 8, s_type_h));
  text_layer_set_font(s_type_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_overflow_mode(s_type_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_text(s_type_layer, "");
  layer_add_child(root, text_layer_get_layer(s_type_layer));

  s_scroll_layer = scroll_layer_create(GRect(0, s_scroll_y, s_w, s_scroll_h));
  scroll_layer_set_click_config_onto_window(s_scroll_layer, w);
  scroll_layer_set_content_size(s_scroll_layer, GSize(s_w, s_scroll_h));

  s_text_layer = text_layer_create(GRect(4, 2, s_w - 8, s_scroll_h));
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_overflow_mode(s_text_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_text_layer, "");
  scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_text_layer));
  layer_add_child(root, scroll_layer_get_layer(s_scroll_layer));

  // P/T pinned flush in the bottom-right corner
  s_pt_layer = layer_create(GRect(s_w - PT_W, s_h - PT_H, PT_W, PT_H));
  layer_set_update_proc(s_pt_layer, pt_draw);
  layer_set_hidden(s_pt_layer, true);
  layer_add_child(root, s_pt_layer);

  s_loading_layer = text_layer_create(GRect(0, s_scroll_y, s_w, s_scroll_h));
  text_layer_set_text(s_loading_layer, "Loading...");
  text_layer_set_text_alignment(s_loading_layer, GTextAlignmentCenter);
  text_layer_set_font(s_loading_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  layer_add_child(root, text_layer_get_layer(s_loading_layer));
}

static void window_unload(Window *w) {
  layer_destroy(s_header_layer);
  layer_destroy(s_art_layer);
  text_layer_destroy(s_type_layer);
  text_layer_destroy(s_text_layer);
  scroll_layer_destroy(s_scroll_layer);
  layer_destroy(s_pt_layer);
  text_layer_destroy(s_loading_layer);
  if (s_art_bmp) { gbitmap_destroy(s_art_bmp); s_art_bmp = NULL; }
  s_loaded = s_art_loaded = false;
}

void card_view_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load   = window_load,
      .appear = window_appear,
      .unload = window_unload,
    });
    window_set_background_color(s_window, GColorWhite);
  }
  window_stack_push(s_window, true);
}
