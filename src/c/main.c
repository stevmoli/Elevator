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
const int DIGIT_HEIGHT = 149;
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
const int TENS_HOUR_ZERO_ONE = 15;
const int TENS_HOUR_ONE_ZERO = 12;
const int TENS_HOUR_ONE_ONE = 28;
const int ONES_HOUR_ZERO = 36;
const int ONES_HOUR_ONE = 29;
const int TENS_MINUTE_ZERO = 80;
const int TENS_MINUTE_ONE = 72;
const int ONES_MINUTE_ZERO_ZERO = 113;
const int ONES_MINUTE_ZERO_ONE = 104;
const int ONES_MINUTE_ONE_ZERO = 96;
const int ONES_MINUTE_ONE_ONE = 88;

BitmapLayer *tens_hour, *ones_hour, *tens_minute, *ones_minute;

char tens_hour_string[] = "0";
char ones_hour_string[] = "0";
char tens_minute_string[] = "0";
char ones_minute_string[] = "0";

// Values of these static ints are initialized in the main() method (only initializing them here)
static int tens_hour_Xpos;
static int ones_hour_Xpos;
static int tens_minute_Xpos;
static int ones_minute_Xpos;

bool format_needs_fix = true;

// Updates BitmapLayers with the correct digit images, based on the current time (digits passed in as digit_string)
void image_update(char digit_string[], BitmapLayer *image){
  int digit;
  digit = atoi(digit_string);
  if (digit == 0){
    bitmap_layer_set_bitmap(image, zero);
  } else if (digit == 1){
    bitmap_layer_set_bitmap(image, one);
  } else if (digit == 2){
    bitmap_layer_set_bitmap(image, two);
  } else if (digit == 3){
    bitmap_layer_set_bitmap(image, three);
  } else if (digit == 4){
    bitmap_layer_set_bitmap(image, four);
  } else if (digit == 5){
    bitmap_layer_set_bitmap(image, five);
  } else if (digit == 6){
    bitmap_layer_set_bitmap(image, six);
  } else if (digit == 7){
    bitmap_layer_set_bitmap(image, seven);
  } else if (digit == 8){
    bitmap_layer_set_bitmap(image, eight);
  } else {
    bitmap_layer_set_bitmap(image, nine);
  }
}

// destroying animations when stopped
void on_animation_stopped(Animation *anim, bool finished, void *context) {
  property_animation_destroy((PropertyAnimation*) anim);
}

