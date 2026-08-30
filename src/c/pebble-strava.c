#include <pebble.h>

// === Action / sport / upload constants ===

#define CMD_START   0
#define CMD_STOP    1
#define CMD_PAUSE   2
#define CMD_RESUME  3

#define PERSIST_KEY_URL          1
#define PERSIST_KEY_SECRET       2
#define PERSIST_KEY_HR_CYCLING   3  // was global HR_INTERVAL — migrates naturally
#define PERSIST_KEY_HR_RUNNING   4
#define PERSIST_KEY_HR_WALKING   5
#define PERSIST_KEY_GPS_ACCURACY  6
#define PERSIST_KEY_UNITS         7
#define PERSIST_KEY_ROTATION      8
#define PERSIST_KEY_SUBFOLDER     9

#define SPORT_CYCLING 0
#define SPORT_RUNNING 1
#define SPORT_WALKING 2

#define UPLOAD_PENDING 0
#define UPLOAD_SUCCESS 1
#define UPLOAD_ERROR   2

#define WORKER_UNKNOWN 0
#define WORKER_OK      1
#define WORKER_ERROR   2
#define WORKER_NONE    3  // no worker configured

// === App state ===

typedef enum {
  STATE_SELECT,
  STATE_ACTIVE,
  STATE_PAUSED,
  STATE_UPLOADING,
  STATE_DONE,
} AppState;

typedef enum {
  VIEW_ALL = 0,
  VIEW_CLOCK,
  VIEW_TIME,
  VIEW_DIST,
  VIEW_SPEED,
  VIEW_HEART,
  VIEW_LAST,
} ViewMode;

#define TIME_COLOR      (GColorWhite)
#define CLOCK_COLOR     (GColorYellow)
#define DIST_COLOR      (GColorCyan)
#define SPEED_COLOR     (GColorChromeYellow)
#define HR_COLOR        (GColorGreen)

/*** Custom big number from png section ***/
#define DIGIT_WIDTH  60
#define DIGIT_HEIGHT 100

// Map index values 10, 11, 12 to our symbols
#define IDX_DOT   10
#define IDX_COLON 11
#define IDX_DASH  12

static Layer *s_canvas_layer;       //Layer for custom graphical elements


// Array to hold references to our loaded image assets
static GBitmap *s_digit_bitmaps[13];
static GBitmap *temp_digit_bitmap;

static const uint32_t RESOURCE_IDS[13] = {
  RESOURCE_ID_IMAGE_DIGIT_0, RESOURCE_ID_IMAGE_DIGIT_1, RESOURCE_ID_IMAGE_DIGIT_2,
  RESOURCE_ID_IMAGE_DIGIT_3, RESOURCE_ID_IMAGE_DIGIT_4, RESOURCE_ID_IMAGE_DIGIT_5,
  RESOURCE_ID_IMAGE_DIGIT_6, RESOURCE_ID_IMAGE_DIGIT_7, RESOURCE_ID_IMAGE_DIGIT_8,
  RESOURCE_ID_IMAGE_DIGIT_9, RESOURCE_ID_IMAGE_DIGIT_DOT, RESOURCE_ID_IMAGE_DIGIT_COLON,
  RESOURCE_ID_IMAGE_DIGIT_DASH
};

typedef enum {
    FMT_CLEAR,
    FMT_CLOCK,
    FMT_TIME,
    FMT_DIST,
    FMT_SPEED,
    FMT_HEART,
} NumberFormat;

typedef enum {
    STY_PLAIN,
    STY_2LEAD0,
    STY_3LEAD0,
    STY_DOT,
    STY_COLON,
    STY_DASH,
} NumberStyle;

static int s_big_numbers[3] = {-1};
static NumberStyle s_big_number_styles[3] = {STY_PLAIN};
static GColor s_big_number_colors[3] = {GColorWhite};
static TextLayer *s_wk_big_number_unit;

#define COLORS_IN_PALETTE 16

static void deep_copy_bitmap(GBitmap *dest, const GBitmap *src) {
  if (!dest || !src) return;
  // 1. Deep-copy the raw pixel byte arrays row by row
  int height = gbitmap_get_bounds(src).size.h;
  uint16_t bytes_per_row = gbitmap_get_bytes_per_row(src);
  uint8_t *dest_raw_bytes = gbitmap_get_data(dest);
  uint8_t *src_raw_bytes = gbitmap_get_data(src);

  if (dest_raw_bytes && src_raw_bytes) {
    // Total raw image data size = rows * stride-width bytes
    memcpy(dest_raw_bytes, src_raw_bytes, height * bytes_per_row);
    // 2. Deep-copy the color palette arrays
    GColor *dest_palette = gbitmap_get_palette(dest);
    GColor *src_palette = gbitmap_get_palette(src);
    if (dest_palette && src_palette) {
        // Standard Pebble palettized PNGs contain a fixed map of 256 GColor bytes
        memcpy(dest_palette, src_palette, COLORS_IN_PALETTE * sizeof(GColor));
    }
  }
}


static void swap_bitmap_color(GBitmap *bitmap, GColor from_color, GColor to_color) {
  if (!bitmap) return;
  // Retrieve the pointer to the image's internal color palette arrays
  GColor *palette = gbitmap_get_palette(bitmap);
  // If the palette doesn't exist (e.g., if the image is monochrome/1-bit), abort
  if (!palette) return;
  // Standard palettized PNGs on Pebble color screens have a 256-color layout
  for (int i = 0; i < COLORS_IN_PALETTE; i++) {
    // If the palette color matches standard white, swap it out
    if (gcolor_equal(palette[i], from_color)) {
      palette[i] = to_color;
    }
  }
}



static void draw_big_number(uint32_t val, NumberFormat format){
    // Display is 
    //    200 pix wide, we need up to 3 digits per row, so each digit should be 60 pixels wide
    //    228 pix high, up to two rows, 100 pixels 
    s_big_numbers[0] = -1;
    s_big_numbers[1] = -1;
    s_big_numbers[2] = -1;
    switch(format){
        case FMT_CLEAR:
            text_layer_set_text(s_wk_big_number_unit, "");
            break;
        case FMT_CLOCK:  //Top MMM Bottom SS
            s_big_numbers[0] = val/60;
            s_big_number_styles[0] = STY_2LEAD0;
            s_big_number_colors[0] = CLOCK_COLOR;
            s_big_numbers[2] = val%60;
            s_big_number_styles[2] = STY_COLON;
            s_big_number_colors[2] = CLOCK_COLOR;
            text_layer_set_text(s_wk_big_number_unit, "time");
            text_layer_set_text_color(s_wk_big_number_unit, CLOCK_COLOR);
            break;
        case FMT_TIME:  //Top MMM Bottom SS
            s_big_numbers[0] = val/60;
            s_big_number_styles[0] = STY_PLAIN;
            s_big_number_colors[0] = TIME_COLOR;
            s_big_numbers[2] = val%60;
            s_big_number_styles[2] = STY_COLON;
            s_big_number_colors[2] = TIME_COLOR;
            text_layer_set_text(s_wk_big_number_unit, "elapsed");
            text_layer_set_text_color(s_wk_big_number_unit, TIME_COLOR);
            break;
        case FMT_DIST:  //Top kkk Bottom mmm
            s_big_numbers[0] = val/1000;
            s_big_number_styles[0] = STY_PLAIN;
            s_big_number_colors[0] = DIST_COLOR;
            s_big_numbers[2] = (val/10)%100;
            s_big_number_styles[2] = STY_DOT;
            s_big_number_colors[2] = DIST_COLOR;
            text_layer_set_text(s_wk_big_number_unit, "km");
            text_layer_set_text_color(s_wk_big_number_unit, DIST_COLOR);
            break;
        case FMT_SPEED: //Middle xxx  (km/h or mph)
            s_big_numbers[1] = (val*36)/1000;
            s_big_number_styles[1] = STY_PLAIN;
            s_big_number_colors[1] = SPEED_COLOR;
            text_layer_set_text(s_wk_big_number_unit, "km/h");
            text_layer_set_text_color(s_wk_big_number_unit, SPEED_COLOR);
            break;
        case FMT_HEART: //Middle xxx  (bpm)
            s_big_numbers[1] = val;
            s_big_number_styles[1] = STY_PLAIN;
            s_big_number_colors[1] = HR_COLOR;
            text_layer_set_text(s_wk_big_number_unit, "bpm");
            text_layer_set_text_color(s_wk_big_number_unit, HR_COLOR);
            break;
    }
    layer_mark_dirty(s_canvas_layer);
}
/*** Custom big number from png section end***/

