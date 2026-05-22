# Magic Pebble — MTG Card Viewer for Pebble

Pebble SDK 3 watchapp (C + pkjs). Target: **emery** (Pebble Time 2, 200×228 rectangular display). Not a watchface.

## Build

```bash
pebble build          # produces build/magic-pebble.pbw
pebble install --emulator emery
```

WAF build system via `wscript`. Resources declared in `appinfo.json`.

## Architecture

Single global state: `g_state` (`AppState` in `state.h`). All windows read from it; `comm.c` writes to it when messages arrive from the phone.

**Window stack** (push order):
1. `main_menu_window` — Random / Search entry point
2. `card_list_window` — `MenuLayer` showing up to 5 cards + "Load more"
3. `card_view_window` — Full card detail view

Filter windows (`filter_color/cmc/type/alpha_window`) are pushed from `main_menu_window` before triggering a search.

## Key files

| File | Purpose |
|---|---|
| `state.h` | `AppState`, `CardData`, `CardList`, `FilterState` structs |
| `comm.c/h` | AppMessage bridge to pkjs; sets callbacks for card/list/error/image events |
| `card_view_window.c` | Card detail UI: header (name+mana), art, type line, scrollable oracle text, P/T overlay |
| `mana_renderer.c/h` | Draws mana cost symbols right-aligned; `mana_renderer_cost_width()` measures without drawing |
| `card_list_window.c` | MenuLayer listing results, paginates via `comm_request_search` with `list_offset` |
| `src/pkjs/index.js` | Phone-side JS: queries Scryfall API, sends results as AppMessages, streams artwork in chunks |

## Communication protocol

Phone → Watch via `AppMessage`. Key `Status` value determines message type:
- `STATUS_CARD` — fills `g_state.card`, triggers card callback
- `STATUS_LIST` — fills `g_state.card_list`, triggers list callback
- `STATUS_IMAGE_CHUNK` — streams bitmap pixels into `card_view_window` via image callback
- `STATUS_ERROR` — triggers error callback with message string

Watch → Phone: sends `Action` key (`ACTION_RANDOM`, `ACTION_SEARCH`, `ACTION_GET_CARD`) plus filter/offset/id params.

## card_view_window layout

Sections are proportional to screen height (`s_h`):

```
┌─────────────────────────┐  ← s_header_h  (~15%)  name + mana cost
│  Header (card color bg) │
├─────────────────────────┤  ← s_art_h     (~33%)  artwork bitmap
│  Art                    │
├─────────────────────────┤  ← s_type_h    (~11%)  type line
│  Type line              │
├─────────────────────────┤  ← s_scroll_h  (rest)  oracle text (ScrollLayer)
│  Oracle text (scroll)   │
└──────────────────────P/T┘  ← PT overlay pinned bottom-right for creatures
```

Card background color derived from `g_state.card.colors` via `card_bg_color()`.

## Pending / in-progress work

**Design changes requested** (not yet implemented):
- Header and type-line sections should have a **slim 1px border**.
- **Marquee scroll**: when title (`card.name`) or type line (`card.type_line`) is too long to fit, auto-scroll through once on page enter then scroll back. Re-trigger when user scrolls oracle text down and returns to top.

Implementation approach for the marquee:
- Replace `TextLayer *s_type_layer` with a custom `Layer` using a `type_draw()` proc so the type text can be drawn with an x-offset.
- Add static marquee state: `s_title_offset`, `s_type_offset`, `s_title_max`, `s_type_max`, `MqPhase` enum, `AppTimer *s_mq_timer`.
- Measure text widths in `card_view_window_on_card` using `graphics_text_layout_get_content_size()` and `mana_renderer_cost_width()`.
- Timer ticks every 25 ms, state machine: `MQ_DELAY` (800 ms) → `MQ_FWD` (1 px/tick) → `MQ_PAUSE` (1200 ms) → `MQ_BACK` (2 px/tick) → `MQ_IDLE`.
- Re-trigger via `scroll_layer_set_callbacks` `content_offset_changed_handler`: set `s_user_scrolled = true` when scroll_y > 20, call `start_marquee()` when it returns to 0 and phase is idle.
- Header draw: use `mana_renderer_cost_width()` (no draw) to compute layout, draw title with wide rect for offset, fill cover rect over mana area, then call `mana_renderer_draw_cost()` on top, then draw border.
