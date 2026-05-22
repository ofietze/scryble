#include <pebble.h>
#include "filter_cmc_window.h"
#include "filter_type_window.h"
#include "state.h"

static Window    *s_window;
static TextLayer *s_value_layer;
static TextLayer *s_label_layer;
static TextLayer *s_hint_layer;

static uint8_t   s_cmc = 0;
static char      s_value_buf[4];

static void update_display(void) {
  if (s_cmc == 7) {
    snprintf(s_value_buf, sizeof(s_value_buf), "7+");
  } else {
    snprintf(s_value_buf, sizeof(s_value_buf), "%d", s_cmc);
  }
  text_layer_set_text(s_value_layer, s_value_buf);
}

static void up_handler(ClickRecognizerRef rec, void *ctx) {
  if (s_cmc < 7) { s_cmc++; update_display(); }
}

static void down_handler(ClickRecognizerRef rec, void *ctx) {
  if (s_cmc > 0) { s_cmc--; update_display(); }
}

static void select_handler(ClickRecognizerRef rec, void *ctx) {
  g_state.filter.cmc = s_cmc;
  filter_type_window_push();
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

  // "Mana Cost" label at top
  s_label_layer = text_layer_create(GRect(0, b.size.h / 6, b.size.w, 28));
  text_layer_set_text(s_label_layer, "Mana Cost");
  text_layer_set_text_alignment(s_label_layer, GTextAlignmentCenter);
  text_layer_set_font(s_label_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_color(s_label_layer, GColorBlack);
  layer_add_child(root, text_layer_get_layer(s_label_layer));

  // Big number in the vertical center
  s_value_layer = text_layer_create(GRect(0, b.size.h / 2 - 36, b.size.w, 72));
  text_layer_set_text_alignment(s_value_layer, GTextAlignmentCenter);
  text_layer_set_font(s_value_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_color(s_value_layer, GColorBlack);
  layer_add_child(root, text_layer_get_layer(s_value_layer));

  // Bottom hint: ▲ / ▼ adjust    ✓ confirm
  s_hint_layer = text_layer_create(GRect(0, b.size.h - 30, b.size.w, 28));
  text_layer_set_text(s_hint_layer, "\xe2\x96\xb2/\xe2\x96\xbc adjust   \xe2\x9c\x93 confirm");
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(s_hint_layer, GColorDarkGray);
  layer_add_child(root, text_layer_get_layer(s_hint_layer));

  window_set_click_config_provider(w, click_config_provider);

  s_cmc = g_state.filter.cmc;
  update_display();
}

static void window_unload(Window *w) {
  text_layer_destroy(s_label_layer);
  text_layer_destroy(s_value_layer);
  text_layer_destroy(s_hint_layer);
}

void filter_cmc_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load, .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
