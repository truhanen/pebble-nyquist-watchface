#include <pebble.h>
#include "weather.h"
#include "messaging.h"

// ── Hand drawing parameters ────────────────────────────────────────────────
#define EDGE_WIDTH         12
#define HALO_WIDTH          2
#define MINUTE_OUTER_W    (2 * EDGE_WIDTH)
#define MINUTE_INNER_W    EDGE_WIDTH
#define HOUR_OUTER_W      ((MINUTE_OUTER_W * 5) / 3 - 3)
#define HOUR_INNER_W      (HOUR_OUTER_W - EDGE_WIDTH)
#define HAND_TAIL          (HOUR_OUTER_W / 2)
#define TICK_WIDTH          2
#define TICK_HALO_W         6   // halo stroke width
#define TICK_LEN            6   // all ticks same length
#define TICK_R  84   // fixed radius for all ticks

// Hand lengths as % of shorter screen half-dim
#define HAND_MINUTE_FRAC   88
#define HAND_HOUR_FRAC     61

// ── Vertical battery icon dimensions ──────────────────────────────────────
#define BAT_W       14
#define BAT_H       20
#define BAT_NUB_W    6
#define BAT_NUB_H    3
#define BAT_BORDER   2

// ── UI layout ──────────────────────────────────────────────────────────────
#define TOP_H     28   // GOTHIC_24 (24px) + padding
#define BOTTOM_H  36   // LECO_32  (32px) + padding

// ── Bottom-strip separator dots ────────────────────────────────────────────
#define LECO36_PAIR_W  40   // approximate px width of two LECO_32 digits
#define DOT_SIZE        4   // separator square side length
#define DOT_GAP         1   // gap between number group and dot

static Window *s_window;
static Layer  *s_canvas_layer;

static int  s_hours, s_minutes;
static char s_hours_str[3];
static char s_minutes_str[3];
static char s_day_str[3];
static char s_month_str[3];

// ── Recolor PDC image ─────────────────────────────────────────────────────
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

// ── Returns inner tick point inset by tick_len toward center ──────────────
static GPoint tick_inner(GPoint outer, GPoint center, int tick_len) {
  int32_t dx = center.x - outer.x;
  int32_t dy = center.y - outer.y;
  int32_t dist = isqrt32(dx * dx + dy * dy);
  if (dist == 0) return outer;
  return (GPoint){
    outer.x + (int32_t)((int64_t)dx * tick_len / dist),
    outer.y + (int32_t)((int64_t)dy * tick_len / dist)
  };
}

// ── Draw all 12 tick marks: white halo, black body, fixed radius ──────────
static void draw_all_ticks(GContext *ctx, GPoint center) {
  for (int i = 0; i < 12; i++) {
    int32_t angle = i * TRIG_MAX_ANGLE / 12;
    int32_t sin_a = sin_lookup(angle);
    int32_t cos_a = cos_lookup(angle);

    int tick_r = TICK_R;

    GPoint outer = {
      center.x + (int32_t)((int64_t)sin_a * tick_r / TRIG_MAX_RATIO),
      center.y - (int32_t)((int64_t)cos_a * tick_r / TRIG_MAX_RATIO),
    };
    GPoint inner = tick_inner(outer, center, TICK_LEN);

    // halo
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, TICK_HALO_W);
    graphics_draw_line(ctx, outer, inner);
    // body
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, TICK_WIDTH);
    graphics_draw_line(ctx, outer, inner);
  }
}

