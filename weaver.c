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
#include <sys/resource.h>

#include "weaver.h"
#include "config.h"
#include "hash.h"
#include "input.h"
#include "../mdb/util.h"

group groups[MAX_GROUPS];
int alphabetic_groups[MAX_GROUPS];
int inhibit_thread_flattening = 0;
int inhibit_file_writes = 0;
int num_groups = 0;

node *nodes;
unsigned int nodes_length = 0;
char *index_dir = NULL;

unsigned int current_node = 0;
static int node_file = 0;

#define MAX_ARTICLES 1000000

static int flattened[MAX_ARTICLES];

void extend_node_storage(void) {
  int new_length = nodes_length * 2;
  node *new_nodes = (node*) cmalloc(new_length * sizeof(node));
#ifdef USAGE
  printf("Extending node storage from %dM to %dM\n",
	 meg(nodes_length * sizeof(node)), meg(new_length * sizeof(node)));
#endif
  memcpy(new_nodes, nodes, nodes_length * sizeof(node));
  crfree(nodes, nodes_length * sizeof(node));
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
  if (! inhibit_file_writes) {
    if (lseek64(node_file, (loff_t)(sizeof(node) * nnode->id), SEEK_SET) < 0) {
      printf("Trying to see to %Ld\n", 
	     (loff_t)(sizeof(node) * nnode->id));
      merror("Seeking the node file");
    }
    write_from(node_file, (char*)nnode, sizeof(node));
  }
}

void flush_nodes(void) {
  if (lseek64(node_file, (loff_t)0, SEEK_SET) < 0) {
    printf("Trying to see to %d\n", 0);
    merror("Seeking the node file");
  }
  write_from(node_file, (char*)nodes, nodes_length * sizeof(node));
}

