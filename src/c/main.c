#include <pebble.h>
#include <stdio.h>
#include <string.h>

Window *my_window;
GBitmap *zero, *one, *two, *three, *four, *five, *six, *seven, *eight, *nine;
TextLayer *text_colon_layer;
static GFont HBH_font;
TextLayer *background_layer;

static char time_buffer[] = "00:00";

const int NORMAL_Y = 8; // Normal y position of digits
const int HIGH_Y = -150; // High y position of digits (for when above top of screen during animation)
const int LOW_Y = 170; // Low y position of digits (for when below bottom of screen during animation)
const int DIGIT_WIDTH = 26;
const int DIGIT_HEIGHT = 149;
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

BitmapLayer *tens_hour, *ones_hour, *tens_minute, *ones_minute;

// Values of these static ints are first set in the main() method (only initializing them here)
// These values aren't the current x positions of the digits; instead they are the target x positions for the next animation
static int tens_hour_Xpos;
static int ones_hour_Xpos;
static int tens_minute_Xpos;
static int ones_minute_Xpos;

// These values are the current x positions of the digits (which we'll use as starting points in animations)
static int tens_hour_current_Xpos;
static int ones_hour_current_Xpos;
static int tens_minute_current_Xpos;
static int ones_minute_current_Xpos;

// These values are the current y positions of the digits (which we'll use to check if we need to animate)
static int tens_hour_current_Ypos;
static int ones_hour_current_Ypos;
static int tens_minute_current_Ypos;
static int ones_minute_current_Ypos;

bool format_needs_fix = true;

// Minutes below are minutes of the day: hour * 60 + minute, from 0 (00:00) to 1439 (23:59)
static int shown_minute_of_day = -1; // the minute currently on screen (-1 until the face first draws)
static AppTimer *leave_timer; // starts the digits leaving near :58
static int leave_timer_minute = -1; // the minute leave_timer was set for
static int already_left_minute = -1; // the last minute whose digits already started leaving

// Updates BitmapLayers with the correct digit images, based on the current time (digits passed in as digit_string)
void image_update(char digit, BitmapLayer *image){
  if (strncmp("0", &digit, 1) == 0){
    bitmap_layer_set_bitmap(image, zero);
  } else if (strncmp("1", &digit, 1) == 0){
    bitmap_layer_set_bitmap(image, one);
  } else if (strncmp("2", &digit, 1) == 0){
    bitmap_layer_set_bitmap(image, two);
  } else if (strncmp("3", &digit, 1) == 0){
    bitmap_layer_set_bitmap(image, three);
  } else if (strncmp("4", &digit, 1) == 0){
    bitmap_layer_set_bitmap(image, four);
  } else if (strncmp("5", &digit, 1) == 0){
    bitmap_layer_set_bitmap(image, five);
  } else if (strncmp("6", &digit, 1) == 0){
    bitmap_layer_set_bitmap(image, six);
  } else if (strncmp("7", &digit, 1) == 0){
    bitmap_layer_set_bitmap(image, seven);
  } else if (strncmp("8", &digit, 1) == 0){
    bitmap_layer_set_bitmap(image, eight);
  } else {
    bitmap_layer_set_bitmap(image, nine);
  }
  
  // Align image of digits to the left of it's bitmap layer since it isn't always as wide as its containing bitmap layer
  // This ensures that our X position values always work as intended, regardless of the width of a digit
  bitmap_layer_set_alignment(image, GAlignLeft);
}

// The animation currently moving each digit (NULL when the digit is at rest)
static Animation *tens_hour_anim, *ones_hour_anim, *tens_minute_anim, *ones_minute_anim;

