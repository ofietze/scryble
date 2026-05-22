#include <pebble.h>
#include "card_list_window.h"
#include "card_view_window.h"
#include "comm.h"
#include "state.h"

static Window    *s_window;
static MenuLayer *s_menu;

static bool s_loading = true;
static char s_error[64];

// Total visible rows: card count + 1 "Load More" if has_more
static uint16_t get_num_rows(MenuLayer *ml, uint16_t sec, void *ctx) {
  if (s_loading) return 1;
  uint16_t n = g_state.card_list.count;
  if (g_state.card_list.has_more) n++;
  return n > 0 ? n : 1; // at least 1 for "no results"
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *ctx_) {
  int row = idx->row;

  if (s_loading) {
    menu_cell_basic_draw(ctx, cell, "Loading...", NULL, NULL);
    return;
  }

  if (g_state.card_list.count == 0) {
    menu_cell_basic_draw(ctx, cell, "No cards found", NULL, NULL);
    return;
  }

  // "Load More" sentinel row
  if (g_state.card_list.has_more && row == g_state.card_list.count) {
    menu_cell_basic_draw(ctx, cell, "Load more \xe2\x86\x92", NULL, NULL);
    return;
  }

  menu_cell_basic_draw(ctx, cell, g_state.card_list.names[row], NULL, NULL);
}

static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) { return 36; }

static void select_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  if (s_loading) return;

  int row = idx->row;

  if (g_state.card_list.has_more && row == g_state.card_list.count) {
    // Load more: advance offset and re-request
    g_state.list_offset += CARDS_PER_PAGE;
    s_loading = true;
    menu_layer_reload_data(s_menu);
    comm_request_search(&g_state.filter, g_state.list_offset);
    return;
  }

  if (row < g_state.card_list.count) {
    // Open card view for this card
    card_view_window_push();
    comm_request_card(g_state.card_list.ids[row]);
  }
}

void card_list_window_on_data(void) {
  s_loading = false;
  s_error[0] = '\0';
  if (s_menu) menu_layer_reload_data(s_menu);
}

void card_list_window_on_error(const char *msg) {
  s_loading = false;
  strncpy(s_error, msg, sizeof(s_error) - 1);
  g_state.card_list.count = 0;
  g_state.card_list.has_more = false;
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void window_appear(Window *w) {
  comm_set_list_callback(card_list_window_on_data);
  comm_set_error_callback(card_list_window_on_error);
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  s_loading = true;
  s_error[0] = '\0';

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
  s_menu = NULL;
}

void card_list_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load   = window_load,
      .appear = window_appear,
      .unload = window_unload,
    });
  }
  s_loading = true;
  window_stack_push(s_window, true);
}