static AppState s_state = STATE_SELECT;
static int      s_sport = SPORT_CYCLING;
static ViewMode s_viewmode = VIEW_ALL;


  
  

// Elapsed time: offset accumulates completed active segments; seg_start is the
// wall-clock time the current segment began. get_elapsed() combines both live.
static uint32_t s_elapsed_offset = 0;
static time_t   s_seg_start      = 0;

static uint32_t s_distance_m = 0;
static uint32_t s_speed_cms  = 0;  // centimeters/sec from phone GPS
static int16_t  s_hr_bpm     = 0;
static bool     s_gps_fix      = false;
static int8_t   s_worker_status = WORKER_NONE;  // hide W until worker explicitly confirmed

static bool      s_back_armed = false;
static AppTimer *s_back_timer       = NULL;
static bool      s_up_armed   = false;
static AppTimer *s_up_timer         = NULL;
static AppTimer *s_upload_done_timer = NULL;

// HR: per-sport send interval; GPS accuracy threshold; units flag
static int     s_hr_interval_s[3] = {5, 5, 15};  // cycling / running / walking defaults
static int     s_hr_tick_count    = 0;
static int16_t s_last_sent_hr     = -1;
static int     s_gps_accuracy     = 25;  // meters — relayed to Android on CMD_START
static bool    s_imperial         = false;
static bool    s_left_hand_mode = false;

// === Windows & layers ===

static Window    *s_select_win;
static TextLayer *s_sel_title;
static TextLayer *s_sel_sport;
static TextLayer *s_sel_up_hint;
static TextLayer *s_sel_sel_hint;
static TextLayer *s_sel_dn_hint;
static TextLayer *s_sel_gps;
static GFont      s_icon_font_14;

static Window    *s_workout_win;
static TextLayer *s_wk_time;
static TextLayer *s_wk_dist;
static TextLayer *s_wk_dist_unit;
static TextLayer *s_wk_speed;
static TextLayer *s_wk_speed_unit;
static TextLayer *s_wk_bpm;
static TextLayer *s_wk_bpm_unit;
static TextLayer *s_wk_big_val;
static TextLayer *s_wk_status;
static TextLayer *s_wk_up_hint;
static TextLayer *s_wk_dn_hint;
static TextLayer *s_wk_sel_hint;
static TextLayer *s_wk_back_hint;

// Which custom graphical elements to show 
static bool      s_draw_speed_decimal_dot=true;
static int       s_dist_decimal_dot = 0;
static bool      s_draw_bpm_dash = true;

static const int s_unit_field_width = 70;
static const int s_top_margin = 4;
static const int s_lineheight = 52;
static int       s_bounds_width = 0;


// Persistent display buffers (TextLayer holds pointer, not a copy)
static char s_sel_sport_buf[16];
static char s_sel_gps_buf[24];
static char s_wk_time_buf[16];
static char s_wk_dist_buf[20];
static char s_wk_speed_buf[24];
static char s_wk_bpm_buf[16];
static char s_wk_status_buf[40];

// === Formatting ===

static void fmt_time(char *buf, size_t n, uint32_t secs) {
  unsigned long h = secs / 3600;
  unsigned long m = (secs % 3600) / 60;
  unsigned long s = secs % 60;
  if (h > 0) snprintf(buf, n, "%lu:%02lu:%02lu", h, m, s);
  else        snprintf(buf, n, "%02lu:%02lu", m, s);
}

static void fmt_dist(char *buf, size_t n, uint32_t m) {
  if (!s_imperial) {
    if (m < 1000) {
      snprintf(buf, n, "%lu", (unsigned long)m);
      s_dist_decimal_dot=0;
    } 
    else if (m<100000){
      unsigned long km  = m / 1000;
      unsigned long dec = (m % 1000) / 10;
      snprintf(buf, n, "%lu  %02lu", km, dec);
      s_dist_decimal_dot=2;
    }
    else {
      unsigned long km  = m / 1000;
      unsigned long dec = (m % 1000) / 100;
      snprintf(buf, n, "%lu  %1lu", km, dec);
      s_dist_decimal_dot=1;
    }
  } else {
    if (m < 1609) {
      unsigned long ft = (unsigned long)m * 5000 / 1524;  // m → feet
      snprintf(buf, n, "%lu", ft);
      s_dist_decimal_dot=0;
    } else if (m<160900){
      unsigned long mi  = m / 1609;
      unsigned long dec = (m % 1609) * 100 / 1609;
      snprintf(buf, n, "%lu  %02lu", mi, dec);
      s_dist_decimal_dot=2;
    } else {
      unsigned long mi  = m / 1609;
      unsigned long dec = (m % 1609) * 10 / 1609;
      snprintf(buf, n, "%lu  %1lu", mi, dec);
      s_dist_decimal_dot=1;
    }
  }
}

static void fmt_speed(char *buf, size_t n, uint32_t cms, int sport) {
  if (sport == SPORT_CYCLING) {
    if (!s_imperial) {
      if (cms < 10) { snprintf(buf, n, "0  0"); return; }
      unsigned long i = (cms * 36) / 1000;
      unsigned long d = ((cms * 36) % 1000) / 10;
      snprintf(buf, n, "%lu %01lu", i,d);
    } else {
      if (cms < 10) { snprintf(buf, n, "0  0"); return; }
      unsigned long i = (cms * 36) / 1609;
      unsigned long d = ((cms * 36) % 1609) * 100 / 1609;
      snprintf(buf, n, "%lu %01lu", i, d);
    }
  } else {
    if (!s_imperial) {
      if (cms < 10) { snprintf(buf, n, "  :  "); return; }
      unsigned long spk = 100000 / cms;
      snprintf(buf, n, "%lu:%02lu", spk / 60, spk % 60);
    } else {
      if (cms < 10) { snprintf(buf, n, "  :  "); return; }
      unsigned long spm = 160934 / cms;
      snprintf(buf, n, "%lu:%02lu", spm / 60, spm % 60);
    }
  }
}

static uint16_t minutes_since_midnight(void){
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  return (tick_time->tm_hour * 60) + tick_time->tm_min;
}

static uint32_t get_elapsed(void) {
  if (s_state == STATE_ACTIVE)
    return s_elapsed_offset + (uint32_t)(time(NULL) - s_seg_start);
  return s_elapsed_offset;
}

// Forward declarations
static void update_workout_display(void);
static void prv_update_gps_label(void);
static void prv_send_creds(void);
static void action_cancel(void);
static void prv_reset_workout_state(void);
static void prv_upload_done_cb(void *ctx);

// === AppMessage ===

