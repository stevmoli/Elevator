#include <pebble.h>
#include <stdio.h>
#include <string.h>

Window *my_window;
Layer *colon_layer;

// The face is designed for a 144x168 screen; the numbers below are in that design's pixels.
// Bigger screens scale the design up and centre it (see set_up_layout)
const int DESIGN_WIDTH = 144;
const int DESIGN_HEIGHT = 168;
const int DESIGN_NORMAL_Y = 8; // y position of the digits at rest
// Edges of the digits' columns and rows, within a digit. Strokes are 10 px with a 6 px gap between them
// (a "1" is a single 10 px stroke); bars and gaps are 10 or 11 px tall, with 6 px gaps in the 2, 3, 5 and 9
static const int COLUMN_EDGES[] = { 0, 10, 16, 26 };
static const int ROW_EDGES[] = { 0, 11, 22, 32, 38, 48, 112, 123, 133, 138, 149 };
const int LEAVE_DURATION = 1850; // How long digits take to leave the screen before the minute changes
const int LEAVE_MARGIN = 150; // How long before the minute changes the leaving animation finishes
const int MIN_LEAVE_DURATION = 500; // Shorter than this and digits skip leaving and just arrive at :00
// Across the face: the hours end 4 px left of the colon and the minutes start 4 px right of it, with
// a 6 px gap between the two digits of each. The colon's dots are at x = 66
const int DESIGN_COLON_X = 66;
const int DESIGN_COLON_GAP = 4;

Layer *tens_hour, *ones_hour, *tens_minute, *ones_minute;

// The time currently on screen, as "HH:MM"
static char shown_time[] = "00:00";

// Minutes below are minutes of the day: hour * 60 + minute, from 0 (00:00) to 1439 (23:59)
static int shown_minute_of_day = -1; // the minute currently on screen (-1 until the face first draws)
static AppTimer *leave_timer; // starts the digits leaving near :58
static int leave_timer_minute = -1; // the minute leave_timer was set for
static int already_left_minute = -1; // the last minute whose digits already started leaving
static AppTimer *return_timer; // checks the digits came back after leaving (see return_timer_fired)

// Each digit is drawn as a white block with black holes cut out of it (at most three per digit).
// Positions are within the digit, in design pixels. A "1" has no holes; it's just narrower
static const GRect DIGIT_HOLES[10][3] = {
  { {{10, 11}, {6, 127}} },                                      // 0
  { },                                                           // 1
  { {{0, 11}, {16, 11}}, {{10, 32}, {16, 6}}, {{10, 38}, {6, 100}} }, // 2
  { {{0, 11}, {16, 11}}, {{0, 32}, {16, 6}}, {{10, 38}, {6, 100}} },  // 3
  { {{10, 0}, {6, 123}}, {{0, 133}, {16, 16}} },                 // 4
  { {{10, 11}, {16, 11}}, {{0, 32}, {16, 6}}, {{10, 38}, {6, 100}} }, // 5
  { {{10, 11}, {16, 11}}, {{10, 32}, {6, 106}} },                // 6
  { {{10, 11}, {6, 101}}, {{0, 112}, {16, 37}} },                // 7
  { {{10, 11}, {6, 11}}, {{10, 32}, {6, 106}} },                 // 8
  { {{10, 11}, {6, 11}}, {{0, 32}, {16, 6}}, {{10, 38}, {6, 100}} },  // 9
};

// The colon's two dots, as wide as the digits' strokes (design pixels, on the screen)
static const GRect COLON_DOTS[2] = { {{66, 40}, {10, 19}}, {{66, 111}, {10, 19}} };

// The layout on this watch's screen, worked out by set_up_layout
static float scale = 1;
static GPoint origin; // where the design's top-left corner lands
static int digit_width, one_width, digit_height;
static int normal_y; // y of the digits at rest
static int high_y, low_y; // y of the digits when just off the top and bottom of the screen
static GRect digit_holes[10][3];
static GRect colon_dots[2];

