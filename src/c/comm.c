#include <pebble.h>
#include "comm.h"
#include "state.h"

static CommCallback      s_card_cb  = NULL;
static CommCallback      s_list_cb  = NULL;
static CommErrorCallback s_error_cb = NULL;
static CommImageCallback s_image_cb = NULL;

// Populated at runtime because MESSAGE_KEY_* are extern vars, not compile-time constants
static uint32_t NAME_KEYS[CARDS_PER_PAGE];
static uint32_t ID_KEYS[CARDS_PER_PAGE];

static void inbox_received(DictionaryIterator *iter, void *ctx) {
  Tuple *status_t = dict_find(iter, MESSAGE_KEY_Status);
  if (!status_t) return;

  switch ((AppStatus)status_t->value->int32) {
    case STATUS_CARD: {
      Tuple *t;
      if ((t = dict_find(iter, MESSAGE_KEY_CardName)))
        strncpy(g_state.card.name, t->value->cstring, sizeof(g_state.card.name) - 1);
      if ((t = dict_find(iter, MESSAGE_KEY_CardManaCost)))
        strncpy(g_state.card.mana_cost, t->value->cstring, sizeof(g_state.card.mana_cost) - 1);
      if ((t = dict_find(iter, MESSAGE_KEY_CardTypeLine)))
        strncpy(g_state.card.type_line, t->value->cstring, sizeof(g_state.card.type_line) - 1);
      if ((t = dict_find(iter, MESSAGE_KEY_CardOracleText)))
        strncpy(g_state.card.oracle_text, t->value->cstring, sizeof(g_state.card.oracle_text) - 1);
      if ((t = dict_find(iter, MESSAGE_KEY_CardPT)))
        strncpy(g_state.card.pt, t->value->cstring, sizeof(g_state.card.pt) - 1);
      if ((t = dict_find(iter, MESSAGE_KEY_CardColors)))
        strncpy(g_state.card.colors, t->value->cstring, sizeof(g_state.card.colors) - 1);
      if (s_card_cb) s_card_cb();
      break;
    }
    case STATUS_LIST: {
      Tuple *count_t = dict_find(iter, MESSAGE_KEY_ListCount);
      Tuple *more_t  = dict_find(iter, MESSAGE_KEY_HasMore);
      g_state.card_list.count    = count_t ? (uint8_t)count_t->value->int32 : 0;
      g_state.card_list.has_more = more_t  ? (bool)more_t->value->int32     : false;
      for (int i = 0; i < g_state.card_list.count && i < CARDS_PER_PAGE; i++) {
        Tuple *n  = dict_find(iter, NAME_KEYS[i]);
        Tuple *id = dict_find(iter, ID_KEYS[i]);
        if (n)  strncpy(g_state.card_list.names[i], n->value->cstring,  63);
        if (id) strncpy(g_state.card_list.ids[i],   id->value->cstring, 39);
      }
      if (s_list_cb) s_list_cb();
      break;
    }
    case STATUS_ERROR: {
      Tuple *t = dict_find(iter, MESSAGE_KEY_CardOracleText);
      if (s_error_cb) s_error_cb(t ? t->value->cstring : "Unknown error");
      break;
    }
    case STATUS_IMAGE_CHUNK: {
      Tuple *off_t  = dict_find(iter, MESSAGE_KEY_ImageChunkOffset);
      Tuple *data_t = dict_find(iter, MESSAGE_KEY_ImageData);
      Tuple *done_t = dict_find(iter, MESSAGE_KEY_ImageDone);
      if (off_t && data_t && s_image_cb) {
        bool done = done_t && done_t->value->int8 != 0;
        s_image_cb(off_t->value->uint32, data_t->value->data, data_t->length, done);
      }
      break;
    }
    default: break;
  }
}

static void inbox_dropped(AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: %d", (int)reason);
  if (s_error_cb) s_error_cb("Phone not connected");
}

void comm_init(void) {
  NAME_KEYS[0] = MESSAGE_KEY_ListCard0Name;
  NAME_KEYS[1] = MESSAGE_KEY_ListCard1Name;
  NAME_KEYS[2] = MESSAGE_KEY_ListCard2Name;
  NAME_KEYS[3] = MESSAGE_KEY_ListCard3Name;
  NAME_KEYS[4] = MESSAGE_KEY_ListCard4Name;
  ID_KEYS[0]   = MESSAGE_KEY_ListCard0Id;
  ID_KEYS[1]   = MESSAGE_KEY_ListCard1Id;
  ID_KEYS[2]   = MESSAGE_KEY_ListCard2Id;
  ID_KEYS[3]   = MESSAGE_KEY_ListCard3Id;
  ID_KEYS[4]   = MESSAGE_KEY_ListCard4Id;

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(2048, 512);
}

void comm_deinit(void) {
  app_message_deregister_callbacks();
}

void comm_set_card_callback(CommCallback cb)       { s_card_cb  = cb; }
void comm_set_list_callback(CommCallback cb)       { s_list_cb  = cb; }
void comm_set_error_callback(CommErrorCallback cb) { s_error_cb = cb; }
void comm_set_image_callback(CommImageCallback cb) { s_image_cb = cb; }

void comm_request_random(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_Action, ACTION_RANDOM);
  app_message_outbox_send();
}

void comm_request_search(const FilterState *f, uint32_t offset) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter,  MESSAGE_KEY_Action,      ACTION_SEARCH);
  dict_write_uint8(iter,  MESSAGE_KEY_FilterColor, f->color);
  dict_write_uint8(iter,  MESSAGE_KEY_FilterCmc,   f->cmc);
  dict_write_uint8(iter,  MESSAGE_KEY_FilterType,  f->type);
  dict_write_uint8(iter,  MESSAGE_KEY_FilterAlpha, f->alpha);
  dict_write_uint32(iter, MESSAGE_KEY_ListOffset,  offset);
  app_message_outbox_send();
}

void comm_request_card(const char *card_id) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter,   MESSAGE_KEY_Action, ACTION_GET_CARD);
  dict_write_cstring(iter, MESSAGE_KEY_CardId, card_id);
  app_message_outbox_send();
}