static void prv_inbox_received(DictionaryIterator *iter, void *ctx) {
  Tuple *t;
  bool gps_updated = false;

  t = dict_find(iter, MESSAGE_KEY_GPS_DISTANCE);
  if (t) {
    uint32_t d = (uint32_t)t->value->int32;
    if (d != s_distance_m) { s_distance_m = d; gps_updated = true; }
  }

  t = dict_find(iter, MESSAGE_KEY_GPS_SPEED);
  if (t) {
    uint32_t spd = (uint32_t)t->value->int32;
    if (spd != s_speed_cms) { s_speed_cms = spd; gps_updated = true; }
  }

  t = dict_find(iter, MESSAGE_KEY_GPS_HAS_FIX);
  if (t) {
    bool fix = (bool)t->value->int8;
    if (fix != s_gps_fix) { s_gps_fix = fix; gps_updated = true; }
  }

  // Refresh whichever window is visible on GPS update
  if (gps_updated) {
    Window *top = window_stack_get_top_window();
    if (top == s_workout_win) update_workout_display();
    else if (top == s_select_win && s_sel_gps) prv_update_gps_label();
  }

  // Credential storage: JS sends creds after config save; watch persists them.
  // On fresh install, JS sends CRED_REQUEST and watch sends them back.
  t = dict_find(iter, MESSAGE_KEY_CRED_URL);
  if (t) persist_write_string(PERSIST_KEY_URL, t->value->cstring);
  t = dict_find(iter, MESSAGE_KEY_CRED_SECRET);
  if (t) persist_write_string(PERSIST_KEY_SECRET, t->value->cstring);
  t = dict_find(iter, MESSAGE_KEY_CRED_REQUEST);
  if (t) prv_send_creds();

  t = dict_find(iter, MESSAGE_KEY_WORKER_STATUS);
  if (t) {
    s_worker_status = t->value->int8;
    if (window_stack_get_top_window() == s_select_win && s_sel_gps) prv_update_gps_label();
  }

  t = dict_find(iter, MESSAGE_KEY_SETTINGS_HR_INTERVAL_CYCLING);
  if (t) {
    int iv = (int)t->value->int8;
    if (iv >= 1 && iv <= 30) { s_hr_interval_s[SPORT_CYCLING] = iv; persist_write_int(PERSIST_KEY_HR_CYCLING, iv); }
  }
  t = dict_find(iter, MESSAGE_KEY_SETTINGS_HR_INTERVAL_RUNNING);
  if (t) {
    int iv = (int)t->value->int8;
    if (iv >= 1 && iv <= 30) { s_hr_interval_s[SPORT_RUNNING] = iv; persist_write_int(PERSIST_KEY_HR_RUNNING, iv); }
  }
  t = dict_find(iter, MESSAGE_KEY_SETTINGS_HR_INTERVAL_WALKING);
  if (t) {
    int iv = (int)t->value->int8;
    if (iv >= 1 && iv <= 30) { s_hr_interval_s[SPORT_WALKING] = iv; persist_write_int(PERSIST_KEY_HR_WALKING, iv); }
  }
  t = dict_find(iter, MESSAGE_KEY_SETTINGS_GPS_ACCURACY);
  if (t) {
    int acc = (int)t->value->int8;
    if (acc == 15 || acc == 25 || acc == 50) { s_gps_accuracy = acc; persist_write_int(PERSIST_KEY_GPS_ACCURACY, acc); }
  }
  t = dict_find(iter, MESSAGE_KEY_SETTINGS_UNITS);
  if (t) {
    s_imperial = (t->value->int8 == 1);
    persist_write_int(PERSIST_KEY_UNITS, s_imperial ? 1 : 0);
  }
  t = dict_find(iter, MESSAGE_KEY_SETTINGS_ROTATION);
  if (t) {
    s_left_hand_mode = (t->value->int8 == 1);
    persist_write_int(PERSIST_KEY_ROTATION, s_left_hand_mode ? 1 : 0);
  }
  t = dict_find(iter, MESSAGE_KEY_SETTINGS_DOWNLOAD_SUBFOLDER);
  if (t) persist_write_string(PERSIST_KEY_SUBFOLDER, t->value->cstring);

  t = dict_find(iter, MESSAGE_KEY_UPLOAD_STATUS);
  if (t) {
    int status = (int)t->value->int8;
    if (status == UPLOAD_SUCCESS) {
      s_state = STATE_DONE;
      snprintf(s_wk_status_buf, sizeof(s_wk_status_buf), "Saved!");
      APP_LOG(APP_LOG_LEVEL_INFO, "Upload succeeded");
      vibes_double_pulse();
      if (s_upload_done_timer) app_timer_cancel(s_upload_done_timer);
      s_upload_done_timer = app_timer_register(2500, prv_upload_done_cb, NULL);
    } else if (status == UPLOAD_ERROR) {
      s_state = STATE_DONE;
      Tuple *msg = dict_find(iter, MESSAGE_KEY_UPLOAD_MSG);
      if (msg) snprintf(s_wk_status_buf, sizeof(s_wk_status_buf), "Error: %.28s", msg->value->cstring);
      else     snprintf(s_wk_status_buf, sizeof(s_wk_status_buf), "Upload failed");
      APP_LOG(APP_LOG_LEVEL_ERROR, "Upload error: %s", s_wk_status_buf);
      vibes_long_pulse();
    }
    if (s_workout_win == window_stack_get_top_window()) {
      update_workout_display();
    }
  } else {
    // Standalone UPLOAD_MSG (no UPLOAD_STATUS): JS diagnostic shown on select screen
    Tuple *msg = dict_find(iter, MESSAGE_KEY_UPLOAD_MSG);
    if (msg && s_sel_gps && window_stack_get_top_window() == s_select_win) {
      snprintf(s_sel_gps_buf, sizeof(s_sel_gps_buf), "%.22s", msg->value->cstring);
      text_layer_set_text(s_sel_gps, s_sel_gps_buf);
      text_layer_set_text_color(s_sel_gps, GColorLightGray);
    }
  }
}

static void prv_send_creds(void) {
  char url[128]       = {0};
  char secret[64]     = {0};
  char subfolder[64]  = {0};
  bool have_creds =
    persist_read_string(PERSIST_KEY_URL,    url,    sizeof(url)) &&
    persist_read_string(PERSIST_KEY_SECRET, secret, sizeof(secret)) &&
    url[0] != '\0';
  persist_read_string(PERSIST_KEY_SUBFOLDER, subfolder, sizeof(subfolder));
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  // Return persisted settings as well as credentials so PKJS can recover
  // from a lost or stale phone-side localStorage cache.
  dict_write_int8(iter, MESSAGE_KEY_SETTINGS_HR_INTERVAL_CYCLING,
                  (int8_t)s_hr_interval_s[SPORT_CYCLING]);
  dict_write_int8(iter, MESSAGE_KEY_SETTINGS_HR_INTERVAL_RUNNING,
                  (int8_t)s_hr_interval_s[SPORT_RUNNING]);
  dict_write_int8(iter, MESSAGE_KEY_SETTINGS_HR_INTERVAL_WALKING,
                  (int8_t)s_hr_interval_s[SPORT_WALKING]);
  dict_write_int8(iter, MESSAGE_KEY_SETTINGS_GPS_ACCURACY, (int8_t)s_gps_accuracy);
  dict_write_int8(iter, MESSAGE_KEY_SETTINGS_UNITS, s_imperial ? 1 : 0);
  dict_write_int8(iter, MESSAGE_KEY_SETTINGS_ROTATION, s_left_hand_mode ? 1 : 0);
  dict_write_cstring(iter, MESSAGE_KEY_SETTINGS_DOWNLOAD_SUBFOLDER, subfolder);
  if (have_creds) {
    dict_write_cstring(iter, MESSAGE_KEY_CRED_URL, url);
    dict_write_cstring(iter, MESSAGE_KEY_CRED_SECRET, secret);
  }
  app_message_outbox_send();
}

