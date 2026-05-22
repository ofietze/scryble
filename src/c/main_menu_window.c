#include <pebble.h>
#include "main_menu_window.h"
#include "filter_color_window.h"
#include "card_view_window.h"
#include "comm.h"
#include "state.h"

static Window    *s_window;
static MenuLayer *s_menu;

static const char *ITEMS[] = { "Random Card", "Browse Cards" };

static uint16_t get_num_rows(MenuLayer *ml, uint16_t section, void *ctx) { return 2; }

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *ctx_) {
  menu_cell_basic_draw(ctx, cell, ITEMS[idx->row], NULL, NULL);
}

static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) { return 44; }

static void select_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (idx->row == 0) {
    g_state.is_random = true;
    card_view_window_push();
    comm_request_random();
  } else {
    g_state.is_random = false;
    filter_color_window_push();
  }
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);

  s_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows    = get_num_rows,
    .draw_row        = draw_row,
    .get_cell_height = get_cell_height,
    .select_click    = select_click,
  });
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *w) {
  menu_layer_destroy(s_menu);
}

void main_menu_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load   = window_load,
      .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
