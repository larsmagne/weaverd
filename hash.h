#ifndef HASH_H
#define HASH_H

node *get_node(const char *message_id, unsigned int group_id);
void init_hash (void);
unsigned int enter_string_storage(const char *string);
void flush_hash(void);
group *get_group(const char *group_name);
node *get_node(const char *message_id, unsigned int group_id);
char *get_string(int offset);
unsigned int previous_instance_node;
void flush_strings(void);
group *find_group(const char *group_name);
void hash_node(const char *message_id, unsigned int node_id);
void newgroup (FILE *output, char *group_name, char **description, int words);
int prohibited_group_p(group *g);
void rmgroup(FILE *output, char *group_name);
node *get_node_any(const char *message_id, unsigned int group_id);

extern int next_string;

#endif
