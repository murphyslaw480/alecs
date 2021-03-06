#include "system/weapon_sys.h"

// lockon constants
static const float indicator_radius = 18;
static const float indicator_thickness = 5;
// grace period after launching during which a projectile cannot hit friendlies
static const double friendly_fire_time = 2;
// flare's effect area
static const double flare_radius = 250;
#define PRIMARY_LOCK_COLOR al_map_rgba(0, 128, 0, 200)
#define SECONDARY_LOCK_COLOR al_map_rgba(0, 0, 128, 128)

// explosion constants
static const double explosion_animate_rate = 50; // frames/sec

static struct ecs_entity *current_target;
static struct ecs_entity *player_entity;
static double current_lockon_time;
static list *lockon_list;
static Weapon *current_weapon, *alternate_weapon;
static WeaponState current_weapon_state = WEAPON_READY;
static double till_next_fire;

static void fire_at_target(struct ecs_entity *fired_by,
    struct ecs_entity *target, double firing_angle);
static void draw_lockon(struct ecs_entity *target, int lockon_count);
// collision handler for projectile
static void hit_target(struct ecs_entity *projectile, struct ecs_entity *target);
// blow up a projectile
static void explode(struct ecs_entity *projectile);
// timer trigger to swich projectile team to neutral
static void friendly_fire_timer_fn(struct ecs_entity *projectile);

void weapon_system_fn(double time) {
  if (current_target) {
    current_lockon_time += time;
    if (current_lockon_time > current_weapon->lockon_time) {
      list_push(lockon_list, current_target);
      weapon_clear_target(current_target);
    }
  }
  
  if (current_weapon_state == WEAPON_FIRING) {
    till_next_fire -= time;
    if (till_next_fire < 0 && lockon_list->length > 0) {
      till_next_fire = current_weapon->fire_delay;
      struct ecs_entity *target = list_popfront(lockon_list);
      fire_at_target(player_entity, target, -PI / 2);
      if (lockon_list->length == 0) { // fired at last lockon
        current_weapon_state = WEAPON_READY;
      }
    }
  }
}

void weapon_system_draw() {
  if (current_target) {
    al_draw_arc(current_target->position.x, current_target->position.y,
        indicator_radius, 0,
        2 * PI * current_lockon_time / current_weapon->lockon_time,
        PRIMARY_LOCK_COLOR, indicator_thickness);
  }
  if (lockon_list) {
    list *already_drawn = list_new();
    for (list_node *node = lockon_list->head; node; node = node->next) {
      ecs_entity *target = node->value;
      if (list_find(already_drawn, target)) { continue; }
      list_push(already_drawn, target);
      draw_lockon(target, list_count(lockon_list, target));
    }
    list_free(already_drawn, NULL);
  }
}

void weapon_set_target(struct ecs_entity *target) {
  if (current_target == NULL) {
    current_lockon_time = 0;  // new target
    current_target = target;
  }
}

void weapon_clear_target(struct ecs_entity *target) {
  if (current_target == target) {
    current_lockon_time = 0;  // new target
    current_target = NULL;
  }
}

void weapon_system_set_weapons(struct ecs_entity *player, Weapon *wep1, Weapon *wep2) {
  if (!lockon_list) { lockon_list = list_new(); }
  player_entity = player;
  current_weapon = wep1;
  alternate_weapon = wep2;
}

void weapon_fire_player() {
  if (current_weapon_state == WEAPON_READY) {
    if (current_weapon->fire_fn) { // special firing function
      current_weapon->fire_fn(player_entity);
    }
    else { // standard firing function
      current_weapon_state = WEAPON_FIRING;
    }
  }
}

static void swarmer_burst_fn(struct ecs_entity *pod) {
  ecs_entity *target;
  while ((target = list_popfront(lockon_list))) {
    fire_at_target(pod, target, randd(0, 2 * PI));
  }
  explode(pod);
}

void fire_swarmer_pod(struct ecs_entity *firing_entity) {
  vector fire_pos = vector_add(firing_entity->position, current_weapon->offset);
  struct ecs_entity *pod = ecs_entity_new(firing_entity->position, ENTITY_MISSILE);
  pod->position = fire_pos;
  ecs_attach_sprite(pod, "swarmer-pod", 0);
  Body *b = &ecs_add_component(pod, ECS_COMPONENT_BODY)->body;
  b->velocity = (vector){-100, 0};
  b->max_linear_velocity = 100;
  b->deceleration_factor = 0.5;
  pod->team = firing_entity->team;
  Timer *timer = &ecs_add_component(pod, ECS_COMPONENT_TIMER)->timer;
  timer->time_left = 1.2;
  timer->timer_action = swarmer_burst_fn;
}

void weapon_fire_enemy(struct ecs_entity *enemy, void *player) {
  fire_at_target(enemy, (struct ecs_entity*)player, -PI / 2);
}

void weapon_swap() {
  if (alternate_weapon) {
    Weapon *temp = current_weapon;
    current_weapon = alternate_weapon;
    alternate_weapon = temp;
    list_clear(lockon_list, NULL);
    weapon_clear_target(current_target);
  }
}

