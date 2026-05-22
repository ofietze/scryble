#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CARDS_PER_PAGE 5

typedef struct {
  uint8_t color;   // 0=W 1=U 2=B 3=R 4=G 5=Multi 6=Colorless
  uint8_t cmc;     // 0-6, 7 means 7+
  uint8_t type;    // 0=Creature 1=Instant 2=Sorcery 3=Enchantment 4=Artifact 5=Land 6=Planeswalker 7=Battle
  uint8_t alpha;   // 0=A-D 1=E-H 2=I-L 3=M-P 4=Q-T 5=U-Z
  char    letter;  // exact starting letter chosen after the alpha range; '\0' means whole range
} FilterState;

typedef struct {
  char names[CARDS_PER_PAGE][64];
  char ids[CARDS_PER_PAGE][40];
  uint8_t count;
  bool has_more;
} CardList;

typedef struct {
  char id[40];
  char name[64];
  char mana_cost[48];
  char type_line[64];
  char oracle_text[512];
  char pt[16];    // "3/4" for creatures, empty otherwise
  char colors[12]; // "WU", "B", "", etc.
} CardData;

typedef struct {
  FilterState filter;
  CardList    card_list;
  CardData    card;
  uint32_t    list_offset;
  bool        is_random;
} AppState;

extern AppState g_state;