static Animation **current_animation_for(Layer *layer) {
  if (layer == bitmap_layer_get_layer(tens_hour)) {
    return &tens_hour_anim;
  } else if (layer == bitmap_layer_get_layer(ones_hour)) {
    return &ones_hour_anim;
  } else if (layer == bitmap_layer_get_layer(tens_minute)) {
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

// PRE DIGIT CHANGE
// Digits that are about to change leave the screen. With a duration of 0 they're only marked as gone,
// so they still arrive from off-screen when the minute changes
static void start_leaving(int hours, int minutes, int duration) {
  // ones digit of minutes falls before changing
  if (duration > 0) {
    GRect digit_start = GRect(ones_minute_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
    GRect digit_finish = GRect(ones_minute_Xpos, LOW_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(ones_minute), &digit_start, &digit_finish, duration, 1);
  }
  ones_minute_current_Xpos = ones_minute_Xpos;
  ones_minute_current_Ypos = LOW_Y;

  // tens digit of minutes rises before changing
  if (strncmp("9", &time_buffer[4], 1) == 0) {
    if (duration > 0) {
      GRect digit_start = GRect(tens_minute_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish = GRect(tens_minute_Xpos, HIGH_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_minute), &digit_start, &digit_finish, duration, 1);
    }
    tens_minute_current_Xpos = tens_minute_Xpos;
    tens_minute_current_Ypos = HIGH_Y;
  }

  // ones digit of hours falls before changing
  if (minutes == 59) {
    if (duration > 0) {
      GRect digit_start = GRect (ones_hour_Xpos, NORMAL_Y, 57, DIGIT_HEIGHT);
      GRect digit_finish = GRect (ones_hour_Xpos, LOW_Y, 57, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(ones_hour), &digit_start, &digit_finish, duration, 1);
    }
    ones_hour_current_Xpos = ones_hour_Xpos;
    ones_hour_current_Ypos = LOW_Y;
  }

  // tens digit of hours rises before changing (and according to 12hr or 24 hr time)
  bool tens_hour_changes;
  if (clock_is_24h_style() == true) {
    // this handles 24hr time
    tens_hour_changes = (minutes == 59) && ((hours == 9) || (hours == 19) || (hours == 23));
  } else {
    // now we will deal with the 12hr case
    tens_hour_changes = (minutes == 59) && ((hours == 9) || (hours == 12) || (hours == 21) || (hours == 0));
  }
  if (tens_hour_changes) {
    if (duration > 0) {
      GRect digit_start = GRect (tens_hour_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, HIGH_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, duration, 1);
    }
    tens_hour_current_Xpos = tens_hour_Xpos;
    tens_hour_current_Ypos = HIGH_Y;
  }
}

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
  start_leaving(t->tm_hour, t->tm_min, duration);
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

void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  
  // Get the time put into the buffer created earlier.  The if statement allows for 12hr and 24hr time
  if (clock_is_24h_style() == true) {
    strftime(time_buffer, sizeof("00:00"), "%R", tick_time);
  } else {
    strftime(time_buffer, sizeof("00:00"), "%I:%M", tick_time);
  }
  
  int hours = tick_time->tm_hour; // allowing for action when the hour changes
  int minutes = tick_time->tm_min;  // allowing for action when the minute changes
  // Decide from the time itself rather than units_changed: the tick service also calls this right after
  // subscribing, with no units set, and that call can arrive just after the minute has changed
  int minute_now = hours * 60 + minutes;
  bool minute_changed = (shown_minute_of_day != -1) && (minute_now != shown_minute_of_day);
  shown_minute_of_day = minute_now;
  
  // image layers updated with image_update method
  image_update((char)time_buffer[0], tens_hour);
  image_update((char)time_buffer[1], ones_hour);
  image_update((char)time_buffer[3], tens_minute);
  image_update((char)time_buffer[4], ones_minute);

  ///// Dealing with digit alignment in cases when the digit 1 is used /////

  /// Hours ///
  // If ones place of hours is a 1:
  if (strncmp("1", &time_buffer[1], 1) == 0) { 
    ones_hour_Xpos = ONES_HOUR_ONE;

    // If tens place of hours is a 1
    if (strncmp("1", &time_buffer[0], 1) == 0) {
      tens_hour_Xpos = TENS_HOUR_ONE_ONE;

    // If tens place of hours isn't a 1
    } else {
      tens_hour_Xpos = TENS_HOUR_ZERO_ONE;
    }

  // If ones place of hours isn't a 1
  } else {
    ones_hour_Xpos = ONES_HOUR_ZERO;

    // If tens place of hours is a 1
    if (strncmp("1", &time_buffer[0], 1) == 0) {
      tens_hour_Xpos = TENS_HOUR_ONE_ZERO;
    
    // If tens place of hours isn't a 1
    } else {
      tens_hour_Xpos = TENS_HOUR_ZERO_ZERO;
    }
  }

  /// Minutes ///  
  // If tens place of minutes is a 1
  if (strncmp("1", &time_buffer[3], 1) == 0) {
    tens_minute_Xpos = TENS_MINUTE_ONE; 

    // If ones place of minutes is a 1
    if (strncmp("1", &time_buffer[4], 1) == 0) {
      ones_minute_Xpos = ONES_MINUTE_ONE_ONE;

    // If ones place of minutes isn't a 1
    } else {
      ones_minute_Xpos = ONES_MINUTE_ONE_ZERO;
    }

  // If tens place of minutes isn't a 1
  } else {
    tens_minute_Xpos = TENS_MINUTE_ZERO;

    // If ones place of minutes is a 1
    if (strncmp("1", &time_buffer[4], 1) == 0) {
      ones_minute_Xpos = ONES_MINUTE_ZERO_ONE;
    
    // If ones place of minutes isn't a 1
    } else {
      ones_minute_Xpos = ONES_MINUTE_ZERO_ZERO;
    }
  }
  
  
  // Triggering the animations:
  
  // POST DIGIT CHANGE
  // Check the Y position of each digit. If equal to HIGH_Y or LOW_Y, we need to animate the return of the newly-changed digit
  if (minute_changed) {
    // tens digit of hours
    if (tens_hour_current_Ypos == HIGH_Y) {
      GRect digit_start = GRect (tens_hour_Xpos, LOW_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 800, 1);
      tens_hour_current_Xpos = tens_hour_Xpos;
      tens_hour_current_Ypos = NORMAL_Y;
    }

    // ones digit of hours
    if (ones_hour_current_Ypos == LOW_Y) {
      GRect digit_start = GRect (ones_hour_Xpos, HIGH_Y, 57, DIGIT_HEIGHT);
      GRect digit_finish = GRect (ones_hour_Xpos, NORMAL_Y, 57, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(ones_hour), &digit_start, &digit_finish, 800, 100);
      ones_hour_current_Xpos = ones_hour_Xpos;
      ones_hour_current_Ypos = NORMAL_Y;
    }

    // tens digit of minutes
    if (tens_minute_current_Ypos == HIGH_Y) {
      GRect digit_start = GRect(tens_minute_Xpos, LOW_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish = GRect(tens_minute_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_minute), &digit_start, &digit_finish, 800, 100);
      tens_minute_current_Xpos = tens_minute_Xpos;
      tens_minute_current_Ypos = NORMAL_Y;
    }

    // ones digit of minutes
    // runs every minute, so no conditional needed
    GRect digit_start = GRect(ones_minute_Xpos, HIGH_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
    GRect digit_finish = GRect(ones_minute_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(ones_minute), &digit_start, &digit_finish, 800, 1);
    ones_minute_current_Xpos = ones_minute_Xpos;
    ones_minute_current_Ypos = NORMAL_Y;  // TODO: consider removing this Ypos altogether
  }
  
  /*
    Digit slide animations: digits that aren't changing slide sideways to account for a digit next to them swapping
    with a digit of a different width.
  */
  // Animating digit adjustment when a one appears in the hours ones place
  if ( (((hours == 1) || (hours == 11)) && (minutes == 0) && minute_changed) ||
    // or moving the digit back when the one disappears from the hours ones place
    (((hours == 2) || (hours == 12)) && (minutes == 0) && minute_changed) ) 
  {
    GRect digit_start = GRect (tens_hour_current_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
    GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 400, 1);
    tens_hour_current_Xpos = tens_hour_Xpos;
  }
  
  // 12hr-mode-specific animations:
  if (clock_is_24h_style() == false) { 
    // dealing with 11 PM
    if ( ((hours == 23) && (minutes == 0) && minute_changed) ||
      // dealiing with 11 PM switching to midnight and 1 PM switching to 2 PM
      (((hours == 0) || (hours == 14)) && (minutes == 0) && minute_changed) ) 
    {
      GRect digit_start = GRect (tens_hour_current_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 400, 1);
      tens_hour_current_Xpos = tens_hour_Xpos;
    }
  
  // 24hr-mode-specific animations:
  } else { 
    // dealing with 21:00
    if (((hours == 21) && (minutes == 0) && minute_changed) ||
      // or dealing with 21:59 switching to 22:00
      ((hours == 22) && (minutes == 0) && minute_changed) ) 
    {
      GRect digit_start = GRect (tens_hour_current_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 400, 1);
      tens_hour_current_Xpos = tens_hour_Xpos;
    }
  }

  // Add one time animations to fix initial positioning when watchface starts
  if (format_needs_fix == true) {
    
    // for if the ones place of the hour is a one (regardless of the tens place) when the face launches)
    if (ones_hour_Xpos == ONES_HOUR_ONE) {
      // move the tens place of the hour
      GRect digit_start = GRect (tens_hour_current_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 1000, 1);
      tens_hour_current_Xpos = tens_hour_Xpos;
      
      // move the ones place of the hour
      GRect digit_start_2 = GRect (ones_hour_current_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish_2 = GRect (ones_hour_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(ones_hour), &digit_start_2, &digit_finish_2, 1000, 1);
      ones_hour_current_Xpos = ones_hour_Xpos;
    
    // for if the tens place of the hour is a one and the ones place of the hour isn't
    } else if (tens_hour_Xpos == TENS_HOUR_ONE_ZERO) { 
      GRect digit_start = GRect (tens_hour_current_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 1000, 1);
      tens_hour_current_Xpos = tens_hour_Xpos;
    }
    
    // for if the tens place of the minute is a one when the face launches
    if (tens_minute_Xpos == TENS_MINUTE_ONE) {
      // move the tens place of the minute
      GRect digit_start = GRect(tens_minute_current_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish = GRect(tens_minute_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_minute), &digit_start, &digit_finish, 1000, 1);
      tens_minute_current_Xpos = tens_minute_Xpos;
      
      // move the ones place of the minute
      GRect digit_start2 = GRect(ones_minute_current_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish2 = GRect(ones_minute_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(ones_minute), &digit_start2, &digit_finish2, 1000, 1);
      ones_minute_current_Xpos = ones_minute_Xpos;
      
    // for if the ones place of the minute is a one while the tens place isn't a one
    } else if (ones_minute_Xpos == ONES_MINUTE_ZERO_ONE) { 
      // for if the ones place of the minute is a one when the face launches
      GRect digit_start = GRect(ones_minute_current_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      GRect digit_finish = GRect(ones_minute_Xpos, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(ones_minute), &digit_start, &digit_finish, 1000, 1);
      ones_minute_current_Xpos = ones_minute_Xpos;
    }
    format_needs_fix = false; 
  }
  
  // DONE with animations

  schedule_leave_timer();
} // end of tick handler

// WINDOW LIFE
void window_load (Window *my_window) {
  // load background
  background_layer = text_layer_create(GRect(0, 0, 144, 168));
  text_layer_set_background_color(background_layer, GColorBlack);
  
  // loading the digit images
  zero = gbitmap_create_with_resource(RESOURCE_ID_N_0);
  one = gbitmap_create_with_resource(RESOURCE_ID_N_1);
  two = gbitmap_create_with_resource(RESOURCE_ID_N_2);
  three = gbitmap_create_with_resource(RESOURCE_ID_N_3);
  four = gbitmap_create_with_resource(RESOURCE_ID_N_4);
  five = gbitmap_create_with_resource(RESOURCE_ID_N_5);
  six = gbitmap_create_with_resource(RESOURCE_ID_N_6);
  seven = gbitmap_create_with_resource(RESOURCE_ID_N_7);
  eight = gbitmap_create_with_resource(RESOURCE_ID_N_8);
  nine = gbitmap_create_with_resource(RESOURCE_ID_N_9);
  
  // creating the gbitmap layers
  tens_hour = bitmap_layer_create(GRect(TENS_HOUR_ZERO_ZERO, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT));
  ones_hour = bitmap_layer_create(GRect(ONES_HOUR_ZERO, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT));
  tens_minute = bitmap_layer_create(GRect(TENS_MINUTE_ZERO, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT));
  ones_minute = bitmap_layer_create(GRect(ONES_MINUTE_ZERO_ZERO, NORMAL_Y, DIGIT_WIDTH, DIGIT_HEIGHT));
  
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
  
  layer_add_child(window_get_root_layer(my_window), bitmap_layer_get_layer(tens_hour));
  layer_add_child(window_get_root_layer(my_window), bitmap_layer_get_layer(ones_hour));
  layer_add_child(window_get_root_layer(my_window), bitmap_layer_get_layer(tens_minute));
  layer_add_child(window_get_root_layer(my_window), bitmap_layer_get_layer(ones_minute));
  
  // preventing face from starting blank
  struct tm *t;
  time_t temp;
  temp = time(NULL);
  t = localtime(&temp);
  tick_handler(t, MINUTE_UNIT);
}

void window_unload (Window *my_window) {
  bitmap_layer_destroy(tens_hour);
  bitmap_layer_destroy(ones_hour);
  bitmap_layer_destroy(tens_minute);
  bitmap_layer_destroy(ones_minute);
  
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
  tick_timer_service_subscribe(MINUTE_UNIT, (TickHandler) tick_handler);
}

void handle_deinit(void) {
  if (leave_timer != NULL) {
    app_timer_cancel(leave_timer);
  }
  window_destroy(my_window);
  tick_timer_service_unsubscribe();
}

int main(void) {
  // set initial Xpos values and Ypos values
  tens_hour_Xpos = TENS_HOUR_ZERO_ZERO;
  ones_hour_Xpos = ONES_HOUR_ZERO;
  tens_minute_Xpos = TENS_MINUTE_ZERO;
  ones_minute_Xpos = ONES_MINUTE_ZERO_ZERO;
  tens_hour_current_Xpos = TENS_HOUR_ZERO_ZERO;
  ones_hour_current_Xpos = ONES_HOUR_ZERO;
  tens_minute_current_Xpos = TENS_MINUTE_ZERO;
  ones_minute_current_Xpos = ONES_MINUTE_ZERO_ZERO;
  tens_hour_current_Ypos = NORMAL_Y;
  ones_hour_current_Ypos = NORMAL_Y;
  tens_minute_current_Ypos = NORMAL_Y;
  ones_minute_current_Ypos = NORMAL_Y;
  
  handle_init();
  app_event_loop();
  handle_deinit();
}