// animating the time changes
void animate_digit_layer(Layer *layer, GRect *digit_start, GRect *digit_finish, int duration, int delay) {
  PropertyAnimation *anim = property_animation_create_layer_frame(layer, digit_start, digit_finish);
  
  // characteristics
  animation_set_duration((Animation*) anim, duration);
  animation_set_delay((Animation*) anim, delay);
  
  // freeing memory while stopped
  AnimationHandlers handlers = {
    .stopped = (AnimationStoppedHandler) on_animation_stopped
  };
  animation_set_handlers((Animation*) anim, handlers, NULL);
  
  // starting the animation
  animation_schedule((Animation*) anim);
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
  int seconds = tick_time->tm_sec; // allowing for action when the second changes

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
  
  //PRE DIGIT CHANGE
  // ones digit of minutes falls before changing
  if (seconds == 58) { 
    GRect digit_start = GRect(ones_minute_Xpos, NORMAL_Y, 26, DIGIT_HEIGHT);  // GOOD
    GRect digit_finish = GRect(ones_minute_Xpos, LOW_Y, 26, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(ones_minute), &digit_start, &digit_finish, 1850, 1);
  }
  
  // tens digit of minutes rises before changing
  if ((seconds == 58) && (strncmp("9", &time_buffer[4], 1) == 0)) { 
    GRect digit_start = GRect(tens_minute_Xpos, NORMAL_Y, 26, DIGIT_HEIGHT);
    GRect digit_finish = GRect(tens_minute_Xpos, HIGH_Y, 26, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(tens_minute), &digit_start, &digit_finish, 1850, 1);
  }
  
  // ones digit of hours falls before changing
  if ((seconds == 58) && (minutes == 59)) { 
    GRect digit_start = GRect (ones_hour_Xpos, NORMAL_Y, 57, DIGIT_HEIGHT);
    GRect digit_finish = GRect (ones_hour_Xpos, LOW_Y, 57, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(ones_hour), &digit_start, &digit_finish, 1850, 1);
  }
  
  // tens digit of hours rises before changing (and according to 12hr or 24 hr time)
  if (clock_is_24h_style() == true) { 
    // this handles 24hr time
    if ((seconds == 58) && (minutes == 59) && ((hours == 9) || (hours == 19) || (hours == 23))) { 
      GRect digit_start = GRect (tens_hour_Xpos, NORMAL_Y, 34, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, HIGH_Y, 34, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 1850, 1);
    }
  } else { 
    // now we will deal with the 12hr case
    if ((seconds == 58) && (minutes == 59) && ((hours == 9) || (hours == 12) || (hours == 21) || (hours == 0))) {
      GRect digit_start = GRect (tens_hour_Xpos, NORMAL_Y, 34, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, HIGH_Y, 34, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 1850, 1);
    }
  }
  
  // POST DIGIT CHANGE
  // ones digit of minutes falls after changing
  if (seconds == 0) { 
    GRect digit_start = GRect(ones_minute_Xpos, HIGH_Y, 26, DIGIT_HEIGHT);  // GOOD
    GRect digit_finish = GRect(ones_minute_Xpos, NORMAL_Y, 26, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(ones_minute), &digit_start, &digit_finish, 800, 1);
  }
  
  // tens digit of minutes rises after changing
  if ((seconds == 0) && (strncmp("0", &time_buffer[4], 1) == 0)) { 
    GRect digit_start = GRect(tens_minute_Xpos, LOW_Y, 26, DIGIT_HEIGHT);
    GRect digit_finish = GRect(tens_minute_Xpos, NORMAL_Y, 26, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(tens_minute), &digit_start, &digit_finish, 800, 100);
  }
  
  // ones digit of hours falls after changing
  if ((seconds == 0) && (minutes == 0)) { 
    GRect digit_start = GRect (ones_hour_Xpos, HIGH_Y, 57, DIGIT_HEIGHT);
    GRect digit_finish = GRect (ones_hour_Xpos, NORMAL_Y, 57, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(ones_hour), &digit_start, &digit_finish, 800, 100);
  }
  
  // tens digit of hours rises after changing (accounting again for 24hr and 12hr time)
  if (clock_is_24h_style() == true) { 
    // 24hr time
    if ((seconds == 0) && (minutes == 0) && ((hours == 10) || (hours == 20) || (hours == 0))) { 
      GRect digit_start = GRect (tens_hour_Xpos, LOW_Y, 34, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, 34, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 800, 1);
    }
  } else { 
    // 12hr time
    if (((seconds == 0) && (minutes == 0)) && ((hours == 10) || (hours == 13) || (hours == 22) || (hours == 1))) {
      GRect digit_start = GRect (tens_hour_Xpos, LOW_Y, 34, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, 34, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 800, 1);
    }
  }
  
  // Animating digit adjustment when a one appears in the hours ones place
  if (((hours == 1) || (hours == 11)) && (minutes == 0) && (seconds == 0)) {
      GRect digit_start = GRect (11, NORMAL_Y, 34, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, 34, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 400, 1);    
  }
  
  // dealing with 11 PM
  if (clock_is_24h_style() == false) { 
    if ((hours == 23) && (minutes == 0) && (seconds == 0)) {
      GRect digit_start = GRect (11, NORMAL_Y, 34, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, 34, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 400, 1);
    }
  } else { 
    // and dealing with 21:00
    if ((hours == 21) && (minutes == 0) && (seconds == 0)) {
      GRect digit_start = GRect (11, NORMAL_Y, 34, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, 34, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 400, 1);
    }
  }
  
  // and moving the digit back when the one disappears from the hours ones place
  if (((hours == 2) || (hours == 12)) && (minutes == 0) && (seconds == 0)) {
    GRect digit_start = GRect (22, NORMAL_Y, 34, DIGIT_HEIGHT);
    GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, 34, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 400, 1);
  }
  
  // dealiing with 11 PM switching to midnight and 1 PM switching to 2 PM
  if (clock_is_24h_style() == false) { 
    if (((hours == 0) || (hours == 14)) && (minutes == 0) && (seconds == 0)) {
    GRect digit_start = GRect (22, NORMAL_Y, 34, DIGIT_HEIGHT);
    GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, 34, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 400, 1);
    }
  } else { 
    // and dealing with 21:59 switching to 22:00
    if ((hours == 22) && (minutes == 0) && (seconds == 0)) {
    GRect digit_start = GRect (22, NORMAL_Y, 34, DIGIT_HEIGHT);
    GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, 34, DIGIT_HEIGHT);
    animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 400, 1);
    }
  }
  
  // Add one time animations to fix initial positioning when watchface starts
  if (format_needs_fix == true) {
    
    // for if the ones place of the hour is a one (regardless of the tens place) when the face launches)
    if (ones_hour_Xpos == ONES_HOUR_ONE) {
      // move the tens place of the hour
      GRect digit_start = GRect (TENS_HOUR_ZERO_ZERO, NORMAL_Y, 26, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, 26, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 1000, 1);
      
      // move the ones place of the hour
      GRect digit_start_2 = GRect (ONES_HOUR_ZERO, NORMAL_Y, 26, DIGIT_HEIGHT);
      GRect digit_finish_2 = GRect (ones_hour_Xpos, NORMAL_Y, 26, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(ones_hour), &digit_start_2, &digit_finish_2, 1000, 1);  
    
    // for if the tens place of the hour is a one and the ones place of the hour isn't
    } else if (tens_hour_Xpos == TENS_HOUR_ONE_ZERO) { 
      GRect digit_start = GRect (TENS_HOUR_ZERO_ZERO, NORMAL_Y, 26, DIGIT_HEIGHT);
      GRect digit_finish = GRect (tens_hour_Xpos, NORMAL_Y, 26, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_hour), &digit_start, &digit_finish, 1000, 1);
    }
    
    // for if the tens place of the minute is a one when the face launches
    if (tens_minute_Xpos == TENS_MINUTE_ONE) {
      // move the tens place of the minute
      GRect digit_start = GRect(TENS_MINUTE_ZERO, NORMAL_Y, 26, DIGIT_HEIGHT);
      GRect digit_finish = GRect(tens_minute_Xpos, NORMAL_Y, 26, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(tens_minute), &digit_start, &digit_finish, 1000, 1);
      
      // move the ones place of the minute
      GRect digit_start2 = GRect(ONES_MINUTE_ZERO_ZERO, NORMAL_Y, 26, DIGIT_HEIGHT);  // the ones place has to be moved accordingly too
      GRect digit_finish2 = GRect(ones_minute_Xpos, NORMAL_Y, 26, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(ones_minute), &digit_start2, &digit_finish2, 1000, 1);
      
    // for if the ones place of the minute is a one while the tens place isn't a one
    } else if (ones_minute_Xpos == ONES_MINUTE_ZERO_ONE) { 
      // for if the ones place of the minute is a one when the face launches
      GRect digit_start = GRect(ONES_MINUTE_ZERO_ZERO, NORMAL_Y, 26, DIGIT_HEIGHT);
      GRect digit_finish = GRect(ones_minute_Xpos, NORMAL_Y, 26, DIGIT_HEIGHT);
      animate_digit_layer(bitmap_layer_get_layer(ones_minute), &digit_start, &digit_finish, 1000, 1);
    }
    format_needs_fix = false; 
  }
  
  // DONE with animations

  // Getting current digits as strings
  *tens_hour_string = time_buffer[0];
  *ones_hour_string = time_buffer[1];
  *tens_minute_string = time_buffer[3];
  *ones_minute_string = time_buffer[4];
  
  // image layers updated with image_update method
  image_update(tens_hour_string, tens_hour);
  image_update(ones_hour_string, ones_hour);
  image_update(tens_minute_string, tens_minute);
  image_update(ones_minute_string, ones_minute);

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
  tens_hour = bitmap_layer_create(GRect(TENS_HOUR_ZERO_ZERO, NORMAL_Y, 26, DIGIT_HEIGHT));
  ones_hour = bitmap_layer_create(GRect(ONES_HOUR_ZERO, NORMAL_Y, 26, DIGIT_HEIGHT)); // 44 when 1??
  tens_minute = bitmap_layer_create(GRect(TENS_MINUTE_ZERO, NORMAL_Y, 26, DIGIT_HEIGHT));
  ones_minute = bitmap_layer_create(GRect(ONES_MINUTE_ZERO_ZERO, NORMAL_Y, 26, DIGIT_HEIGHT));
  
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
  tick_timer_service_subscribe(SECOND_UNIT, (TickHandler) tick_handler);
}

void handle_deinit(void) {
  window_destroy(my_window);
  tick_timer_service_unsubscribe();
}

int main(void) {
  tens_hour_Xpos = TENS_HOUR_ZERO_ZERO;
  ones_hour_Xpos = ONES_HOUR_ZERO;
  tens_minute_Xpos = TENS_MINUTE_ZERO;
  ones_minute_Xpos = ONES_MINUTE_ZERO_ZERO;
  
  handle_init();
  app_event_loop();
  handle_deinit();
}