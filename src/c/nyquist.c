#include <pebble.h>
#include "weather.h"
#include "messaging.h"

// ── Clock dimensions: Emery set ───────────────────────────────────────────
#define EMERY_WATCHFACE_CENTER_Y             112  // Clock center Y position on Emery
#define EMERY_TICK_RADIUS                     85  // Tick ring radius in pixels
#define EMERY_MINUTE_HAND_LENGTH_PERCENT      89  // Minute hand length as % of half-screen
#define EMERY_HOUR_HAND_LENGTH_PERCENT        60  // Hour hand length as % of half-screen
#define EMERY_HAND_EDGE_WIDTH                 12  // Base hand stroke width
#define EMERY_HAND_HALO_WIDTH                  2  // Halo stroke width around hands
#define EMERY_MINUTE_HAND_OUTER_WIDTH         24  // Minute hand outer polygon width
#define EMERY_MINUTE_HAND_FILL_WIDTH          12  // Minute hand filled body width
#define EMERY_HOUR_HAND_OUTER_WIDTH           33  // Hour hand outer polygon width
#define EMERY_HOUR_HAND_FILL_WIDTH            22  // Hour hand filled body width
#define EMERY_HAND_TAIL_LENGTH                18  // Tail length behind hand pivot
#define EMERY_TICK_WIDTH                       2  // Tick body stroke width
#define EMERY_TICK_HALO_WIDTH                  6  // Tick halo stroke width
#define EMERY_TICK_LENGTH                      6  // Tick length toward center
#define EMERY_PIVOT_RADIUS                     3  // Center pivot circle radius

// ── Clock dimensions: Gabbro set ──────────────────────────────────────────
#define GABBRO_TICK_RADIUS_PERCENT            84  // Tick radius as % of round-screen half-size
#define GABBRO_MINUTE_HAND_LENGTH_PERCENT    104  // Minute hand length as % of gabbro tick radius
#define GABBRO_HOUR_HAND_LENGTH_PERCENT       60  // Hour hand length as % of gabbro tick radius
#define GABBRO_HAND_EDGE_WIDTH                11  // Base hand stroke width
#define GABBRO_HAND_HALO_WIDTH                 2  // Halo stroke width around hands
#define GABBRO_MINUTE_HAND_OUTER_WIDTH        26  // Minute hand outer polygon width
#define GABBRO_MINUTE_HAND_FILL_WIDTH         13  // Minute hand filled body width
#define GABBRO_HOUR_HAND_OUTER_WIDTH          38  // Hour hand outer polygon width
#define GABBRO_HOUR_HAND_FILL_WIDTH           26  // Hour hand filled body width
#define GABBRO_HAND_TAIL_LENGTH               16  // Tail length behind hand pivot
#define GABBRO_TICK_WIDTH                      2  // Tick body stroke width
#define GABBRO_TICK_HALO_WIDTH                 7  // Tick halo stroke width
#define GABBRO_TICK_LENGTH                     8  // Tick length toward center
#define GABBRO_PIVOT_RADIUS                    3  // Center pivot circle radius

// ── Vertical battery icon dimensions ──────────────────────────────────────
#define BATTERY_ICON_WIDTH        14   // outer width of the battery icon
#define BATTERY_ICON_HEIGHT       20   // outer height of the battery icon
#define BATTERY_ICON_NUB_WIDTH     6   // width of the battery nub
#define BATTERY_ICON_NUB_HEIGHT    3   // height of the battery nub
#define BATTERY_ICON_BORDER        2   // white border thickness inside the icon
#define BATTERY_ICON_FILL_GAP      2   // inset gap around the fill inside the icon

// ── Corner group offsets (gap from screen corner to group corner) ──────────
#define TOP_LEFT_GROUP_OFFSET_X       4   // top-left corner to weather text group
#define TOP_LEFT_GROUP_OFFSET_Y       1   // top-left corner to weather text group
#define TOP_RIGHT_GROUP_OFFSET_X      4   // top-right corner to battery group
#define TOP_RIGHT_GROUP_OFFSET_Y      1   // top-right corner to battery group
#define BOTTOM_LEFT_GROUP_OFFSET_X    4   // bottom-left corner to time group
#define BOTTOM_LEFT_GROUP_OFFSET_Y    3   // bottom-left corner to time group
#define BOTTOM_RIGHT_GROUP_OFFSET_X   4   // bottom-right corner to date group
#define BOTTOM_RIGHT_GROUP_OFFSET_Y   3   // bottom-right corner to date group

