#include "al_game.h"
#include "util/al_helper.h"

/// passed to \c al_reserve_samples
static const int num_simultaneous_audio_samples = 10;
/// random variance to volume each time \ref al_game_play_sound is called
static const double sound_volume_variance = 0.3;
/// random variance to speed each time \ref al_game_play_sound is called
static const double sound_speed_variance = 0.1;

ALLEGRO_DISPLAY *display;
ALLEGRO_EVENT_QUEUE *event_queue;
ALLEGRO_TIMER *frame_timer;
ALLEGRO_FONT *main_font;

// store preloaded resources accessible by name
static stringmap *bitmap_resources, *font_resources, *sound_resources;

// function to load a resource from a file
typedef void *(*resource_load_fn)(const char *filename);
static stringmap* load_resource_dir(const char *path, resource_load_fn loader, 
    list_lambda resource_free_fn);
static void* bitmap_from_file(const char *filename);
static void* font_from_file(const char *filename);
static void* sound_from_file(const char *filename);

int al_game_init() {
  srand((unsigned)time(NULL));

  // initialize allegro and subsystems
  if (!al_init()) {
    fprintf(stderr, "failed to init allegro\n");
    return -1;
  }
  if (!al_init_primitives_addon()) {
    fprintf(stderr, "failed to init primitives\n");
    return -1;
  }
  al_init_font_addon();
  al_init_ttf_addon();
  if (!al_install_keyboard()) {
    fprintf(stderr, "failed to init keyboard\n");
    return -1;
  }
  if (!al_install_mouse()) {
    fprintf(stderr, "failed to init mouse\n");
    return -1;
  }
  if (!al_install_audio()) {
    fprintf(stderr, "failed to init audio\n");
    return -1;
  }
  if (!al_init_acodec_addon()) {
    fprintf(stderr, "failed to init acodec addon\n");
    return -1;
  }
  if (!al_reserve_samples(num_simultaneous_audio_samples)) { 
    fprintf(stderr, "failed to reserve audio samples\n");
    return -1;
  }
  if (!al_init_image_addon()) {
    fprintf(stderr, "failed to init image addon\n");
    return -1;
  }
  if (!(frame_timer = al_create_timer(1.0 / FPS))) {
    fprintf(stderr, "failed to create timer\n");
  }
  if (!(display = al_create_display(SCREEN_W,SCREEN_H))) {
    fprintf(stderr, "failed to create display\n");
    return -1;
  }
  if (!(event_queue = al_create_event_queue())) {
    fprintf(stderr, "failed to create event_queue\n");
    al_destroy_display(display);
    return -1;
  }

  // load resources
  font_resources = load_resource_dir(FONT_DIR, font_from_file,
      (list_lambda)al_destroy_font);
  bitmap_resources = load_resource_dir(BITMAP_DIR, bitmap_from_file,
      (list_lambda)al_destroy_bitmap);
  sound_resources = load_resource_dir(SOUND_DIR, sound_from_file,
      (list_lambda)al_destroy_sample);
  main_font = al_game_get_font(MAIN_FONT_NAME);

  // event source setup
  al_register_event_source(event_queue, al_get_display_event_source(display));
  al_register_event_source(event_queue, al_get_timer_event_source(frame_timer));
  al_register_event_source(event_queue, al_get_mouse_event_source());
  al_register_event_source(event_queue, al_get_keyboard_event_source());

  // display setup
  al_set_target_bitmap(al_get_backbuffer(display));
  al_clear_to_color(al_map_rgb(0,0,0));
  al_flip_display();

  // start frame timer to begin game loop
  al_start_timer(frame_timer);

  return 0; // initialization succeeded
}

void al_game_shutdown() {
  if (frame_timer != NULL) {
    al_destroy_timer(frame_timer);
  }
  if (event_queue != NULL) {
    al_destroy_event_queue(event_queue);
  }
  if (display != NULL) {
    al_destroy_display(display);
  }
  if (font_resources != NULL) { // destroy all preloaded font resources
    stringmap_free(font_resources);
  }
  if (bitmap_resources != NULL) { // destroy all preloaded bitmap resources
    stringmap_free(bitmap_resources);
  }
}

ALLEGRO_BITMAP* al_game_get_bitmap(const char *name) {
  ALLEGRO_BITMAP *bmp = stringmap_find(bitmap_resources, name);
  if (bmp != NULL) {
    return bmp;
  }
  fprintf(stderr, "could not find bitmap resource named '%s'\n", name);
  abort();
}

ALLEGRO_FONT* al_game_get_font(const char *name) {
  ALLEGRO_FONT *font = stringmap_find(font_resources, name);
  if (font != NULL) {
    return font;
  }
  fprintf(stderr, "could not find bitmap resource named '%s'\n", name);
  abort();
}

ALLEGRO_SAMPLE* al_game_get_sound(const char *name) {
  ALLEGRO_SAMPLE *sound = stringmap_find(sound_resources, name);
  if (sound) { return sound; }
  fprintf(stderr, "could not find sound resource named '%s'\n", name);
  abort();
}

static stringmap* load_resource_dir(const char *path, resource_load_fn loader,
    list_lambda resource_free_fn) 
{
  ALLEGRO_FS_ENTRY *resource_dir = al_create_fs_entry(path);
  // open the directory to read all entries
  if (!al_open_directory(resource_dir)) {
    fprintf(stderr, "failed to open resource directory %s\n", path);
    abort();
  }
  // create a list to store loaded resources
  stringmap *res_map = stringmap_new(resource_free_fn);
  ALLEGRO_FS_ENTRY *res_file_entry;
  while ((res_file_entry = al_read_directory(resource_dir))) {
    // get full path to resource
    const char *full_path = al_get_fs_entry_name(res_file_entry);
    // get filename without extension
    ALLEGRO_PATH *path = al_create_path(full_path);
    const char *name = al_get_path_basename(path);
    // add resource to list, identified by the basename of its file
    stringmap_add(res_map, name, loader(full_path));
    // clean up
    al_destroy_path(path);
    al_destroy_fs_entry(res_file_entry);
  }
  return res_map;
}

static void* bitmap_from_file(const char *filename) {
  return al_load_bitmap(filename);
}

static void* font_from_file(const char *filename) {
  return al_load_ttf_font(filename, 12, 0);
}

static void* sound_from_file(const char *filename) {
  return al_load_sample(filename);
}

/// play the sound identified by \c name
ALLEGRO_SAMPLE_ID al_game_play_sound(const char *name, bool loop) {
  ALLEGRO_SAMPLE *sound = al_game_get_sound(name);
  ALLEGRO_SAMPLE_ID id;
  double vol = randd(1 - sound_volume_variance, 1 + sound_volume_variance);
  double speed = randd(1 - sound_speed_variance, 1 + sound_speed_variance);
  al_play_sample(sound, vol, 0, speed, 
      loop ? ALLEGRO_PLAYMODE_LOOP : ALLEGRO_PLAYMODE_ONCE, &id); 
  return id;
}
