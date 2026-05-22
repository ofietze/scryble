#include <pebble.h>
#include "filter_letter_window.h"
#include "card_list_window.h"
#include "comm.h"
#include "state.h"

// Letter-range bounds matching filter_alpha_window's ALPHA_RANGES.
static const char ALPHA_LO[] = { 'A', 'E', 'I', 'M', 'Q', 'U' };
static const char ALPHA_HI[] = { 'D', 'H', 'L', 'P', 'T', 'Z' };

static Window    *s_window;
static TextLayer *s_label_layer;
static TextLayer *s_value_layer;
static Layer     *s_arrows_layer;

static char s_value_buf[2];
static char s_letter;
static char s_lo, s_hi;

// Arrow paths matching filter_cmc_window.
static const GPoint UP_PTS[]   = {{0,-6},{7,4},{-7,4}};
static const GPathInfo UP_INFO = {3, (GPoint*)UP_PTS};

static const GPoint DOWN_PTS[]   = {{0,6},{7,-4},{-7,-4}};
static const GPathInfo DOWN_INFO = {3, (GPoint*)DOWN_PTS};

static GPath *s_up_path  = NULL;
static GPath *s_down_path = NULL;

static void arrows_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  gpath_move_to(s_up_path, GPoint(b.size.w / 2, b.size.h * 18 / 100));
  gpath_draw_filled(ctx, s_up_path);
  graphics_fill_circle(ctx, GPoint(b.size.w / 2, b.size.h / 2), 3);
  gpath_move_to(s_down_path, GPoint(b.size.w / 2, b.size.h * 82 / 100));
  gpath_draw_filled(ctx, s_down_path);
}

static void update_display(void) {
  s_value_buf[0] = s_letter;
  s_value_buf[1] = '\0';
  text_layer_set_text(s_value_layer, s_value_buf);
}

static void up_handler(ClickRecognizerRef rec, void *ctx) {
  if (s_letter < s_hi) { s_letter++; update_display(); }
}

static void down_handler(ClickRecognizerRef rec, void *ctx) {
  if (s_letter > s_lo) { s_letter--; update_display(); }
}

static void select_handler(ClickRecognizerRef rec, void *ctx) {
  g_state.filter.letter = s_letter;
  g_state.list_offset   = 0;
  card_list_window_push();
  comm_request_search(&g_state.filter, 0);
}

static void click_config_provider(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP,   150, up_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, down_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_handler);
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  window_set_background_color(w, GColorWhite);

  s_label_layer = text_layer_create(GRect(0, b.size.h / 6, b.size.w - 24, 28));
  text_layer_set_text(s_label_layer, "Letter");
  text_layer_set_text_alignment(s_label_layer, GTextAlignmentCenter);
  text_layer_set_font(s_label_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_color(s_label_layer, GColorBlack);
  layer_add_child(root, text_layer_get_layer(s_label_layer));

  s_value_layer = text_layer_create(GRect(0, b.size.h / 2 - 36, b.size.w - 24, 72));
  text_layer_set_text_alignment(s_value_layer, GTextAlignmentCenter);
  text_layer_set_font(s_value_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_color(s_value_layer, GColorBlack);
  layer_add_child(root, text_layer_get_layer(s_value_layer));

  s_up_path   = gpath_create(&UP_INFO);
  s_down_path = gpath_create(&DOWN_INFO);
  s_arrows_layer = layer_create(GRect(b.size.w - 20, 0, 20, b.size.h));
  layer_set_update_proc(s_arrows_layer, arrows_draw);
  layer_add_child(root, s_arrows_layer);

  window_set_click_config_provider(w, click_config_provider);

  uint8_t a = g_state.filter.alpha;
  if (a > 5) a = 0;
  s_lo = ALPHA_LO[a];
  s_hi = ALPHA_HI[a];
  s_letter = s_lo;
  update_display();
}

static void window_unload(Window *w) {
  text_layer_destroy(s_label_layer);
  text_layer_destroy(s_value_layer);
  layer_destroy(s_arrows_layer);
  gpath_destroy(s_up_path);
  gpath_destroy(s_down_path);
  s_up_path = s_down_path = NULL;
}

void filter_letter_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load, .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
