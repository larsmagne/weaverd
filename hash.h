#ifndef HASH_H
#define HASH_H

node *get_node(const char *message_id, unsigned int group_id);
void init_hash (void);
unsigned int enter_string_storage(const char *string);
void flush_hash(void);
group *get_group(const char *group_name);

#endif
