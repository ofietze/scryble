#include <pebble.h>
#include "filter_color_window.h"
#include "filter_cmc_window.h"
#include "state.h"

static Window    *s_window;
static MenuLayer *s_menu;

static const char *LABELS[] = {
  "White", "Blue", "Black", "Red", "Green", "Multi-color", "Colorless"
};

static GColor BOX_COLORS[] = {
  GColorWhite, GColorVividCerulean, GColorBlack,
  GColorRed,   GColorIslamicGreen
};

// ── GPath cache ───────────────────────────────────────────────────────────────
// All paths are defined relative to origin (0,0), placed via gpath_move_to().

static const GPoint DROPLET_PTS[] = {
  {0,-14},{5,-8},{9,-1},{9,6},{0,12},{-9,6},{-9,-1},{-5,-8}
};
static const GPathInfo DROPLET_INFO = {8, (GPoint*)DROPLET_PTS};

static const GPoint SKULL_PTS[] = {
  {0,-13},{8,-9},{11,-2},{10,5},{7,11},{-7,11},{-10,5},{-11,-2},{-8,-9}
};
static const GPathInfo SKULL_INFO = {9, (GPoint*)SKULL_PTS};

static const GPoint FLAME_PTS[] = {
  {0,-14},{4,-8},{8,-3},{11,4},{8,11},{-8,11},{-11,4},{-8,-3},{-4,-8}
};
static const GPathInfo FLAME_INFO = {9, (GPoint*)FLAME_PTS};

static const GPoint TREE_TOP_PTS[] = {{0,-14},{-8,-2},{8,-2}};
static const GPathInfo TREE_TOP_INFO = {3, (GPoint*)TREE_TOP_PTS};

static const GPoint TREE_BOT_PTS[] = {{-5,-4},{5,-4},{11,9},{-11,9}};
static const GPathInfo TREE_BOT_INFO = {4, (GPoint*)TREE_BOT_PTS};

static GPath *s_droplet  = NULL;
static GPath *s_skull    = NULL;
static GPath *s_flame    = NULL;
static GPath *s_tree_top = NULL;
static GPath *s_tree_bot = NULL;

// ── Symbol draw functions ─────────────────────────────────────────────────────

// Sun (W) — central disc + 8 rays
static void draw_sun(GContext *ctx, GPoint c, int r) {
  // ×10 unit vectors for 8 directions
  static const int8_t DIR[8][2] = {
    {0,-10},{7,-7},{10,0},{7,7},{0,10},{-7,7},{-10,0},{-7,-7}
  };
  int inner = r * 5 / 10;
  int outer = r * 9 / 10;
  int core  = r * 3 / 10;

  graphics_fill_circle(ctx, c, core);

  for (int i = 0; i < 8; i++) {
    GPoint p1 = GPoint(c.x + DIR[i][0] * inner / 10,
                       c.y + DIR[i][1] * inner / 10);
    GPoint p2 = GPoint(c.x + DIR[i][0] * outer / 10,
                       c.y + DIR[i][1] * outer / 10);
    graphics_draw_line(ctx, p1, p2);
  }
}

// Water droplet (U) — teardrop pointing up
static void draw_droplet(GContext *ctx, GPoint c) {
  gpath_move_to(s_droplet, c);
  gpath_draw_filled(ctx, s_droplet);
}

// Skull (B) — filled cranium + white eye sockets
static void draw_skull(GContext *ctx, GPoint c) {
  gpath_move_to(s_skull, c);
  gpath_draw_filled(ctx, s_skull);

  // Punch out eye sockets with white circles
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(c.x - 4, c.y - 1), 3);
  graphics_fill_circle(ctx, GPoint(c.x + 4, c.y - 1), 3);

  // Nose cavity
  graphics_fill_circle(ctx, GPoint(c.x, c.y + 4), 2);

  // Tooth gaps at the jaw line
  graphics_fill_rect(ctx, GRect(c.x - 7, c.y + 7, 3, 4), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(c.x - 2, c.y + 7, 3, 4), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(c.x + 3, c.y + 7, 3, 4), 0, GCornerNone);
}

// Flame (R) — flared base tapering to a tip
static void draw_flame(GContext *ctx, GPoint c) {
  gpath_move_to(s_flame, c);
  gpath_draw_filled(ctx, s_flame);

  // Inner "hot core" — slightly smaller white shape for depth
  // Reuse flame path scaled is complex, so draw a small oval instead
  graphics_context_set_fill_color(ctx, GColorWhite);
  int cx = c.x, cy = c.y;
  // Small white ellipse-ish area in the lower-centre of the flame
  graphics_fill_circle(ctx, GPoint(cx, cy + 3), 4);
}

// Pine tree (G) — two stacked triangles + trunk
static void draw_tree(GContext *ctx, GPoint c) {
  gpath_move_to(s_tree_top, c);
  gpath_draw_filled(ctx, s_tree_top);

  gpath_move_to(s_tree_bot, c);
  gpath_draw_filled(ctx, s_tree_bot);

  // Trunk
  graphics_fill_rect(ctx, GRect(c.x - 2, c.y + 9, 5, 5), 0, GCornerNone);
}