// ── Font metric correction offsets (compensate for internal font padding) ─
#define TOP_CORNER_VERTICAL_CORRECTION     (-5)  // top corner content is this many px too low
#define BOTTOM_STRIP_VERTICAL_CORRECTION   (-2)  // bottom strip text is this many px too low

// ── Bottom strip layout ───────────────────────────────────────────────────
#define BOTTOM_STRIP_TEXT_WIDTH_LIMIT      72   // max width budget for Bitham 30 bottom-strip labels
#define BOTTOM_STRIP_SEPARATOR_WIDTH        6   // separator column width

static Window *s_window;
static Layer  *s_canvas_layer;

static int  s_hours, s_minutes;
static char s_hours_str[3];
static char s_minutes_str[3];
static char s_day_str[3];
static char s_month_str[4];

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
static void draw_all_ticks(GContext *ctx, GPoint center, int tick_r, int tick_len,
                           int tick_halo_width, int tick_width,
                           GColor halo_color, GColor body_color) {
  for (int i = 0; i < 12; i++) {
    int32_t angle = i * TRIG_MAX_ANGLE / 12;
    int32_t sin_a = sin_lookup(angle);
    int32_t cos_a = cos_lookup(angle);

    GPoint outer = {
      center.x + (int32_t)((int64_t)sin_a * tick_r / TRIG_MAX_RATIO),
      center.y - (int32_t)((int64_t)cos_a * tick_r / TRIG_MAX_RATIO),
    };
    GPoint inner = tick_inner(outer, center, tick_len);

    // halo
    graphics_context_set_stroke_color(ctx, halo_color);
    graphics_context_set_stroke_width(ctx, tick_halo_width);
    graphics_draw_line(ctx, outer, inner);
    // body
    graphics_context_set_stroke_color(ctx, body_color);
    graphics_context_set_stroke_width(ctx, tick_width);
    graphics_draw_line(ctx, outer, inner);
  }
}

// ── Draws the pentagon outline of a hand ──────────────────────────────────
static int32_t prv_trig_offset_trunc(int32_t value, int32_t trig) {
  return ((int64_t)value * trig) / TRIG_MAX_RATIO;
}

static int32_t prv_trig_offset_compensated(int32_t value, int32_t trig) {
  int64_t scaled = (int64_t)value * trig;
  int32_t base = scaled / TRIG_MAX_RATIO;
  if (scaled % TRIG_MAX_RATIO != 0) {
    base += (scaled > 0 ? 1 : -1);
  }
  return base;
}