static void prv_send_cmd(int action) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_int8(iter, MESSAGE_KEY_CMD_ACTION, (int8_t)action);
  if (action == CMD_START) {
    dict_write_int8(iter, MESSAGE_KEY_CMD_SPORT,            (int8_t)s_sport);
    dict_write_int8(iter, MESSAGE_KEY_SETTINGS_GPS_ACCURACY,(int8_t)s_gps_accuracy);
    dict_write_int8(iter, MESSAGE_KEY_SETTINGS_UNITS,       s_imperial ? 1 : 0);
    dict_write_int8(iter, MESSAGE_KEY_SETTINGS_ROTATION,    s_left_hand_mode ? 1 : 0);
  }
  app_message_outbox_send();
}

static void prv_send_hr(void) {
  if (s_hr_bpm <= 0 || s_hr_bpm == s_last_sent_hr) return;
  s_last_sent_hr = s_hr_bpm;
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_int16(iter, MESSAGE_KEY_HR_BPM, s_hr_bpm);
  app_message_outbox_send();
}

// === HR reading ===

static void prv_read_hr(void) {
  time_t now = time(NULL);
  HealthServiceAccessibilityMask mask =
    health_service_metric_accessible(HealthMetricHeartRateBPM, now, now);
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
    if (hr > 0) s_hr_bpm = (int16_t)hr;
  }
}

// === Workout display ===
static void refresh_all_units(void){
    // Check which updates are needed before refreshing
    static ViewMode last_viewmode = VIEW_ALL;

    if (s_viewmode == VIEW_ALL){
        if (!s_imperial) {
            text_layer_set_text(s_wk_dist_unit,    (s_distance_m<1000)?"m":"km");
        }
        else{
            text_layer_set_text(s_wk_dist_unit,    (s_distance_m<1609)?"ft":"mi");
        }


        if (s_sport == SPORT_CYCLING) {
            if (!s_imperial) {
                text_layer_set_text(s_wk_speed_unit,    "km/h");
            }
            else{
                text_layer_set_text(s_wk_speed_unit,    "mph");
            }
            s_draw_speed_decimal_dot=true;
        }
        else{
            if (!s_imperial) {
                text_layer_set_text(s_wk_speed_unit,    "/km");
            }
            else{
                text_layer_set_text(s_wk_speed_unit,    "/mi");
            }
            s_draw_speed_decimal_dot=false;
        }
        text_layer_set_text(s_wk_bpm_unit,      "bpm");
        layer_mark_dirty(s_canvas_layer);
    }
    else if (last_viewmode == VIEW_ALL){
        text_layer_set_text(s_wk_dist_unit,     "");
        text_layer_set_text(s_wk_speed_unit,    "");
        text_layer_set_text(s_wk_bpm_unit,      "");
        s_draw_speed_decimal_dot=false;
        s_dist_decimal_dot = 0;
        s_draw_bpm_dash = false;
        layer_mark_dirty(s_canvas_layer);
    }
    last_viewmode = s_viewmode;
}


static void update_workout_display(void) {
  fmt_time(s_wk_time_buf,   sizeof(s_wk_time_buf),  get_elapsed());
  fmt_dist(s_wk_dist_buf,   sizeof(s_wk_dist_buf),  s_distance_m);
  fmt_speed(s_wk_speed_buf, sizeof(s_wk_speed_buf), s_speed_cms, s_sport);
  
  if (s_viewmode==VIEW_ALL){
      if (s_hr_bpm > 0){ 
        snprintf(s_wk_bpm_buf, sizeof(s_wk_bpm_buf), "%d", s_hr_bpm);
        s_draw_bpm_dash = false;
      }
      else {              
        snprintf(s_wk_bpm_buf, sizeof(s_wk_bpm_buf), " ");
        s_draw_bpm_dash = true;
      }
  }

  // Status row: HRM/GPS icons normally; state messages override when needed.
  // STATE_DONE: leave s_wk_status_buf as-is (set by inbox handler with result).
  if (s_state == STATE_UPLOADING) {
    snprintf(s_wk_status_buf, sizeof(s_wk_status_buf), "Sending GPX email...");
  } else if (s_state != STATE_DONE) {
    snprintf(s_wk_status_buf, sizeof(s_wk_status_buf),
             "HRM %s  GPS %s",
             s_hr_bpm > 0 ? "\xe2\x9c\x93" : "--",   // ✓ U+2713
             s_gps_fix    ? "\xe2\x9c\x93" : "--");
  }

  // Dim elapsed timer when paused — subtle state indicator
  text_layer_set_text_color(s_wk_time,
    s_state == STATE_PAUSED ? GColorLightGray : GColorWhite);

  // SELECT hint: || when active (will pause), ▶ when paused (will resume)
  text_layer_set_text(s_wk_sel_hint,
    s_state == STATE_ACTIVE ? "||" : "\xe2\x96\xb6");
  text_layer_set_text_color(s_wk_sel_hint,
    (s_state == STATE_UPLOADING || s_state == STATE_DONE) ? GColorDarkGray : GColorLightGray);
  // Status text: white for done/error result, gray otherwise
  text_layer_set_text_color(s_wk_status,
    s_state == STATE_DONE ? GColorWhite : GColorLightGray);

  switch (s_viewmode){
    case VIEW_ALL:
      text_layer_set_text(s_wk_time,     s_wk_time_buf);
      text_layer_set_text(s_wk_dist,     s_wk_dist_buf);
      text_layer_set_text(s_wk_speed,    s_wk_speed_buf);
      text_layer_set_text(s_wk_bpm,      s_wk_bpm_buf);
      refresh_all_units();
      draw_big_number(s_hr_bpm,FMT_CLEAR);
      break;
    case VIEW_CLOCK:
        draw_big_number(minutes_since_midnight(),FMT_CLOCK);
        break;
    case VIEW_TIME:
        draw_big_number(get_elapsed(),FMT_TIME);
        break;
    case VIEW_DIST:
        draw_big_number(s_distance_m,FMT_DIST);
        break;
    case VIEW_SPEED:
        draw_big_number(s_speed_cms,FMT_SPEED);
        break;
    case VIEW_HEART:
        draw_big_number(s_hr_bpm,FMT_HEART);
        break;
    case VIEW_LAST: //Just a placeholder, will not happen
        break;
    
  }
  text_layer_set_text(s_wk_status,   s_wk_status_buf);
}


// === Workout tick (1 s via TickTimerService) — minimal redraws for power efficiency ===
// Only the elapsed time changes every second; dist/speed/status are refreshed
// by the GPS inbox handler when new data arrives. HR refreshes on its own interval.

