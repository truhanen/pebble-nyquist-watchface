#include <pebble.h>
#include "weather.h"
#include "messaging.h"

static void (*s_message_processed_callback)(void);
enum {
  PERSIST_SHOW_TOP_LEFT = 300,
  PERSIST_SHOW_TOP_RIGHT = 301,
  PERSIST_SHOW_BOTTOM_LEFT = 302,
  PERSIST_SHOW_BOTTOM_RIGHT = 303,
  PERSIST_INVERT_COLORS = 304,
  PERSIST_TIME_FORMAT_24H = 305
};

static bool s_show_top_left = true;
static bool s_show_top_right = true;
static bool s_show_bottom_left = true;
static bool s_show_bottom_right = true;
static bool s_invert_colors = false;
static bool s_time_format_24h = true;

static void prv_load_bool(int key, bool *target) {
  if (persist_exists(key)) {
    *target = persist_read_bool(key);
  }
}

static void prv_store_bool_from_tuple(Tuple *t, int key, bool *target) {
  if (!t) return;
  *target = t->value->int32 != 0;
  persist_write_bool(key, *target);
}

void messaging_requestNewWeatherData() {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_uint32(iter, 0, 0);
  app_message_outbox_send();
}

void messaging_init(void (*processed_callback)(void)) {
  s_message_processed_callback = processed_callback;
  prv_load_bool(PERSIST_SHOW_TOP_LEFT, &s_show_top_left);
  prv_load_bool(PERSIST_SHOW_TOP_RIGHT, &s_show_top_right);
  prv_load_bool(PERSIST_SHOW_BOTTOM_LEFT, &s_show_bottom_left);
  prv_load_bool(PERSIST_SHOW_BOTTOM_RIGHT, &s_show_bottom_right);
  prv_load_bool(PERSIST_INVERT_COLORS, &s_invert_colors);
  if (persist_exists(PERSIST_TIME_FORMAT_24H)) {
    s_time_format_24h = persist_read_bool(PERSIST_TIME_FORMAT_24H);
  }
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_open(512, 8);
}

void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  bool weatherDataUpdated = false;

  Tuple *t;

  t = dict_find(iterator, MESSAGE_KEY_WeatherTemperature);
  if (t) { Weather_weatherInfo.currentTemp = (int)t->value->int32; weatherDataUpdated = true; }

  t = dict_find(iterator, MESSAGE_KEY_WeatherCondition);
  if (t) { Weather_setCurrentCondition(t->value->int32); weatherDataUpdated = true; }

  t = dict_find(iterator, MESSAGE_KEY_WeatherUVIndex);
  if (t) { Weather_weatherInfo.currentUVIndex = (int)t->value->int32; weatherDataUpdated = true; }

  t = dict_find(iterator, MESSAGE_KEY_WeatherForecastCondition);
  if (t) { Weather_setForecastCondition(t->value->int32); weatherDataUpdated = true; }

  t = dict_find(iterator, MESSAGE_KEY_WeatherForecastHighTemp);
  if (t) { Weather_weatherInfo.todaysHighTemp = (int)t->value->int32; weatherDataUpdated = true; }

  t = dict_find(iterator, MESSAGE_KEY_WeatherForecastLowTemp);
  if (t) { Weather_weatherInfo.todaysLowTemp = (int)t->value->int32; weatherDataUpdated = true; }

  prv_store_bool_from_tuple(dict_find(iterator, MESSAGE_KEY_ShowTopLeft), PERSIST_SHOW_TOP_LEFT, &s_show_top_left);
  prv_store_bool_from_tuple(dict_find(iterator, MESSAGE_KEY_ShowTopRight), PERSIST_SHOW_TOP_RIGHT, &s_show_top_right);
  prv_store_bool_from_tuple(dict_find(iterator, MESSAGE_KEY_ShowBottomLeft), PERSIST_SHOW_BOTTOM_LEFT, &s_show_bottom_left);
  prv_store_bool_from_tuple(dict_find(iterator, MESSAGE_KEY_ShowBottomRight), PERSIST_SHOW_BOTTOM_RIGHT, &s_show_bottom_right);
  prv_store_bool_from_tuple(dict_find(iterator, MESSAGE_KEY_InvertColors), PERSIST_INVERT_COLORS, &s_invert_colors);
  prv_store_bool_from_tuple(dict_find(iterator, MESSAGE_KEY_TimeFormat24h), PERSIST_TIME_FORMAT_24H, &s_time_format_24h);

  if (weatherDataUpdated) {
    Weather_saveData();
  }

  if (s_message_processed_callback) {
    s_message_processed_callback();
  }
}

void inbox_dropped_callback(AppMessageResult reason, void *context) {}
void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {}
void outbox_sent_callback(DictionaryIterator *iterator, void *context) {}

bool messaging_show_top_left(void) { return s_show_top_left; }
bool messaging_show_top_right(void) { return s_show_top_right; }
bool messaging_show_bottom_left(void) { return s_show_bottom_left; }
bool messaging_show_bottom_right(void) { return s_show_bottom_right; }
bool messaging_invert_colors(void) { return s_invert_colors; }
bool messaging_time_format_24h(void) { return s_time_format_24h; }
