#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#include "weaver.h"
#include "../mdb/util.h"
#include "hash.h"

typedef struct {
  int group_id;
  char *message_id;
  char *parent_message_id;
  char *subject;
  char *author;
  time_t date;
} parsed_article;

int thread_file(const char *file_name) {
  parsed_article *pa;
  word_count *words;
  node *tnode;
  char group[MAX_FILE_NAME];
  int id, group_id;

  printf("%s\n", file_name);  
  if (path_to_article_spec(file_name, group, &article)) {
    pa = parse_file(file_name);

    /* Check that group/article hasn't been threaded already.  */
    
    /* Look up stuff and create the node. */

    group_id = get_group_id(group);

    if (! (tnode = get_node(pa->message_id, group_id)))
      tnode = (node*) cmalloc(sizeof(node));
    
    tnode->group_id = group_id;
    id = next_id();
    tnode->id = next_id();
    
    tnode->parent = get_parent(pa->parent_message_id);
    
    /* Set next_instance in previous instances to point to us. */
    
    tnode->subject = enter_string_storage(pa->subject);
    tnode->author = enter_string_storage(pa->author);
    tnode->date = pa->date;

    thread(tnode);
    
    write_node(tnode);
  } else {
    printf("Can't find an article spec for %s\n", file_name);
  }
  return 0;
}
