#include <pebble.h>
#include "card_view_window.h"
#include "mana_renderer.h"
#include "comm.h"
#include "state.h"
#include "collection.h"

#define MANA_DIAM  18
#define PT_W       46
#define PT_H       22
#define ART_W      200
#define ART_H       75

#define MQ_TICK_MS      25
#define MQ_DELAY_TICKS  32   // 800 ms
#define MQ_PAUSE_TICKS  48   // 1200 ms
#define MQ_FWD_STEP     1
#define MQ_BACK_STEP    2

// Computed dynamically in window_load from the actual screen size
static int s_w, s_h;
static int s_header_h, s_art_h, s_type_h, s_scroll_y, s_scroll_h;

static Window      *s_window;
static Layer       *s_header_layer;
static Layer       *s_art_layer;
static Layer       *s_type_layer;
static ScrollLayer *s_scroll_layer;
static TextLayer   *s_text_layer;
static Layer       *s_pt_layer;
static TextLayer   *s_loading_layer;
static TextLayer   *s_toast_layer;
static AppTimer    *s_toast_timer = NULL;

static bool      s_loaded     = false;
static bool      s_art_loaded = false;
static GBitmap  *s_art_bmp   = NULL;

// ── Marquee state ─────────────────────────────────────────────────────────────
typedef enum { MQ_DELAY, MQ_FWD, MQ_PAUSE, MQ_BACK, MQ_IDLE } MqPhase;

static int       s_title_offset    = 0;
static int       s_type_offset     = 0;
static int       s_title_max       = 0;  // overflow distance in px (0 = fits)
static int       s_type_max        = 0;
static int       s_title_text_w    = 0;
static int       s_type_text_w     = 0;
static MqPhase   s_title_phase     = MQ_IDLE;
static MqPhase   s_type_phase      = MQ_IDLE;
static int       s_title_ticks     = 0;
static int       s_type_ticks      = 0;
static AppTimer *s_mq_timer        = NULL;
static bool      s_user_scrolled   = false;

