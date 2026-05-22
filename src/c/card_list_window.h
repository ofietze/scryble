#pragma once
void card_list_window_push(void);
void card_list_window_on_data(void);   // called by comm when list arrives
void card_list_window_on_error(const char *msg);
