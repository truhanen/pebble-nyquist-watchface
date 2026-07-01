#include <pebble.h>
#include "weather.h"
#include "messaging.h"

// ── Clock dimensions: Emery set ───────────────────────────────────────────
#define EMERY_WATCHFACE_CENTER_Y             110  // Clock center Y position on Emery
#define EMERY_TICK_RADIUS                     85  // Tick ring radius in pixels
#define EMERY_HAND_EDGE_WIDTH                  9  // Base hand stroke width
#define EMERY_HAND_HALO_WIDTH                  2  // Halo stroke width around hands
#define EMERY_TICK_WIDTH                       2  // Tick body stroke width
#define EMERY_TICK_HALO_WIDTH                  6  // Tick halo stroke width
#define EMERY_TICK_LENGTH                      6  // Tick length toward center
#define EMERY_PIVOT_RADIUS                     3  // Center pivot circle radius

// ── Clock dimensions: Gabbro set ──────────────────────────────────────────
#define GABBRO_TICK_RADIUS_PERCENT            84  // Tick radius as % of round-screen half-size
#define GABBRO_HAND_EDGE_WIDTH                12  // Base hand stroke width (scaled from Emery)
#define GABBRO_HAND_HALO_WIDTH                 2  // Halo stroke width around hands
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
static char s_hours_str[4];
static char s_minutes_str[3];
static char s_meridiem_str[3];
static char s_day_str[3];
static char s_month_str[4];

static int prv_temperature_for_display(int temp_c) {
  if (!messaging_use_fahrenheit()) {
    return temp_c;
  }
  return temp_c * 9 / 5 + 32;
}

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

static int64_t prv_abs64(int64_t v);