static int64_t prv_abs64(int64_t v) {
  return v < 0 ? -v : v;
}

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
    center.x + prv_trig_offset_compensated(outer_dist, sin_a),
    center.y - prv_trig_offset_compensated(outer_dist, cos_a),
  };

  int32_t x_diff_left  = prv_trig_offset_trunc(half_width, cos_a);
  int32_t y_diff_left  = prv_trig_offset_trunc(half_width, sin_a);
  int32_t x_diff_right = prv_trig_offset_compensated(half_width, cos_a);
  int32_t y_diff_right = prv_trig_offset_compensated(half_width, sin_a);

  GPoint inner_right = { inner_pt.x + x_diff_right, inner_pt.y + y_diff_right };
  GPoint inner_left  = { inner_pt.x - x_diff_left,  inner_pt.y - y_diff_left };
  GPoint outer_right = { outer_pt.x + x_diff_right, outer_pt.y + y_diff_right };
  GPoint outer_left  = { outer_pt.x - x_diff_left,  outer_pt.y - y_diff_left };
  GPoint apex        = {
    outer_pt.x + prv_trig_offset_compensated(apex_ext, sin_a),
    outer_pt.y - prv_trig_offset_compensated(apex_ext, cos_a),
  };

  // Additional correction on top of side compensation:
  // shift inner_left and inner_right together by 1 px if it brings
  // the watch center closer to the hand center axis.
  int32_t inner_mid_x = (inner_left.x + inner_right.x) / 2;
  int32_t inner_mid_y = (inner_left.y + inner_right.y) / 2;
  int32_t outer_mid_x = (outer_left.x + outer_right.x) / 2;
  int32_t outer_mid_y = (outer_left.y + outer_right.y) / 2;
  int32_t axis_dx = outer_mid_x - inner_mid_x;
  int32_t axis_dy = outer_mid_y - inner_mid_y;

  if (axis_dx != 0 || axis_dy != 0) {
    const int shifts[5][2] = { {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    int best_index = 0;
    int64_t best_error = -1;

    for (int i = 0; i < 5; i++) {
      int32_t sx = shifts[i][0];
      int32_t sy = shifts[i][1];
      int32_t test_inner_x = inner_mid_x + sx;
      int32_t test_inner_y = inner_mid_y + sy;
      int64_t cross = (int64_t)axis_dx * (center.y - test_inner_y)
                    - (int64_t)axis_dy * (center.x - test_inner_x);
      int64_t err = prv_abs64(cross);
      if (best_error < 0 || err < best_error) {
        best_error = err;
        best_index = i;
      }
    }

    if (best_index != 0) {
      int32_t sx = shifts[best_index][0];
      int32_t sy = shifts[best_index][1];
      inner_left.x += sx;
      inner_left.y += sy;
      inner_right.x += sx;
      inner_right.y += sy;
    }
  }

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
                           int length, int width, int tail, GColor color) {
  int hw = width / 2;
  GPoint pts[5] = {
    {-hw, tail}, {hw, tail}, {hw, -length}, {0, -(length + hw)}, {-hw, -length}
  };
  GPathInfo info = { .num_points = 5, .points = pts };
  GPath *path = gpath_create(&info);
  gpath_rotate_to(path, angle);
  gpath_move_to(path, center);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

// ── Separator dot helper ──────────────────────────────────────────────────
#define SEP_DOT_SIZE  4

static void prv_draw_sep_dot(GContext *ctx, int x, int y, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(x, y, SEP_DOT_SIZE, SEP_DOT_SIZE), 0, GCornerNone);
}

// Colon: two dots vertically, centred on the actual glyph height
static void draw_sep_colon(GContext *ctx, int x, int glyph_center_y, GColor color) {
  prv_draw_sep_dot(ctx, x, glyph_center_y - 7, color);
  prv_draw_sep_dot(ctx, x, glyph_center_y + 3, color);
}

// ── Main draw callback ─────────────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int w = bounds.size.w;
  int h = bounds.size.h;
  bool invert_colors = messaging_invert_colors();
  GColor bg = invert_colors ? GColorBlack : GColorWhite;
  GColor fg = invert_colors ? GColorWhite : GColorBlack;
  GColor minute_halo = bg;
#if defined(PBL_PLATFORM_GABBRO)
  const bool is_gabbro = true;
#else
  const bool is_gabbro = false;
#endif
  bool show_top_left = !is_gabbro && messaging_show_top_left();
  bool show_top_right = !is_gabbro && messaging_show_top_right();
  bool show_bottom_left = !is_gabbro && messaging_show_bottom_left();
  bool show_bottom_right = !is_gabbro && messaging_show_bottom_right();

  // Clock center: shifted upward from vertical center between strips
  GPoint center = is_gabbro ? GPoint(w / 2, h / 2) : GPoint(w / 2, EMERY_WATCHFACE_CENTER_Y);

  int half = (w < h ? w : h) / 2;
  int tick_r = is_gabbro
    ? ((half * GABBRO_TICK_RADIUS_PERCENT + 50) / 100)
    : EMERY_TICK_RADIUS;
  int minute_len = is_gabbro
    ? (tick_r * GABBRO_MINUTE_HAND_LENGTH_PERCENT / 100)
    : (half * EMERY_MINUTE_HAND_LENGTH_PERCENT / 100);
  int hour_len = is_gabbro
    ? (tick_r * GABBRO_HOUR_HAND_LENGTH_PERCENT / 100)
    : (half * EMERY_HOUR_HAND_LENGTH_PERCENT / 100);
  int hand_edge_w = is_gabbro ? GABBRO_HAND_EDGE_WIDTH : EMERY_HAND_EDGE_WIDTH;
  int hand_halo_w = is_gabbro ? GABBRO_HAND_HALO_WIDTH : EMERY_HAND_HALO_WIDTH;
  int minute_outer_w = is_gabbro ? GABBRO_MINUTE_HAND_OUTER_WIDTH : EMERY_MINUTE_HAND_OUTER_WIDTH;
  int minute_fill_w = is_gabbro ? GABBRO_MINUTE_HAND_FILL_WIDTH : EMERY_MINUTE_HAND_FILL_WIDTH;
  int hour_outer_w = is_gabbro ? GABBRO_HOUR_HAND_OUTER_WIDTH : EMERY_HOUR_HAND_OUTER_WIDTH;
  int hour_fill_w = is_gabbro ? GABBRO_HOUR_HAND_FILL_WIDTH : EMERY_HOUR_HAND_FILL_WIDTH;
  int hand_tail = is_gabbro ? GABBRO_HAND_TAIL_LENGTH : EMERY_HAND_TAIL_LENGTH;
  int tick_w = is_gabbro ? GABBRO_TICK_WIDTH : EMERY_TICK_WIDTH;
  int tick_halo_w = is_gabbro ? GABBRO_TICK_HALO_WIDTH : EMERY_TICK_HALO_WIDTH;
  int tick_len = is_gabbro ? GABBRO_TICK_LENGTH : EMERY_TICK_LENGTH;
  int pivot_r = is_gabbro ? GABBRO_PIVOT_RADIUS : EMERY_PIVOT_RADIUS;

  int32_t min_angle  = s_minutes * TRIG_MAX_ANGLE / 60;
  int32_t hour_angle = (s_hours % 12) * TRIG_MAX_ANGLE / 12
                       + s_minutes * TRIG_MAX_ANGLE / 720;

  // ── Background ────────────────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ── Hour hand ─────────────────────────────────────────────────────────
  draw_hand_border(ctx, center, hour_angle,
                   hour_len - hand_edge_w / 2,
                   hour_outer_w / 2 - hand_edge_w / 2,
                   hour_outer_w / 2 - hand_edge_w / 2,
                   hand_tail, hand_edge_w, fg);
  draw_hand_fill(ctx, center, hour_angle,
                 hour_len - hand_edge_w / 2 - 1, hour_fill_w, hand_tail, fg);

  // ── Minute hand ───────────────────────────────────────────────────────
  draw_hand_border(ctx, center, min_angle,
                   minute_len - hand_edge_w / 2,
                   minute_outer_w / 2 - hand_edge_w / 2,
                   minute_outer_w / 2 - hand_edge_w / 2,
                   hand_tail, hand_edge_w + 2 * hand_halo_w, minute_halo);
  draw_hand_border(ctx, center, min_angle,
                   minute_len - hand_edge_w / 2,
                   minute_outer_w / 2 - hand_edge_w / 2,
                   minute_outer_w / 2 - hand_edge_w / 2,
                   hand_tail, hand_edge_w, fg);
  draw_hand_fill(ctx, center, min_angle,
                 minute_len - hand_edge_w / 2 - 1, minute_fill_w, hand_tail, fg);

  // ── Center pivot ──────────────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_circle(ctx, center, pivot_r);

  // ── Ticks: drawn after hands so they appear on top ────────────────────
  draw_all_ticks(ctx, center, tick_r, tick_len, tick_halo_w, tick_w, bg, fg);

  // ── Top-left: temperature above, weather icon below ──────────────────
  GFont bitham30_corner = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  int corner_text_h = 30; // measure budget
  if (show_top_left) {
    if (Weather_weatherInfo.currentTemp != INT32_MIN) {
      char temp_str[6];
      snprintf(temp_str, sizeof(temp_str), "%d", Weather_weatherInfo.currentTemp);
      GSize sz_temp = graphics_text_layout_get_content_size(temp_str, bitham30_corner,
        GRect(0, 0, 60, corner_text_h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
      int top_left_group_x = TOP_LEFT_GROUP_OFFSET_X;
      int top_left_group_y = TOP_LEFT_GROUP_OFFSET_Y + TOP_CORNER_VERTICAL_CORRECTION;
      int top_left_group_icon_y = top_left_group_y + sz_temp.h + 4;
      graphics_context_set_text_color(ctx, fg);
      graphics_draw_text(ctx, temp_str, bitham30_corner,
                         GRect(top_left_group_x, top_left_group_y, sz_temp.w + 2, corner_text_h),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      if (Weather_currentWeatherIcon) {
        gdraw_command_image_recolor(Weather_currentWeatherIcon, bg, fg);
        gdraw_command_image_draw(ctx, Weather_currentWeatherIcon, GPoint(top_left_group_x, top_left_group_icon_y));
      }
    } else if (Weather_currentWeatherIcon) {
      int top_left_group_x = TOP_LEFT_GROUP_OFFSET_X;
      int top_left_group_y = TOP_LEFT_GROUP_OFFSET_Y + TOP_CORNER_VERTICAL_CORRECTION;
      gdraw_command_image_recolor(Weather_currentWeatherIcon, bg, fg);
      gdraw_command_image_draw(ctx, Weather_currentWeatherIcon, GPoint(top_left_group_x, top_left_group_y));
    }
  }

  // ── Top-right: percentage above, battery icon below ──────────────────
  BatteryChargeState bat = battery_state_service_peek();
  uint8_t pct = (bat.charge_percent > 0) ? bat.charge_percent : 5;

  if (show_top_right) {
    char bat_str[4];
    snprintf(bat_str, sizeof(bat_str), "%d", pct);
    GSize sz_bat = graphics_text_layout_get_content_size(bat_str, bitham30_corner,
      GRect(0, 0, 60, corner_text_h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    int top_right_group_right = w - TOP_RIGHT_GROUP_OFFSET_X;
    int top_right_group_y = TOP_RIGHT_GROUP_OFFSET_Y + TOP_CORNER_VERTICAL_CORRECTION;
    int bat_text_x = top_right_group_right - sz_bat.w;
    int bat_icon_y = top_right_group_y + sz_bat.h + 4;

    int bat_x = top_right_group_right - BATTERY_ICON_WIDTH;
    int bat_y = bat_icon_y;

    int nub_x = bat_x + (BATTERY_ICON_WIDTH - BATTERY_ICON_NUB_WIDTH) / 2;
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, GRect(nub_x, bat_y, BATTERY_ICON_NUB_WIDTH, BATTERY_ICON_NUB_HEIGHT), 0, GCornerNone);

    // Rect 1: foreground outer body
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, GRect(bat_x, bat_y + BATTERY_ICON_NUB_HEIGHT, BATTERY_ICON_WIDTH, BATTERY_ICON_HEIGHT), 2, GCornersAll);

    // Rect 2: background inner area
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, GRect(bat_x + BATTERY_ICON_BORDER, bat_y + BATTERY_ICON_NUB_HEIGHT + BATTERY_ICON_BORDER,
                                  BATTERY_ICON_WIDTH - 2 * BATTERY_ICON_BORDER, BATTERY_ICON_HEIGHT - 2 * BATTERY_ICON_BORDER), 1, GCornersAll);

    // Rect 3: charge fill
    int fill_inner_h = BATTERY_ICON_HEIGHT - 2 * (BATTERY_ICON_BORDER + BATTERY_ICON_FILL_GAP);
    int fill_h = fill_inner_h * pct / 100;
    int fill_y = bat_y + BATTERY_ICON_NUB_HEIGHT + BATTERY_ICON_BORDER + BATTERY_ICON_FILL_GAP + (fill_inner_h - fill_h);
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, pct <= 20 ? GColorRed : fg);
#else
    graphics_context_set_fill_color(ctx, fg);
#endif
    if (bat.is_charging) {
      graphics_fill_rect(ctx, GRect(bat_x + BATTERY_ICON_BORDER + BATTERY_ICON_FILL_GAP, bat_y + BATTERY_ICON_NUB_HEIGHT + BATTERY_ICON_BORDER + BATTERY_ICON_FILL_GAP,
                                    BATTERY_ICON_WIDTH - 2 * (BATTERY_ICON_BORDER + BATTERY_ICON_FILL_GAP), fill_inner_h), 0, GCornerNone);
    } else {
      graphics_fill_rect(ctx, GRect(bat_x + BATTERY_ICON_BORDER + BATTERY_ICON_FILL_GAP, fill_y,
                                    BATTERY_ICON_WIDTH - 2 * (BATTERY_ICON_BORDER + BATTERY_ICON_FILL_GAP), fill_h), 0, GCornerNone);
    }

    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, bat_str, bitham30_corner,
                       GRect(bat_text_x, top_right_group_y, sz_bat.w + 2, corner_text_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  // ── Bottom strip: time (left) and date (right) ────────────────────────
  if (show_bottom_left || show_bottom_right) {
    GFont bitham30_strip = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
    int text_h   = h - EMERY_WATCHFACE_CENTER_Y;

    graphics_context_set_text_color(ctx, fg);

    // Measure hours first to derive strip_text_y from bottom gap
    GSize sz_hours = graphics_text_layout_get_content_size(s_hours_str, bitham30_strip,
      GRect(0, 0, BOTTOM_STRIP_TEXT_WIDTH_LIMIT + 10, text_h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    int strip_group_bottom_left = h - BOTTOM_LEFT_GROUP_OFFSET_Y;
    int strip_group_bottom_right = h - BOTTOM_RIGHT_GROUP_OFFSET_Y;
    int strip_text_y = strip_group_bottom_left - sz_hours.h + BOTTOM_STRIP_VERTICAL_CORRECTION;

    // Time: HH : MM
    if (show_bottom_left) {
      int tx = BOTTOM_LEFT_GROUP_OFFSET_X;
      graphics_draw_text(ctx, s_hours_str, bitham30_strip,
                         GRect(tx, strip_text_y, BOTTOM_STRIP_TEXT_WIDTH_LIMIT, text_h),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      tx += sz_hours.w + 2;
      int colon_center_y = strip_text_y + sz_hours.h / 2 + 5;
      draw_sep_colon(ctx, tx, colon_center_y, fg);
      tx += BOTTOM_STRIP_SEPARATOR_WIDTH;
      graphics_draw_text(ctx, s_minutes_str, bitham30_strip,
                         GRect(tx, strip_text_y, BOTTOM_STRIP_TEXT_WIDTH_LIMIT, text_h),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }

    // Date: two-line block, right-aligned (day over month abbreviation)
    GSize sz_month = graphics_text_layout_get_content_size(s_month_str, bitham30_strip,
      GRect(0, 0, BOTTOM_STRIP_TEXT_WIDTH_LIMIT + 10, text_h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    GSize sz_day   = graphics_text_layout_get_content_size(s_day_str, bitham30_strip,
      GRect(0, 0, BOTTOM_STRIP_TEXT_WIDTH_LIMIT + 10, text_h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

    if (show_bottom_right) {
      int date_right = w - BOTTOM_RIGHT_GROUP_OFFSET_X;
      int date_gap_y = -5;
      int date_block_h = sz_day.h + date_gap_y + sz_month.h;
      int day_y = strip_group_bottom_right - date_block_h + BOTTOM_STRIP_VERTICAL_CORRECTION;
      int month_y = day_y + sz_day.h + date_gap_y;

      int day_x = date_right - sz_day.w;
      graphics_draw_text(ctx, s_day_str, bitham30_strip,
                         GRect(day_x, day_y, BOTTOM_STRIP_TEXT_WIDTH_LIMIT, text_h),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

      int month_x = date_right - sz_month.w;
      graphics_draw_text(ctx, s_month_str, bitham30_strip,
                         GRect(month_x, month_y, BOTTOM_STRIP_TEXT_WIDTH_LIMIT, text_h),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
  }
}

// ── Time update ───────────────────────────────────────────────────────────
static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
  s_hours   = tick_time->tm_hour;
  s_minutes = tick_time->tm_min;
  snprintf(s_hours_str,   sizeof(s_hours_str),   "%02d", tick_time->tm_hour);
  snprintf(s_minutes_str, sizeof(s_minutes_str), "%02d", tick_time->tm_min);
  snprintf(s_day_str,     sizeof(s_day_str),     "%d", tick_time->tm_mday);
  static const char *month_abbrevs[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  snprintf(s_month_str, sizeof(s_month_str), "%s", month_abbrevs[tick_time->tm_mon]);

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
