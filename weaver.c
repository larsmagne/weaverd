#define _LARGEFILE64_SOURCE

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <gmime/gmime.h>

#include "weaver.h"
#include "config.h"
#include "hash.h"
#include "../mdb/util.h"

group groups[MAX_GROUPS];
int inhibit_thread_flattening = 0;

node *nodes;
unsigned int nodes_length = 0;
char *index_dir = NULL;

static unsigned int current_node = 0;
static int node_file = 0;

#define MAX_ARTICLES 1000000

static int flattened[MAX_ARTICLES];

void extend_node_storage(void) {
  int new_length = nodes_length * 2;
  node *new_nodes = (node*) cmalloc(new_length * sizeof(node));
  memcpy(new_nodes, nodes, nodes_length * sizeof(node));
  free(nodes);
  nodes = new_nodes;
  nodes_length = new_length;
}

unsigned int next_id(void) {
  current_node++;

  if (current_node >= nodes_length) 
    extend_node_storage();

  return current_node;
}

char *index_file_name(char *name) {
  static char file_name[1024];
  strcpy(file_name, index_dir);
  strcat(file_name, "/");
  strcat(file_name, name);
  return file_name;
}

void store_node(node *nnode) {
  memcpy(&nodes[nnode->id], nnode, sizeof(node));
}

void write_node(node *nnode) {
  if (lseek64(node_file, (loff_t)(sizeof(node) * nnode->id), SEEK_SET) < 0) {
    printf("Trying to see to %Ld\n", 
	   (loff_t)(sizeof(node) * nnode->id));
    perror("Seeking the node file");
    nnode->id = 3 / 0;
  }
  write_from(node_file, (char*)nnode, sizeof(node));
}

void flatten_groups(void) {
  int i;
  group *g;

  for (i = 0; i<MAX_GROUPS; i++) {
    g = &groups[i];
    if (g->group_id != 0) {
#ifdef DEBUG
      printf("Flattening %s\n", get_string(g->group_name));
#endif
      flatten_threads(g);
    }
  }
}