static void prv_tick(struct tm *tick_time, TimeUnits units_changed) {
  if (s_state != STATE_ACTIVE) return;

  // HR: read and send on its interval; redraw only if the value changed.
  s_hr_tick_count++;
  if (s_hr_tick_count >= s_hr_interval_s[s_sport]) {
    s_hr_tick_count = 0;
    int16_t prev_hr = s_hr_bpm;
    prv_read_hr();
    prv_send_hr();
    if (s_hr_bpm != prev_hr) {
      if (s_viewmode == VIEW_ALL){
          if (s_hr_bpm > 0){ 
            snprintf(s_wk_bpm_buf, sizeof(s_wk_bpm_buf), "%d", s_hr_bpm);
            s_draw_bpm_dash = false;
          }
          else{              
            snprintf(s_wk_bpm_buf, sizeof(s_wk_bpm_buf), " ");
            s_draw_bpm_dash = true;
          }
          layer_mark_dirty(s_canvas_layer);
          text_layer_set_text(s_wk_bpm, s_wk_bpm_buf);
      }
      if (s_viewmode == VIEW_HEART){
          draw_big_number(s_hr_bpm,FMT_HEART);
      }
    }
  }

  // Elapsed time is the only field that changes every second — redraw it alone.
  fmt_time(s_wk_time_buf, sizeof(s_wk_time_buf), get_elapsed());
  if (s_viewmode == VIEW_ALL){
    text_layer_set_text(s_wk_time, s_wk_time_buf);
  }
  if (s_viewmode == VIEW_TIME){
      draw_big_number(get_elapsed(),FMT_TIME);
  }
  if (s_viewmode == VIEW_CLOCK){
      draw_big_number(minutes_since_midnight(),FMT_CLOCK);
  }
}

static void start_timer(void) {
  tick_timer_service_subscribe(SECOND_UNIT, prv_tick);
}

static void stop_timer(void) {
  tick_timer_service_unsubscribe();
}

// === Workout actions ===

static void action_start(void) {
  s_state          = STATE_ACTIVE;
  s_elapsed_offset = 0;
  s_seg_start      = time(NULL);
  s_distance_m     = 0;
  s_speed_cms      = 0;
  s_hr_bpm         = 0;
  s_gps_fix        = false;
  s_hr_tick_count  = 0;
  s_last_sent_hr   = -1;

  prv_read_hr();
  prv_send_cmd(CMD_START);
  start_timer();

  APP_LOG(APP_LOG_LEVEL_INFO, "Workout started: sport=%d", s_sport);
  // prv_workout_load calls update_workout_display() on push
  window_stack_push(s_workout_win, true);
}

static void action_pause(void) {
  s_elapsed_offset = get_elapsed();  // freeze before state changes
  APP_LOG(APP_LOG_LEVEL_INFO, "Paused at %lus", (unsigned long)s_elapsed_offset);
  s_state = STATE_PAUSED;
  stop_timer();
  prv_send_cmd(CMD_PAUSE);
  update_workout_display();
  vibes_short_pulse();
}

static void action_resume(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Resumed");
  s_seg_start     = time(NULL);
  s_state         = STATE_ACTIVE;
  s_hr_tick_count = 0;
  prv_send_cmd(CMD_RESUME);
  start_timer();
  update_workout_display();
  vibes_short_pulse();
}

static void action_stop(void) {
  if (s_back_timer) { app_timer_cancel(s_back_timer); s_back_timer = NULL; }
  s_back_armed = false;
  s_elapsed_offset = get_elapsed();
  APP_LOG(APP_LOG_LEVEL_INFO, "Stopped: elapsed=%lus dist=%lum", (unsigned long)s_elapsed_offset, (unsigned long)s_distance_m);
  s_state = STATE_UPLOADING;
  stop_timer();
  prv_send_cmd(CMD_STOP);
  update_workout_display();
  vibes_long_pulse();
}

// === Double-press timers ===

static void prv_back_timer_cb(void *ctx) {
  s_back_armed = false;
  s_back_timer = NULL;
  if (s_state == STATE_ACTIVE || s_state == STATE_PAUSED) update_workout_display();
}

static void prv_up_timer_cb(void *ctx) {
  s_up_armed = false;
  s_up_timer = NULL;
  if (s_state == STATE_ACTIVE || s_state == STATE_PAUSED) update_workout_display();
}

static void action_cancel(void) {
  if (s_back_timer) { app_timer_cancel(s_back_timer); s_back_timer = NULL; }
  s_back_armed = false;
  stop_timer();
  s_state          = STATE_SELECT;
  s_elapsed_offset = 0;
  s_distance_m     = 0;
  s_speed_cms      = 0;
  s_hr_bpm         = 0;
  s_gps_fix        = false;
  prv_send_cmd(CMD_PAUSE);  // tell JS to stop tracking without triggering upload
  APP_LOG(APP_LOG_LEVEL_INFO, "Workout cancelled");
  vibes_short_pulse();
  window_stack_pop(true);
}

static void prv_reset_workout_state(void) {
  s_state          = STATE_SELECT;
  s_elapsed_offset = 0;
  s_distance_m     = 0;
  s_speed_cms      = 0;
  s_hr_bpm         = 0;
  s_gps_fix        = false;
}

static void prv_upload_done_cb(void *ctx) {
  s_upload_done_timer = NULL;
  prv_reset_workout_state();
  window_stack_pop_all(true);
}

// === Click handlers — Workout window ===

static void prv_wk_select(ClickRecognizerRef r, void *ctx) {
  if (s_state == STATE_ACTIVE)       action_pause();
  else if (s_state == STATE_PAUSED)  action_resume();
  else if (s_state == STATE_DONE) {
    if (s_upload_done_timer) { app_timer_cancel(s_upload_done_timer); s_upload_done_timer = NULL; }
    prv_reset_workout_state();
    window_stack_pop(true);
  }
}

static void prv_wk_up(ClickRecognizerRef r, void *ctx) {
  if (s_state == STATE_UPLOADING || s_state == STATE_DONE) return;

  if (s_up_armed) {
    if (s_up_timer) { app_timer_cancel(s_up_timer); s_up_timer = NULL; }
    s_up_armed = false;
    action_stop();
  } else {
    if (s_back_timer) { app_timer_cancel(s_back_timer); s_back_timer = NULL; }
    s_back_armed = false;
    s_up_armed = true;
    snprintf(s_wk_status_buf, sizeof(s_wk_status_buf), "Press UP again to stop");
    text_layer_set_text(s_wk_status, s_wk_status_buf);
    s_up_timer = app_timer_register(3000, prv_up_timer_cb, NULL);
  }
}

static void prv_wk_back(ClickRecognizerRef r, void *ctx) {
  if (s_state == STATE_DONE) {
    if (s_upload_done_timer) { app_timer_cancel(s_upload_done_timer); s_upload_done_timer = NULL; }
    prv_reset_workout_state();
    window_stack_pop(true);
    return;
  }
  if (s_state == STATE_UPLOADING) return;

  if (s_back_armed) {
    if (s_back_timer) { app_timer_cancel(s_back_timer); s_back_timer = NULL; }
    s_back_armed = false;
    action_cancel();
  } else {
    if (s_up_timer) { app_timer_cancel(s_up_timer); s_up_timer = NULL; }
    s_up_armed = false;
    s_back_armed = true;
    snprintf(s_wk_status_buf, sizeof(s_wk_status_buf), "Press BACK again to cancel");
    text_layer_set_text(s_wk_status, s_wk_status_buf);
    s_back_timer = app_timer_register(3000, prv_back_timer_cb, NULL);
  }
}

static void prv_wk_down(ClickRecognizerRef r, void *ctx) {
    s_viewmode++;
    s_viewmode %= VIEW_LAST;
    
    if (s_viewmode != VIEW_ALL){
        text_layer_set_text(s_wk_time,     "");
        text_layer_set_text(s_wk_dist,     "");
        text_layer_set_text(s_wk_speed,    "");
        text_layer_set_text(s_wk_bpm,      "");
    }    
    update_workout_display();
    refresh_all_units();
    layer_mark_dirty(s_canvas_layer);
}

