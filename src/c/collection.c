#include <pebble.h>
#include <string.h>
#include "collection.h"

#define KEY_COUNT 1000
#define KEY_BASE  1001  // entries live at KEY_BASE + i

static uint16_t s_count = 0;
static bool     s_inited = false;

void collection_init(void) {
  if (s_inited) return;
  s_count = (uint16_t)persist_read_int(KEY_COUNT);
  if (s_count > COLLECTION_MAX) s_count = COLLECTION_MAX;
  s_inited = true;
}

uint16_t collection_count(void) {
  collection_init();
  return s_count;
}

static CollectionEntry s_scratch;

const CollectionEntry *collection_get(uint16_t index) {
  collection_init();
  if (index >= s_count) return NULL;
  memset(&s_scratch, 0, sizeof(s_scratch));
  if (!persist_exists(KEY_BASE + index)) return NULL;
  persist_read_data(KEY_BASE + index, &s_scratch, sizeof(s_scratch));
  return &s_scratch;
}

static int find_index(const char *id) {
  collection_init();
  CollectionEntry e;
  for (uint16_t i = 0; i < s_count; i++) {
    if (!persist_exists(KEY_BASE + i)) continue;
    persist_read_data(KEY_BASE + i, &e, sizeof(e));
    if (strncmp(e.id, id, sizeof(e.id)) == 0) return (int)i;
  }
  return -1;
}

bool collection_contains(const char *id) {
  return find_index(id) >= 0;
}

bool collection_add(const char *id, const char *name) {
  collection_init();
  if (collection_contains(id)) return true;
  if (s_count >= COLLECTION_MAX) return false;
  CollectionEntry e;
  memset(&e, 0, sizeof(e));
  strncpy(e.id,   id,   sizeof(e.id)   - 1);
  strncpy(e.name, name, sizeof(e.name) - 1);
  persist_write_data(KEY_BASE + s_count, &e, sizeof(e));
  s_count++;
  persist_write_int(KEY_COUNT, s_count);
  return true;
}

void collection_remove(const char *id) {
  int idx = find_index(id);
  if (idx < 0) return;
  // Shift everything after idx down by one slot.
  CollectionEntry buf;
  for (uint16_t i = (uint16_t)idx; i + 1 < s_count; i++) {
    persist_read_data(KEY_BASE + i + 1, &buf, sizeof(buf));
    persist_write_data(KEY_BASE + i,     &buf, sizeof(buf));
  }
  persist_delete(KEY_BASE + s_count - 1);
  s_count--;
  persist_write_int(KEY_COUNT, s_count);
}