// ── Draw all 12 tick marks: white halo, black body, fixed radius ──────────
static void draw_all_ticks(GContext *ctx, GPoint center, int tick_r, int tick_len,
                           int tick_halo_width, int tick_width,
                           GColor halo_color, GColor body_color) {
  const int shifts[9][2] = {
    {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
  };

  for (int i = 0; i < 12; i++) {
    int32_t angle = i * TRIG_MAX_ANGLE / 12;
    int32_t sin_a = sin_lookup(angle);
    int32_t cos_a = cos_lookup(angle);

    GPoint outer = {
      center.x + (int32_t)((int64_t)sin_a * tick_r / TRIG_MAX_RATIO),
      center.y - (int32_t)((int64_t)cos_a * tick_r / TRIG_MAX_RATIO),
    };
    GPoint inner = tick_inner(outer, center, tick_len);

    // Shared pixel nudge: shift both endpoints together to keep the tick
    // segment direction aligned with the intended radial angle.
    int best_index = 0;
    int64_t best_error = -1;
    for (int k = 0; k < 9; k++) {
      int32_t sx = shifts[k][0];
      int32_t sy = shifts[k][1];
      int32_t seg_dx = (outer.x + sx) - (inner.x + sx);
      int32_t seg_dy = (outer.y + sy) - (inner.y + sy);
      int64_t cross = (int64_t)sin_a * seg_dy + (int64_t)cos_a * seg_dx;
      int64_t err = prv_abs64(cross);
      if (best_error < 0 || err < best_error) {
        best_error = err;
        best_index = k;
      }
    }
    if (best_index != 0) {
      int32_t sx = shifts[best_index][0];
      int32_t sy = shifts[best_index][1];
      outer.x += sx;
      outer.y += sy;
      inner.x += sx;
      inner.y += sy;
    }

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

static int64_t prv_abs64(int64_t v) {
  return v < 0 ? -v : v;
}

typedef struct {
  int16_t x;
  int16_t y;
} RelPoint;

typedef struct {
  RelPoint inner_left;
  RelPoint inner_right;
  RelPoint outer_left;
  RelPoint outer_right;
  RelPoint apex;
} HandPose;


// Pre-calculated snapped minute/hour poses for Emery (center-relative).
static const HandPose s_minute_base_00_to_07_emery[8] = {
  { .inner_left = { .x = -7, .y = 18 }, .inner_right = { .x = 8, .y = 18 }, .outer_left = { .x = -7, .y = -86 }, .outer_right = { .x = 8, .y = -86 }, .apex = { .x = 0, .y = -92 } },
  { .inner_left = { .x = -9, .y = 17 }, .inner_right = { .x = 6, .y = 19 }, .outer_left = { .x = 2, .y = -86 }, .outer_right = { .x = 16, .y = -85 }, .apex = { .x = 10, .y = -91 } },
  { .inner_left = { .x = -11, .y = 16 }, .inner_right = { .x = 4, .y = 19 }, .outer_left = { .x = 11, .y = -86 }, .outer_right = { .x = 25, .y = -83 }, .apex = { .x = 19, .y = -90 } },
  { .inner_left = { .x = -13, .y = 15 }, .inner_right = { .x = 2, .y = 19 }, .outer_left = { .x = 19, .y = -84 }, .outer_right = { .x = 34, .y = -79 }, .apex = { .x = 28, .y = -87 } },
  { .inner_left = { .x = -14, .y = 13 }, .inner_right = { .x = 0, .y = 19 }, .outer_left = { .x = 28, .y = -82 }, .outer_right = { .x = 42, .y = -76 }, .apex = { .x = 37, .y = -84 } },
  { .inner_left = { .x = -15, .y = 12 }, .inner_right = { .x = -3, .y = 19 }, .outer_left = { .x = 37, .y = -78 }, .outer_right = { .x = 49, .y = -71 }, .apex = { .x = 46, .y = -80 } },
  { .inner_left = { .x = -17, .y = 10 }, .inner_right = { .x = -5, .y = 19 }, .outer_left = { .x = 44, .y = -74 }, .outer_right = { .x = 57, .y = -65 }, .apex = { .x = 54, .y = -74 } },
  { .inner_left = { .x = -18, .y = 8 }, .inner_right = { .x = -6, .y = 18 }, .outer_left = { .x = 52, .y = -69 }, .outer_right = { .x = 63, .y = -59 }, .apex = { .x = 62, .y = -68 } },
};

static const HandPose s_hour_base_00_to_89_emery_5min[18] = {
  /* 0:00-0:02 */ { .inner_left = { .x = -12, .y = 18 }, .inner_right = { .x = 13, .y = 18 }, .outer_left = { .x = -12, .y = -54 }, .outer_right = { .x = 13, .y = -54 }, .apex = { .x = 0, .y = -64 } },
  /* 0:03-0:07 */ { .inner_left = { .x = -13, .y = 17 }, .inner_right = { .x = 12, .y = 19 }, .outer_left = { .x = -10, .y = -54 }, .outer_right = { .x = 15, .y = -53 }, .apex = { .x = 3, .y = -64 } },
  /* 0:08-0:12 */ { .inner_left = { .x = -14, .y = 17 }, .inner_right = { .x = 11, .y = 19 }, .outer_left = { .x = -8, .y = -55 }, .outer_right = { .x = 17, .y = -53 }, .apex = { .x = 6, .y = -64 } },
  /* 0:13-0:17 */ { .inner_left = { .x = -15, .y = 16 }, .inner_right = { .x = 10, .y = 19 }, .outer_left = { .x = -5, .y = -55 }, .outer_right = { .x = 19, .y = -52 }, .apex = { .x = 8, .y = -63 } },
  /* 0:18-0:22 */ { .inner_left = { .x = -15, .y = 16 }, .inner_right = { .x = 9, .y = 20 }, .outer_left = { .x = -3, .y = -55 }, .outer_right = { .x = 22, .y = -51 }, .apex = { .x = 11, .y = -63 } },
  /* 0:23-0:27 */ { .inner_left = { .x = -16, .y = 15 }, .inner_right = { .x = 8, .y = 20 }, .outer_left = { .x = -1, .y = -55 }, .outer_right = { .x = 24, .y = -50 }, .apex = { .x = 14, .y = -62 } },
  /* 0:28-0:32 */ { .inner_left = { .x = -17, .y = 14 }, .inner_right = { .x = 7, .y = 21 }, .outer_left = { .x = 2, .y = -55 }, .outer_right = { .x = 26, .y = -49 }, .apex = { .x = 17, .y = -62 } },
  /* 0:33-0:37 */ { .inner_left = { .x = -17, .y = 13 }, .inner_right = { .x = 7, .y = 21 }, .outer_left = { .x = 4, .y = -55 }, .outer_right = { .x = 28, .y = -48 }, .apex = { .x = 19, .y = -61 } },
  /* 0:38-0:42 */ { .inner_left = { .x = -18, .y = 13 }, .inner_right = { .x = 6, .y = 21 }, .outer_left = { .x = 7, .y = -55 }, .outer_right = { .x = 30, .y = -46 }, .apex = { .x = 22, .y = -60 } },
  /* 0:43-0:47 */ { .inner_left = { .x = -18, .y = 12 }, .inner_right = { .x = 5, .y = 21 }, .outer_left = { .x = 9, .y = -55 }, .outer_right = { .x = 32, .y = -45 }, .apex = { .x = 24, .y = -59 } },
  /* 0:48-0:52 */ { .inner_left = { .x = -19, .y = 11 }, .inner_right = { .x = 4, .y = 22 }, .outer_left = { .x = 11, .y = -54 }, .outer_right = { .x = 34, .y = -44 }, .apex = { .x = 27, .y = -58 } },
  /* 0:53-0:57 */ { .inner_left = { .x = -19, .y = 10 }, .inner_right = { .x = 3, .y = 22 }, .outer_left = { .x = 14, .y = -54 }, .outer_right = { .x = 36, .y = -42 }, .apex = { .x = 30, .y = -57 } },
  /* 0:58-1:02 */ { .inner_left = { .x = -20, .y = 9 }, .inner_right = { .x = 2, .y = 22 }, .outer_left = { .x = 16, .y = -53 }, .outer_right = { .x = 38, .y = -41 }, .apex = { .x = 32, .y = -55 } },
  /* 1:03-1:07 */ { .inner_left = { .x = -20, .y = 8 }, .inner_right = { .x = 1, .y = 22 }, .outer_left = { .x = 18, .y = -52 }, .outer_right = { .x = 40, .y = -39 }, .apex = { .x = 34, .y = -54 } },
  /* 1:08-1:12 */ { .inner_left = { .x = -21, .y = 8 }, .inner_right = { .x = 0, .y = 22 }, .outer_left = { .x = 21, .y = -51 }, .outer_right = { .x = 41, .y = -37 }, .apex = { .x = 37, .y = -52 } },
  /* 1:13-1:17 */ { .inner_left = { .x = -21, .y = 7 }, .inner_right = { .x = -1, .y = 22 }, .outer_left = { .x = 23, .y = -50 }, .outer_right = { .x = 43, .y = -35 }, .apex = { .x = 39, .y = -51 } },
  /* 1:18-1:22 */ { .inner_left = { .x = -21, .y = 6 }, .inner_right = { .x = -2, .y = 22 }, .outer_left = { .x = 25, .y = -49 }, .outer_right = { .x = 44, .y = -33 }, .apex = { .x = 41, .y = -49 } },
  /* 1:23-1:27 */ { .inner_left = { .x = -21, .y = 5 }, .inner_right = { .x = -3, .y = 22 }, .outer_left = { .x = 27, .y = -48 }, .outer_right = { .x = 46, .y = -31 }, .apex = { .x = 43, .y = -47 } },
};

// Pre-calculated snapped minute/hour poses for Gabbro (center-relative).
static const HandPose s_minute_base_00_to_07_gabbro[8] = {
  { .inner_left = { .x = -9, .y = 23 }, .inner_right = { .x = 10, .y = 23 }, .outer_left = { .x = -9, .y = -110 }, .outer_right = { .x = 10, .y = -110 }, .apex = { .x = 0, .y = -118 } },
  { .inner_left = { .x = -12, .y = 22 }, .inner_right = { .x = 7, .y = 24 }, .outer_left = { .x = 2, .y = -110 }, .outer_right = { .x = 21, .y = -108 }, .apex = { .x = 12, .y = -117 } },
  { .inner_left = { .x = -14, .y = 21 }, .inner_right = { .x = 5, .y = 24 }, .outer_left = { .x = 14, .y = -110 }, .outer_right = { .x = 32, .y = -106 }, .apex = { .x = 25, .y = -115 } },
  { .inner_left = { .x = -16, .y = 19 }, .inner_right = { .x = 2, .y = 25 }, .outer_left = { .x = 25, .y = -108 }, .outer_right = { .x = 43, .y = -102 }, .apex = { .x = 36, .y = -112 } },
  { .inner_left = { .x = -18, .y = 17 }, .inner_right = { .x = -1, .y = 25 }, .outer_left = { .x = 36, .y = -104 }, .outer_right = { .x = 53, .y = -97 }, .apex = { .x = 48, .y = -108 } },
  { .inner_left = { .x = -20, .y = 15 }, .inner_right = { .x = -3, .y = 25 }, .outer_left = { .x = 47, .y = -100 }, .outer_right = { .x = 63, .y = -91 }, .apex = { .x = 59, .y = -102 } },
  { .inner_left = { .x = -21, .y = 13 }, .inner_right = { .x = -6, .y = 24 }, .outer_left = { .x = 57, .y = -95 }, .outer_right = { .x = 72, .y = -83 }, .apex = { .x = 69, .y = -95 } },
  { .inner_left = { .x = -22, .y = 11 }, .inner_right = { .x = -8, .y = 23 }, .outer_left = { .x = 67, .y = -88 }, .outer_right = { .x = 81, .y = -75 }, .apex = { .x = 79, .y = -88 } },
};

static const HandPose s_hour_base_00_to_89_gabbro_5min[18] = {
  /* 0:00-0:02 */ { .inner_left = { .x = -16, .y = 23 }, .inner_right = { .x = 16, .y = 23 }, .outer_left = { .x = -16, .y = -69 }, .outer_right = { .x = 16, .y = -69 }, .apex = { .x = 0, .y = -82 } },
  /* 0:03-0:07 */ { .inner_left = { .x = -17, .y = 22 }, .inner_right = { .x = 15, .y = 24 }, .outer_left = { .x = -13, .y = -70 }, .outer_right = { .x = 19, .y = -68 }, .apex = { .x = 4, .y = -82 } },
  /* 0:08-0:12 */ { .inner_left = { .x = -18, .y = 22 }, .inner_right = { .x = 14, .y = 24 }, .outer_left = { .x = -10, .y = -70 }, .outer_right = { .x = 22, .y = -67 }, .apex = { .x = 7, .y = -82 } },
  /* 0:13-0:17 */ { .inner_left = { .x = -19, .y = 21 }, .inner_right = { .x = 13, .y = 25 }, .outer_left = { .x = -7, .y = -70 }, .outer_right = { .x = 25, .y = -66 }, .apex = { .x = 11, .y = -81 } },
  /* 0:18-0:22 */ { .inner_left = { .x = -20, .y = 20 }, .inner_right = { .x = 12, .y = 25 }, .outer_left = { .x = -4, .y = -71 }, .outer_right = { .x = 28, .y = -65 }, .apex = { .x = 14, .y = -81 } },
  /* 0:23-0:27 */ { .inner_left = { .x = -21, .y = 19 }, .inner_right = { .x = 11, .y = 26 }, .outer_left = { .x = -1, .y = -71 }, .outer_right = { .x = 31, .y = -64 }, .apex = { .x = 18, .y = -80 } },
  /* 0:28-0:32 */ { .inner_left = { .x = -21, .y = 18 }, .inner_right = { .x = 10, .y = 26 }, .outer_left = { .x = 2, .y = -71 }, .outer_right = { .x = 33, .y = -63 }, .apex = { .x = 21, .y = -79 } },
  /* 0:33-0:37 */ { .inner_left = { .x = -22, .y = 17 }, .inner_right = { .x = 8, .y = 27 }, .outer_left = { .x = 5, .y = -71 }, .outer_right = { .x = 36, .y = -61 }, .apex = { .x = 25, .y = -78 } },
  /* 0:38-0:42 */ { .inner_left = { .x = -23, .y = 16 }, .inner_right = { .x = 7, .y = 27 }, .outer_left = { .x = 9, .y = -70 }, .outer_right = { .x = 39, .y = -59 }, .apex = { .x = 28, .y = -77 } },
  /* 0:43-0:47 */ { .inner_left = { .x = -24, .y = 15 }, .inner_right = { .x = 6, .y = 27 }, .outer_left = { .x = 12, .y = -70 }, .outer_right = { .x = 41, .y = -58 }, .apex = { .x = 31, .y = -76 } },
  /* 0:48-0:52 */ { .inner_left = { .x = -24, .y = 14 }, .inner_right = { .x = 5, .y = 28 }, .outer_left = { .x = 15, .y = -69 }, .outer_right = { .x = 44, .y = -56 }, .apex = { .x = 35, .y = -74 } },
  /* 0:53-0:57 */ { .inner_left = { .x = -25, .y = 13 }, .inner_right = { .x = 4, .y = 28 }, .outer_left = { .x = 18, .y = -69 }, .outer_right = { .x = 46, .y = -54 }, .apex = { .x = 38, .y = -73 } },
  /* 0:58-1:02 */ { .inner_left = { .x = -25, .y = 12 }, .inner_right = { .x = 2, .y = 28 }, .outer_left = { .x = 21, .y = -68 }, .outer_right = { .x = 48, .y = -52 }, .apex = { .x = 41, .y = -71 } },
  /* 1:03-1:07 */ { .inner_left = { .x = -26, .y = 11 }, .inner_right = { .x = 1, .y = 28 }, .outer_left = { .x = 24, .y = -67 }, .outer_right = { .x = 51, .y = -50 }, .apex = { .x = 44, .y = -69 } },
  /* 1:08-1:12 */ { .inner_left = { .x = -26, .y = 10 }, .inner_right = { .x = 0, .y = 28 }, .outer_left = { .x = 26, .y = -66 }, .outer_right = { .x = 53, .y = -47 }, .apex = { .x = 47, .y = -67 } },
  /* 1:13-1:17 */ { .inner_left = { .x = -27, .y = 9 }, .inner_right = { .x = -1, .y = 28 }, .outer_left = { .x = 29, .y = -64 }, .outer_right = { .x = 55, .y = -45 }, .apex = { .x = 50, .y = -65 } },
  /* 1:18-1:22 */ { .inner_left = { .x = -27, .y = 7 }, .inner_right = { .x = -3, .y = 28 }, .outer_left = { .x = 32, .y = -63 }, .outer_right = { .x = 57, .y = -43 }, .apex = { .x = 53, .y = -63 } },
  /* 1:23-1:27 */ { .inner_left = { .x = -27, .y = 6 }, .inner_right = { .x = -4, .y = 28 }, .outer_left = { .x = 35, .y = -62 }, .outer_right = { .x = 58, .y = -40 }, .apex = { .x = 55, .y = -60 } },
};

static RelPoint prv_rel_rotate_90_cw(RelPoint p) {
  return (RelPoint){ .x = -p.y, .y = p.x };
}

static RelPoint prv_rel_transpose_invert(RelPoint p) {
  return (RelPoint){ .x = -p.y, .y = -p.x };
}

static HandPose prv_pose_rotate_90_cw(HandPose pose) {
  return (HandPose){
    .inner_left  = prv_rel_rotate_90_cw(pose.inner_left),
    .inner_right = prv_rel_rotate_90_cw(pose.inner_right),
    .outer_left  = prv_rel_rotate_90_cw(pose.outer_left),
    .outer_right = prv_rel_rotate_90_cw(pose.outer_right),
    .apex        = prv_rel_rotate_90_cw(pose.apex),
  };
}

static HandPose prv_pose_transpose_invert(HandPose pose) {
  return (HandPose){
    .inner_left  = prv_rel_transpose_invert(pose.inner_left),
    .inner_right = prv_rel_transpose_invert(pose.inner_right),
    .outer_left  = prv_rel_transpose_invert(pose.outer_left),
    .outer_right = prv_rel_transpose_invert(pose.outer_right),
    .apex        = prv_rel_transpose_invert(pose.apex),
  };
}

static HandPose prv_minute_pose_for_minute(uint8_t minute, bool is_gabbro) {
  const HandPose *base = is_gabbro ? s_minute_base_00_to_07_gabbro : s_minute_base_00_to_07_emery;
  uint8_t quarter = minute / 15;
  uint8_t quarter_index = minute % 15;
  HandPose pose = (quarter_index <= 7)
    ? base[quarter_index]
    : prv_pose_transpose_invert(base[15 - quarter_index]);

  for (uint8_t i = 0; i < quarter; i++) {
    pose = prv_pose_rotate_90_cw(pose);
  }
  return pose;
}

static uint8_t prv_hour_5min_sample_index(uint8_t quarter_index) {
  // Round to nearest 5-minute sample (0,5,...,85), with edge clamping.
  uint8_t sample = (quarter_index + 2) / 5;
  return sample > 17 ? 17 : sample;
}

static HandPose prv_hour_pose_for_time(uint8_t hour_12, uint8_t minute, bool is_gabbro) {
  const HandPose *base = is_gabbro ? s_hour_base_00_to_89_gabbro_5min : s_hour_base_00_to_89_emery_5min;
  uint16_t total = (uint16_t)hour_12 * 60 + minute; // 0..719
  uint8_t quarter = total / 180;
  uint8_t quarter_index = total % 180;
  HandPose pose = (quarter_index < 90)
    ? base[prv_hour_5min_sample_index(quarter_index)]
    : prv_pose_transpose_invert(base[prv_hour_5min_sample_index((uint8_t)(179 - quarter_index))]);

  for (uint8_t i = 0; i < quarter; i++) {
    pose = prv_pose_rotate_90_cw(pose);
  }
  return pose;
}

static GPoint prv_abs_from_rel(GPoint center, RelPoint p) {
  return (GPoint){ .x = center.x + p.x, .y = center.y + p.y };
}

static void draw_hand_pose_border(GContext *ctx, GPoint center, HandPose pose,
                                  int stroke_w, GColor color) {
  GPoint inner_left  = prv_abs_from_rel(center, pose.inner_left);
  GPoint inner_right = prv_abs_from_rel(center, pose.inner_right);
  GPoint outer_left  = prv_abs_from_rel(center, pose.outer_left);
  GPoint outer_right = prv_abs_from_rel(center, pose.outer_right);
  GPoint apex        = prv_abs_from_rel(center, pose.apex);

  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, stroke_w);
  graphics_draw_line(ctx, inner_left,  inner_right);
  graphics_draw_line(ctx, inner_right, outer_right);
  graphics_draw_line(ctx, inner_left,  outer_left);
  graphics_draw_line(ctx, outer_right, apex);
  graphics_draw_line(ctx, outer_left,  apex);
}

static void draw_hand_pose_fill(GContext *ctx, GPoint center, HandPose pose, GColor color) {
  GPoint pts[5] = {
    { pose.inner_left.x,  pose.inner_left.y  },
    { pose.inner_right.x, pose.inner_right.y },
    { pose.outer_right.x, pose.outer_right.y },
    { pose.apex.x,        pose.apex.y        },
    { pose.outer_left.x,  pose.outer_left.y  },
  };
  GPathInfo info = { .num_points = 5, .points = pts };
  GPath *path = gpath_create(&info);
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
  int hand_edge_w = is_gabbro ? GABBRO_HAND_EDGE_WIDTH : EMERY_HAND_EDGE_WIDTH;
  int hand_halo_w = is_gabbro ? GABBRO_HAND_HALO_WIDTH : EMERY_HAND_HALO_WIDTH;
  int tick_w = is_gabbro ? GABBRO_TICK_WIDTH : EMERY_TICK_WIDTH;
  int tick_halo_w = is_gabbro ? GABBRO_TICK_HALO_WIDTH : EMERY_TICK_HALO_WIDTH;
  int tick_len = is_gabbro ? GABBRO_TICK_LENGTH : EMERY_TICK_LENGTH;
  int pivot_r = is_gabbro ? GABBRO_PIVOT_RADIUS : EMERY_PIVOT_RADIUS;

  HandPose minute_pose = prv_minute_pose_for_minute((uint8_t)s_minutes, is_gabbro);
  HandPose hour_pose = prv_hour_pose_for_time((uint8_t)(s_hours % 12), (uint8_t)s_minutes, is_gabbro);

  // ── Background ────────────────────────────────────────────────────────
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // ── Hour hand ─────────────────────────────────────────────────────────
  draw_hand_pose_fill(ctx, center, hour_pose, fg);
  draw_hand_pose_border(ctx, center, hour_pose, hand_edge_w, fg);

  // ── Minute hand ───────────────────────────────────────────────────────
  draw_hand_pose_border(ctx, center, minute_pose, hand_edge_w + 2 * hand_halo_w, minute_halo);
  draw_hand_pose_fill(ctx, center, minute_pose, fg);
  draw_hand_pose_border(ctx, center, minute_pose, hand_edge_w, fg);

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
      int temp_display = prv_temperature_for_display(Weather_weatherInfo.currentTemp);
      char temp_str[12];
      snprintf(temp_str, sizeof(temp_str), "%d", temp_display);
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

    // Time: HH : MM with optional am/pm suffix (no space)
    if (show_bottom_left) {
      GSize sz_minutes = graphics_text_layout_get_content_size(s_minutes_str, bitham30_strip,
        GRect(0, 0, BOTTOM_STRIP_TEXT_WIDTH_LIMIT + 10, text_h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
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
      if (s_meridiem_str[0] != '\0') {
        tx += sz_minutes.w;
        graphics_draw_text(ctx, s_meridiem_str, bitham30_strip,
                           GRect(tx, strip_text_y, BOTTOM_STRIP_TEXT_WIDTH_LIMIT, text_h),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      }
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
  bool use_24h = messaging_time_format_24h();
  int display_hour = tick_time->tm_hour;
  if (use_24h) {
    snprintf(s_hours_str, sizeof(s_hours_str), "%02d", display_hour);
    s_meridiem_str[0] = '\0';
  } else {
    display_hour = tick_time->tm_hour % 12;
    if (display_hour == 0) display_hour = 12;
    snprintf(s_hours_str, sizeof(s_hours_str), "%d", display_hour);
    snprintf(s_meridiem_str, sizeof(s_meridiem_str), "%s", tick_time->tm_hour >= 12 ? "pm" : "am");
  }
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
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  handle_tick(t, 0);
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
