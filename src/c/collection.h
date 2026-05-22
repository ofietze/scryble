#pragma once
#include <stdbool.h>
#include <stdint.h>

// Local-only collection of cards (id + display name), persisted via
// Pebble's persistent storage. Storage budget per Pebble docs is ~4 KiB
// in total; entries are kept compact (id 40 + name 64 = 104 bytes each).

#define COLLECTION_MAX 30

typedef struct {
  char id[40];
  char name[64];
} CollectionEntry;

void collection_init(void);

uint16_t collection_count(void);
const CollectionEntry *collection_get(uint16_t index);

bool collection_contains(const char *id);
bool collection_add(const char *id, const char *name);
void collection_remove(const char *id);
