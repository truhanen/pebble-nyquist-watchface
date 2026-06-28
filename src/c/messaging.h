#pragma once
#include <pebble.h>

void messaging_requestNewWeatherData();
void messaging_init(void (*message_processed_callback)(void));
void inbox_received_callback(DictionaryIterator *iterator, void *context);
void inbox_dropped_callback(AppMessageResult reason, void *context);
void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context);
void outbox_sent_callback(DictionaryIterator *iterator, void *context);

bool messaging_show_top_left(void);
bool messaging_show_top_right(void);
bool messaging_show_bottom_left(void);
bool messaging_show_bottom_right(void);
bool messaging_invert_colors(void);
bool messaging_time_format_24h(void);
bool messaging_use_fahrenheit(void);