// ── Draws the pentagon outline of a hand ──────────────────────────────────
static void draw_hand_border(GContext *ctx, GPoint center, int32_t angle,
                             int outer_dist, int half_width, int apex_ext,
                             int tail, int stroke_w, GColor color) {
  int32_t sin_a = sin_lookup(angle);
  int32_t cos_a = cos_lookup(angle);

  GPoint inner_pt = {
    center.x - (int32_t)tail       * sin_a / TRIG_MAX_RATIO,
    center.y + (int32_t)tail       * cos_a / TRIG_MAX_RATIO,
  };
  GPoint outer_pt = {
    center.x + (int32_t)outer_dist * sin_a / TRIG_MAX_RATIO,
    center.y - (int32_t)outer_dist * cos_a / TRIG_MAX_RATIO,
  };

  int x_diff = (int32_t)half_width * cos_a / TRIG_MAX_RATIO;
  int y_diff = (int32_t)half_width * sin_a / TRIG_MAX_RATIO;

  GPoint inner_right = { inner_pt.x + x_diff, inner_pt.y + y_diff };
  GPoint inner_left  = { inner_pt.x - x_diff, inner_pt.y - y_diff };
  GPoint outer_right = { outer_pt.x + x_diff, outer_pt.y + y_diff };
  GPoint outer_left  = { outer_pt.x - x_diff, outer_pt.y - y_diff };
  GPoint apex        = {
    outer_pt.x + (int32_t)apex_ext * sin_a / TRIG_MAX_RATIO,
    outer_pt.y - (int32_t)apex_ext * cos_a / TRIG_MAX_RATIO,
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

// ── Draw a dot at the number baseline, with fill + stroke ─────────────────
#define LECO32_BASELINE_Y  29  // y-offset of bottom of LECO_32 glyphs in BOTTOM_H strip

static void prv_draw_dot(GContext *ctx, int x, int y) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_fill_rect(ctx, GRect(x, y, DOT_SIZE, DOT_SIZE), 0, GCornerNone);
  graphics_draw_rect(ctx, GRect(x, y, DOT_SIZE, DOT_SIZE));
}

// ── Draw a colon (two stacked dots), fixed position relative to glyph ─────
static void draw_colon(GContext *ctx, int x, int strip_y) {
  prv_draw_dot(ctx, x, strip_y + 15);  // upper dot
  prv_draw_dot(ctx, x, strip_y + 26);  // lower dot
}

// ── Draw a period dot at number baseline ──────────────────────────────────
static void draw_period(GContext *ctx, int x, int strip_y) {
  prv_draw_dot(ctx, x, strip_y + LECO32_BASELINE_Y);
}

// ── Main draw callback ─────────────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int w = bounds.size.w;
  int h = bounds.size.h;

  // Clock center: vertically centered between the two strips
  GPoint center = GPoint(w / 2, TOP_H + (h - TOP_H - BOTTOM_H) / 2 - 2);

  int half       = (w < h ? w : h) / 2;
  int minute_len = half * HAND_MINUTE_FRAC / 100;
  int hour_len   = half * HAND_HOUR_FRAC   / 100;
  int bottom_y   = h - BOTTOM_H;

  int32_t min_angle  = s_minutes * TRIG_MAX_ANGLE / 60;
  int32_t hour_angle = (s_hours % 12) * TRIG_MAX_ANGLE / 12
                       + s_minutes * TRIG_MAX_ANGLE / 720;

  // ── Background ────────────────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ── Hour hand ─────────────────────────────────────────────────────────
  draw_hand_border(ctx, center, hour_angle,
                   hour_len - EDGE_WIDTH / 2,
                   HOUR_OUTER_W / 2 - EDGE_WIDTH / 2,
                   HOUR_OUTER_W / 2 - EDGE_WIDTH / 2,
                   HAND_TAIL, EDGE_WIDTH, GColorBlack);
  draw_hand_fill(ctx, center, hour_angle,
                 hour_len - EDGE_WIDTH / 2 - 1, HOUR_INNER_W, GColorBlack);

  // ── Minute hand ───────────────────────────────────────────────────────
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

  // ── Center pivot ──────────────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, center, 3);

  // ── Ticks: drawn after hands so they appear on top ────────────────────
  draw_all_ticks(ctx, center);

  // ── Top-left: weather icon + temperature ─────────────────────────────
  if (Weather_currentWeatherIcon) {
    gdraw_command_image_recolor(Weather_currentWeatherIcon, GColorWhite, GColorBlack);
    gdraw_command_image_draw(ctx, Weather_currentWeatherIcon, GPoint(3, 3));
  }
  if (Weather_weatherInfo.currentTemp != INT32_MIN) {
    char temp_str[6];
    snprintf(temp_str, sizeof(temp_str), "%d", Weather_weatherInfo.currentTemp);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, temp_str,
                       fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS),
                       GRect(31, 3, 60, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  // ── Top-right: battery icon + percentage ─────────────────────────────
  BatteryChargeState bat = battery_state_service_peek();
  uint8_t pct = (bat.charge_percent > 0) ? bat.charge_percent : 5;

  int bat_x = w - BAT_W - 4;
  int bat_y = 3;

  int nub_x = bat_x + (BAT_W - BAT_NUB_W) / 2;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(nub_x, bat_y, BAT_NUB_W, BAT_NUB_H), 0, GCornerNone);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, BAT_BORDER);
  graphics_draw_rect(ctx, GRect(bat_x, bat_y + BAT_NUB_H, BAT_W, BAT_H));

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

  char bat_str[4];
  snprintf(bat_str, sizeof(bat_str), "%d", pct);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, bat_str,
                     fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS),
                     GRect(w - BAT_W - 4 - 4 - 50, 3, 48, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // ── Bottom strip: time (left) and date (right) with custom dot seps ───
  GFont leco36  = fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS);
  int  strip_y  = h - BOTTOM_H;
  int  text_h   = BOTTOM_H;

  graphics_context_set_text_color(ctx, GColorBlack);

  // Time — "HH" : "MM", left-aligned from x=3
  int tx = 3;
  graphics_draw_text(ctx, s_hours_str,   leco36,
                     GRect(tx, strip_y, LECO36_PAIR_W, text_h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  tx += LECO36_PAIR_W + DOT_GAP;
  draw_colon(ctx, tx - 4, strip_y);
  tx += DOT_SIZE + DOT_GAP;
  graphics_draw_text(ctx, s_minutes_str, leco36,
                     GRect(tx - 4, strip_y, LECO36_PAIR_W, text_h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Date — "dd" . "mm" ., right-aligned to x=w−4
  // Layout width: PAIR + GAP + DOT + GAP + PAIR + GAP + DOT
  int date_w = LECO36_PAIR_W + DOT_GAP + DOT_SIZE + DOT_GAP
             + LECO36_PAIR_W + DOT_GAP + DOT_SIZE;
  int dx = w - 4 - date_w + 1;
  graphics_draw_text(ctx, s_day_str,   leco36,
                     GRect(dx + 1, strip_y, LECO36_PAIR_W, text_h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  dx += LECO36_PAIR_W + DOT_GAP;
  draw_period(ctx, dx - 1 + 1, strip_y - 1);
  dx += DOT_SIZE + DOT_GAP;
  graphics_draw_text(ctx, s_month_str, leco36,
                     GRect(dx, strip_y, LECO36_PAIR_W, text_h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  dx += LECO36_PAIR_W + DOT_GAP;
  draw_period(ctx, dx - 1, strip_y - 1);
}

// ── Time update ───────────────────────────────────────────────────────────
static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
  s_hours   = tick_time->tm_hour;
  s_minutes = tick_time->tm_min;
  snprintf(s_hours_str,   sizeof(s_hours_str),   "%02d", tick_time->tm_hour);
  snprintf(s_minutes_str, sizeof(s_minutes_str), "%02d", tick_time->tm_min);
  snprintf(s_day_str,     sizeof(s_day_str),     "%02d", tick_time->tm_mday);
  snprintf(s_month_str,   sizeof(s_month_str),   "%02d", tick_time->tm_mon + 1);

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

  Weather_init();
  messaging_init(on_message_received);
  battery_state_service_subscribe(on_battery_state_changed);
  tick_timer_service_subscribe(MINUTE_UNIT | HOUR_UNIT, handle_tick);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  handle_tick(t, MINUTE_UNIT | HOUR_UNIT);

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
