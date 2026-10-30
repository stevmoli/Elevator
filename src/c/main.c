#include <pebble.h>
#include <stdio.h>
#include <string.h>

Window *my_window;
TextLayer *text_colon_layer;
static GFont HBH_font;
TextLayer *background_layer;

const int NORMAL_Y = 8; // Normal y position of digits
const int HIGH_Y = -150; // High y position of digits (for when above top of screen during animation)
const int LOW_Y = 170; // Low y position of digits (for when below bottom of screen during animation)
const int DIGIT_WIDTH = 26;
const int DIGIT_HEIGHT = 149;
const int ONE_WIDTH = 10; // a "1" is just a bar the width of the other digits' strokes
const int LEAVE_DURATION = 1850; // How long digits take to leave the screen before the minute changes
const int LEAVE_MARGIN = 150; // How long before the minute changes the leaving animation finishes
const int MIN_LEAVE_DURATION = 500; // Shorter than this and digits skip leaving and just arrive at :00
/* 
  The below constant ints are the x position of the specified digit when the minute or hour digits match
  the format of the digits in the variable name. ONE represents a 1, ZERO represents any other digit.
  The tens place of the hour and ones place of the minutes can be offset by the ones place of the hour
  and tens place of the minute, respectively, so these digits require formats including both digits.
  I.E., the hour "12" would have its tens place positioned with TENS_HOUR_ONE_ZERO and ones place positioned
  with ONES_HOUR_ZERO, while the minute "12" would have its tens place positioned with TENS_MINUTE_ONE and 
  its ones place positioned with ONES_MINUTE_ONE_ZERO
*/
const int TENS_HOUR_ZERO_ZERO = 4;
const int TENS_HOUR_ZERO_ONE = 20;
const int TENS_HOUR_ONE_ZERO = 20;
const int TENS_HOUR_ONE_ONE = 36;
const int ONES_HOUR_ZERO = 36;
const int ONES_HOUR_ONE = 52;
const int TENS_MINUTE_ZERO = 80;
const int TENS_MINUTE_ONE = 80;
const int ONES_MINUTE_ZERO_ZERO = 113;
const int ONES_MINUTE_ZERO_ONE = 112;
const int ONES_MINUTE_ONE_ZERO = 96;
const int ONES_MINUTE_ONE_ONE = 96;

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
// Positions are within the digit, in pixels. A "1" has no holes; it's just narrower
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

static void draw_digit(Layer *layer, GContext *ctx) {
  char digit = *(char *) layer_get_data(layer);
  int width = (digit == '1') ? ONE_WIDTH : DIGIT_WIDTH;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, 0, width, DIGIT_HEIGHT), 0, GCornerNone);

  graphics_context_set_fill_color(ctx, GColorBlack);
  const GRect *holes = DIGIT_HOLES[digit - '0'];
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

// x position of the digit at index in time string t. A "1" is narrower than other digits, so the
// digits next to one shift to keep the spacing even
static int digit_x(const char *t, int index) {
  switch (index) {
    case 0:
      if (t[0] == '1') {
        return (t[1] == '1') ? TENS_HOUR_ONE_ONE : TENS_HOUR_ONE_ZERO;
      }
      return (t[1] == '1') ? TENS_HOUR_ZERO_ONE : TENS_HOUR_ZERO_ZERO;
    case 1:
      return (t[1] == '1') ? ONES_HOUR_ONE : ONES_HOUR_ZERO;
    case 3:
      return (t[3] == '1') ? TENS_MINUTE_ONE : TENS_MINUTE_ZERO;
    default:
      if (t[3] == '1') {
        return (t[4] == '1') ? ONES_MINUTE_ONE_ONE : ONES_MINUTE_ONE_ZERO;
      }
      return (t[4] == '1') ? ONES_MINUTE_ZERO_ONE : ONES_MINUTE_ZERO_ZERO;
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
      GRect digit_finish = GRect(digit_start.origin.x, d->rises ? HIGH_Y : LOW_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
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
    GRect digit_finish = GRect(digit_x(shown_time, DIGITS[i].index), NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
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
    GRect digit_finish = GRect(digit_x(new_time, d->index), NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);

    if (first_draw) {
      // The face starts with every digit at its spot for "00:00"; float each one to where it belongs
      GRect digit_start = layer_get_frame(layer);
      animate_digit_layer(layer, &digit_start, &digit_finish, 1000, 1);
    } else if (new_time[d->index] != shown_time[d->index]) {
      // A changed digit comes in from off-screen
      GRect digit_start = GRect(digit_finish.origin.x, d->rises ? LOW_Y : HIGH_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
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
void window_load (Window *my_window) {
  // load background
  background_layer = text_layer_create(GRect(0, 0, 144, 168));
  text_layer_set_background_color(background_layer, GColorBlack);
  
  // creating the digit layers
  tens_hour = create_digit_layer(GRect(TENS_HOUR_ZERO_ZERO, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT));
  ones_hour = create_digit_layer(GRect(ONES_HOUR_ZERO, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT));
  tens_minute = create_digit_layer(GRect(TENS_MINUTE_ZERO, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT));
  ones_minute = create_digit_layer(GRect(ONES_MINUTE_ZERO_ZERO, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT));
  
  // loading the font and the colon_layer
  HBH_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_HBH_120));
  
  text_colon_layer = text_layer_create(GRect(1, 10, 143, 120));
  text_layer_set_background_color(text_colon_layer, GColorClear);
  text_layer_set_text_color(text_colon_layer, GColorWhite);
  text_layer_set_text_alignment(text_colon_layer, GTextAlignmentCenter);
  text_layer_set_font(text_colon_layer, HBH_font);
  text_layer_set_text(text_colon_layer, ":");
  
  //loading the layers
  layer_add_child(window_get_root_layer(my_window), text_layer_get_layer(background_layer));
  layer_add_child(window_get_root_layer(my_window), text_layer_get_layer(text_colon_layer));
  
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
  
  text_layer_destroy(background_layer);
  text_layer_destroy(text_colon_layer);
  fonts_unload_custom_font(HBH_font);
}

// APP LIFE
void handle_init(void) {
  my_window = window_create();
  window_set_window_handlers(my_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(my_window, true);
  // Hours and days too: a clock jump that keeps the minute number (10:20 to 11:20) isn't a minute change
  tick_timer_service_subscribe(MINUTE_UNIT | HOUR_UNIT | DAY_UNIT, (TickHandler) tick_handler);
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