// ── Color helpers ─────────────────────────────────────────────────────────────
static GColor card_bg_color(void) {
  const char *c = g_state.card.colors;
  int len = (int)strlen(c);

  // Basic lands have no colors[] entry but should be tinted by what they produce.
  if (strstr(g_state.card.type_line, "Basic") && strstr(g_state.card.type_line, "Land")) {
    if (strstr(g_state.card.type_line, "Plains")   || strstr(g_state.card.name, "Plains"))   return GColorWhite;
    if (strstr(g_state.card.type_line, "Island")   || strstr(g_state.card.name, "Island"))   return GColorVividCerulean;
    if (strstr(g_state.card.type_line, "Swamp")    || strstr(g_state.card.name, "Swamp"))    return GColorBlack;
    if (strstr(g_state.card.type_line, "Mountain") || strstr(g_state.card.name, "Mountain")) return GColorRed;
    if (strstr(g_state.card.type_line, "Forest")   || strstr(g_state.card.name, "Forest"))   return GColorIslamicGreen;
    if (strstr(g_state.card.type_line, "Wastes")   || strstr(g_state.card.name, "Wastes"))   return GColorLightGray;
  }

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

// ── Marquee state machine ─────────────────────────────────────────────────────
static void marquee_tick(void *ctx);

static void schedule_marquee_timer(void) {
  if (!s_mq_timer) {
    s_mq_timer = app_timer_register(MQ_TICK_MS, marquee_tick, NULL);
  }
}

static void cancel_marquee(void) {
  if (s_mq_timer) {
    app_timer_cancel(s_mq_timer);
    s_mq_timer = NULL;
  }
  s_title_phase = MQ_IDLE;
  s_type_phase  = MQ_IDLE;
  s_title_offset = 0;
  s_type_offset  = 0;
  s_title_ticks  = 0;
  s_type_ticks   = 0;
}

// Advance one field's state machine; returns true if anything changed.
static bool step_phase(MqPhase *phase, int *offset, int *ticks, int max) {
  if (max <= 0) { *phase = MQ_IDLE; return false; }
  switch (*phase) {
    case MQ_DELAY:
      if (--(*ticks) <= 0) { *phase = MQ_FWD; }
      return false;
    case MQ_FWD:
      *offset += MQ_FWD_STEP;
      if (*offset >= max) { *offset = max; *phase = MQ_PAUSE; *ticks = MQ_PAUSE_TICKS; }
      return true;
    case MQ_PAUSE:
      if (--(*ticks) <= 0) { *phase = MQ_BACK; }
      return false;
    case MQ_BACK:
      *offset -= MQ_BACK_STEP;
      if (*offset <= 0) { *offset = 0; *phase = MQ_IDLE; }
      return true;
    case MQ_IDLE:
    default:
      return false;
  }
}

static void marquee_tick(void *ctx) {
  s_mq_timer = NULL;

  bool title_changed = step_phase(&s_title_phase, &s_title_offset, &s_title_ticks, s_title_max);
  bool type_changed  = step_phase(&s_type_phase,  &s_type_offset,  &s_type_ticks,  s_type_max);

  if (title_changed && s_header_layer) layer_mark_dirty(s_header_layer);
  if (type_changed  && s_type_layer)   layer_mark_dirty(s_type_layer);

  if (s_title_phase != MQ_IDLE || s_type_phase != MQ_IDLE) {
    schedule_marquee_timer();
  }
}

static void start_marquee(void) {
  if (s_mq_timer) { app_timer_cancel(s_mq_timer); s_mq_timer = NULL; }
  s_title_offset = 0;
  s_type_offset  = 0;
  if (s_title_max > 0) { s_title_phase = MQ_DELAY; s_title_ticks = MQ_DELAY_TICKS; }
  else                 { s_title_phase = MQ_IDLE;  }
  if (s_type_max > 0)  { s_type_phase  = MQ_DELAY; s_type_ticks  = MQ_DELAY_TICKS; }
  else                 { s_type_phase  = MQ_IDLE;  }
  if (s_title_phase != MQ_IDLE || s_type_phase != MQ_IDLE) {
    schedule_marquee_timer();
  }
  if (s_header_layer) layer_mark_dirty(s_header_layer);
  if (s_type_layer)   layer_mark_dirty(s_type_layer);
}

// ── Header layer ──────────────────────────────────────────────────────────────
static void header_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = card_bg_color();
  GColor fg = card_text_color(bg);

  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (!s_loaded) {
    // Still draw the border so the section is visually defined.
    graphics_context_set_stroke_color(ctx, fg);
    graphics_draw_rect(ctx, b);
    return;
  }

  int mana_w  = mana_renderer_cost_width(g_state.card.mana_cost, MANA_DIAM);
  int mana_pad = mana_w > 0 ? 6 : 4;
  int avail_w  = b.size.w - mana_w - mana_pad - 4 - 4;
  graphics_context_set_text_color(ctx, fg);

  if (s_title_max > 0) {
    // Marquee mode: draw with overflow=Fill, offsetting x left.
    GRect name_rect = GRect(4 - s_title_offset,
                            (b.size.h - 26) / 2,
                            s_title_text_w + 20,
                            26);
    graphics_draw_text(ctx, g_state.card.name,
                       fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       name_rect, GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
    // Mask anything that bled into the mana area, then redraw mana on top.
    int mask_w = mana_w + mana_pad + 4;
    GRect mask = GRect(b.size.w - mask_w, 0, mask_w, b.size.h);
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, mask, 0, GCornerNone);
  } else {
    GRect name_rect = GRect(4, (b.size.h - 26) / 2, avail_w, 26);
    graphics_draw_text(ctx, g_state.card.name,
                       fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       name_rect, GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
  }

  mana_renderer_draw_cost(ctx, g_state.card.mana_cost, b, MANA_DIAM);

  graphics_context_set_stroke_color(ctx, fg);
  graphics_draw_rect(ctx, b);
}

// ── Type-line layer (custom, supports marquee + border) ──────────────────────
static void type_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  if (s_loaded) {
    graphics_context_set_text_color(ctx, GColorBlack);
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    if (s_type_max > 0) {
      GRect r = GRect(4 - s_type_offset, 0, s_type_text_w + 20, b.size.h);
      graphics_draw_text(ctx, g_state.card.type_line, font, r,
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    } else {
      GRect r = GRect(4, 0, b.size.w - 8, b.size.h);
      graphics_draw_text(ctx, g_state.card.type_line, font, r,
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentLeft, NULL);
    }
  }

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_rect(ctx, b);
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

// ── Toast (collection add/remove confirmation) ────────────────────────────────
static void toast_hide(void *ctx) {
  s_toast_timer = NULL;
  if (s_toast_layer) layer_set_hidden(text_layer_get_layer(s_toast_layer), true);
}

static void show_toast(const char *msg) {
  if (!s_toast_layer) return;
  text_layer_set_text(s_toast_layer, msg);
  layer_set_hidden(text_layer_get_layer(s_toast_layer), false);
  if (s_toast_timer) app_timer_cancel(s_toast_timer);
  s_toast_timer = app_timer_register(1200, toast_hide, NULL);
}

// ── Scroll callback (re-trigger marquee on scroll-to-top) ────────────────────
static void on_scroll(struct ScrollLayer *scroll, void *context) {
  GPoint off = scroll_layer_get_content_offset(scroll);
  if (off.y < -20) {
    s_user_scrolled = true;
  } else if (off.y == 0 && s_user_scrolled &&
             s_title_phase == MQ_IDLE && s_type_phase == MQ_IDLE) {
    s_user_scrolled = false;
    start_marquee();
  }
}

// ── Click handlers ───────────────────────────────────────────────────────────
// Pebble's ScrollLayer can install its own click config, but we also want
// Select to toggle collection membership — so we register all three buttons
// ourselves and drive the scroll layer manually on Up/Down.

static void scroll_up_click(ClickRecognizerRef rec, void *ctx) {
  GPoint off = scroll_layer_get_content_offset(s_scroll_layer);
  GPoint dst = GPoint(0, off.y + 30);
  if (dst.y > 0) dst.y = 0;
  scroll_layer_set_content_offset(s_scroll_layer, dst, true);
}

static void scroll_down_click(ClickRecognizerRef rec, void *ctx) {
  GPoint off  = scroll_layer_get_content_offset(s_scroll_layer);
  GSize  size = scroll_layer_get_content_size(s_scroll_layer);
  int min_y = -(size.h - s_scroll_h);
  if (min_y > 0) min_y = 0;
  GPoint dst = GPoint(0, off.y - 30);
  if (dst.y < min_y) dst.y = min_y;
  scroll_layer_set_content_offset(s_scroll_layer, dst, true);
}

static void select_click(ClickRecognizerRef rec, void *ctx) {
  if (!s_loaded) return;
  const char *id   = g_state.card.id;
  const char *name = g_state.card.name;
  if (id[0] == '\0') return;
  if (collection_contains(id)) {
    collection_remove(id);
    show_toast("Removed from collection");
  } else {
    collection_add(id, name);
    show_toast("Added to collection");
  }
}

static void click_config_provider(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP,   100, scroll_up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, scroll_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

// ── Data callbacks ────────────────────────────────────────────────────────────
void card_view_window_on_card(void) {
  s_loaded = true;
  cancel_marquee();

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
  scroll_layer_set_content_size(s_scroll_layer,
                                GSize(s_w, text_frame.origin.y + text_frame.size.h));
  // Reset scroll to top whenever a new card arrives.
  scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
  s_user_scrolled = false;

  // Measure title & type-line widths for marquee.
  GSize title_size = graphics_text_layout_get_content_size(
      g_state.card.name,
      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(0, 0, 2000, 30),
      GTextOverflowModeFill,
      GTextAlignmentLeft);
  s_title_text_w = title_size.w;

  int mana_w  = mana_renderer_cost_width(g_state.card.mana_cost, MANA_DIAM);
  int mana_pad = mana_w > 0 ? 6 : 4;
  int avail_title = s_w - mana_w - mana_pad - 4 - 4;
  s_title_max = (s_title_text_w > avail_title) ? (s_title_text_w - avail_title) : 0;

  GSize type_size = graphics_text_layout_get_content_size(
      g_state.card.type_line,
      fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(0, 0, 2000, 30),
      GTextOverflowModeFill,
      GTextAlignmentLeft);
  s_type_text_w = type_size.w;
  int avail_type = s_w - 8;
  s_type_max = (s_type_text_w > avail_type) ? (s_type_text_w - avail_type) : 0;

  layer_set_hidden(s_pt_layer, g_state.card.pt[0] == '\0');

  layer_mark_dirty(s_header_layer);
  layer_mark_dirty(s_type_layer);
  layer_mark_dirty(s_art_layer);

  start_marquee();
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

  s_type_layer = layer_create(GRect(0, s_header_h + s_art_h, s_w, s_type_h));
  layer_set_update_proc(s_type_layer, type_draw);
  layer_add_child(root, s_type_layer);

  s_scroll_layer = scroll_layer_create(GRect(0, s_scroll_y, s_w, s_scroll_h));
  scroll_layer_set_content_size(s_scroll_layer, GSize(s_w, s_scroll_h));
  scroll_layer_set_callbacks(s_scroll_layer, (ScrollLayerCallbacks){
    .content_offset_changed_handler = on_scroll,
  });

  s_text_layer = text_layer_create(GRect(4, 2, s_w - 8, s_scroll_h));
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_overflow_mode(s_text_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_text_layer, "");
  scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_text_layer));
  layer_add_child(root, scroll_layer_get_layer(s_scroll_layer));

  // Put our own click handler on the window: keeps the scroll layer's up/down
  // bindings but overrides SELECT to toggle collection membership.
  window_set_click_config_provider(w, click_config_provider);

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

  // Toast layer for collection add/remove confirmations.
  s_toast_layer = text_layer_create(GRect(4, s_h / 2 - 18, s_w - 8, 36));
  text_layer_set_text_alignment(s_toast_layer, GTextAlignmentCenter);
  text_layer_set_font(s_toast_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_toast_layer, GColorBlack);
  text_layer_set_text_color(s_toast_layer, GColorWhite);
  layer_set_hidden(text_layer_get_layer(s_toast_layer), true);
  layer_add_child(root, text_layer_get_layer(s_toast_layer));
}

static void window_unload(Window *w) {
  cancel_marquee();
  if (s_toast_timer) { app_timer_cancel(s_toast_timer); s_toast_timer = NULL; }
  layer_destroy(s_header_layer);
  layer_destroy(s_art_layer);
  layer_destroy(s_type_layer);
  text_layer_destroy(s_text_layer);
  scroll_layer_destroy(s_scroll_layer);
  layer_destroy(s_pt_layer);
  text_layer_destroy(s_loading_layer);
  text_layer_destroy(s_toast_layer);
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
