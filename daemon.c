#include <stdlib.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <getopt.h>

#include "weaver.h"
#include "config.h"
#include "input.h"
#include "../mdb/util.h"
#include "hash.h"

#define TRUE 1

union sock {
  struct sockaddr s;
  struct sockaddr_in i;
};

struct option long_options[] = {
  {"spool", 1, 0, 's'},
  {"index", 1, 0, 'i'},
  {"help", 1, 0, 'h'},
  {"port", 1, 0, 'p'},
  {0, 0, 0, 0}
};

void closedown(int);

#define BUFFER_SIZE 4096

int server_socket = 0;
static int port = 8010;

int parse_args(int argc, char **argv) {
  int option_index = 0, c;
  while (1) {
    c = getopt_long(argc, argv, "hs:i:p:b:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'i':
      index_dir = optarg;
      break;
      
    case 's':
      news_spool = optarg;
      break;
      
    case 'p':
      port = atoi(optarg);
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

int main(int argc, char **argv) {
  int wsd;
  int addlen, peerlen;
  time_t now;
  char *s;
  char buffer[BUFFER_SIZE];
  char *expression[MAX_SEARCH_ITEMS];
  struct sockaddr_in sin, caddr;
  int nitems = 0;
  static int so_reuseaddr = TRUE;
  int dirn, i;
  char *command, *group_name;
  int page, page_size, last;
  int message = 0;
  time_t start_time;
  int commands = 0, last_total_commands = 0;
  int elapsed;
  FILE *client;
  char *match;

  dirn = parse_args(argc, argv);

  init();
  /* Inhibit thread flattering by default. */
  inhibit_thread_flattening = 1;

  if (signal(SIGHUP, closedown) == SIG_ERR) {
    perror("Signal");
    exit(1);
  }

  if (signal(SIGINT, closedown) == SIG_ERR) {
    perror("Signal");
    exit(1);
  }

  if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("No socket");
    exit(1);
  }

  setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, 
	     sizeof(so_reuseaddr));

  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  sin.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(server_socket, (struct sockaddr*)&sin, sizeof(sin)) == -1) {
    perror("Bind");
    exit(1);
  }

  if (listen(server_socket, 120) == -1) {
    perror("Bad listen");
    exit(1);
  }

  time(&start_time);

  printf("Accepting...\n");

  while (TRUE) {
    nitems = 0;
    wsd = accept(server_socket, (struct sockaddr*)&caddr, &addlen);
    peerlen = sizeof(struct sockaddr);

    i = 0;
    while (read(wsd, buffer+i, 1) == 1 &&
	   *(buffer+i) != '\n' &&
	   i++ < BUFFER_SIZE)
      ;
    if (*(buffer+i) == '\n')
      *(buffer+i+1) = 0;

    if (*buffer == 0) 
      goto out;
    
    //printf("Got %s", buffer);

    s = strtok(buffer, " \n");

    while (s && nitems < MAX_SEARCH_ITEMS) {
      expression[nitems++] = s;
      s = strtok(NULL, " \n");
    }
    
    expression[nitems] = NULL;

    message = 1;

    if (nitems >= 1) {
      client = fdopen(wsd, "rw");

      command = expression[0];
      if (!strcmp(command, "group-thread") && nitems == 6) {
	group_name = expression[1];
	page = atoi(expression[2]);
	page_size = atoi(expression[3]);
	last = atoi(expression[4]);
	printf("Outputting thread for %s (%d)\n",
	       group_name, page);
	output_group_threads(client, group_name, page, page_size, last);
      } else if (!strcmp(command, "input") && nitems == 2) {
	thread_file(expression[1]);
	message = 0;
      } else if (!strcmp(command, "thread") && nitems == 3) {
	output_one_thread(client, expression[1], atoi(expression[2]));
	message = 0;
      } else if (!strcmp(command, "groups") && nitems == 2) {
	match = expression[1];
	if (strlen(match) == 0)
	  match = NULL;
	output_groups(client, match);
      } else if (!strcmp(command, "hierarchy") && nitems == 2) {
	match = expression[1];
	output_hierarchy(client, match);
      } else if (!strcmp(command, "flatten")) {
	inhibit_thread_flattening = 0;
	flatten_groups();
      } 

      if (!(commands++ % 100)) {
	time(&now);
	elapsed = now-start_time;
	printf("    %d commands (%d/s last %d seconds)\n",
	       commands - 1, 
	       (elapsed?
		(int)((commands-last_total_commands) / elapsed):
		0),
	       (int)elapsed);
	last_total_commands = commands;
      }

      fclose(client);
    }

  out:
    close(wsd);

    if (message) {
      time(&now);
      printf("Connection closed at %s", ctime(&now));
    }
  }

  exit(1);
}

void closedown(int i) {
 time_t now = time(NULL);

 if (server_socket)
   close(server_socket);
 flush_hash();
 printf("Closed down at %s", ctime(&now));
 exit(0);
}
