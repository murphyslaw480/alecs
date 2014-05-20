#include "ecs.h"

// externally declared in ecs.h - will be populated and used here
list *ecs_systems;
list *ecs_entities;
list *ecs_component_store[NUM_COMPONENT_TYPES];

void ecs_init() {
  sprite_init();
  ecs_systems = list_new();
  ecs_entities = list_new();
  for (int i = 0; i < NUM_COMPONENT_TYPES; i++) {
    ecs_component_store[i] = list_new();
  }
  list_push(ecs_systems, scenery_system_fn);
  list_push(ecs_systems, body_system_fn);
  list_push(ecs_systems, propulsion_system_fn);
  list_push(ecs_systems, collision_system_fn);
  list_push(ecs_systems, weapon_system_fn);
  list_push(ecs_systems, behavior_system_fn);
}

ecs_entity* ecs_entity_new(vector position) {
  ecs_entity *entity = calloc(1, sizeof(ecs_entity));
  entity->position = position;
  entity->_node = list_push(ecs_entities, entity); // push onto entity list
  return entity;
}

void ecs_entity_free(ecs_entity *entity) {
  ecs_remove_sprite(entity);
  for (ecs_component_type i = 0; i < NUM_COMPONENT_TYPES; i++) {
    ecs_remove_component(entity, i);
  }
  list_remove(ecs_entities, entity->_node, free);
}

ecs_component* ecs_add_component(ecs_entity *entity, ecs_component_type type) {
  assert(entity != NULL);
  ecs_component *comp = calloc(1, sizeof(ecs_component));
  comp->type = type;           // tag entity type
  comp->owner_entity = entity; // point component back to owner
  assert(entity->components[(int)type] == NULL);
  // place component in entity's component slot for that type
  entity->components[(int)type] = comp;
  // place component in global store, give it a pointer to its node
  comp->node = list_push(ecs_component_store[(int)type], comp);
  return comp;
}

void ecs_remove_component(ecs_entity *entity, ecs_component_type type) {
  assert(entity != NULL);
  // get component of given type from entity
  ecs_component *comp = entity->components[(int)type];
  // if entity did not have a component of this type, do nothing
  if (comp != NULL) {
    // remove component from global component store and free its memory
    list_remove(ecs_component_store[type], comp->node, free);
    // make sure entity no longer references a component for that type
    entity->components[(int)type] = NULL;
  }
}

sprite* ecs_attach_sprite(ecs_entity *entity, const char *name, int depth) {
  assert(entity->sprite == NULL); // shouldn't have sprite already
  sprite* s = sprite_new(name, &(entity->position), &(entity->angle), depth);
  entity->sprite = s;
  return s;
}

void ecs_remove_sprite(ecs_entity *entity) {
  if (entity->sprite != NULL) {
    sprite_free(entity->sprite);
    entity->sprite = NULL;
  }
}

void ecs_update_systems(double time) {
  list_node *sys_node = ecs_systems->head;
  while (sys_node != NULL) {  // iterate through every update function
    ecs_system sys = (ecs_system)sys_node->value;
    sys(time);  // run the system update function, passing the elapsed time
    sys_node = sys_node->next;
  }
}

void ecs_free_all_entities() {
  // free every entity without destroying the list
  list_clear(ecs_entities, (list_lambda)ecs_entity_free);
}

void ecs_shutdown() {
  list_free(ecs_systems, NULL);
  // use list each instead of list_free - ecs_entity_free handles removal of
  // entity from list
  list_each(ecs_entities, (list_lambda)ecs_entity_free);
  free(ecs_entities);
}