void flatten_groups(void) {
  int i;
  group *g;

  for (i = 1; i<MAX_GROUPS; i++) {
    g = &groups[i];
    if (g->group_id == 0) 
      break;
#if 0
    printf("Flattening %s\n", get_string(g->group_name));
#endif
    flatten_threads(g);
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
    nodes_length = 12000000;
  else
    nodes_length = fsize * 1.2;

  nodes = (node*) cmalloc(nodes_length * sizeof(node));
#ifdef USAGE
  printf("Allocating %dM for nodes\n",
	 meg(nodes_length * sizeof(node)));
#endif

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

int min (int val1, int val2) {
  if (val1 < val2)
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
  printf("Flattening %d, %s %s, child %d\n",
	 nnode->id,
	 get_string(nnode->subject),
	 get_string(nnode->message_id),
	 nnode->first_child);
#endif

  while (nnode->group_id != group_id &&
	 nnode->next_instance != 0)
    nnode = &nodes[nnode->next_instance];

  if (flattened[nnode->number]) 
    return;

  flattened[nnode->number] = 1;

  if (nnode->group_id == group_id) {
    tnode = &(tnodes[thread_index++]);
    tnode->id = nnode->id;
    tnode->depth = depth;
    //printf("%d %d\n", thread_index, nnode->number);
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
  unsigned int max = tgroup->max_article, id, 
    group_id = tgroup->group_id;
  int i;

  thread_index = 0;
  bzero(flattened, max * sizeof(int));

  for (i = max - 1; i>=0; i--) {
    //printf("numeric nodes %d: %d\n", i, tgroup->numeric_nodes[i]);
    if ((id = tgroup->numeric_nodes[i]) != 0) {
      nnode = &nodes[id];
      if (nnode->parent == 0 || nodes[nnode->parent].number == 0) 
	flatten_thread(nnode, tnodes, group_id, 0);
    }
  }

  tgroup->threads_length = thread_index - 1;

  if (!strcmp("gmane.discuss", get_string(tgroup->group_name)))
    printf("Total articles %d, thread length %d\n",
	   tgroup->total_articles, tgroup->threads_length);

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

#ifdef USAGE
  printf("Extending group node tables from %dM to %dM (times two)\n",
	 meg(area), meg(new_area));
#endif

  tgroup->numeric_nodes = (int*)crealloc(tgroup->numeric_nodes, new_area,
					 area);
  bzero((char*)tgroup->numeric_nodes + area, new_area - area);

  area = length * sizeof(thread_node);
  new_area = new_length * sizeof(thread_node);

  tgroup->thread_nodes = (thread_node*)crealloc(tgroup->thread_nodes, new_area,
						area);
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

static char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static char format_time_buffer[256];
char *format_time(time_t date) {
  struct tm *time;
  time = localtime(&date);
  snprintf(format_time_buffer, 256, 
	   "%d %s %02d:%02d", time->tm_mday, months[time->tm_mon],
	   time->tm_hour, time->tm_min);
  return format_time_buffer;
}

int num_children(node *nnode) {
  int children = 0;

  if (nnode->first_child == 0)
    return 0;

  nnode = &nodes[nnode->first_child];

  while (nnode != NULL) {
    children++;
    if (nnode->next_sibling != 0)
      nnode = &nodes[nnode->next_sibling];
    else
      nnode = NULL;
  }

  return children;    
}

void output_one_thread(FILE *client, const char *group_name, int article) {
  group *g = get_group(group_name);
  int i = g->numeric_nodes[article];
  node *nnode;

  if (i) {
    nnode = &nodes[i];
    while (nnode->parent) {
      nnode = &nodes[nnode->parent];
    }
  }
  
}

void output_group_threads(FILE *client, const char *group_name, 
			  int page, int page_size, 
			  int last) {
  group *g = get_group(group_name);
  int total = g->threads_length, i, j;
  node *nnode;
  thread_node *tnode;
  int start, stop, articles = 0, tstart;
  int children[256];
  int skip_past = page_size*page;

  if (last == 0)
    last = g->max_article;

  /* Find the first article in this page. */
  for (start = 0; start < total; start++) {
    tnode = &(g->thread_nodes[start]);
    nnode = &nodes[tnode->id];
    if (nnode->number <= last)
      articles++;
    if (articles >= (page_size*page))
      break;
  }

  /* Find the last article on this page. */
  articles = 0;
  for (stop = start; stop < total; stop++) {
    tnode = &(g->thread_nodes[stop]);
    nnode = &nodes[tnode->id];
    if (nnode->number <= last)
      articles++;
    if (articles >= page_size)
      break;
  }

  /* Find the start of the thread we might be in the middle of. */
  for (tstart = start; tstart > 0; tstart--) {
    tnode = &(g->thread_nodes[tstart]);
    if (tnode->depth == 0)
      break;
  }

  printf("Outputting group thread from %d\n", start);

  /* We have now found the start of the first thread to
     be output to the client. */
  if (start != stop) {
    for (i = tstart; i <= stop; i++) {
      tnode = &(g->thread_nodes[i]);
      nnode = &nodes[tnode->id];
      children[tnode->depth] = num_children(nnode);
      if (i >= start) {
	fprintf(client, "%d\t%d\t%s\t%s\t%s\t", 
		tnode->depth,
		nnode->number, 
		get_string(nnode->subject), 
		get_string(nnode->author), format_time(nnode->date));
	for (j = 0; j<tnode->depth; j++) 
	  fprintf(client, "%d\t", children[j]);
	fprintf(client, "\n");
      }
      if (tnode->depth > 0)
	children[(tnode->depth) - 1]--;
    }
  }
  fprintf(client, ".\n");
}

void output_group_line(FILE *client, group *g) {
  fprintf(client, "%d\t%d\t%s\t%s\n",
	  g->max_article, g->total_articles, 
	  get_string(g->group_name), get_string(g->group_description));
}

void output_groups(FILE *client, const char *match) {
  group *g;
  int i;
  char *group_name;

  for (i = 1; i<num_groups; i++) {
    g = &groups[alphabetic_groups[i]];
    if (g->group_name == 0) 
      break;
    if (g->group_description != 0) {
      group_name = get_string(g->group_name);
      if (strstr(group_name, match)) 
	output_group_line(client, g);
    }
  }
  fprintf(client, ".\n");
}

int find_levels (const char *string) {
  int levels = 0;
  while (*string) {
    if (*string++ == '.')
      levels++;
  }
  return levels + 1;
}

int levels_equal (const char *name1, const char *name2, int levels) {
  int n = 0;

  if (name1 == NULL || name2 == NULL)
    return 0;

  while (*name1 && *name2 && *name1 == *name2) {
    if (*name1 == '.')
      n++;
    name1++;
    name2++;
  }

  /* We count a group name as a level.  (Implicit trailing dot.) */
  if (*name1 == 0 || *name2 == 0)
    n++;

  if (n >= levels)
    return 1;
  else
    return 0;
}

static char group_buffer[1024];
char *prefix_group(char *group_name, int levels) {
  char *p = group_buffer;
  while ((*p++ = *group_name++) != 0) {
    if (*(p-1) == '.' && levels-- == 0) {
      p--;
      break;
    }
  }
  *p = 0;
  return group_buffer;
}

void output_prev(FILE *client, char *prev, int levels,
		       int ngroups, group *g) {
  if (ngroups == 1)
    output_group_line(client, g);
  else if (find_levels(prev) != levels + 1)
    fprintf(client, "%d\t%d\t%s\t%s\n",
	    -1, ngroups, prefix_group(prev, levels), "");
}

void output_hierarchy(FILE *client, const char *prefix) {
  group *g, *prev_group;
  int i;
  char *group_name;
  int levels = find_levels(prefix);
  char *prev = NULL;
  int ngroups = 0;
  char *prev_output = NULL;

  for (i = 1; i<num_groups; i++) {
    g = &groups[alphabetic_groups[i]];
    group_name = get_string(g->group_name);
    if (strstr(group_name, prefix) == group_name &&
	g->group_description != 0) {
      if (! levels_equal(prev, group_name, levels + 1)) {
	if (prev && prev_output != prev) {
	  prev_output = prev;
	  output_prev(client, prev, levels, ngroups, prev_group);
	  ngroups = 0;
	} 
      } 
      
      prev = group_name;
      prev_group = g;

      if (find_levels(group_name) == levels + 1) {
	prev_output = group_name;
	output_group_line(client, g);
      } else {
	ngroups++;
      }
    }
  }
  if (prev && prev_output != prev) 
    output_prev(client, prev, levels, ngroups, prev_group);
  fprintf(client, ".\n");
}

void init(void) {
  //g_mime_init(GMIME_INIT_FLAG_UTF8);
  g_mime_init(0);
  index_dir = "/index/weave";
  init_hash();
  init_nodes();
  read_conf_file();
}

int meg(int size) {
  return size/(1024*1024);
}

void usage(void) {
  struct rusage usage;

  if (getrusage(RUSAGE_SELF, &usage) < 0) {
    merror("getrusage");
  }

  printf("Usage: %ld\n", usage.ru_idrss + usage.ru_ixrss + usage.ru_isrss);
}

void flush(void) {
  flush_hash();
  if (inhibit_file_writes) {
    flush_nodes();
    flush_strings();
  }
}

int group_name_cmp(const void *sr1, const void *sr2) {
  return strcmp(get_string(groups[*(int*)sr1].group_name),
		get_string(groups[*(int*)sr2].group_name));
}

void alphabetize_groups (void) {
  group *g;

  for (num_groups = 1; num_groups<MAX_GROUPS; num_groups++) {
     g = &groups[num_groups];
     if (g->group_id == 0)
       break;
     alphabetic_groups[num_groups] = g->group_id;
  }

  qsort(alphabetic_groups, num_groups, sizeof(int), group_name_cmp);
}