static int scaled(int design) {
  return (int) (design * scale + 0.5f);
}

// Scales a position within a digit that falls on one of its edges. Each stretch between two edges
// is rounded on its own, so strokes, bars and gaps that were the same size stay the same size
static int scaled_edge(int design, const int *edges, int edge_count) {
  int result = 0;
  for (int i = 1; i < edge_count && edges[i] <= design; i++) {
    result += scaled(edges[i] - edges[i - 1]);
  }
  return result;
}

static GRect scaled_hole(GRect design) {
  int x0 = scaled_edge(design.origin.x, COLUMN_EDGES, ARRAY_LENGTH(COLUMN_EDGES));
  int x1 = scaled_edge(design.origin.x + design.size.w, COLUMN_EDGES, ARRAY_LENGTH(COLUMN_EDGES));
  int y0 = scaled_edge(design.origin.y, ROW_EDGES, ARRAY_LENGTH(ROW_EDGES));
  int y1 = scaled_edge(design.origin.y + design.size.h, ROW_EDGES, ARRAY_LENGTH(ROW_EDGES));
  return GRect(x0, y0, x1 - x0, y1 - y0);
}

// Fits the face into area, the part of the screen (bounds) that nothing covers
static void set_up_layout(GRect bounds, GRect area) {
#if defined(PBL_ROUND)
  // On a round screen the corners of the widest time (20:08) stay inside the circle up to this size
  scale = area.size.w / 208.0f;
#else
  float scale_x = area.size.w / (float) DESIGN_WIDTH;
  float scale_y = area.size.h / (float) DESIGN_HEIGHT;
  scale = (scale_x < scale_y) ? scale_x : scale_y;
#endif
  origin = GPoint(area.origin.x + (area.size.w - scaled(DESIGN_WIDTH)) / 2,
                  area.origin.y + (area.size.h - scaled(DESIGN_HEIGHT)) / 2);

  digit_width = scaled_edge(26, COLUMN_EDGES, ARRAY_LENGTH(COLUMN_EDGES));
  one_width = scaled_edge(10, COLUMN_EDGES, ARRAY_LENGTH(COLUMN_EDGES));
  digit_height = scaled_edge(149, ROW_EDGES, ARRAY_LENGTH(ROW_EDGES));
  normal_y = origin.y + scaled(DESIGN_NORMAL_Y);
  high_y = -(digit_height + 1);
  low_y = bounds.size.h + 2;

  for (int digit = 0; digit < 10; digit++) {
    for (int i = 0; i < 3; i++) {
      digit_holes[digit][i] = (DIGIT_HOLES[digit][i].size.w > 0) ? scaled_hole(DIGIT_HOLES[digit][i]) : GRectZero;
    }
  }
  for (int i = 0; i < 2; i++) {
    colon_dots[i] = GRect(origin.x + scaled(COLON_DOTS[i].origin.x), origin.y + scaled(COLON_DOTS[i].origin.y),
                          one_width, scaled(COLON_DOTS[i].size.h));
  }
}

static void draw_digit(Layer *layer, GContext *ctx) {
  char digit = *(char *) layer_get_data(layer);
  int width = (digit == '1') ? one_width : digit_width;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, width, digit_height), 0, GCornerNone);

  graphics_context_set_fill_color(ctx, GColorBlack);
  const GRect *holes = digit_holes[digit - '0'];
  for (int i = 0; i < 3; i++) {
    if (holes[i].size.w > 0) {
      graphics_fill_rect(ctx, holes[i], 0, GCornerNone);
    }
  }
}

// Shows digit (a character, '0' to '9') on a digit layer
static void set_digit(Layer *layer, char digit) {
  char *shown = layer_get_data(layer);
  if (*shown != digit) {
    *shown = digit;
    layer_mark_dirty(layer);
  }
}