static void prv_wk_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_wk_select);
  window_single_click_subscribe(BUTTON_ID_UP,     prv_wk_up);
  window_single_click_subscribe(BUTTON_ID_BACK,   prv_wk_back);
  window_single_click_subscribe(BUTTON_ID_DOWN,   prv_wk_down);
}

// === Click handlers — Sport select window ===

static void prv_update_sport_label(void) {
  static const char *labels[] = {"CYCLING", "RUNNING", "WALKING"};
  snprintf(s_sel_sport_buf, sizeof(s_sel_sport_buf), "%s", labels[s_sport]);
  text_layer_set_text(s_sel_sport, s_sel_sport_buf);
}

static void prv_sel_up(ClickRecognizerRef r, void *ctx) {
  s_sport = (s_sport == 0) ? SPORT_WALKING : s_sport - 1;
  prv_update_sport_label();
}

static void prv_sel_down(ClickRecognizerRef r, void *ctx) {
  s_sport = (s_sport == SPORT_WALKING) ? 0 : s_sport + 1;
  prv_update_sport_label();
}

static void prv_sel_select(ClickRecognizerRef r, void *ctx) {
  action_start();
}

static void prv_sel_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP,     prv_sel_up);
  window_single_click_subscribe(BUTTON_ID_DOWN,   prv_sel_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_sel_select);
}

// === Sport select window ===

static void prv_update_gps_label(void) {
  if (s_worker_status == WORKER_NONE && !s_gps_fix) {
    // Companion app hasn't responded yet — guide the user
    snprintf(s_sel_gps_buf, sizeof(s_sel_gps_buf), "Open companion app");
    text_layer_set_text(s_sel_gps, s_sel_gps_buf);
    text_layer_set_text_color(s_sel_gps, GColorDarkGray);
    return;
  }

  time_t now = time(NULL);
  HealthServiceAccessibilityMask mask =
    health_service_metric_accessible(HealthMetricHeartRateBPM, now, now);
  bool hrm_ok = (mask & HealthServiceAccessibilityMaskAvailable) &&
                health_service_peek_current_value(HealthMetricHeartRateBPM) > 0;

  if (s_worker_status == WORKER_NONE) {
    snprintf(s_sel_gps_buf, sizeof(s_sel_gps_buf), "HRM%s GPS%s",
             hrm_ok    ? "\xe2\x9c\x93" : "--",
             s_gps_fix ? "\xe2\x9c\x93" : "--");
  } else {
    const char *wkr = s_worker_status == WORKER_OK    ? "\xe2\x9c\x93" :
                      s_worker_status == WORKER_ERROR  ? "!"             : "?";
    snprintf(s_sel_gps_buf, sizeof(s_sel_gps_buf), "W%s HRM%s GPS%s",
             wkr,
             hrm_ok    ? "\xe2\x9c\x93" : "--",
             s_gps_fix ? "\xe2\x9c\x93" : "--");
  }
  text_layer_set_text(s_sel_gps, s_sel_gps_buf);
  text_layer_set_text_color(s_sel_gps, GColorLightGray);
}

static void prv_select_load(Window *win) {
  Layer  *root   = window_get_root_layer(win);
  GRect   bounds = layer_get_bounds(root);
  int     w      = bounds.size.w;

  s_sel_title = text_layer_create(GRect(0, 24, w, 26));
  text_layer_set_text(s_sel_title, "SELECT SPORT");
  text_layer_set_text_alignment(s_sel_title, GTextAlignmentCenter);
  text_layer_set_font(s_sel_title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_sel_title, GColorClear);
  text_layer_set_text_color(s_sel_title, GColorWhite);
  layer_add_child(root, text_layer_get_layer(s_sel_title));

  s_sel_sport = text_layer_create(GRect(0, 88, w, 42));
  prv_update_sport_label();
  text_layer_set_text_alignment(s_sel_sport, GTextAlignmentCenter);
  text_layer_set_font(s_sel_sport, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_background_color(s_sel_sport, GColorClear);
  text_layer_set_text_color(s_sel_sport, SPEED_COLOR);
  layer_add_child(root, text_layer_get_layer(s_sel_sport));

  // HRM + GPS status — bottom of screen, dimmed
  s_sel_gps = text_layer_create(GRect(0, 196, w, 18));
  prv_update_gps_label();
  text_layer_set_text_alignment(s_sel_gps, GTextAlignmentCenter);
  text_layer_set_font(s_sel_gps, s_icon_font_14);
  text_layer_set_background_color(s_sel_gps, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_sel_gps));
 
  // Button hints — unicode glyphs from custom font
  if (!s_left_hand_mode) {
    s_sel_up_hint = text_layer_create(GRect(w - 20, 42, 20, 24));
    s_sel_sel_hint = text_layer_create(GRect(w - 20, 101, 20, 24));
    s_sel_dn_hint = text_layer_create(GRect(w - 20, 161, 20, 24));
  }
  else{
    s_sel_up_hint = text_layer_create(GRect(0, 42, 20, 24));
    s_sel_sel_hint = text_layer_create(GRect(0, 101, 20, 24));
    s_sel_dn_hint = text_layer_create(GRect(0, 161, 20, 24));
  }
  text_layer_set_text(s_sel_up_hint, "\xe2\x96\xb2");   // ▲ U+25B2
  text_layer_set_text_alignment(s_sel_up_hint, GTextAlignmentCenter);
  text_layer_set_font(s_sel_up_hint, s_icon_font_14);
  text_layer_set_background_color(s_sel_up_hint, GColorClear);
  text_layer_set_text_color(s_sel_up_hint, GColorLightGray);
  layer_add_child(root, text_layer_get_layer(s_sel_up_hint));

  text_layer_set_text(s_sel_sel_hint, "\xe2\x96\xb6"); // ▶ U+25B6
  text_layer_set_text_alignment(s_sel_sel_hint, GTextAlignmentCenter);
  text_layer_set_font(s_sel_sel_hint, s_icon_font_14);
  text_layer_set_background_color(s_sel_sel_hint, GColorClear);
  text_layer_set_text_color(s_sel_sel_hint, GColorLightGray);
  layer_add_child(root, text_layer_get_layer(s_sel_sel_hint));

  text_layer_set_text(s_sel_dn_hint, "\xe2\x96\xbc");  // ▼ U+25BC
  text_layer_set_text_alignment(s_sel_dn_hint, GTextAlignmentCenter);
  text_layer_set_font(s_sel_dn_hint, s_icon_font_14);
  text_layer_set_background_color(s_sel_dn_hint, GColorClear);
  text_layer_set_text_color(s_sel_dn_hint, GColorLightGray);
  layer_add_child(root, text_layer_get_layer(s_sel_dn_hint));
}

static void prv_select_unload(Window *win) {
  text_layer_destroy(s_sel_title);
  text_layer_destroy(s_sel_sport);
  text_layer_destroy(s_sel_gps);
  text_layer_destroy(s_sel_up_hint);
  text_layer_destroy(s_sel_sel_hint);
  text_layer_destroy(s_sel_dn_hint);
  layer_destroy(s_canvas_layer);
}
  

