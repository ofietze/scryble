#include <pebble.h>
#include "main_menu_window.h"
#include "filter_color_window.h"
#include "card_view_window.h"
#include "collection_window.h"
#include "comm.h"
#include "state.h"

static Window     *s_window;
static MenuLayer  *s_menu;
static BitmapLayer *s_logo_layer;
static GBitmap    *s_logo_bmp;
static int         s_logo_h = 0;

#define LOGO_PAD 6

static const char *ITEMS[] = { "Random Card", "Browse Cards", "Collection" };
#define NUM_ITEMS (sizeof(ITEMS) / sizeof(ITEMS[0]))

static uint16_t get_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  return NUM_ITEMS;
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *ctx_) {
  menu_cell_basic_draw(ctx, cell, ITEMS[idx->row], NULL, NULL);
}

static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) { return 40; }

static void select_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  switch (idx->row) {
    case 0:
      g_state.is_random = true;
      card_view_window_push();
      comm_request_random();
      break;
    case 1:
      g_state.is_random = false;
      filter_color_window_push();
      break;
    case 2:
      collection_window_push();
      break;
  }
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  window_set_background_color(w, GColorWhite);

  // Logo banner at the top: drawn from the LOGO resource, centred.
  s_logo_bmp = gbitmap_create_with_resource(RESOURCE_ID_LOGO);
  if (s_logo_bmp) {
    GSize sz = gbitmap_get_bounds(s_logo_bmp).size;
    s_logo_h = sz.h + LOGO_PAD * 2;
    GRect logo_rect = GRect((bounds.size.w - sz.w) / 2, LOGO_PAD, sz.w, sz.h);
    s_logo_layer = bitmap_layer_create(logo_rect);
    bitmap_layer_set_bitmap(s_logo_layer, s_logo_bmp);
    bitmap_layer_set_compositing_mode(s_logo_layer, GCompOpSet);
    layer_add_child(root, bitmap_layer_get_layer(s_logo_layer));
  }

  GRect menu_rect = GRect(0, s_logo_h, bounds.size.w, bounds.size.h - s_logo_h);
  s_menu = menu_layer_create(menu_rect);
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
  if (s_logo_layer) { bitmap_layer_destroy(s_logo_layer); s_logo_layer = NULL; }
  if (s_logo_bmp)   { gbitmap_destroy(s_logo_bmp);        s_logo_bmp = NULL; }
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
