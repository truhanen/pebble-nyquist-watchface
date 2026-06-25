#include <pebble.h>
#include "weather.h"
#include "messaging.h"

static void (*s_message_processed_callback)(void);

void messaging_requestNewWeatherData() {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_uint32(iter, 0, 0);
  app_message_outbox_send();
}

void messaging_init(void (*processed_callback)(void)) {
  s_message_processed_callback = processed_callback;
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
