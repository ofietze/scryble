# Scryble — MTG Card Viewer for Pebble

Pebble SDK 3 watchapp (C + pkjs). Target: **emery** (Pebble Time 2, 200×228 rectangular display). Not a watchface.

## Build

```bash
pebble build          # produces build/scryble.pbw
pebble install --emulator emery
```

WAF build system via `wscript`. Resources declared in `appinfo.json`.

pkjs depends on `jpeg-js` (declared in `package.json`). The SDK bundles it via `pebble build`.

## Architecture

Single global state: `g_state` (`AppState` in `state.h`). All windows read from it; `comm.c` writes to it when messages arrive from the phone.

**Window stack** (push order):
1. `main_menu_window` — Random / Search / Collection entry point
2. `card_list_window` — `MenuLayer` showing up to 5 cards + "Load more"
3. `card_view_window` — Full card detail view

Filter windows (`filter_color/cmc/type/alpha/letter_window`) are pushed from `main_menu_window` before triggering a search. `collection_window` is pushed from `main_menu_window` to browse saved cards.

## Key files

| File | Purpose |
|---|---|
| `state.h` | `AppState`, `CardData`, `CardList`, `FilterState` structs |
| `comm.c/h` | AppMessage bridge to pkjs; sets callbacks for card/list/error/image events |
| `card_view_window.c` | Card detail UI: fixed header (name+mana), scrollable art+type+oracle, P/T overlay, marquee, toast |
| `mana_renderer.c/h` | Draws mana cost symbols right-aligned; `mana_renderer_cost_width()` measures without drawing |
| `card_list_window.c` | MenuLayer listing results, paginates via `comm_request_search` with `list_offset` |
| `collection.c/h` | Persistent local collection (up to 30 cards); Pebble storage, ~4 KiB budget |
| `collection_window.c/h` | MenuLayer browsing the local collection |
| `filter_letter_window.c/h` | Exact-first-letter narrowing filter (complements alpha-bucket filter) |
| `src/pkjs/index.js` | Phone-side JS: queries Scryfall, sends AppMessages, decodes+dithers JPEG art |

## Communication protocol

Phone → Watch via `AppMessage`. Key `Status` value determines message type:
- `STATUS_CARD` — fills `g_state.card`, triggers card callback
- `STATUS_LIST` — fills `g_state.card_list`, triggers list callback
- `STATUS_IMAGE_CHUNK` — streams bitmap pixels into `card_view_window` via image callback
- `STATUS_ERROR` — triggers error callback with message string

Watch → Phone: sends `Action` key (`ACTION_RANDOM`, `ACTION_SEARCH`, `ACTION_GET_CARD`) plus filter/offset/id params. Filter params include `FilterLetter` for exact-letter narrowing.

## card_view_window layout

The header is **fixed** (not inside the scroll layer). Everything else scrolls:

```
┌─────────────────────────┐  ← s_header_h = 30 px   name + mana cost, card-color bg, 1px border
│  Header (card color bg) │
├─────────────────────────┤  ╮
│  Art        200×146     │  │ ScrollLayer (s_h - 30)
├─────────────────────────┤  │  ← s_type_h (~11%)    type line, white bg, 1px border
│  Type line              │  │
├─────────────────────────┤  │  ← oracle text TextLayer, word-wrapped
│  Oracle text (scroll)   │  │
└──────────────────────P/T┘  ╯  ← PT overlay pinned bottom-right of root (not scroll content)
```

Card background color derived from `g_state.card.colors` (and type line for basic lands) via `card_bg_color()`.

**Buttons in card_view_window:**
- Up / Down (repeating, 100 ms): scroll oracle text by 30 px
- Select: toggle current card in/out of local collection; shows a 1.2 s toast confirmation

## Marquee scroll

Both the card name (header) and type line support marquee when text overflows:

- State machine per field: `MQ_DELAY` (800 ms) → `MQ_FWD` (1 px/25 ms) → `MQ_PAUSE` (1200 ms) → `MQ_BACK` (2 px/25 ms) → `MQ_IDLE`
- Triggered automatically on card load via `start_marquee()`.
- Re-triggered when the user scrolls the oracle text down and returns to the top (tracked via `s_user_scrolled` and `on_scroll` callback).
- Header marquee: title drawn with x-offset; mana area masked with a fill rect, then mana redrawn on top so it never scrolls.
- Type-line layer is a custom `Layer` with `type_draw()` proc (not a `TextLayer`) to support the x-offset.

## Image pipeline (pkjs)

`index.js` fetches the Scryfall `art_crop` JPEG, decodes it with `jpeg-js`, and converts to Pebble 8-bit (`GBitmapFormat8Bit`):

1. Center-fill crop to 200×146 aspect ratio
2. Bilinear downsample in linear light (sRGB LUT avoids repeated `Math.pow`)
3. Floyd-Steinberg dither → 2-bit-per-channel RGB packed as `0xC0 | (r2<<4) | (g2<<2) | b2`
4. Sent in 1700-byte chunks as `STATUS_IMAGE_CHUNK` messages

## Collection

`collection.c` stores up to 30 `{id[40], name[64]}` entries in Pebble persistent storage. `collection_contains` / `collection_add` / `collection_remove` are called from `card_view_window.c` on Select press.
