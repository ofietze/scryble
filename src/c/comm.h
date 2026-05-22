#pragma once
#include "state.h"

typedef enum {
  ACTION_RANDOM   = 0,
  ACTION_SEARCH   = 1,
  ACTION_GET_CARD = 2,
} AppAction;

typedef enum {
  STATUS_CARD        = 0,
  STATUS_LIST        = 1,
  STATUS_LOADING     = 2,
  STATUS_ERROR       = 3,
  STATUS_IMAGE_CHUNK = 4,
} AppStatus;

typedef void (*CommCallback)(void);
typedef void (*CommErrorCallback)(const char *msg);
typedef void (*CommImageCallback)(uint32_t offset, const uint8_t *data,
                                   uint16_t len, bool done);

void comm_init(void);
void comm_deinit(void);

void comm_request_random(void);
void comm_request_search(const FilterState *filter, uint32_t offset);
void comm_request_card(const char *card_id);

void comm_set_card_callback(CommCallback cb);
void comm_set_list_callback(CommCallback cb);
void comm_set_error_callback(CommErrorCallback cb);
void comm_set_image_callback(CommImageCallback cb);
