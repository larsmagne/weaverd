#ifndef WEAVER_H
#define WEAVER_H

typedef struct {
  unsigned int group_id;
  unsigned int id;

  unsigned int parent;
  unsigned int next_instance;  /* Used for cross-posting. */

  /* These two are offsets into the string storage. */
  unsigned int subject;
  unsigned int author;
  time_t date;

  unsigned int first_child;
  unsigned int next_sibling;
} node;

typedef struct {
  char *group_name;
  char *group_description;
  unsigned int group_id;
  
  unsigned int nodes_on_disk;
  unsigned int next_node;

  unsigned int min_article;
  unsigned int max_article;
  unsigned int total_articles;

  unsigned int **numeric_nodes;
  unsigned int **thread_nodes;
} group;

#endif
