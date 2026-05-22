#include <pebble.h>
#include "state.h"
#include "comm.h"
#include "main_menu_window.h"

AppState g_state = {0};

static void init(void) {
  comm_init();
  main_menu_window_push();
}

static void deinit(void) {
  comm_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