void init_nodes(void) {
  loff_t fsize;
  int i;
  node *tnode;

  if ((node_file = open64(index_file_name(NODE_FILE),
			  O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the node file.");
 
  fsize = file_size(node_file);
  if (fsize == 0) 
    nodes_length = 1024 * 16;
  else
    nodes_length = fsize * 2;

  nodes = (node*) cmalloc(nodes_length * sizeof(node));

  if (fsize == 0) {
    write_from(node_file, (char*)&nodes[0], sizeof(node));
  }

  current_node = 1;

  read_block(node_file, (char*) nodes, fsize);
  for (i = 1; i<fsize / sizeof(node); i++) {
    tnode = &nodes[i];
#if 0
    printf("Reading node %d %s %s\n    %s,\n   child %d, parent %d, group %d\n",
	   i, get_string(tnode->author), 
	   get_string(tnode->subject), 
	   get_string(tnode->message_id), 
	   tnode->first_child,
	   tnode->parent,
	   tnode->group_id);
#endif
    get_node(get_string(tnode->message_id), tnode->group_id);
    thread(tnode, 0);
  }

  flatten_groups();
}

unsigned int get_parent(const char *parent_message_id, unsigned int group_id) {
  node *pnode = get_node(parent_message_id, group_id);
  return pnode->id;
}

int max (int val1, int val2) {
  if (val1 > val2)
    return val1;
  else
    return val2;
}

void enter_node_numerically(group *tgroup, node *tnode) {
  unsigned int *nnodes = tgroup->numeric_nodes;
  nnodes[tnode->number] = tnode->id;
}

static int thread_index = 0;

void flatten_thread(node *nnode, thread_node *tnodes, unsigned int group_id,
		    unsigned int depth) {
  int sibling;
  thread_node *tnode;

#if 0
  printf("Flattening %s %s, child %d\n",
	 get_string(nnode->subject),
	 get_string(nnode->message_id),
	 nnode->first_child);
#endif

  while (nnode->group_id != group_id &&
	 nnode->next_instance != 0)
    nnode = &nodes[nnode->next_instance];

  if (flattened[nnode->id]) 
    return;

  flattened[nnode->id] = 1;

  if (nnode->group_id == group_id) {
    tnode = &(tnodes[thread_index++]);
    tnode->id = nnode->id;
    tnode->depth = depth;
    if (nnode->first_child) {
#if 0
      printf("Descending into child\n");
#endif
      flatten_thread(&nodes[nnode->first_child], tnodes, group_id, depth+1);
    }

    while ((sibling = nnode->next_sibling) != 0) {
      nnode = &nodes[nnode->next_sibling];
      flatten_thread(nnode, tnodes, group_id, depth);
    }
  }
}

void flatten_threads(group *tgroup) {
  thread_node *tnodes = tgroup->thread_nodes;
  node *nnode;
  unsigned int max = tgroup->max_article, i, id, 
    group_id = tgroup->group_id;

  thread_index = 0;
  bzero(flattened, MAX_ARTICLES * sizeof(int));

  for (i = 0; i<max; i++) {
    if ((id = tgroup->numeric_nodes[i]) != 0) {
      nnode = &nodes[id];
      if (nnode->parent == 0) 
	flatten_thread(nnode, tnodes, group_id, 0);
    }
  }
}

void enter_node_threadly(group *tgroup, node *tnode) {
  unsigned int id = tnode->id;
  node *pnode, *snode;

  /* Hook this node onto the chain of children. */
  if (tnode->parent) {
    pnode = &nodes[tnode->parent];
    if (! pnode->first_child) {
      pnode->first_child = id;
      write_node(pnode);
    } else {
      snode = &nodes[pnode->first_child];
      while (snode->next_sibling != 0)
	snode = &nodes[snode->next_sibling];
      snode->next_sibling = id;
      write_node(snode);
    }
  }

  if (! inhibit_thread_flattening)
    flatten_threads(tgroup);
}

void extend_group_node_tables(group *tgroup, unsigned int min) {
  unsigned int length = tgroup->nodes_length, new_length = 0;
  unsigned int area, new_area;

  new_length = length;
  while (new_length < min) {
    if (new_length == 0) 
      new_length = 64;
    else
      new_length = new_length * 2;
  }

  area = length * sizeof(int);
  new_area = new_length * sizeof(int);

  tgroup->numeric_nodes = (int*)realloc(tgroup->numeric_nodes, new_area);
  bzero((char*)tgroup->numeric_nodes + area, new_area - area);

  area = length * sizeof(thread_node);
  new_area = new_length * sizeof(thread_node);

  tgroup->thread_nodes = (thread_node*)realloc(tgroup->thread_nodes, new_area);
  bzero((char*)tgroup->thread_nodes + area, new_area - area);

  tgroup->nodes_length = new_length;
}

void thread(node *tnode, int do_thread) {
  group *tgroup = &groups[tnode->group_id];
  unsigned int number = tnode->number;

  if (number >= tgroup->nodes_length)
    extend_group_node_tables(tgroup, number);

  enter_node_numerically(tgroup, tnode);
  if (do_thread) {
    tgroup->max_article = max(tgroup->max_article, tnode->number);
    tgroup->total_articles++;
    tgroup->dirtyp = 1;
    enter_node_threadly(tgroup, tnode);
  }
}

void output_threads(char *group_name) {
  group *g = get_group(group_name);
  int total = g->total_articles, i;
  node *nnode;
  thread_node *tnode;
  
  for (i = 1; i<total; i++) {
    tnode = &(g->thread_nodes[i]);
    nnode = &nodes[tnode->id];
    if (! nnode->id)
      break;
    printf("%d %d, %s %s %s\n", i, tnode->depth,
	   get_string(nnode->author), 
	   get_string(nnode->message_id), 
	   get_string(nnode->subject));
  }
}

void output_group_threads(const char *group_name, int from, int to) {
  group *g = get_group(group_name);
  int total = g->total_articles, i;
  node *nnode;
  thread_node *tnode;
  
  for (i = 1; i<total; i++) {
    tnode = &(g->thread_nodes[i]);
    nnode = &nodes[tnode->id];
    if (! nnode->id)
      break;
    printf("%d %d, %s %s %s\n", i, tnode->depth,
	   get_string(nnode->author), 
	   get_string(nnode->message_id), 
	   get_string(nnode->subject));
  }
}

void output_groups(const char *match) {
}

void init(void) {
  //g_mime_init(GMIME_INIT_FLAG_UTF8);
  index_dir = "/index/weave";
  g_mime_init(0);
  init_hash();
  init_nodes();
}
