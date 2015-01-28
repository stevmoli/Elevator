#include <pebble.h>
#include <stdio.h>
#include <string.h>

Window *my_window;
TextLayer *text_hours_layer;
TextLayer *text_minutes_layer;
TextLayer *text_colon_layer;
TextLayer *text_long_hours_layer;
TextLayer *text_short_minutes_layer;
static GFont HBH_font;
TextLayer *background_layer;

static char time_buffer[] = "00:00";

char lnghr[] = "0";
char srthr[] = "0";
char lngmn[] = "0";
char srtmn[] = "0";


int long_hours_offset = 11;
int short_hours_offset = 10;
int short_minutes_offset = 99;

int format_fix = 1;



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
  
  // get the time put into the buffer created earlier.  if statement allows for 12hr and 24hr time
  if (clock_is_24h_style() == true) {
    strftime(time_buffer, sizeof("00:00"), "%R", tick_time);
  } else {
    strftime(time_buffer, sizeof("00:00"), "%I:%M", tick_time);
  }
  
  int hours = tick_time->tm_hour; // allowing for action when the hour changes
  int minutes = tick_time->tm_min;  // allowing for action when the minute changes
  int seconds = tick_time->tm_sec; // allowing for action when the second changes
  
  
  // Dealing with font alignment when the digit 1 is used
  if (strncmp("1", &time_buffer[1], 1) == 0) { // handles tens place and ones place of hours when ones place is one
    long_hours_offset = 22;
    short_hours_offset = 9;
  } else {
    long_hours_offset = 11;
    short_hours_offset = 10;
  }
  
  if ((strncmp("1", &time_buffer[0], 1) == 0) && (strncmp("1", &time_buffer[1], 1) == 0)) { // handles tens place of hours when it is a one and the ones place is a one too
    long_hours_offset = 21;
  }
  
  if ((strncmp("1", &time_buffer[0], 1) == 0) && (strncmp("1", &time_buffer[1], 1) != 0)) { // handles tens place of hours when it is a one and the ones place is not a one
    long_hours_offset = 10;
  }
  
  if (strncmp("1", &time_buffer[3], 1) == 0) { // handles ones place of minutes
    short_minutes_offset = 88;
  } else {
    short_minutes_offset = 99;
  }
  
  // Triggering the animations:
  
  //PRE DIGIT CHANGE
  if (seconds == 58) {  // ones digit of minutes falls before changing
    GRect digit_start = GRect(short_minutes_offset, 20, 33, 140);
    GRect digit_finish = GRect(short_minutes_offset, 170, 33, 140);
    animate_digit_layer(text_layer_get_layer(text_short_minutes_layer), &digit_start, &digit_finish, 1850, 1);
  }
  
  if ((seconds == 58) && (strncmp("9", &time_buffer[4], 1) == 0)) {  // tens digit of minutes rises before changing
    GRect digit_start = GRect(77, 20, 66, 140);
    GRect digit_finish = GRect(77, -134, 66, 140);
    animate_digit_layer(text_layer_get_layer(text_minutes_layer), &digit_start, &digit_finish, 1850, 1);
  }
  
  if ((seconds == 58) && (minutes == 59)) {  // ones digit of hours falls before changing
    GRect digit_start = GRect (short_hours_offset, 20, 57, 140);
    GRect digit_finish = GRect (short_hours_offset, 170, 57, 140);
    animate_digit_layer(text_layer_get_layer(text_hours_layer), &digit_start, &digit_finish, 1850, 1);
  }
  
  if (clock_is_24h_style() == true) {  // tens digit of hours rises before changing (and according to 12hr or 24 hr time)
    if ((seconds == 58) && (minutes == 59) && ((hours == 9) || (hours == 19) || (hours == 23))) {  // this handles 24hr time
      GRect digit_start = GRect (long_hours_offset, 20, 34, 140);
      GRect digit_finish = GRect (long_hours_offset, -134, 34, 140);
      animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 1850, 1);
    }
  } else { // now we will deal with the 12hr case
    if ((seconds == 58) && (minutes == 59) && ((hours == 9) || (hours == 12) || (hours == 0))) {
      GRect digit_start = GRect (long_hours_offset, 20, 34, 140);
      GRect digit_finish = GRect (long_hours_offset, -134, 34, 140);
      animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 1850, 1);
    }
  }
  
  // POST DIGIT CHANGE
  if (seconds == 0) {  // ones digit of minutes falls after changing
    GRect digit_start = GRect(short_minutes_offset, -134, 33, 140);
    GRect digit_finish = GRect(short_minutes_offset, 20, 33, 140);
    animate_digit_layer(text_layer_get_layer(text_short_minutes_layer), &digit_start, &digit_finish, 800, 1);
  }
  
  if ((seconds == 0) && (strncmp("0", &time_buffer[4], 1) == 0)) {  // tens digit of minutes rises after changing
    GRect digit_start = GRect(77, 170, 66, 140);
    GRect digit_finish = GRect(77, 20, 66, 140);
    animate_digit_layer(text_layer_get_layer(text_minutes_layer), &digit_start, &digit_finish, 800, 100);
  }
  
  if ((seconds == 0) && (minutes == 0)) {  // ones digit of hours falls after changing
    GRect digit_start = GRect (short_hours_offset, -134, 57, 140);
    GRect digit_finish = GRect (short_hours_offset, 20, 57, 140);
    animate_digit_layer(text_layer_get_layer(text_hours_layer), &digit_start, &digit_finish, 800, 100);
  }
  
  if (clock_is_24h_style() == true) { // tens digit of hours rises after changing (accounting again for 24hr and 12hr time)
    if ((seconds == 0) && (minutes == 0) && ((hours == 10) || (hours == 20) || (hours == 0))) { // 24hr time
      GRect digit_start = GRect (long_hours_offset, 170, 34, 140);
      GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
      animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 800, 1);
    }
  } else { // 12hr time
    if (((seconds == 0) && (minutes == 0)) && ((hours == 10) || (hours == 13) || (hours == 1))) {
      GRect digit_start = GRect (long_hours_offset, 170, 34, 140);
      GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
      animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 800, 1);
    }
  }
  
  // Animating digit adjustment when a one appears in the hours ones place
  if (((hours == 1) || (hours == 11)) && (minutes == 0) && (seconds == 0)) {
      GRect digit_start = GRect (11, 20, 34, 140);
      GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
      animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 400, 1);    
  }
  
  if (clock_is_24h_style() == false) { // dealing with 11 PM
    if ((hours == 23) && (minutes == 0) && (seconds == 0)) {
      GRect digit_start = GRect (11, 20, 34, 140);
      GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
      animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 400, 1);
    }
  } else { // and dealing with 21:00
    if ((hours == 21) && (minutes == 0) && (seconds == 0)) {
      GRect digit_start = GRect (11, 20, 34, 140);
      GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
      animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 400, 1);
    }
  }
  
  // and moving the digit back when the one disappears from the hours ones place
  if (((hours == 2) || (hours == 12)) && (minutes == 0) && (seconds == 0)) {
    GRect digit_start = GRect (22, 20, 34, 140);
    GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
    animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 400, 1);
  }
  
  if (clock_is_24h_style() == false) { // dealiing with 11 PM switching to midnight and 1 PM switching to 2 PM
    if (((hours == 0) || (hours == 14)) && (minutes == 0) && (seconds == 0)) {
    GRect digit_start = GRect (22, 20, 34, 140);
    GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
    animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 400, 1);
    }
  } else { // and dealing with 21:59 switching to 22:00
    if ((hours == 22) && (minutes == 0) && (seconds == 0)) {
    GRect digit_start = GRect (22, 20, 34, 140);
    GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
    animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 400, 1);
    }
  }
  
  // Add one time animations to fix initial positioning when watchface starts
  if (format_fix == 1) {
    if (long_hours_offset == 22) { // for if the one's place of the hour is a one when the face launches
      GRect digit_start = GRect (11, 20, 34, 140);
      GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
      animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 100, 1);
    }
    if (long_hours_offset == 21) {
      GRect digit_start = GRect (11, 20, 34, 140);
      GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
      animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 100, 1);
    }
    if (long_hours_offset == 10) {
      GRect digit_start = GRect (11, 20, 34, 140);
      GRect digit_finish = GRect (long_hours_offset, 20, 34, 140);
      animate_digit_layer(text_layer_get_layer(text_long_hours_layer), &digit_start, &digit_finish, 1, 1);
    }
    if (short_hours_offset == 9) {
      GRect digit_start = GRect(10, 20, 57, 140);
      GRect digit_finish = GRect(short_hours_offset, 20, 57, 140);
      animate_digit_layer(text_layer_get_layer(text_hours_layer), &digit_start, &digit_finish, 1, 1);
    }
    if (short_minutes_offset == 88) {
      GRect digit_start = GRect (99, 20, 33, 140);
      GRect digit_finish = GRect (short_minutes_offset, 20, 33, 140);
      animate_digit_layer(text_layer_get_layer(text_short_minutes_layer), &digit_start, &digit_finish, 100, 1);
    }
    format_fix--; 
  }
  
  // DONE with animations
  
  *lnghr = time_buffer[0];
  *srthr = time_buffer[1];
  *lngmn = time_buffer[3];
  *srtmn = time_buffer[4];
  
  text_layer_set_text(text_hours_layer, srthr);
  text_layer_set_text(text_minutes_layer, lngmn);
  text_layer_set_text(text_long_hours_layer, lnghr);
  text_layer_set_text(text_short_minutes_layer, srtmn);
  
}

