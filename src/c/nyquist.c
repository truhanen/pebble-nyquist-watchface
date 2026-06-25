#include <pebble.h>
#include "weather.h"
#include "messaging.h"

// ── Hand drawing parameters ────────────────────────────────────────────────
#define EDGE_WIDTH         10
#define HALO_WIDTH          2
#define MINUTE_OUTER_W    (2 * EDGE_WIDTH)
#define MINUTE_INNER_W    EDGE_WIDTH
#define HOUR_OUTER_W      ((MINUTE_OUTER_W * 5) / 3 - 3)
#define HOUR_INNER_W      (HOUR_OUTER_W - EDGE_WIDTH)
#define MINUTE_HAND_LENGTH 68
#define HOUR_HAND_LENGTH   48
#define HAND_TAIL          (HOUR_OUTER_W / 2)
#define TICK_RECT_R        70
#define TICK_WIDTH          2
#define TICK_MINOR_LEN      6
#define TICK_MAJOR_LEN     10

// ── UI layout ──────────────────────────────────────────────────────────────
#define TOP_H     22   // height of top info strip
#define BOTTOM_H  24   // height of bottom info strip
#define ICON_SIZE 20   // nominal icon size

static Window *s_window;
static Layer  *s_canvas_layer;

static int  s_hours, s_minutes;
static char s_time_str[6];   // "HH:MM"
static char s_date_str[8];   // "dd.mm."

static GDrawCommandImage *s_battery_image;
static GDrawCommandImage *s_battery_charge_image;

static bool recolor_iterator_cb(GDrawCommand *command, uint32_t index, void *context) {
  GColor *colors = (GColor *)context;
  gdraw_command_set_fill_color(command, colors[0]);
  gdraw_command_set_stroke_color(command, colors[1]);
  return true;
}

static void gdraw_command_image_recolor(GDrawCommandImage *img, GColor fill_color,
                                        GColor stroke_color) {
  if (!img) {
    return;
  }

  GColor colors[2] = { fill_color, stroke_color };
  gdraw_command_list_iterate(gdraw_command_image_get_command_list(img),
                             recolor_iterator_cb, colors);
}

// ── Helper: integer square root ───────────────────────────────────────────
static int32_t isqrt32(int32_t n) {
  if (n <= 0) return 0;
  int32_t x = n, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + n / x) / 2; }
  return x;
}

// ── Returns the point where the ray from center in direction (sin_a, cos_a)
//    intersects the square [center ± r].
static GPoint rect_edge_point(GPoint center, int32_t sin_a, int32_t cos_a, int r) {
  int32_t adx = (sin_a >= 0) ? sin_a : -sin_a;
  int32_t ady = (cos_a >= 0) ? cos_a : -cos_a;
  GPoint p;
  if (adx >= ady) {
    p.x = center.x + (sin_a >= 0 ? r : -r);
    p.y = center.y + (int32_t)(-(int64_t)cos_a * r / adx);
  } else {
    p.y = center.y + (cos_a >= 0 ? -r : r);
    p.x = center.x + (int32_t)((int64_t)sin_a * r / ady);
  }
  return p;
}

// ── Returns inner tick point, inset radially toward center by tick_len px ─
static GPoint rect_tick_inner(GPoint outer, GPoint center, int tick_len) {
  int32_t dx = center.x - outer.x;
  int32_t dy = center.y - outer.y;
  int32_t dist = isqrt32(dx * dx + dy * dy);
  if (dist == 0) return outer;
  return (GPoint){
    outer.x + (int32_t)((int64_t)dx * tick_len / dist),
    outer.y + (int32_t)((int64_t)dy * tick_len / dist)
  };
}

// ── Draws the pentagon outline of a hand (base + two edges + two tip lines) ─
static void draw_hand_border(GContext *ctx, GPoint center, int32_t angle,
                             int outer_dist, int half_width, int apex_ext,
                             int tail, int stroke_w, GColor color) {
  int32_t sin_a = sin_lookup(angle);
  int32_t cos_a = cos_lookup(angle);

  GPoint inner = {
    center.x - (int32_t)tail       * sin_a / TRIG_MAX_RATIO,
    center.y + (int32_t)tail       * cos_a / TRIG_MAX_RATIO,
  };
  GPoint outer = {
    center.x + (int32_t)outer_dist * sin_a / TRIG_MAX_RATIO,
    center.y - (int32_t)outer_dist * cos_a / TRIG_MAX_RATIO,
  };

  int x_diff = (int32_t)half_width * cos_a / TRIG_MAX_RATIO;
  int y_diff = (int32_t)half_width * sin_a / TRIG_MAX_RATIO;

  GPoint inner_right = { inner.x + x_diff, inner.y + y_diff };
  GPoint inner_left  = { inner.x - x_diff, inner.y - y_diff };
  GPoint outer_right = { outer.x + x_diff, outer.y + y_diff };
  GPoint outer_left  = { outer.x - x_diff, outer.y - y_diff };
  GPoint apex        = {
    outer.x + (int32_t)apex_ext * sin_a / TRIG_MAX_RATIO,
    outer.y - (int32_t)apex_ext * cos_a / TRIG_MAX_RATIO,
  };

  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, stroke_w);
  graphics_draw_line(ctx, inner_left,  inner_right);
  graphics_draw_line(ctx, inner_right, outer_right);
  graphics_draw_line(ctx, inner_left,  outer_left);
  graphics_draw_line(ctx, outer_right, apex);
  graphics_draw_line(ctx, outer_left,  apex);
}