static void draw_colon(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  for (int i = 0; i < 2; i++) {
    graphics_fill_rect(ctx, colon_dots[i], 0, GCornerNone);
  }
}

static Layer *create_digit_layer(GRect frame) {
  Layer *layer = layer_create_with_data(frame, sizeof(char));
  *(char *) layer_get_data(layer) = '0';
  layer_set_update_proc(layer, draw_digit);
  return layer;
}

// The animation currently moving each digit (NULL when the digit is at rest)
static Animation *tens_hour_anim, *ones_hour_anim, *tens_minute_anim, *ones_minute_anim;

static Animation **current_animation_for(Layer *layer) {
  if (layer == tens_hour) {
    return &tens_hour_anim;
  } else if (layer == ones_hour) {
    return &ones_hour_anim;
  } else if (layer == tens_minute) {
    return &tens_minute_anim;
  } else {
    return &ones_minute_anim;
  }
}

// destroying animations when stopped
void on_animation_stopped(Animation *anim, bool finished, void *context) {
  Animation **current = (Animation**) context;
  if (*current == (Animation*) anim) {
    *current = NULL;
  }
  property_animation_destroy((PropertyAnimation*) anim);
}

// animating the time changes
void animate_digit_layer(Layer *layer, GRect *digit_start, GRect *digit_finish, int duration, int delay) {
  // Nothing to move, so don't interrupt an animation that is actually moving this digit
  if (grect_equal(digit_start, digit_finish)) {
    return;
  }

  // Stop anything still moving this digit. Otherwise an older animation that finishes later
  // (e.g. a :58 fall that started late) has the last word and can leave the digit off-screen
  Animation **current = current_animation_for(layer);
  if (*current != NULL) {
    animation_unschedule(*current);
  }

  PropertyAnimation *anim = property_animation_create_layer_frame(layer, digit_start, digit_finish);
  
  // characteristics
  animation_set_duration((Animation*) anim, duration);
  animation_set_delay((Animation*) anim, delay);
  
  // freeing memory while stopped
  AnimationHandlers handlers = {
    .stopped = (AnimationStoppedHandler) on_animation_stopped
  };
  animation_set_handlers((Animation*) anim, handlers, current);
  
  // starting the animation
  animation_schedule((Animation*) anim);
  *current = (Animation*) anim;
}

// The four digits, with how each one leaves and comes back: tens digits rise and come back up from below,
// ones digits fall and come back down from above
typedef struct {
  Layer **layer;
  int index;        // position in the "HH:MM" time string
  bool rises;       // rises off the top and comes back up from below (otherwise falls and comes back down)
  int arrive_delay; // ms before it starts coming in, so neighbouring digits don't move in lockstep
} Digit;

static const Digit DIGITS[] = {
  { &tens_hour,   0, true,  1 },
  { &ones_hour,   1, false, 100 },
  { &tens_minute, 3, true,  100 },
  { &ones_minute, 4, false, 1 },
};
#define DIGIT_COUNT 4

// x position of the digit at index in time string t, on this screen. A "1" is narrower than other
// digits, so the digits next to one shift to keep the spacing even
static int digit_x(const char *t, int index) {
  int gap = scaled_edge(16, COLUMN_EDGES, ARRAY_LENGTH(COLUMN_EDGES)) - one_width; // as wide as the hollow in a digit
  int colon_x = origin.x + scaled(DESIGN_COLON_X);
  int colon_gap = scaled(DESIGN_COLON_GAP);
  int tens_hour_width = (t[0] == '1') ? one_width : digit_width;
  int ones_hour_width = (t[1] == '1') ? one_width : digit_width;
  int tens_minute_width = (t[3] == '1') ? one_width : digit_width;

  int ones_hour_x = colon_x - colon_gap - ones_hour_width;
  int tens_minute_x = colon_x + one_width + colon_gap;
  switch (index) {
    case 0:
      return ones_hour_x - gap - tens_hour_width;
    case 1:
      return ones_hour_x;
    case 3:
      return tens_minute_x;
    default:
      // One pixel further right when neither minute digit is a 1, as in the original design
      return tens_minute_x + tens_minute_width + gap + ((t[3] != '1' && t[4] != '1') ? 1 : 0);
  }
}

