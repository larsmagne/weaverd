#ifndef WEAVER_H
#define WEAVER_H

#define MAX_SEARCH_ITEMS 1024

typedef unsigned int node_id;

typedef struct {
  unsigned short group_id;
  unsigned int number;         /* Article number in group, max 3*/
  node_id id;                  /* Unique article id */

  node_id parent;
  node_id next_instance;       /* Used for cross-posting. */

  /* These three are offsets into the string storage. */
  unsigned int subject;
  unsigned int author;
  unsigned int message_id;
  time_t date;

  node_id first_child;
  node_id next_sibling;
} node;

typedef struct {
  unsigned int id;
  unsigned char depth;
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
  unsigned int nodes_length;
  thread_node *thread_nodes;
  unsigned int threads_length;

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
void output_group_threads(FILE *client, const char *group_name,
			  int page, int page_size,
			  int last);
void output_groups(FILE *client, const char *match);
void init(void);
void flatten_groups(void);
int meg(int size);
void usage(void);
void flush(void);
void output_one_thread(FILE *client, const char *group_name, int article);
void alphabetize_groups(void);
void output_hierarchy(FILE *client, const char *prefix);

extern char *index_dir;
extern group groups[];
extern node *nodes;
extern int inhibit_thread_flattening;
extern int inhibit_file_writes;
extern unsigned int mem;
extern unsigned int current_node;

#endif

