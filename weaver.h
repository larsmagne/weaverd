#ifndef WEAVER_H
#define WEAVER_H

#define MAX_SEARCH_ITEMS 1024

typedef struct {
  unsigned int group_id;
  unsigned int number;         /* Article number in group */
  unsigned int id;             /* Unique article id */

  unsigned int parent;
  unsigned int next_instance;  /* Used for cross-posting. */

  /* These three are offsets into the string storage. */
  unsigned int subject;
  unsigned int author;
  unsigned int message_id;
  time_t date;

  unsigned int first_child;
  unsigned int next_sibling;
} node;

typedef struct {
  unsigned int id;
  unsigned int depth;
} thread_node;

typedef struct {
  unsigned int group_name;
  unsigned int group_description;
  unsigned int group_id;
  
  unsigned int nodes_on_disk;
  unsigned int next_node;

  unsigned int min_article;
  unsigned int max_article;
  unsigned int total_articles;

  unsigned int *numeric_nodes;
  thread_node *thread_nodes;
  unsigned int nodes_length;

  int dirtyp;
} group;

unsigned int get_parent(const char *parent_message_id, unsigned int group_id);
void thread(node *node, int do_thread);
char *index_file_name(char *name);
unsigned int next_id(void);
void write_node(node *nnode);
void init_nodes(void);
void flatten_threads(group *tgroup);
void output_threads(char *group_name);
void store_node(node *nnode);
void output_group_threads(const char *group_name, int from, int to);
void output_groups(const char *match);
void init(void);
void flatten_groups(void);

extern char *index_dir;
extern group groups[];
extern node *nodes;
extern int inhibit_thread_flattening;

#endif

