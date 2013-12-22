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
#include "dispatch.h"
#include "util.h"
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

dispatcher dispatchers[] = {
  {"group-thread",  output_group_threads, {STRING, INT, INT, INT, EOA}},
  //{"input", thread_file, {STRING, EOA}},
  {"thread", output_one_thread, {STRING, INT, EOA}},
  {"root", output_root, {STRING, INT, EOA}},
  {"groups", output_groups, {STRING, EOA}},
  {"hierarchy", output_hierarchy, {STRING, EOA}},
  {"lookup", output_lookup, {STRING, EOA}},
  {"flatten", flatten_groups, {EOA}},
  {"quit", closedown, {EOA}},
  {"flush", flush, {EOA}},
  {NULL, NULL, {0}}  
};

#define BUFFER_SIZE 8192

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

time_t start_time;

void handle_clients () {
  int wsd;
  socklen_t addlen;
  char *s;
  char buffer[BUFFER_SIZE];
  char *expression[MAX_SEARCH_ITEMS];
  int nitems = 0;
  int i;
  struct sockaddr_in caddr;
  time_t now;
  char *command, *group_name;
  int page, page_size, last;
  int message = 0;
  int commands = 0, last_total_commands = 0;
  int elapsed;
  FILE *client;
  char *match;
  int input_times = 0, auto_flush_p = 0, read_result;

  printf("Accepting...\n");

  while (TRUE) {
    nitems = 0;
    addlen = sizeof(caddr);
    wsd = accept(server_socket, (struct sockaddr*)&caddr, &addlen);
    if (wsd == -1) {
      printf("Server socket %d\n", server_socket);
      perror("weaverd");
      goto out;
    }

    i = 0;
    while ((read_result = read(wsd, buffer+i, 1)) >= 0 &&
	   *(buffer+i) != '\n' &&
	   i++ < BUFFER_SIZE) {
      if (read_result == 0)
	sleep(1);
    }

    if (read_result < 0) {
      printf("Got error %d on %d\n", read_result, wsd);
      perror("weaverd");
    }
    
    if (*(buffer+i) == '\n')
      *(buffer+i+1) = 0;

    if (*buffer == 0) {
      printf("Got empty input\n");
      goto out;
    }
    
    printf("Got %s", buffer);

    s = strtok(buffer, " \n");

    while (s && nitems < MAX_SEARCH_ITEMS) {
      expression[nitems++] = s;
      s = strtok(NULL, " \n");
    }
    
    expression[nitems] = NULL;

    message = 1;

    if (nitems >= 1) {
      client = fdopen(wsd, "r+");

      command = expression[0];
      if (!strcmp(command, "group-thread") && nitems == 5) {
	group_name = expression[1];
	page = atoi(expression[2]);
	page_size = atoi(expression[3]);
	last = atoi(expression[4]);
	//printf("Outputting thread for %s (%d)\n", group_name, page);
	output_group_threads(client, group_name, page, page_size, last);
      } else if (!strcmp(command, "input") && nitems == 2) {
	thread_file(expression[1]);
	if (! (input_times++ % 100) && auto_flush_p)
	  flush();
	flush();
	message = 0;
      } else if (!strcmp(command, "thread") && nitems == 3) {
	output_one_thread(client, expression[1], atoi(expression[2]));
	message = 0;
      } else if (!strcmp(command, "thread-roots") && nitems == 5) {
	output_thread_roots(client, expression[1], atoi(expression[2]), 
			    atoi(expression[3]), atoi(expression[4]));
	message = 0;
      } else if (!strcmp(command, "article-period") && nitems == 6) {
	output_articles_in_period(client, expression[1], atoi(expression[2]), 
				  atoi(expression[3]), 
				  atoi(expression[4]), 
				  atoi(expression[5]));
	message = 0;
      } else if (!strcmp(command, "group-months") && nitems == 2) {
	output_months(client, expression[1]);
	message = 0;
      } else if (!strcmp(command, "group-days") && nitems == 3) {
	output_days(client, expression[1], atoi(expression[2]));
	message = 0;
      } else if (!strcmp(command, "root") && nitems == 3) {
	output_root(client, expression[1], atoi(expression[2]));
	message = 0;
      } else if (!strcmp(command, "groups") && nitems == 2) {
	match = expression[1];
	if (strlen(match) == 0)
	  match = NULL;
	output_groups(client, match);
      } else if (!strcmp(command, "hierarchy") && nitems == 2) {
	match = expression[1];
	output_hierarchy(client, match);
      } else if (!strcmp(command, "lookup") && nitems == 2) {
	output_lookup(client, expression[1]);
      } else if (!strcmp(command, "newgroup") && nitems > 2) {
	newgroup(client, expression[1], expression + 2, nitems - 2);
	flush();
      } else if (!strcmp(command, "rmgroup") && nitems == 2) {
	rmgroup(client, expression[1]);
	flush();
      } else if (!strcmp(command, "cancel") && nitems == 2) {
	cancel_message_id(client, expression[1]);
	flush();
      } else if (!strcmp(command, "cancel-article") && nitems == 3) {
	cancel_article(client, expression[1], atoi(expression[2]));
	flush();
      } else if (!strcmp(command, "flatten")) {
	inhibit_thread_flattening = 0;
	flatten_groups();
      } else if (!strcmp(command, "quit")) {
	closedown(0);
      } else if (!strcmp(command, "flush")) {
	flush();
      } else if (!strcmp(command, "auto-flush")) {
	flush();
	auto_flush_p = 1;
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
	mem_usage();
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
}

void sigpipe_handle_clients(int);

void sigpipe_handle_clients(int ignore) {
  signal(SIGPIPE, sigpipe_handle_clients);
  printf("Got a SIGPIPE; continuing.\n");
}

int main(int argc, char **argv) {
  parse_args(argc, argv);
  struct sockaddr_in sin;
  static int so_reuseaddr = TRUE;

  init();
  /* Don't inhibit thread flattering by default. */
  inhibit_thread_flattening = 0;

  if (signal(SIGHUP, closedown) == SIG_ERR) {
    perror("Signal");
    exit(1);
  }

  if (signal(SIGINT, closedown) == SIG_ERR) {
    perror("Signal");
    exit(1);
  }

  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
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

  handle_clients();

  exit(1);
}

void closedown(int i) {
 time_t now = time(NULL);

 if (server_socket)
   close(server_socket);
 flush();
 printf("Closed down at %s", ctime(&now));
 exit(0);
}


