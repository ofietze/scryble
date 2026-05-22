#include <pebble.h>
#include "collection_window.h"
#include "collection.h"
#include "card_view_window.h"
#include "comm.h"
#include "state.h"

static Window     *s_window;
static MenuLayer  *s_menu;
static TextLayer  *s_empty_layer;
static char        s_header_buf[40];

// MenuLayer with a header showing "X cards in collection".

static uint16_t get_num_sections(MenuLayer *ml, void *ctx) { return 1; }

static uint16_t get_num_rows(MenuLayer *ml, uint16_t sec, void *ctx) {
  return collection_count();
}

static int16_t get_header_h(MenuLayer *ml, uint16_t sec, void *ctx) { return 26; }

static void draw_header(GContext *ctx, const Layer *cell, uint16_t sec, void *ctx_) {
  uint16_t n = collection_count();
  snprintf(s_header_buf, sizeof(s_header_buf), "%u cards in collection", (unsigned)n);
  menu_cell_basic_header_draw(ctx, cell, s_header_buf);
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *ctx_) {
  const CollectionEntry *e = collection_get(idx->row);
  menu_cell_basic_draw(ctx, cell, e ? e->name : "(missing)", NULL, NULL);
}

static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) { return 36; }

static void select_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  const CollectionEntry *e = collection_get(idx->row);
  if (!e) return;
  card_view_window_push();
  comm_request_card(e->id);
}

static void window_appear(Window *w) {
  // Refresh — entries may have changed since this window was last shown.
  if (s_menu) menu_layer_reload_data(s_menu);
  layer_set_hidden(text_layer_get_layer(s_empty_layer),
                   collection_count() > 0);
  layer_set_hidden(menu_layer_get_layer(s_menu),
                   collection_count() == 0);
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);

  s_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_sections        = get_num_sections,
    .get_num_rows            = get_num_rows,
    .get_header_height       = get_header_h,
    .draw_header             = draw_header,
    .draw_row                = draw_row,
    .get_cell_height         = get_cell_height,
    .select_click            = select_click,
  });
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));

  // Empty-state layer: shown only when the collection has no entries.
  s_empty_layer = text_layer_create(GRect(8, bounds.size.h / 3, bounds.size.w - 16, 60));
  text_layer_set_text(s_empty_layer, "0 cards in collection");
  text_layer_set_text_alignment(s_empty_layer, GTextAlignmentCenter);
  text_layer_set_font(s_empty_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_background_color(s_empty_layer, GColorWhite);
  text_layer_set_text_color(s_empty_layer, GColorBlack);
  layer_set_hidden(text_layer_get_layer(s_empty_layer), true);
  layer_add_child(root, text_layer_get_layer(s_empty_layer));
}

static void window_unload(Window *w) {
  menu_layer_destroy(s_menu);
  text_layer_destroy(s_empty_layer);
  s_menu = NULL;
}

void collection_window_push(void) {
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
