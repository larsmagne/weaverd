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
#include <time.h>

#include "weaver.h"
#include "config.h"
#include "hash.h"
#include "input.h"
#include "util.h"

group groups[MAX_GROUPS];
int alphabetic_groups[MAX_GROUPS];
int inhibit_thread_flattening = 0;
int inhibit_file_writes = 0;
int num_groups = 0;
char *news_spool = NEWS_SPOOL;

node *nodes;
loff_t nodes_length = 0;
char *index_dir = INDEX_DIR;

unsigned int current_node = 0;
static int node_file = 0;

#define MAX_ARTICLES 4000000

static int flattened[MAX_ARTICLES];

void extend_node_storage(void) {
  int new_length = nodes_length * 2;
  node *new_nodes = (node*) cmalloc(new_length * sizeof(node));
  printf("Extending node storage from %dM to %dM\n",
	 meg(nodes_length * sizeof(node)), meg(new_length * sizeof(node)));
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

static time_t month_table[70*12];

void compute_month_table(void) {
  struct tm tmtime;
  int year, month, mnum = 0;

  bzero(&tmtime, sizeof(struct tm));
  tmtime.tm_sec = 1;
  tmtime.tm_mday = 1;

  for (year = 1971 - 1900; year < 2035 - 1900; year++) {
    for (month = 0; month < 12; month++) {
      tmtime.tm_year = year;
      tmtime.tm_mon = month;
      month_table[mnum++] = mktime(&tmtime);
      //printf("%s\n", ctime(&month_table[mnum-1]));
    }
  }
  month_table[0] = 0;
}

static int last_month_index = 0;

int find_month_number(time_t date) {
  if (date < 0)
    date = 0;
  if (date < month_table[last_month_index])
    last_month_index = 0;
  while (date > month_table[last_month_index]) {
    last_month_index++;
    if (last_month_index > 12*65) {
      last_month_index = 0;
      break;
    }
  }
  return last_month_index;
}

char *index_file_name(char *name) {
  static char file_name[1024];
  strcpy(file_name, index_dir);
  strcat(file_name, "/");
  strcat(file_name, name);
  return file_name;
}

time_t first_article_date(group *g) {
  node *nnode;
  int i, n;

  for (i = 1; i < g->nodes_length; i++) {
    n = g->numeric_nodes[i];
    if (n != 0) {
      nnode = &nodes[n];
      return nnode->date;
    }
  }
  return 0;
}


void dump_group(char *group_name) {
  group *g = find_group(group_name);
  unsigned int *nnodes = g->numeric_nodes;
  node *nnode;
  int i;
  
  for (i = 1; i<g->nodes_length; i++) {
    if (nnodes[i] != 0) {
      nnode = &nodes[nnodes[i]];      
      printf("%d %s %d %d %d %x %s\n", 
	     i,
	     get_string(groups[nnode->group_id].group_name),
	     nnode->number,
	     g->nodes_length,
	     nnode->id,
	     nnode->id,
	     get_string(nnode->message_id));
    }
  }
}

void store_node(node *nnode) {
  memcpy(&nodes[nnode->id], nnode, sizeof(node));
}

void write_node(node *nnode) {
  if (! inhibit_file_writes) {
    if (lseek64(node_file, (loff_t)(sizeof(node) * nnode->id), SEEK_SET) < 0) {
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

loff_t allocate_nodes(void) {
  loff_t fsize;

  if ((node_file = open64(index_file_name(NODE_FILE),
			  O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the node file.");
 
  fsize = file_size(node_file);
  if (fsize == 0 || 
      fsize < (INITIAL_NODE_LENGTH  * sizeof(node))) 
    nodes_length = INITIAL_NODE_LENGTH * sizeof(node);
  else 
    nodes_length = fsize * 1.4;

  printf("Allocating %ld bytes for nodes\n", nodes_length);
  nodes = (node*) cmalloc(nodes_length);

  return fsize;
}

void init_nodes(loff_t fsize) {
  int i;
  node *tnode;

  if (fsize == 0) {
    write_from(node_file, (char*)&nodes[0], sizeof(node));
    current_node = 1;
  } else {
    current_node = fsize / sizeof(node);
  }

  read_block(node_file, (char*) nodes, fsize);
  for (i = 1; i<fsize / sizeof(node); i++) {
    tnode = &nodes[i];
    hash_node(get_string(tnode->message_id), tnode->id);
    if (tnode->group_id > MAX_GROUPS)
      printf("Ignoring tnode with group_id %x\n", tnode->group_id);
    else
      thread(tnode, 0);
  }
  
  if (inhibit_thread_flattening == 0)
    flatten_groups();
}

unsigned int get_parent_by_subject(const char *subject, 
				   unsigned int group_id, 
				   time_t date) {
  /* Don't search further back than seven days. */
  time_t cutoff = date - 60*60*24*7;
  node *nnode, *parent;
  int i, n;
  int recursion = 0;
  group *g = &groups[group_id];

  //printf("Searching for subject %s\n", subject);

  for (i = g->nodes_length - 1; i >= 0; i--) {
    n = g->numeric_nodes[i];
    if (n != 0) {
      nnode = &nodes[n];
      if (nnode->date < cutoff)
	return 0;
      if (! strncasecmp(subject, get_string(nnode->subject), 20)) {
	/* We found a message with a matching Subject header. 
	   Go up the thread until we find the root or something
	   that doesn't have a matching Subject. */
	while (nnode->parent) {
	  parent = &nodes[nnode->parent];
	  if (strncasecmp(subject, get_string(parent->subject), 20))
	    break;
	  else
	    nnode = parent;
	  /* Make sure we don't have infinite recursion. */
	  if (recursion++ > 2000)
	    return 0;
	}
	return nnode->id;
      }
    }
  }
  return 0;
}

unsigned int get_parent(const char *parent_message_id, unsigned int group_id) {
  node *pnode = get_node_any(parent_message_id, group_id);
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
    /* Article hasn't been cancelled. */
    if (nnode->author != 0) {
      tnode = &(tnodes[thread_index++]);
      tnode->id = nnode->id;
      tnode->depth = depth;
    }

    if (nnode->first_child) 
      flatten_thread(&nodes[nnode->first_child], tnodes, group_id,
		     (nnode->author == 0? depth: depth+1));

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
  bzero(flattened, (max + 1) * sizeof(int));

  for (i = max; i>0; i--) {
    //printf("numeric nodes %d: %d\n", i, tgroup->numeric_nodes[i]);
    if ((id = tgroup->numeric_nodes[i]) != 0) {
      nnode = &nodes[id];
      if (nnode->parent == 0 || nodes[nnode->parent].number == 0) 
	flatten_thread(nnode, tnodes, group_id, 0);
    }
  }

  tgroup->threads_length = thread_index;

  if (!strcmp("gmane.discuss", external_group_name(tgroup)))
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
  size_t area, new_area;

  new_length = length * 2;
  while (new_length < (min + 2)) {
    if (new_length == 0) 
      new_length = 64;
    else
      new_length = new_length * 2;
  }

  area = length * sizeof(int);
  new_area = new_length * sizeof(int);

  printf("Extending group node table from %ld to %ld (times two)\n",
	 area, new_area);

  tgroup->numeric_nodes = (unsigned int*)crealloc(tgroup->numeric_nodes,
						  new_area,
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

  if (number + 2 >= tgroup->nodes_length)
    extend_group_node_tables(tgroup, number + 2);

  enter_node_numerically(tgroup, tnode);

  if (do_thread) {
    tgroup->max_article = max(tgroup->max_article, tnode->number);
    tgroup->total_articles++;
    tgroup->dirtyp = 1;
    enter_node_threadly(tgroup, tnode);
  }

}

void output_threads(char *group_name) {
  group *g = find_group(internal_group_name(group_name));
  int total, i;
  node *nnode;
  thread_node *tnode;

  if (g == NULL)
    return;

  total = g->total_articles;

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

int num_total_children_depth = 0;

int num_total_children(node *nnode, int reset_countp) {
  int children = 0;

  if (reset_countp)
    num_total_children_depth = 0;

  if (num_total_children_depth++ > 500)
    return 0;

  if (nnode->first_child == 0)
    return 0;

  nnode = &nodes[nnode->first_child];

  while (nnode != NULL) {
    children++;
    children += num_total_children(nnode, 0);
    if (nnode->next_sibling != 0)
      nnode = &nodes[nnode->next_sibling];
    else
      nnode = NULL;
  }

  return children;    
}

static char output_articles[MAX_THREAD_LENGTH];
static int output_thread_length;
static char output_children[MAX_THREAD_DEPTH];

static node *crossposted_children[MAX_THREAD_LENGTH];

int new_child_p(int children, node *nnode) {
  int i;

  for (i = 0; i < children; i++) {
    if (crossposted_children[i]->message_id == nnode->message_id)
      return 0;
  }
  return 1;
}

int num_crossposted_children(node *nnode) {
  int children = 0;
  int child;
  node *cnode;

  while (nnode != NULL) {
    child = nnode->first_child;

    while (child != 0) {
      cnode = &nodes[child];
      if (new_child_p(children, cnode)) {
	crossposted_children[children++] = cnode;
      }
      child = cnode->next_sibling;
    }
    if (nnode->next_instance)
      nnode = &nodes[nnode->next_instance];
    else
      nnode = NULL;
  }

  return children;    
}



int output_one_node(FILE *client, node *nnode, int depth) {
  int children = 0, j;

  if (++output_thread_length >= MAX_THREAD_LENGTH)
    return -1;

  children = num_crossposted_children(nnode);
  output_children[depth] = children;
  fprintf(client, "%d\t%d\t%s\t%s\t%s\t", 
	  depth,
	  nnode->number, 
	  get_string(nnode->subject), 
	  get_string(nnode->author),
	  format_time(nnode->date));

  for (j = 0; j<depth; j++) 
    fprintf(client, "%d\t", output_children[j]);

  fprintf(client, "\t%s", external_group_name(&groups[nnode->group_id]));
  
  fprintf(client, "\n");

  return children;
}

void output_thread(FILE *client, node *nnode, int depth) {
  int i, children;
  node **cchildren;

  if (depth >= MAX_THREAD_DEPTH - 1)
    return;

  /* Find the first instance of this Message-ID in case of
     cross-posting. */
  nnode = find_node(get_string(nnode->message_id));
  if (! nnode)
    return;

  for (i = 0; i < output_thread_length; i++) {
    if (output_articles[i] == nnode->id) {
      printf("Got recursion with %s\n", get_string(nnode->message_id));
      return;
    }
  }

  output_articles[i] = nnode->id;

  children = output_one_node(client, nnode, depth);

  if (children == -1)
    return;

  if (depth > 0)
    output_children[depth - 1]--;

  cchildren = (node**) malloc(children * sizeof(node*));
  if (cchildren == NULL) {
    int size;
    printf("Failed to allocate %ld bytes\n", children * sizeof(node*));
    size = 0;
    size = 4 / size;
  }
  for (i = 0; i < children; i++) 
    cchildren[i] = crossposted_children[i];

  for (i = 0; i < children; i++) 
    output_thread(client, cchildren[i], depth + 1);

  free(cchildren);

}

void output_one_thread(FILE *client, const char *group_name, int article) {
  group *g = find_group(internal_group_name(group_name));
  node *nnode;

  if (g == NULL)
    return;

  if (article <= g->max_article) {
    bzero(output_articles, MAX_THREAD_LENGTH);
    output_thread_length = 0;

    nnode = &nodes[g->numeric_nodes[article]];
    output_thread(client, nnode, 0);
  }
  fprintf(client, ".\n");
}

void output_lookup(FILE *client, const char *message_id) {
  node *nnode = find_node(message_id);
  if (nnode != NULL)
    fprintf(client, "%s\t%d\n",
	    external_group_name(&groups[nnode->group_id]),
	    nnode->number);
  fprintf(client, ".\n");
}

void output_root(FILE *client, const char *group_name, int article) {
  group *g = find_group(internal_group_name(group_name));
  node *nnode;
  int i;
  int recursion_level = 0;

  if (g == NULL)
    return;

  if (article <= g->max_article) {
    i = g->numeric_nodes[article];
    //printf("Got node id %d %s\n", i, group_name);
    if (i != 0) {
      nnode = &nodes[i];
      /*
      printf("In group %d, %s, %s\n", 
	     nnode->group_id,
	     external_group_name(&groups[nnode->group_id]),
	     get_string(nnode->subject));
      */
      while (nnode->parent != 0 &&
	     nodes[nnode->parent].number != 0) {
	nnode = &nodes[nnode->parent];
	if (recursion_level++ > 2000)
	  break;
	//printf("Parent message_id '%s'\n", get_string(nnode->message_id));
      }

      fprintf(client, "%s\t%d\n", 
	      external_group_name(&groups[nnode->group_id]),
	      nnode->number);
    }
  }
  fprintf(client, ".\n");
}

void output_thread_roots(FILE *client, const char *group_name, 
			 int page, int page_size, int rootsp) {
  group *g = find_group(internal_group_name(group_name));
  int total = 0, i, nthread = 0;
  node *nnode;
  thread_node *tnode;

  output_thread_length = 0;

  if (g == NULL || prohibited_group_p(g)) {
    fprintf(client, ".\n");
    return;
  }

  fprintf(client, "#max\t%d\n", total);
  fprintf(client, "#description\t%s\n", get_string(g->group_description));

  total = g->threads_length;
  
  for (i = 0; i < total; i++) {
    tnode = &(g->thread_nodes[i]);
    nnode = &nodes[tnode->id];
    if (tnode->depth == 0 || ! rootsp) {
      if (nthread > (page + 1) * page_size)
	break;
      else if (nthread >= page * page_size) {
	fprintf(client, "%d\t", num_total_children(nnode, 1));
	output_one_node(client, nnode, tnode->depth);
      }
      nthread++;
    }
  }

  fprintf(client, ".\n");
}

void output_months(FILE *client, const char *group_name) {
  group *g = find_group(internal_group_name(group_name));
  node *nnode;
  int i, n;
  int months[12*70];

  bzero(months, 12*70 * sizeof(int));

  if (g == NULL || prohibited_group_p(g)) {
    fprintf(client, ".\n");
    return;
  }

  for (i = 0; i < g->nodes_length; i++) {
    n = g->numeric_nodes[i];
    if (n != 0) {
      nnode = &nodes[n];
      months[find_month_number(nnode->date)]++;
    }
  }
  for (i = 0; i < 12*60; i++) {
    if (months[i]) 
      fprintf(client, "%ld\t%d\n", month_table[i], months[i]);
  }
  fprintf(client, ".\n");
}

void output_days(FILE *client, const char *group_name, time_t start) {
  group *g = find_group(internal_group_name(group_name));
  node *nnode;
  int i, n;
  time_t stop = month_table[find_month_number(start + 1)];
  int days[31];

  bzero(days, 31 * sizeof(int));

  if (g == NULL || prohibited_group_p(g)) {
    fprintf(client, ".\n");
    return;
  }

  for (i = 0; i < g->nodes_length; i++) {
    n = g->numeric_nodes[i];
    if (n != 0) {
      nnode = &nodes[n];
      if (nnode->date >= start && nnode->date <= stop) {
	int day = (nnode->date - start) / (24*60*60);
	if (day <= 31 && day >= 0)
	  days[day]++;
      }
    }
  }

  for (i = 0; i < 31; i++) {
    if (days[i]) 
      fprintf(client, "%d\t%d\n", i, days[i]);
  }
  fprintf(client, ".\n");
}

void output_articles_in_period(FILE *client, const char *group_name, 
			       int start_time, int stop_time, 
			       int page, int page_size) {
  group *g = find_group(internal_group_name(group_name));
  node *nnode;
  int i, n, narts = 0;

  if (g == NULL || prohibited_group_p(g)) {
    fprintf(client, ".\n");
    return;
  }

  output_thread_length = 0;

  for (i = 0; i < g->nodes_length; i++) {
    n = g->numeric_nodes[i];
    if (n != 0) {
      nnode = &nodes[n];
      if (nnode->date >= start_time && nnode->date <= stop_time) {
	if ((narts >= page * page_size) && (narts < (page + 1) * page_size)) {
	  fprintf(client, "%d\t", num_total_children(nnode, 1));
	  output_one_node(client, nnode, 0);
	}
	narts++;
      }
    }
  }

  fprintf(client, "#max\t%d\n", narts);
  fprintf(client, ".\n");
}

void output_group_threads(FILE *client, const char *group_name, 
			  int page, int page_size, 
			  int last) {
  group *g = find_group(internal_group_name(group_name));
  int total, i, j;
  node *nnode;
  thread_node *tnode;
  int start, stop, articles = 0, tstart;
  int children[MAX_THREAD_DEPTH];

  if (g == NULL || prohibited_group_p(g)) {
    fprintf(client, ".\n");
    return;
  }

  total = g->threads_length;

  if (last == 0)
    last = g->max_article;

  fprintf(client, "#max\t%d\n", total);
  /*
    FIXME:
    fprintf(client, "#description\t%s\n", get_string(g->description));
  */

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
    /* We want to output one more article that is going to be
       displayed.  Loom wants one extra article do to the nice
       threaded display that's to be continued on the next page. */
    if (articles >= page_size + 1)
      break;
  }

  /* Find the start of the thread we might be in the middle of. */
  for (tstart = start; tstart > 0; tstart--) {
    tnode = &(g->thread_nodes[tstart]);
    if (tnode->depth == 0)
      break;
  }

  //printf("Outputting group thread from %d to %d, last %d\n", start, stop, last);

  /* We have now found the start of the first thread to
     be output to the client. */
  if (start != stop) {
    for (i = tstart; i <= stop; i++) {
      tnode = &(g->thread_nodes[i]);
      nnode = &nodes[tnode->id];
      children[tnode->depth] = num_children(nnode);
      if (i >= start && nnode->number <= last && nnode->number > 0) {
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
	  external_group_name(g), get_string(g->group_description));
}

void output_groups(FILE *client, const char *match) {
  group *g;
  int i;
  char *group_name;

  for (i = 1; i<num_groups; i++) {
    g = &groups[alphabetic_groups[i]];
    if (g->group_name != 0) {
      group_name = external_group_name(g);
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
  group *g, *prev_group = NULL;
  int i;
  char *group_name;
  int levels = find_levels(prefix);
  char *prev = NULL;
  int ngroups = 0;
  char *prev_output = NULL;

  for (i = 1; i<num_groups; i++) {
    g = &groups[alphabetic_groups[i]];
    group_name = external_group_name(g);
    if (strstr(group_name, prefix) == group_name) {
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
  loff_t fsize;

  g_mime_init(GMIME_INIT_FLAG_UTF8);
  compute_month_table();
  //g_mime_init(0);
  fsize = allocate_nodes();
  init_hash();
  init_nodes(fsize);
  read_conf_file();
}

int meg(unsigned int size) {
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

void clean_up(void) {
}

int group_name_cmp(const void *sr1, const void *sr2) {
  return strcmp(external_group_name(&groups[*(int*)sr1]),
		external_group_name(&groups[*(int*)sr2]));
}

void alphabetize_groups (void) {
  group *g;
  int i;

  num_groups = 1;

  for (i = 1; i<MAX_GROUPS; i++) {
     g = &groups[i];
     if (g->group_id == 0)
       break;
     if (! prohibited_group_p(g))
       alphabetic_groups[num_groups++] = g->group_id;
  }

  qsort(alphabetic_groups, num_groups, sizeof(int), group_name_cmp);
}

void cancel_article(FILE *client, const char *group_name, int article) {
  group *g = find_group(internal_group_name(group_name));
  node *nnode;
  int i;

  if (g == NULL)
    return;

  if (article > g->max_article) 
    return;

  i = g->numeric_nodes[article];
  if (i == 0) 
    return;

  nnode = &nodes[i];
  cancel_message_id(client, get_string(nnode->message_id));  
}

void cancel_message_id(FILE *client, const char *message_id) {
  node *nnode = find_node(message_id);
  group *g;

  if (nnode == NULL) {
    fprintf(client, "%s does not exist.\n.\n", message_id);
  } else {
    while (nnode != NULL) {
      g = &groups[nnode->group_id];
      fprintf(client, "Cancelling %s:%d\n", 
	      external_group_name(g),
	      nnode->number);
      nnode->author = 0;
      write_node(nnode);
      flatten_threads(g);
      if (nnode->next_instance != 0)
	nnode = &nodes[nnode->next_instance];
      else
	nnode = NULL;
    }
    fprintf(client, ".\n");
  }
}

void rename_group(FILE *client, const char *from_group_name,
		  const char *to_group_name) {
  group *g = find_group(from_group_name);

  if (g == NULL)
    fprintf(client, "%s does not exist.\n.\n", from_group_name);
  else {
    fprintf(client, "Renaming %s to %s.\n.\n", from_group_name,
	    to_group_name);
    g->external_group_name = enter_string_storage(to_group_name);
    g->dirtyp = 1;
    enter_external_to_internal_group_name_map(to_group_name, from_group_name);
  }
}