// ── Row drawing ───────────────────────────────────────────────────────────────

static GColor contrast(GColor bg) {
  if (gcolor_equal(bg, GColorWhite) ||
      gcolor_equal(bg, GColorChromeYellow) ||
      gcolor_equal(bg, GColorLightGray))
    return GColorBlack;
  return GColorWhite;
}

static void draw_symbol_for_row(GContext *ctx, int row, GPoint center, int sym_r) {
  // Symbol is drawn in black on the white inner circle
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  switch (row) {
    case 0: draw_sun    (ctx, center, sym_r); break;
    case 1: draw_droplet(ctx, center);        break;
    case 2: draw_skull  (ctx, center);        break;
    case 3: draw_flame  (ctx, center);        break;
    case 4: draw_tree   (ctx, center);        break;
  }
}

static void draw_color_box_row(GContext *ctx, const Layer *cell,
                                int row, bool highlighted) {
  GRect b = layer_get_bounds(cell);
  GColor bg = BOX_COLORS[row];
  GColor fg = contrast(bg);

  // Colored background
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Inset selection border when highlighted
  if (highlighted) {
    graphics_context_set_stroke_color(ctx, fg);
    graphics_draw_rect(ctx, GRect(3, 3, b.size.w - 6, b.size.h - 6));
    GRect inner = GRect(5, 5, b.size.w - 10, b.size.h - 10);
    graphics_draw_rect(ctx, inner);
  }

  // Symbol circle on the right, vertically centred
  int sym_r = 18;
  GPoint sym_c = GPoint(b.size.w - sym_r - 12, b.size.h / 2);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, sym_c, sym_r);
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_circle(ctx, sym_c, sym_r);

  // MTG symbol inside the white circle
  draw_symbol_for_row(ctx, row, sym_c, sym_r);

  // Colour name to the left of the circle
  graphics_context_set_text_color(ctx, fg);
  int label_w = sym_c.x - sym_r - 16;
  GRect label_rect = GRect(12, (b.size.h - 22) / 2, label_w, 22);
  graphics_draw_text(ctx, LABELS[row],
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     label_rect, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
}

static void draw_list_row(GContext *ctx, const Layer *cell,
                           int row, bool highlighted) {
  GRect b = layer_get_bounds(cell);
  GColor dot_color = (row == 5) ? GColorChromeYellow : GColorLightGray;

  if (highlighted) {
    graphics_context_set_fill_color(ctx, dot_color);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    graphics_context_set_text_color(ctx, contrast(dot_color));
    GRect tr = GRect(0, (b.size.h - 20) / 2, b.size.w, 20);
    graphics_draw_text(ctx, LABELS[row],
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       tr, GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  } else {
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, b, 0, GCornerNone);

    GPoint dot = GPoint(18, b.size.h / 2);
    graphics_context_set_fill_color(ctx, dot_color);
    graphics_fill_circle(ctx, dot, 8);
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_circle(ctx, dot, 8);

    GRect tr = GRect(34, (b.size.h - 18) / 2, b.size.w - 38, 18);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, LABELS[row],
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       tr, GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
  }
}

// ── MenuLayer callbacks ───────────────────────────────────────────────────────

static uint16_t get_num_rows(MenuLayer *ml, uint16_t sec, void *ctx) { return 7; }

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *ctx_) {
  bool hi = menu_cell_layer_is_highlighted(cell);
  if (idx->row < 5) draw_color_box_row(ctx, cell, idx->row, hi);
  else              draw_list_row     (ctx, cell, idx->row, hi);
}

static int16_t get_cell_height(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  return (idx->row < 5) ? 64 : 44;
}

static void select_click(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  g_state.filter.color = (uint8_t)idx->row;
  filter_cmc_window_push();
}

// ── Window lifecycle ──────────────────────────────────────────────────────────

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect bounds = layer_get_bounds(root);
  window_set_background_color(w, GColorWhite);

  s_droplet  = gpath_create(&DROPLET_INFO);
  s_skull    = gpath_create(&SKULL_INFO);
  s_flame    = gpath_create(&FLAME_INFO);
  s_tree_top = gpath_create(&TREE_TOP_INFO);
  s_tree_bot = gpath_create(&TREE_BOT_INFO);

  s_menu = menu_layer_create(bounds);
  menu_layer_set_highlight_colors(s_menu, GColorClear, GColorBlack);
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
  gpath_destroy(s_droplet);
  gpath_destroy(s_skull);
  gpath_destroy(s_flame);
  gpath_destroy(s_tree_top);
  gpath_destroy(s_tree_bot);
  s_droplet = s_skull = s_flame = s_tree_top = s_tree_bot = NULL;
}

void filter_color_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load, .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}