// Get the time as "HH:MM", allowing for 12hr and 24hr time
static void format_time(struct tm *t, char *buffer) {
  if (clock_is_24h_style() == true) {
    strftime(buffer, sizeof("00:00"), "%R", t);
  } else {
    strftime(buffer, sizeof("00:00"), "%I:%M", t);
  }
}

// PRE DIGIT CHANGE
// Digits that are about to change leave the screen. With a duration of 0 they're left in place;
// they still arrive from off-screen when the minute changes
static void start_leaving(const char *next_time, int duration) {
  if (duration == 0) {
    return;
  }
  for (int i = 0; i < DIGIT_COUNT; i++) {
    const Digit *d = &DIGITS[i];
    if (next_time[d->index] != shown_time[d->index]) {
      Layer *layer = *d->layer;
      GRect digit_start = layer_get_frame(layer);
      GRect digit_finish = GRect(digit_start.origin.x, d->rises ? high_y : low_y, digit_width, digit_height);
      animate_digit_layer(layer, &digit_start, &digit_finish, duration, 1);
    }
  }
}

static void return_timer_fired(void *context);

// Fires near :58 to start the digits leaving, so the watch only wakes for the animations instead of every second
static void leave_timer_fired(void *context) {
  leave_timer = NULL;

  time_t now;
  uint16_t ms;
  time_ms(&now, &ms);
  struct tm *t = localtime(&now);

  // The timer can fire late, or the clock can change while it waits; only act at the end of the minute it was set for
  if ((t->tm_hour * 60 + t->tm_min) != leave_timer_minute || t->tm_sec < 57) {
    return;
  }

  // Don't start leaving twice in one minute (the timer is set again whenever the time is redrawn)
  if (already_left_minute == leave_timer_minute) {
    return;
  }
  already_left_minute = leave_timer_minute;

  // Finish just before the minute changes. Launching near the end of a minute shortens the fall,
  // and when too little time is left for a fall to read as one, the digits just arrive at :00
  int ms_left = (60 - t->tm_sec) * 1000 - ms;
  int duration = ms_left - LEAVE_MARGIN;
  if (duration > LEAVE_DURATION) {
    duration = LEAVE_DURATION;
  } else if (duration < MIN_LEAVE_DURATION) {
    duration = 0;
  }

  // Compare with the time the next minute will show to find which digits are about to change
  time_t next_minute = now - t->tm_sec + 60;
  char next_time[] = "00:00";
  format_time(localtime(&next_minute), next_time);
  start_leaving(next_time, duration);

  // App timers count elapsed time, so this still fires if the clock is set back before the minute changes
  if (return_timer != NULL) {
    app_timer_cancel(return_timer);
  }
  return_timer = app_timer_register(ms_left + 1000, return_timer_fired, NULL);
}

static void schedule_leave_timer(void) {
  if (leave_timer != NULL) {
    app_timer_cancel(leave_timer);
  }

  time_t now;
  uint16_t ms;
  time_ms(&now, &ms);
  struct tm *t = localtime(&now);

  leave_timer_minute = t->tm_hour * 60 + t->tm_min;
  int ms_until_58 = (58 - t->tm_sec) * 1000 - ms;
  leave_timer = app_timer_register(ms_until_58 > 0 ? ms_until_58 : 0, leave_timer_fired, NULL);
}