// === Workout window ===
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  // Dist decimal dot
  if (s_dist_decimal_dot){
      graphics_context_set_fill_color(ctx, DIST_COLOR);
      graphics_fill_circle(ctx, GPoint(s_bounds_width-s_unit_field_width-(30*s_dist_decimal_dot),s_top_margin+1*s_lineheight+45), 4);        
  }
  // Speed decimal dot
  if (s_draw_speed_decimal_dot){
      graphics_context_set_fill_color(ctx, SPEED_COLOR);
      graphics_fill_circle(ctx, GPoint(s_bounds_width-s_unit_field_width-30,s_top_margin+2*s_lineheight+45), 4);
  }
  if (s_draw_bpm_dash){
      graphics_context_set_fill_color(ctx, HR_COLOR);
      int x = s_bounds_width-s_unit_field_width-35;
      int w = 10;
      int y = s_top_margin+3*s_lineheight+45-8;
      int h = 4;
      int cr = 2;
      graphics_fill_rect(ctx, GRect(x,y,w,h),cr,GCornersAll);
  }

  for (size_t row = 0; row <3 ;row++){
      int big_number = s_big_numbers[row];

      if(big_number >=0){
        GRect bounds = layer_get_bounds(layer);
        // Total width of 3 digits (3 * 60) = 180 pixels. center of screen 
        int tracking_x = (bounds.size.w - 10 - DIGIT_WIDTH);
        //int start_y = (bounds.size.h - DIGIT_HEIGHT) / 2 + (DIGIT_HEIGHT/2 * (row-1));
        int start_y = s_top_margin + (DIGIT_HEIGHT/2-10)*row;
        // Set compositing mode to cleanly render transparent PNG backgrounds
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        uint32_t val = big_number>999?999:big_number; 
        for(int ix = 0; ix < 3; ix++) {
            int bitmap_index = val%10;
            if (ix==2){
                switch (s_big_number_styles[row]){
                    case STY_COLON: bitmap_index=IDX_COLON; break;//set colon as leftmost digit  
                    case STY_DOT:   bitmap_index=IDX_DOT; break;//set dot as leftmost digit  
                    case STY_DASH:  bitmap_index=IDX_DASH; break;//set dash as leftmost digit  
                    default: break;
                }
            }
            val/=10;
            //Make a copy of the original bitmap
            deep_copy_bitmap(temp_digit_bitmap, s_digit_bitmaps[bitmap_index]);
            // Set the tint color
            swap_bitmap_color(temp_digit_bitmap, GColorWhite , s_big_number_colors[row]);
            // Draw the digit on the right position
            GRect target_rect = GRect(tracking_x, start_y, DIGIT_WIDTH, DIGIT_HEIGHT);
            graphics_draw_bitmap_in_rect(ctx, temp_digit_bitmap, target_rect);
            tracking_x -= (DIGIT_WIDTH + 5);
            if (val==0){
                if (s_big_number_styles[row]==STY_PLAIN) break; //No lead zero
                if (s_big_number_styles[row]==STY_2LEAD0 && ix==1) break; //No lead zero beyond 2 digits
            }
        }
      }
  }
}


static void prv_workout_load(Window *win) {
  Layer *root   = window_get_root_layer(win);
  GRect  bounds = layer_get_bounds(root);
 

  // Load all 13 PNG images into memory loops
  for(int i = 0; i < 13; i++) {
    s_digit_bitmaps[i] = gbitmap_create_with_resource(RESOURCE_IDS[i]);
  }
  temp_digit_bitmap = gbitmap_create_with_resource(RESOURCE_IDS[0]);

  // Create canvas layer for custom graphics
  s_canvas_layer = layer_create(bounds);
  // Assign the custom drawing procedure
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  // Add to Window
  layer_add_child(root, s_canvas_layer);
  // Redraw this as soon as possible
  layer_mark_dirty(s_canvas_layer);
  
  const int    w      = bounds.size.w;
  s_bounds_width = w;

  #define TIME_VALUE_FONT FONT_KEY_ROBOTO_BOLD_SUBSET_49
  #define VALUE_FONT FONT_KEY_ROBOTO_BOLD_SUBSET_49
  #define UNIT_FONT  FONT_KEY_GOTHIC_28_BOLD
  #define BIG_UNIT_FONT   FONT_KEY_GOTHIC_28_BOLD
  const int font_hdiff = (49-28);

  // Layout (200×228): time(y=14) / dist(y=74) / speed(y=108) / bpm(y=142) / status(y=180)
  // Text layers span full w so they centre on the 200px screen.
  // The ▶ hint on the right edge is a separate layer drawn on top.

  // Elapsed time — large, dims to gray on pause
  s_wk_time = text_layer_create(GRect(0, s_top_margin, w , 56));
  text_layer_set_text_alignment(s_wk_time, GTextAlignmentCenter);
  text_layer_set_font(s_wk_time, fonts_get_system_font(TIME_VALUE_FONT));
  text_layer_set_background_color(s_wk_time, GColorClear);
  text_layer_set_text_color(s_wk_time, TIME_COLOR);
  layer_add_child(root, text_layer_get_layer(s_wk_time));

  // Distance
  s_wk_dist = text_layer_create(GRect(0, s_top_margin + s_lineheight, w-s_unit_field_width, 56));
  text_layer_set_text_alignment(s_wk_dist, GTextAlignmentRight);
  text_layer_set_font(s_wk_dist, fonts_get_system_font(VALUE_FONT));
  text_layer_set_background_color(s_wk_dist, GColorClear);
  text_layer_set_text_color(s_wk_dist, DIST_COLOR);
  layer_add_child(root, text_layer_get_layer(s_wk_dist));
  // Distance unit
  s_wk_dist_unit = text_layer_create(GRect(w-s_unit_field_width, s_top_margin + s_lineheight +font_hdiff, s_unit_field_width, 32));
  text_layer_set_text_alignment(s_wk_dist_unit, GTextAlignmentLeft);
  text_layer_set_font(s_wk_dist_unit, fonts_get_system_font(UNIT_FONT));
  text_layer_set_background_color(s_wk_dist_unit, GColorClear);
  text_layer_set_text_color(s_wk_dist_unit, DIST_COLOR);
  layer_add_child(root, text_layer_get_layer(s_wk_dist_unit));

  // Speed / pace
  s_wk_speed = text_layer_create(GRect(0, s_top_margin + 2* s_lineheight, w-s_unit_field_width, 56));
  text_layer_set_text_alignment(s_wk_speed, GTextAlignmentRight);
  text_layer_set_font(s_wk_speed, fonts_get_system_font(VALUE_FONT));
  text_layer_set_background_color(s_wk_speed, GColorClear);
  text_layer_set_text_color(s_wk_speed, SPEED_COLOR);
  layer_add_child(root, text_layer_get_layer(s_wk_speed));
  // Speed unit
  s_wk_speed_unit = text_layer_create(GRect(w-s_unit_field_width, s_top_margin + 2* s_lineheight +font_hdiff, s_unit_field_width, 32));
  text_layer_set_text_alignment(s_wk_speed_unit, GTextAlignmentLeft);
  text_layer_set_font(s_wk_speed_unit, fonts_get_system_font(UNIT_FONT));
  text_layer_set_background_color(s_wk_speed_unit, GColorClear);
  text_layer_set_text_color(s_wk_speed_unit, SPEED_COLOR);
  layer_add_child(root, text_layer_get_layer(s_wk_speed_unit));

  // Heart rate
  s_wk_bpm = text_layer_create(GRect(0, s_top_margin + 3* s_lineheight, w-s_unit_field_width, 56));
  text_layer_set_text_alignment(s_wk_bpm, GTextAlignmentRight);
  text_layer_set_font(s_wk_bpm, fonts_get_system_font(VALUE_FONT));
  text_layer_set_background_color(s_wk_bpm, GColorClear);
  text_layer_set_text_color(s_wk_bpm, HR_COLOR);
  layer_add_child(root, text_layer_get_layer(s_wk_bpm));
  // Heart rate unit
  s_wk_bpm_unit = text_layer_create(GRect(w-s_unit_field_width, s_top_margin + 3* s_lineheight +font_hdiff, s_unit_field_width, 32));
  text_layer_set_text_alignment(s_wk_bpm_unit, GTextAlignmentLeft);
  text_layer_set_font(s_wk_bpm_unit, fonts_get_system_font(UNIT_FONT));
  text_layer_set_background_color(s_wk_bpm_unit, GColorClear);
  text_layer_set_text_color(s_wk_bpm_unit, HR_COLOR);
  layer_add_child(root, text_layer_get_layer(s_wk_bpm_unit));

  // BIG SINGLE VALUE UNIT 
  s_wk_big_number_unit = text_layer_create(GRect(0, s_top_margin + 4 *s_lineheight - 30, w, 56));
  text_layer_set_text_alignment(s_wk_big_number_unit, GTextAlignmentCenter);
  text_layer_set_font(s_wk_big_number_unit, fonts_get_system_font(BIG_UNIT_FONT));
  text_layer_set_background_color(s_wk_big_number_unit, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_wk_big_number_unit));

  // HRM ✓  GPS ✓ — small dimmed status row (same style as select screen)
  s_wk_status = text_layer_create(GRect(0, s_top_margin + 4* s_lineheight , w, 16));
  text_layer_set_text_alignment(s_wk_status, GTextAlignmentCenter);
  text_layer_set_font(s_wk_status, s_icon_font_14);
  text_layer_set_background_color(s_wk_status, GColorClear);
  text_layer_set_text_color(s_wk_status, GColorLightGray);
  layer_add_child(root, text_layer_get_layer(s_wk_status));

  if (!s_left_hand_mode) {
      s_wk_up_hint = text_layer_create(GRect(w - 20, 42, 20, 24));
      s_wk_sel_hint = text_layer_create(GRect(w - 20, 101, 20, 24));
      s_wk_back_hint = text_layer_create(GRect(0, 42, 20, 24));
      s_wk_dn_hint = text_layer_create(GRect(w - 20, 161, 20, 24));
  }
  else{
      s_wk_up_hint = text_layer_create(GRect(0, 42, 20, 24));
      s_wk_sel_hint = text_layer_create(GRect(0, 101, 20, 24));
      s_wk_back_hint = text_layer_create(GRect(w-20, 161, 20, 24));
      s_wk_dn_hint = text_layer_create(GRect(0, 161, 20, 24));
  }
  // UP hint (■ stop) — top-right, aligned with UP button (~y=42), double-press to stop+upload
  text_layer_set_text(s_wk_up_hint, "\xe2\x96\xa0");  // ■ U+25A0
  text_layer_set_text_alignment(s_wk_up_hint, GTextAlignmentCenter);
  text_layer_set_font(s_wk_up_hint, s_icon_font_14);
  text_layer_set_background_color(s_wk_up_hint, GColorClear);
  text_layer_set_text_color(s_wk_up_hint, GColorDarkGray);
  layer_add_child(root, text_layer_get_layer(s_wk_up_hint));

  // SELECT hint (▶/||) — right edge, aligned with SELECT button (~y=101)
  text_layer_set_text(s_wk_sel_hint, "\xe2\x96\xb6");  // ▶ U+25B6
  text_layer_set_text_alignment(s_wk_sel_hint, GTextAlignmentCenter);
  text_layer_set_font(s_wk_sel_hint, s_icon_font_14);
  text_layer_set_background_color(s_wk_sel_hint, GColorClear);
  text_layer_set_text_color(s_wk_sel_hint, GColorLightGray);
  layer_add_child(root, text_layer_get_layer(s_wk_sel_hint));

  // BACK hint (◀ cancel) — left edge, aligned with BACK button (~y=42), double-press to cancel
  text_layer_set_text(s_wk_back_hint, "\xe2\x97\x80");  // ◀ U+25C0
  text_layer_set_text_alignment(s_wk_back_hint, GTextAlignmentCenter);
  text_layer_set_font(s_wk_back_hint, s_icon_font_14);
  text_layer_set_background_color(s_wk_back_hint, GColorClear);
  text_layer_set_text_color(s_wk_back_hint, GColorDarkGray);
  layer_add_child(root, text_layer_get_layer(s_wk_back_hint));
  
  // DOWN hint (🔃 cycle view) — press to cycle view mode
  text_layer_set_text(s_wk_dn_hint, "\xf0\x9f\x94\x83");  // 🔃 U+1F503
  text_layer_set_text_alignment(s_wk_dn_hint, GTextAlignmentCenter);
  text_layer_set_font(s_wk_dn_hint, s_icon_font_14);
  text_layer_set_background_color(s_wk_dn_hint, GColorClear);
  text_layer_set_text_color(s_wk_dn_hint, GColorDarkGray);
  layer_add_child(root, text_layer_get_layer(s_wk_dn_hint));

  update_workout_display();
}

