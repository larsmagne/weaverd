#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#if defined(__FreeBSD__)
#  include <sys/mman.h>
#endif
#include <fcntl.h>
#include <getopt.h>

#include "weaver.h"
#include "config.h"
#include "hash.h"
#include "input.h"
#include "../mdb/util.h"

struct option long_options[] = {
  {"spool", 1, 0, 's'},
  {"index", 1, 0, 'i'},
  {"help", 0, 0, 'h'},
  {0, 0, 0, 0}
};

int parse_args(int argc, char **argv) {
  int option_index = 0, c;
  while (1) {
    c = getopt_long(argc, argv, "hs:i:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 's':
      news_spool = optarg;
      break;
      
    case 'i':
      index_dir = optarg;
      break;
      
    case 'h':
      printf ("Usage: we:index [--spool <directory>] <directories ...>\n");
      break;

    default:
      break;
    }
  }

  return optind;
}

int main(int argc, char **argv)
{
  int dirn;
  struct stat stat_buf;

  index_dir = "/index/weave";

  dirn = parse_args(argc, argv);
  
  /* Initialize key/data structures. */
  init_hash();
  init_nodes();

  if (stat(argv[dirn], &stat_buf) == -1) {
    perror("interactive");
    exit(0);
  }
      
  printf("Threading...\n");

  if (S_ISREG(stat_buf.st_mode)) 
    thread_file(argv[dirn]);

  flush_hash();

  exit(0);
}