static void fire_at_target(struct ecs_entity *firing_entity,
    struct ecs_entity *target, double firing_angle)
{
  vector fire_pos = vector_add(firing_entity->position, current_weapon->offset);
  struct ecs_entity *projectile = ecs_entity_new(firing_entity->position, ENTITY_MISSILE);
  projectile->position = fire_pos;
  projectile->angle = firing_angle;
  ecs_attach_sprite(projectile, current_weapon->name, 0);
  Body *b = &ecs_add_component(projectile, ECS_COMPONENT_BODY)->body;
  b->velocity = current_weapon->initial_velocity;
  b->max_linear_velocity = current_weapon->max_speed;
  b->deceleration_factor = current_weapon->deceleration_factor;
  Propulsion *p =
    &ecs_add_component(projectile, ECS_COMPONENT_PROPULSION)->propulsion;
  p->linear_accel = current_weapon->acceleration;
  p->turn_rate = current_weapon->turn_rate;
  p->particle_effect =
    get_particle_generator((char*)current_weapon->particle_effect);
  p->directed = true;
  Behavior *behavior = &ecs_add_component(projectile,
      ECS_COMPONENT_BEHAVIOR)->behavior;
  behavior->target = target;
  behavior->type = BEHAVIOR_FOLLOW;
  Collider *collider = &ecs_add_component(projectile,
      ECS_COMPONENT_COLLIDER)->collider;
  collider->rect = hitrect_from_sprite(projectile->sprite);
  collider->on_collision = hit_target;
  projectile->team = firing_entity->team;
  Timer *timer = &ecs_add_component(projectile, ECS_COMPONENT_TIMER)->timer;
  timer->time_left = friendly_fire_time;
  timer->timer_action = friendly_fire_timer_fn;
  // mouse listener (for weapon lockon)
  MouseListener *listener =
    &ecs_add_component(projectile, ECS_COMPONENT_MOUSE_LISTENER)->mouse_listener;
  listener->click_rect = collider->rect;
  listener->on_enter = weapon_set_target;
  listener->on_leave = weapon_clear_target; // make small explosion for launch
  scenery_make_explosion(fire_pos, (vector){1,2}, 50, al_map_rgb_f(1,1,1), "launch");
}

static void draw_lockon(struct ecs_entity *target, int lockon_count) {
  Collider *collider = &target->components[ECS_COMPONENT_COLLIDER]->collider;
  // draw lockon rect
  if (collider) {
    rectangle r = collider->rect;
    al_draw_rounded_rectangle(r.x, r.y, r.x + r.w, r.y + r.h, 1, 1, PRIMARY_LOCK_COLOR, 3);
    // draw lock count
    al_draw_textf(main_font, PRIMARY_LOCK_COLOR, r.x + r.w, r.y, 0, "%d", lockon_count);
  }
}

static void hit_target(struct ecs_entity *projectile, struct ecs_entity *target)
{
  if (target->tag == ENTITY_FLARE) {
    // get distracted by flare
    projectile->components[ECS_COMPONENT_BEHAVIOR]->behavior.target = target;
  }
  else {
    deal_damage(target, 10);
    explode(projectile);
  }
}

static void explode(struct ecs_entity *projectile) {
  scenery_make_explosion(projectile->position, (vector){3,3},
      explosion_animate_rate, al_map_rgb(255,255,255), "explosion1");
  ecs_entity_free(projectile);
}

static void friendly_fire_timer_fn(struct ecs_entity *projectile) {
  projectile->team = TEAM_NEUTRAL;
  Timer *t = &projectile->components[ECS_COMPONENT_TIMER]->timer;
  t->time_left = 5.0;  // TODO: use projectile duration time
  t->timer_action = explode;
}

void launch_flare(vector pos) {
  struct ecs_entity *flare = ecs_entity_new(pos, ENTITY_FLARE);
  flare->angle = -PI / 2;
  Body *b = &ecs_add_component(flare, ECS_COMPONENT_BODY)->body;
  b->max_linear_velocity = 600;
  b->velocity = (vector){-50, -600};
  b->destroy_on_exit = NONE;
  Propulsion *p =
    &ecs_add_component(flare, ECS_COMPONENT_PROPULSION)->propulsion;
  p->linear_accel = 400;
  p->linear_throttle = (vector){-1, 0};
  p->turn_rate = 0;
  p->particle_effect =
    get_particle_generator("flare");
  p->directed = true;
  Collider *col = &ecs_add_component(flare, ECS_COMPONENT_COLLIDER)->collider;
  col->rect = (rectangle){.w = flare_radius, .h = flare_radius};
  // timer to destroy flare after 6 seconds
  Timer *t = &ecs_add_component(flare, ECS_COMPONENT_TIMER)->timer;
  t->time_left = 6;
  t->timer_action = ecs_entity_free;
  // make small explosion for launch
  scenery_make_explosion(pos, (vector){1,2}, 50, al_map_rgb_f(1,0,0), "launch");
}
