#include <pebble.h>
#include "weather.h"
#include "messaging.h"

// ── Hand drawing parameters (widths fixed; lengths computed from screen size) ─
#define EDGE_WIDTH         12
#define HALO_WIDTH          2
#define MINUTE_OUTER_W    (2 * EDGE_WIDTH)
#define MINUTE_INNER_W    EDGE_WIDTH
#define HOUR_OUTER_W      ((MINUTE_OUTER_W * 5) / 3 - 3)
#define HOUR_INNER_W      (HOUR_OUTER_W - EDGE_WIDTH)
#define HAND_TAIL          (HOUR_OUTER_W / 2)
#define TICK_WIDTH          2
#define TICK_MINOR_LEN      6
#define TICK_MAJOR_LEN     10

// Hand lengths and tick radius are derived from the shorter screen half-dim:
//   minute_len = half * 94%,  hour_len = half * 66%,  tick_r = half * 96%
#define HAND_MINUTE_FRAC   88
#define HAND_HOUR_FRAC     61
#define TICK_R_FRAC        82

// ── Vertical battery icon dimensions ──────────────────────────────────────
#define BAT_W       14   // body width
#define BAT_H       20   // body height
#define BAT_NUB_W    6   // terminal nub width
#define BAT_NUB_H    3   // terminal nub height
#define BAT_BORDER   2   // stroke/border width

// ── UI layout ──────────────────────────────────────────────────────────────
#define TOP_H     22   // height of top info strip
#define BOTTOM_H  34   // height of bottom info strip

static Window *s_window;
static Layer  *s_canvas_layer;

static int  s_hours, s_minutes;
static char s_time_str[6];   // "HH:MM"
static char s_date_str[8];   // "dd.mm."