// Fires a second after the minute should have changed. Normally it has, and the digits have arrived.
// If the clock was set back within the same minute while the digits were leaving (a phone time sync
// right at :59, say), that minute change won't come for a while; bring the digits back instead of
// leaving them off-screen until it does
static void return_timer_fired(void *context) {
  return_timer = NULL;

  time_t now;
  uint16_t ms;
  time_ms(&now, &ms);
  struct tm *t = localtime(&now);

  if ((t->tm_hour * 60 + t->tm_min) != shown_minute_of_day || already_left_minute != shown_minute_of_day) {
    return;
  }

  // Set back to the end of the minute again: the digits should be leaving anyway, so check after it ends
  if (t->tm_sec >= 57) {
    return_timer = app_timer_register((60 - t->tm_sec) * 1000 - ms + 1000, return_timer_fired, NULL);
    return;
  }

  for (int i = 0; i < DIGIT_COUNT; i++) {
    Layer *layer = *DIGITS[i].layer;
    GRect digit_start = layer_get_frame(layer);
    GRect digit_finish = GRect(digit_x(shown_time, DIGITS[i].index), normal_y, digit_width, digit_height);
    animate_digit_layer(layer, &digit_start, &digit_finish, 800, DIGITS[i].arrive_delay);
  }
  already_left_minute = -1;
  schedule_leave_timer();
}

void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  char new_time[] = "00:00";
  format_time(tick_time, new_time);

  // Decide from the time itself rather than units_changed: the tick service also calls this right after
  // subscribing, with no units set, and that call can arrive just after the minute has changed
  int minute_now = tick_time->tm_hour * 60 + tick_time->tm_min;
  bool first_draw = (shown_minute_of_day == -1);
  bool minute_changed = !first_draw && (minute_now != shown_minute_of_day);
  shown_minute_of_day = minute_now;
  if (!first_draw && !minute_changed) {
    schedule_leave_timer();
    return;
  }

  // POST DIGIT CHANGE
  // Every digit ends up at its spot for the new time, wherever it is now. This also covers a clock that
  // jumps (daylight saving, a time zone change): it's handled like any other minute change
  for (int i = 0; i < DIGIT_COUNT; i++) {
    const Digit *d = &DIGITS[i];
    Layer *layer = *d->layer;
    set_digit(layer, new_time[d->index]);
    GRect digit_finish = GRect(digit_x(new_time, d->index), normal_y, digit_width, digit_height);

    if (first_draw) {
      // The face starts with every digit at its spot for "00:00"; float each one to where it belongs
      GRect digit_start = layer_get_frame(layer);
      animate_digit_layer(layer, &digit_start, &digit_finish, 1000, 1);
    } else if (new_time[d->index] != shown_time[d->index]) {
      // A changed digit comes in from off-screen
      GRect digit_start = GRect(digit_finish.origin.x, d->rises ? low_y : high_y, digit_width, digit_height);
      animate_digit_layer(layer, &digit_start, &digit_finish, 800, d->arrive_delay);
    } else {
      // An unchanged digit slides to its new spot if a 1 appeared or disappeared next to it
      GRect digit_start = layer_get_frame(layer);
      animate_digit_layer(layer, &digit_start, &digit_finish, 400, 1);
    }
  }
  strcpy(shown_time, new_time);

  schedule_leave_timer();
} // end of tick handler

// WINDOW LIFE
#if !defined(PBL_PLATFORM_APLITE) // the original Pebble has no Quick View
// Quick View (an upcoming event shown at the bottom of rectangular watches) covers part of the screen.
// The time shrinks to fit the space above it, following Quick View as it slides in and out
static void fit_to_unobstructed_area(AnimationProgress progress, void *context) {
  Layer *root = window_get_root_layer(my_window);
  int old_normal_y = normal_y;
  set_up_layout(layer_get_bounds(root), layer_get_unobstructed_bounds(root));

  // Digits at rest move with it. Moving digits are left to finish; quick_view_done tidies up after them
  for (int i = 0; i < DIGIT_COUNT; i++) {
    Layer *layer = *DIGITS[i].layer;
    if (*current_animation_for(layer) == NULL && layer_get_frame(layer).origin.y == old_normal_y) {
      layer_set_frame(layer, GRect(digit_x(shown_time, DIGITS[i].index), normal_y, digit_width, digit_height));
    }
    layer_mark_dirty(layer);
  }
  layer_mark_dirty(colon_layer);
}

