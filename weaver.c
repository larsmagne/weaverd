#define _LARGEFILE64_SOURCE

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "weaver.h"
#include "config.h"
#include "../mdb/util.h"

group groups[MAX_GROUPS];

node *nodes = NULL;
unsigned int nodes_length = 0;
char *index_dir = NULL;

static unsigned int current_node = 0;
static int node_file = 0;

unsigned int next_id(void) {
  return current_node++;
}

char *index_file_name(char *name) {
  static char file_name[1024];
  strcpy(file_name, index_dir);
  strcat(file_name, "/");
  strcat(file_name, name);
  return file_name;
}

void extend_node_storage(void) {
  int new_length = nodes_length * 0.5;
  node *new_nodes = (node*) cmalloc(new_length * sizeof(node));
  memcpy(new_nodes, nodes, nodes_length * sizeof(node));
  free(nodes);
  nodes = new_nodes;
  nodes_length = new_length;
}

void write_node(node *nnode) {
  printf("Writing node\n");
  lseek64(node_file, (loff_t)(sizeof(node) * nnode->id), SEEK_SET);
  printf("node size: %d\n", sizeof(node));
  write_from(node_file, (char*)nnode, sizeof(node));

  if (nnode->id > current_node)
    extend_node_storage();

  memcpy(&nodes[nnode->id], nnode, sizeof(node));
}

void init_nodes(void) {
  loff_t fsize;

  if ((node_file = open64(index_file_name(NODE_FILE),
			  O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the node file.");
 
  fsize = file_size(node_file);
  if (fsize == 0)
    nodes_length = 1024 * 256;
  else
    nodes_length = fsize * 2;

  nodes = (node*) cmalloc(nodes_length * sizeof(node));

  read_block(node_file, (char*) nodes, fsize);
  current_node = fsize / sizeof(node);
}

unsigned int get_parent(const char *parent_message_id) {
}

void thread(node *node) {
}