// ── Recolor all draw commands in a PDC image ──────────────────────────────
static bool prv_recolor_cb(GDrawCommand *cmd, uint32_t index, void *ctx) {
  GColor *c = (GColor *)ctx;
  gdraw_command_set_fill_color(cmd, c[0]);
  gdraw_command_set_stroke_color(cmd, c[1]);
  return true;
}
static void gdraw_command_image_recolor(GDrawCommandImage *img, GColor fill, GColor stroke) {
  if (!img) return;
  GColor c[2] = { fill, stroke };
  gdraw_command_list_iterate(gdraw_command_image_get_command_list(img), prv_recolor_cb, c);
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

  // Derive hand lengths and tick radius from the shorter half-dimension so the
  // watchface scales correctly across basalt (144×168) and emery (200×228).
  int half = (w < h ? w : h) / 2;
  int minute_len = half * HAND_MINUTE_FRAC / 100;
  int hour_len   = half * HAND_HOUR_FRAC   / 100;
  int tick_r     = half * TICK_R_FRAC      / 100;

  // Background — white
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ── Tick marks ──────────────────────────────────────────────────────────
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, TICK_WIDTH);
  for (int i = 0; i < 12; i++) {
    int32_t tick_angle = i * TRIG_MAX_ANGLE / 12;
    int32_t sin_a = sin_lookup(tick_angle);
    int32_t cos_a = cos_lookup(tick_angle);
    int tick_len = (i % 3 == 0) ? TICK_MAJOR_LEN : TICK_MINOR_LEN;
    // Side ticks (2,3,4,8,9,10) snap to the left/right screen edges;
    // the rest stay on the fixed circle.
    bool is_side = (i == 2 || i == 3 || i == 4 || i == 8 || i == 9 || i == 10);
    GPoint outer = is_side
      ? rect_edge_point(center, sin_a, cos_a, w / 2 - 6)
      : rect_edge_point(center, sin_a, cos_a, tick_r);
    GPoint inner = rect_tick_inner(outer, center, tick_len);
    graphics_draw_line(ctx, outer, inner);
  }

  // ── Angles ──────────────────────────────────────────────────────────────
  int32_t min_angle  = s_minutes * TRIG_MAX_ANGLE / 60;
  int32_t hour_angle = (s_hours % 12) * TRIG_MAX_ANGLE / 12
                       + s_minutes * TRIG_MAX_ANGLE / 720;

  // ── Hour hand ───────────────────────────────────────────────────────────
  draw_hand_border(ctx, center, hour_angle,
                   hour_len - EDGE_WIDTH / 2,
                   HOUR_OUTER_W / 2 - EDGE_WIDTH / 2,
                   HOUR_OUTER_W / 2 - EDGE_WIDTH / 2,
                   HAND_TAIL, EDGE_WIDTH, GColorBlack);
  draw_hand_fill(ctx, center, hour_angle,
                 hour_len - EDGE_WIDTH / 2 - 1, HOUR_INNER_W, GColorBlack);

  // ── Minute hand (white halo separates it from tick marks on white bg) ────
  draw_hand_border(ctx, center, min_angle,
                   minute_len - EDGE_WIDTH / 2,
                   MINUTE_OUTER_W / 2 - EDGE_WIDTH / 2,
                   MINUTE_OUTER_W / 2 - EDGE_WIDTH / 2,
                   HAND_TAIL, EDGE_WIDTH + 2 * HALO_WIDTH, GColorWhite);
  draw_hand_border(ctx, center, min_angle,
                   minute_len - EDGE_WIDTH / 2,
                   MINUTE_OUTER_W / 2 - EDGE_WIDTH / 2,
                   MINUTE_OUTER_W / 2 - EDGE_WIDTH / 2,
                   HAND_TAIL, EDGE_WIDTH, GColorBlack);
  draw_hand_fill(ctx, center, min_angle,
                 minute_len - EDGE_WIDTH / 2 - 1, MINUTE_INNER_W, GColorBlack);

  // ── Center pivot ─────────────────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, center, 3);

  // ── Top-left: weather icon + temperature ─────────────────────────────────
  if (Weather_currentWeatherIcon) {
    gdraw_command_image_recolor(Weather_currentWeatherIcon, GColorWhite, GColorBlack);
    gdraw_command_image_draw(ctx, Weather_currentWeatherIcon, GPoint(2, 2));
  }
  if (Weather_weatherInfo.currentTemp != INT32_MIN) {
    char temp_str[8];
    snprintf(temp_str, sizeof(temp_str), "%d°", Weather_weatherInfo.currentTemp);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, temp_str,
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(30, 3, 60, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  // ── Top-right: vertical battery icon + percentage to its left ───────────
  BatteryChargeState bat = battery_state_service_peek();
  uint8_t pct = (bat.charge_percent > 0) ? bat.charge_percent : 5;

  // Icon: nub at top, body below; right edge flush with screen minus 3px
  int bat_x = w - BAT_W - 3;
  int bat_y = 3;  // top padding

  // Nub (terminal)
  int nub_x = bat_x + (BAT_W - BAT_NUB_W) / 2;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(nub_x, bat_y, BAT_NUB_W, BAT_NUB_H), 0, GCornerNone);

  // Outer border
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, BAT_BORDER);
  graphics_draw_rect(ctx, GRect(bat_x, bat_y + BAT_NUB_H, BAT_W, BAT_H));

  // Fill bar — grows from bottom up
  int fill_inner_h = BAT_H - 2 * BAT_BORDER;
  int fill_h = fill_inner_h * pct / 100;
  int fill_y = bat_y + BAT_NUB_H + BAT_BORDER + (fill_inner_h - fill_h);
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, pct <= 20 ? GColorRed : GColorBlack);
#else
  graphics_context_set_fill_color(ctx, GColorBlack);
#endif
  if (bat.is_charging) {
    graphics_fill_rect(ctx, GRect(bat_x + BAT_BORDER, bat_y + BAT_NUB_H + BAT_BORDER,
                                  BAT_W - 2 * BAT_BORDER, fill_inner_h), 0, GCornerNone);
  } else {
    graphics_fill_rect(ctx, GRect(bat_x + BAT_BORDER, fill_y,
                                  BAT_W - 2 * BAT_BORDER, fill_h), 0, GCornerNone);
  }

  // Percentage text — right-aligned, left of the icon with a small gap
  char bat_str[6];
  snprintf(bat_str, sizeof(bat_str), "%d%%", pct);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, bat_str,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(w - BAT_W - 3 - 4 - 46, 3, 44, 20),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // ── Bottom-left: digital time ─────────────────────────────────────────────
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_time_str,
                     fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS),
                     GRect(2, h - BOTTOM_H, w / 2 - 2, BOTTOM_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // ── Bottom-right: date ────────────────────────────────────────────────────
  graphics_draw_text(ctx, s_date_str,
                     fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS),
                     GRect(w / 2, h - BOTTOM_H, w / 2 - 2, BOTTOM_H),
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