static void prv_workout_unload(Window *win) {
  text_layer_destroy(s_wk_time);
  text_layer_destroy(s_wk_dist);
  text_layer_destroy(s_wk_dist_unit);
  text_layer_destroy(s_wk_speed);
  text_layer_destroy(s_wk_speed_unit);
  text_layer_destroy(s_wk_bpm);
  text_layer_destroy(s_wk_bpm_unit);
  text_layer_destroy(s_wk_status);
  text_layer_destroy(s_wk_up_hint);
  text_layer_destroy(s_wk_sel_hint);
  text_layer_destroy(s_wk_back_hint);
}

// === Init / Deinit ===

static void prv_init(void) {
  s_icon_font_14 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ICONS_14));
  int iv;
  iv = persist_read_int(PERSIST_KEY_HR_CYCLING);
  if (iv >= 1 && iv <= 30) s_hr_interval_s[SPORT_CYCLING] = iv;
  iv = persist_read_int(PERSIST_KEY_HR_RUNNING);
  if (iv >= 1 && iv <= 30) s_hr_interval_s[SPORT_RUNNING] = iv;
  iv = persist_read_int(PERSIST_KEY_HR_WALKING);
  if (iv >= 1 && iv <= 30) s_hr_interval_s[SPORT_WALKING] = iv;
  iv = persist_read_int(PERSIST_KEY_GPS_ACCURACY);
  if (iv == 15 || iv == 25 || iv == 50) s_gps_accuracy = iv;
  iv = persist_read_int(PERSIST_KEY_UNITS);
  if (iv == 1) s_imperial = true;
  iv = persist_read_int(PERSIST_KEY_ROTATION);
  if (iv == 1) s_left_hand_mode = true;
  APP_LOG(APP_LOG_LEVEL_INFO, "prv_init PERSIST_KEY_ROTATION read %d", iv);  

  s_select_win = window_create();
  window_set_background_color(s_select_win, GColorBlack);
  window_set_click_config_provider(s_select_win, prv_sel_click_config);
  window_set_window_handlers(s_select_win, (WindowHandlers){
    .load   = prv_select_load,
    .unload = prv_select_unload,
  });

  s_workout_win = window_create();
  window_set_background_color(s_workout_win, GColorBlack);
  window_set_click_config_provider(s_workout_win, prv_wk_click_config);
  window_set_window_handlers(s_workout_win, (WindowHandlers){
    .load   = prv_workout_load,
    .unload = prv_workout_unload,
  });

  app_message_open(512, 512);
  app_message_register_inbox_received(prv_inbox_received);

  window_stack_push(s_select_win, false);
}

static void prv_deinit(void) {
  stop_timer();
  if (s_upload_done_timer) { app_timer_cancel(s_upload_done_timer); s_upload_done_timer = NULL; }
  window_destroy(s_select_win);
  window_destroy(s_workout_win);
  fonts_unload_custom_font(s_icon_font_14);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
