#include <pebble.h>
#include "filter_alpha_window.h"
#include "filter_letter_window.h"
#include "state.h"

static Window    *s_window;
static MenuLayer *s_menu;

static const char *LABELS[] = {
  "A \xe2\x80\x93 D",  // en-dash UTF-8
  "E \xe2\x80\x93 H",
  "I \xe2\x80\x93 L",
  "M \xe2\x80\x93 P",
  "Q \xe2\x80\x93 T",
  "U \xe2\x80\x93 Z",
};

static uint16_t get_num_rows(MenuLayer *ml, uint16_t sec, void *ctx) { return 6; }

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *ctx_) {
  menu_cell_basic_draw(ctx, cell, LABELS[idx->row], NULL, NULL);
}

static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) { return 36; }

static void select_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  g_state.filter.alpha = (uint8_t)idx->row;
  filter_letter_window_push();
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows    = get_num_rows,
    .draw_row        = draw_row,
    .get_cell_height = get_cell_height,
    .select_click    = select_click,
  });
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *w) { menu_layer_destroy(s_menu); }

void filter_alpha_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load, .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
