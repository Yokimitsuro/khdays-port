#ifndef KHDAYS_WALK_H
#define KHDAYS_WALK_H
void khdays_walk_movement(int actor, int speed, int full_speed);
void khdays_walk_draw_begin(int node);
void khdays_walk_draw_end(void);
void khdays_walk_joint(unsigned int joint, void *result);
void khdays_walk_mapped_joint(unsigned int data_index, const void *animation, void *result);
void khdays_walk_send_begin(unsigned int joint, void *result);
void khdays_walk_send_end(void *result);
/* runtime/mods/models.cpp, as a model's draw begins: the local player's weapon drawn as an object of its own
 * is moved to the walk's hand (`model_resource` the DS address of the model drawn). */
void khdays_walk_model_draw(unsigned model_resource);
void khdays_walk_model_draw_end(void);
#endif