static void quick_view_done(void *context) {
  fit_to_unobstructed_area(ANIMATION_NORMALIZED_MAX, context);

  // Digits that were moving while Quick View slid in or out (arriving or sliding) were left to the old
  // layout; send every digit to its spot in the new one (one already there doesn't move). Digits leaving
  // at the end of the minute arrive at the new spots when the minute changes anyway
  if (already_left_minute != shown_minute_of_day) {
    for (int i = 0; i < DIGIT_COUNT; i++) {
      Layer *layer = *DIGITS[i].layer;
      GRect digit_start = layer_get_frame(layer);
      GRect digit_finish = GRect(digit_x(shown_time, DIGITS[i].index), normal_y, digit_width, digit_height);
      animate_digit_layer(layer, &digit_start, &digit_finish, 400, 1);
    }
  }
}
#endif

void window_load (Window *my_window) {
  GRect bounds = layer_get_bounds(window_get_root_layer(my_window));
  set_up_layout(bounds, layer_get_unobstructed_bounds(window_get_root_layer(my_window)));

  // creating the digit layers
  tens_hour = create_digit_layer(GRect(digit_x("00:00", 0), normal_y, digit_width, digit_height));
  ones_hour = create_digit_layer(GRect(digit_x("00:00", 1), normal_y, digit_width, digit_height));
  tens_minute = create_digit_layer(GRect(digit_x("00:00", 3), normal_y, digit_width, digit_height));
  ones_minute = create_digit_layer(GRect(digit_x("00:00", 4), normal_y, digit_width, digit_height));
  
  // creating the colon layer
  colon_layer = layer_create(bounds);
  layer_set_update_proc(colon_layer, draw_colon);
  
  //loading the layers
  layer_add_child(window_get_root_layer(my_window), colon_layer);
  
  layer_add_child(window_get_root_layer(my_window), tens_hour);
  layer_add_child(window_get_root_layer(my_window), ones_hour);
  layer_add_child(window_get_root_layer(my_window), tens_minute);
  layer_add_child(window_get_root_layer(my_window), ones_minute);
  
  // preventing face from starting blank
  struct tm *t;
  time_t temp;
  temp = time(NULL);
  t = localtime(&temp);
  tick_handler(t, MINUTE_UNIT);
}

void window_unload (Window *my_window) {
  layer_destroy(tens_hour);
  layer_destroy(ones_hour);
  layer_destroy(tens_minute);
  layer_destroy(ones_minute);
  layer_destroy(colon_layer);
}

// APP LIFE
void handle_init(void) {
  my_window = window_create();
  window_set_background_color(my_window, GColorBlack);
  window_set_window_handlers(my_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(my_window, true);
  // Hours and days too: a clock jump that keeps the minute number (10:20 to 11:20) isn't a minute change
  tick_timer_service_subscribe(MINUTE_UNIT | HOUR_UNIT | DAY_UNIT, (TickHandler) tick_handler);
#if !defined(PBL_PLATFORM_APLITE) // the original Pebble has no Quick View
  unobstructed_area_service_subscribe((UnobstructedAreaHandlers) {
    .change = fit_to_unobstructed_area,
    .did_change = quick_view_done,
  }, NULL);
#endif
}

void handle_deinit(void) {
  if (leave_timer != NULL) {
    app_timer_cancel(leave_timer);
  }
  if (return_timer != NULL) {
    app_timer_cancel(return_timer);
  }
  window_destroy(my_window);
  tick_timer_service_unsubscribe();
}

int main(void) {
  handle_init();
  app_event_loop();
  handle_deinit();
}