// ── Draws the filled pentagon body of a hand ──────────────────────────────
static void draw_hand_fill(GContext *ctx, GPoint center, int32_t angle,
                           int length, int width, GColor color) {
  int hw = width / 2;
  GPoint pts[5] = {
    {-hw, HAND_TAIL}, {hw, HAND_TAIL}, {hw, -length}, {0, -(length + hw)}, {-hw, -length}
  };
  GPathInfo info = { .num_points = 5, .points = pts };
  GPath *path = gpath_create(&info);
  gpath_rotate_to(path, angle);
  gpath_move_to(path, center);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// ── Main draw callback ─────────────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int w = bounds.size.w;
  int h = bounds.size.h;
  GPoint center = GPoint(w / 2, h / 2);

  // Background
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ── Tick marks ──────────────────────────────────────────────────────────
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, TICK_WIDTH);
  for (int i = 0; i < 12; i++) {
    int32_t tick_angle = i * TRIG_MAX_ANGLE / 12;
    int32_t sin_a = sin_lookup(tick_angle);
    int32_t cos_a = cos_lookup(tick_angle);
    int tick_len = (i % 3 == 0) ? TICK_MAJOR_LEN : TICK_MINOR_LEN;
    GPoint outer = rect_edge_point(center, sin_a, cos_a, TICK_RECT_R);
    GPoint inner = rect_tick_inner(outer, center, tick_len);
    graphics_draw_line(ctx, outer, inner);
  }

  // ── Angles ──────────────────────────────────────────────────────────────
  int32_t min_angle  = s_minutes * TRIG_MAX_ANGLE / 60;
  int32_t hour_angle = (s_hours % 12) * TRIG_MAX_ANGLE / 12
                       + s_minutes * TRIG_MAX_ANGLE / 720;

  // ── Hour hand ───────────────────────────────────────────────────────────
  draw_hand_border(ctx, center, hour_angle,
                   HOUR_HAND_LENGTH - EDGE_WIDTH / 2,
                   HOUR_OUTER_W / 2 - EDGE_WIDTH / 2,
                   HOUR_OUTER_W / 2 - EDGE_WIDTH / 2,
                   HAND_TAIL, EDGE_WIDTH, GColorWhite);
  draw_hand_fill(ctx, center, hour_angle,
                 HOUR_HAND_LENGTH - EDGE_WIDTH / 2 - 1, HOUR_INNER_W, GColorWhite);

  // ── Minute hand (with halo) ──────────────────────────────────────────────
  draw_hand_border(ctx, center, min_angle,
                   MINUTE_HAND_LENGTH - EDGE_WIDTH / 2,
                   MINUTE_OUTER_W / 2 - EDGE_WIDTH / 2,
                   MINUTE_OUTER_W / 2 - EDGE_WIDTH / 2,
                   HAND_TAIL, EDGE_WIDTH + 2 * HALO_WIDTH, GColorBlack);
  draw_hand_border(ctx, center, min_angle,
                   MINUTE_HAND_LENGTH - EDGE_WIDTH / 2,
                   MINUTE_OUTER_W / 2 - EDGE_WIDTH / 2,
                   MINUTE_OUTER_W / 2 - EDGE_WIDTH / 2,
                   HAND_TAIL, EDGE_WIDTH, GColorWhite);
  draw_hand_fill(ctx, center, min_angle,
                 MINUTE_HAND_LENGTH - EDGE_WIDTH / 2 - 1, MINUTE_INNER_W, GColorWhite);

  // ── Center pivot ─────────────────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 3);

  // ── Top-left: weather icon + temperature ─────────────────────────────────
  if (Weather_currentWeatherIcon) {
    gdraw_command_image_recolor(Weather_currentWeatherIcon, GColorBlack, GColorWhite);
    gdraw_command_image_draw(ctx, Weather_currentWeatherIcon, GPoint(2, 2));
  }
  if (Weather_weatherInfo.currentTemp != INT32_MIN) {
    char temp_str[8];
    snprintf(temp_str, sizeof(temp_str), "%d°", Weather_weatherInfo.currentTemp);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, temp_str,
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(24, 2, 50, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  // ── Top-right: battery icon + percentage ─────────────────────────────────
  BatteryChargeState bat = battery_state_service_peek();
  uint8_t pct = (bat.charge_percent > 0) ? bat.charge_percent : 5;

  // Draw battery icon at top-right
  int bat_icon_w = s_battery_image ? gdraw_command_image_get_bounds_size(s_battery_image).w : ICON_SIZE;
  int bat_x = w - bat_icon_w - 2;
  int bat_y = -5;                   // correct for vertical padding in icon

  if (s_battery_image) {
    gdraw_command_image_recolor(s_battery_image, GColorBlack, GColorWhite);
    gdraw_command_image_draw(ctx, s_battery_image, GPoint(bat_x, bat_y));
  }

  if (bat.is_charging) {
    if (s_battery_charge_image) {
      gdraw_command_image_recolor(s_battery_charge_image, GColorWhite, GColorBlack);
      gdraw_command_image_draw(ctx, s_battery_charge_image, GPoint(bat_x, bat_y));
    }
  } else {
    // fill bar (icon interior starts at offset +3, +8 within the PDC icon, width 18)
    int fill_w = (int)(18 * pct / 100);
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, pct <= 20 ? GColorRed : GColorWhite);
#else
    graphics_context_set_fill_color(ctx, GColorWhite);
#endif
    graphics_fill_rect(ctx, GRect(bat_x + 3, bat_y + 8, fill_w, 8), 0, GCornerNone);
  }

  // Battery % text
  char bat_str[6];
  snprintf(bat_str, sizeof(bat_str), "%d%%", pct);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, bat_str,
                     fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(w - 36, 14, 36, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // ── Bottom-left: digital time ─────────────────────────────────────────────
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_time_str,
                     fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(2, h - BOTTOM_H - 2, 80, BOTTOM_H + 2),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // ── Bottom-right: date ────────────────────────────────────────────────────
  graphics_draw_text(ctx, s_date_str,
                     fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                     GRect(w - 72, h - BOTTOM_H - 2, 70, BOTTOM_H + 2),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

// ── Time update ───────────────────────────────────────────────────────────
static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
  s_hours   = tick_time->tm_hour;
  s_minutes = tick_time->tm_min;
  snprintf(s_time_str, sizeof(s_time_str), "%02d:%02d", s_hours, s_minutes);
  snprintf(s_date_str, sizeof(s_date_str), "%d.%d.",
           tick_time->tm_mday, tick_time->tm_mon + 1);

  // Request weather update every hour
  if (units_changed & HOUR_UNIT) {
    messaging_requestNewWeatherData();
  }

  layer_mark_dirty(s_canvas_layer);
}

// ── Messaging callback ─────────────────────────────────────────────────────
static void on_message_received(void) {
  layer_mark_dirty(s_canvas_layer);
}

// ── Battery callback ──────────────────────────────────────────────────────
static void on_battery_state_changed(BatteryChargeState state) {
  layer_mark_dirty(s_canvas_layer);
}

// ── Window lifecycle ──────────────────────────────────────────────────────
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root, s_canvas_layer);

  // Load battery images
  s_battery_image        = gdraw_command_image_create_with_resource(RESOURCE_ID_BATTERY_BG);
  s_battery_charge_image = gdraw_command_image_create_with_resource(RESOURCE_ID_BATTERY_CHARGE);

  // Init weather (loads from persistent storage if available)
  Weather_init();

  // Init messaging
  messaging_init(on_message_received);

  // Register battery handler
  battery_state_service_subscribe(on_battery_state_changed);

  // Register tick handler (every minute)
  tick_timer_service_subscribe(MINUTE_UNIT | HOUR_UNIT, handle_tick);

  // Set initial time
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  handle_tick(t, MINUTE_UNIT | HOUR_UNIT);

  // Request initial weather
  messaging_requestNewWeatherData();
}

static void window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();

  gdraw_command_image_destroy(s_battery_image);
  gdraw_command_image_destroy(s_battery_charge_image);

  Weather_deinit();

  layer_destroy(s_canvas_layer);
}

// ── App entry point ───────────────────────────────────────────────────────
int main(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  app_event_loop();
  window_destroy(s_window);
  return 0;
}