// WINDOW LIFE
void window_load (Window *my_window) {
  // load background
  background_layer = text_layer_create(GRect(0, 0, 144, 168));
  text_layer_set_background_color(background_layer, GColorBlack);
  
  // loading the time
  HBH_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_HBH_120));
  
  text_hours_layer = text_layer_create(GRect(short_hours_offset, 20, 57, 140));
  text_layer_set_background_color(text_hours_layer, GColorClear);
  text_layer_set_text_color(text_hours_layer, GColorWhite);
  text_layer_set_text_alignment(text_hours_layer, GTextAlignmentRight);
  text_layer_set_font(text_hours_layer, HBH_font);
  
  text_minutes_layer = text_layer_create(GRect(77, 20, 66, 140));
  text_layer_set_background_color(text_minutes_layer, GColorClear);
  text_layer_set_text_color(text_minutes_layer, GColorWhite);
  text_layer_set_text_alignment(text_minutes_layer, GTextAlignmentLeft);
  text_layer_set_font(text_minutes_layer, HBH_font);
  
  text_colon_layer = text_layer_create(GRect(0, 10, 144, 120));
  text_layer_set_background_color(text_colon_layer, GColorClear);
  text_layer_set_text_color(text_colon_layer, GColorWhite);
  text_layer_set_text_alignment(text_colon_layer, GTextAlignmentCenter);
  text_layer_set_font(text_colon_layer, HBH_font);
  text_layer_set_text(text_colon_layer, ":");
  
  text_long_hours_layer = text_layer_create(GRect(long_hours_offset, 20, 34, 140));
  text_layer_set_background_color(text_long_hours_layer, GColorClear);
  text_layer_set_text_color(text_long_hours_layer, GColorWhite);
  text_layer_set_text_alignment(text_long_hours_layer, GTextAlignmentRight);
  text_layer_set_font(text_long_hours_layer, HBH_font);
  
  text_short_minutes_layer = text_layer_create(GRect(short_minutes_offset, 20, 33, 140));
  text_layer_set_background_color(text_short_minutes_layer, GColorClear);
  text_layer_set_text_color(text_short_minutes_layer, GColorWhite);
  text_layer_set_text_alignment(text_short_minutes_layer, GTextAlignmentLeft);
  text_layer_set_font(text_short_minutes_layer, HBH_font);
  
  
  //loading the layers
  layer_add_child(window_get_root_layer(my_window), text_layer_get_layer(background_layer));
  layer_add_child(window_get_root_layer(my_window), text_layer_get_layer(text_hours_layer));
  layer_add_child(window_get_root_layer(my_window), text_layer_get_layer(text_minutes_layer));
  layer_add_child(window_get_root_layer(my_window), text_layer_get_layer(text_long_hours_layer));
  layer_add_child(window_get_root_layer(my_window), text_layer_get_layer(text_short_minutes_layer));
  layer_add_child(window_get_root_layer(my_window), text_layer_get_layer(text_colon_layer));
  
  
  // preventing face from starting blank
  struct tm *t;
  time_t temp;
  temp = time(NULL);
  t = localtime(&temp);
  tick_handler(t, MINUTE_UNIT);

}

void window_unload (Window *my_window) {
  text_layer_destroy(text_hours_layer);
  text_layer_destroy(text_minutes_layer);
  text_layer_destroy(background_layer);
  text_layer_destroy(text_colon_layer);
  text_layer_destroy(text_long_hours_layer);
  text_layer_destroy(text_short_minutes_layer);
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
  handle_init();
  app_event_loop();
  handle_deinit();
}