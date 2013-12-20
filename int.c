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
#include <gmime/gmime.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#include "weaver.h"
#include "config.h"
#include "hash.h"
#include "input.h"
#include "util.h"

struct option long_options[] = {
  {"spool", 1, 0, 's'},
  {"index", 1, 0, 'i'},
  {"help", 0, 0, 'h'},
  {"user", 1, 0, 'u'},
  {"recursive", 0, 0, 'r'},
  {"conf", 0, 0, 'c'},
  {"skip", 0, 0, 'S'},
  {0, 0, 0, 0}
};

static int do_output_thread = 0;
static int input_spool = 0;
static int input_conf = 0;
static char *lock_user = NULL;
static char *skip_until_group = NULL;

void closedown(int i);

int parse_args(int argc, char **argv) {
  int option_index = 0, c;
  while (1) {
    c = getopt_long(argc, argv, "htrcs:i:u:S:", long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 's':
      news_spool = optarg;
      break;
      
    case 'i':
      index_dir = optarg;
      break;
      
    case 'u':
      lock_user = optarg;
      break;
      
    case 'S':
      skip_until_group = optarg;
      break;
      
    case 't':
      do_output_thread = 1;
      break;
      
    case 'r':
      input_spool = 1;
      break;
      
    case 'c':
      input_conf = 1;
      break;
      
    case 'h':
      printf ("Usage: int [--spool <directory>] -r\n");
      break;

    default:
      break;
    }
  }

  return optind;
}

#define MAX_FILE_NAME 10000

static int commands = 0;
static time_t start_time = 0;

int compare (const void *a, const void *b) {
  return strcmp(*(char**) a, *(char**)b);
}

void input_directory(const char* dir_name) {
  time_t now;
  int elapsed;
  DIR *dirp;
  struct dirent *dp;
  char file_name[MAX_FILE_NAME];
  struct stat stat_buf;
  char *all_files, *files;
  char *all_dirs, *dirs;
  size_t dir_size;
  int total_files = 0, i = 0;
  char **file_array;

  printf("%s\n", dir_name); 

  if ((dirp = opendir(dir_name)) == NULL)
    return;

  if (fstat(dirfd(dirp), &stat_buf) == -1) {
    closedir(dirp);
    return;
  }
  
  // The total size of the file names shouldn't exceed the size
  // of the directory file.  So we just use that as the size.  It's
  // wasteful, but meh.
  dir_size = stat_buf.st_size + 1024;
  files = all_files = malloc(dir_size);
  bzero(all_files, dir_size);
  
  // Go through all the files in the directory and put files
  // in one array and directories in another.
  while ((dp = readdir(dirp)) != NULL) {
    if (strcmp(dp->d_name, ".") &&
        strcmp(dp->d_name, "..")) {
      total_files++;
      strcpy(files, dp->d_name);
      files += strlen(dp->d_name) + 1;
    }
  }
  closedir(dirp);

  // Sort the files by name.
  files = all_files;
  file_array = calloc(sizeof(char*), total_files);
  while (*files) {
    file_array[i++] = files;
    files += strlen(files) + 1;
  }
  qsort(file_array, total_files, sizeof(char*), compare);
  
  dirs = all_dirs = malloc(dir_size);
  bzero(all_dirs, dir_size);

  // Go through all the files, stat them/thread them, and separate
  // out the directories.
  for (i = 0; i < total_files; i++) {
    snprintf(file_name, sizeof(file_name), "%s/%s", dir_name, file_array[i]);
    if (lstat(file_name, &stat_buf) != -1) {
      if (S_ISDIR(stat_buf.st_mode)) {
	strcpy(dirs, file_array[i]);
	dirs += strlen(file_array[i]) + 1;
      } else if (S_ISREG(stat_buf.st_mode)) {
	if (is_number(file_array[i])) {
	  thread_file(file_name);
	  if (! (commands++ % 10000)) {
	    time(&now);
	    elapsed = now - start_time;
	    printf("    %d files (%d/s, last %d seconds)\n",
		   commands - 1, 
		   (elapsed?
		    (int)(10000 / elapsed):
		    0), 
		   elapsed);
	    start_time = now;
	    printf("    %d bytes string storage per file\n",
		   next_string / commands);
	    mem_usage();
	  }
	}
      }
    }
  }
  free(file_array);
  
  dirs = all_dirs;
  while (*dirs) {
    snprintf(file_name, sizeof(file_name), "%s/%s", dir_name, dirs);
    input_directory(file_name);
    dirs += strlen(dirs) + 1;
  }
  free(all_files);
  free(all_dirs);
}

char *get_group_directory(const char *group) {
  static char file_name[1024];
  char *f = file_name + strlen(news_spool);
  
  snprintf(file_name, 1024, "%s%s", news_spool, group);
  
  while (*f != 0) {
    if (*f == '.')
      *f = '/';
    f++;
  }

  return file_name;
}

void input_group(const char* group_name) {
  time_t now;
  int elapsed;
  DIR *dirp;
  struct dirent *dp;
  char file_name[MAX_FILE_NAME];
  struct stat stat_buf;
  char *dir_name = get_group_directory(group_name);

  printf("%s\n", dir_name); 

  if ((dirp = opendir(dir_name)) == NULL)
    return;
    
  while ((dp = readdir(dirp)) != NULL) {

    snprintf(file_name, sizeof(file_name), "%s/%s", dir_name,
	     dp->d_name);

    if (stat(file_name, &stat_buf) == -1) {
      perror("tokenizer");
      break;
    }
    
    if (! S_ISDIR(stat_buf.st_mode) &&
	is_number(dp->d_name)) {
      thread_file(file_name);
      if (!(commands++ % 10000)) {
	time(&now);
	elapsed = now - start_time;
	printf("    %d files (%d/s, last %d seconds)\n",
	       commands - 1, 
	       (elapsed?
		(int)(10000 / elapsed):
		0), 
	       elapsed);
	start_time = now;
	printf("    %d bytes string storage per file\n",
	       next_string / commands);
	mem_usage();
      }
    }
  }
  closedir(dirp);
}

int main(int argc, char **argv)
{
  int dirn;
  struct stat stat_buf;
  char *spool;
  char *file;
  int i;
  int found = 1;

  dirn = parse_args(argc, argv);
  
  if (signal(SIGHUP, closedown) == SIG_ERR) {
    perror("Signal");
    exit(1);
  }

  if (signal(SIGINT, closedown) == SIG_ERR) {
    perror("Signal");
    exit(1);
  }

  if (input_conf) {
    inhibit_thread_flattening = 1;
  }

  /* Initialize key/data structures. */
  init();
  time(&start_time);

  if (do_output_thread) {
    // First
    output_thread(stdout, find_node("m3ljlvshlj.fsf@quimbies.gnus.org"), 0);
    // Second
    //output_thread(stdout, find_node("m3skg3y031.fsf@quimbies.gnus.org"), 0);
    //output_threads("gmane.discuss");
    exit(0);
  } 

  if (lock_user != NULL)
    lock_and_uid(lock_user);

  if (input_spool) {
    spool = cmalloc(strlen(news_spool) + 1);
    memcpy(spool, news_spool, strlen(news_spool));
    inhibit_thread_flattening = 1;
    inhibit_file_writes = 0;
    *(spool+strlen(news_spool)-1) = 0;
    //input_directory("/mirror/var/spool/news/articles/gmane/comp/lib/glibc/bugs");
    //input_directory("/mirror/var/spool/news/articles/gmane/comp/gnu/stow/bugs");
    //input_directory("/mirror/var/spool/news/articles/gmane/linux");
    //input_directory("/mirror/var/spool/news/articles/gmane/discuss");
    //input_directory("/mirror/var/spool/news/articles/gmane/test");
    //input_directory("/mirror/var/spool/news/articles/gmane/comp/graphics/ipe/general");
    //input_directory(spool);
    input_directory("/mirror/var/spool/news/articles/gmane/comp/hardware");
    flush();
    clean_up();
    clean_up_hash();
    printf("Total files: %d, total nodes: %d\n", commands, current_node);
    exit(0);
  } 

  if (input_conf) {
    if (skip_until_group != NULL)
      found = 0;

    inhibit_thread_flattening = 1;
    inhibit_file_writes = 0;
    for (i = 1; i < num_groups; i++) {
      if (found == 0 && !strcmp(get_string(groups[i].group_name), 
				skip_until_group))
	found = 1;
      if (found == 1)
	input_group(get_string(groups[i].group_name));
    }
    flush();
    clean_up();
    clean_up_hash();
    printf("Total files: %d, total nodes: %d\n", commands, current_node);
    exit(0);
  } 

  if (dirn == 1)
    file = "/mirror/var/spool/news/articles/gmane/discuss/4001";
  else
    file = argv[dirn];

  if (stat(file, &stat_buf) == -1) {
    perror("interactive");
    exit(0);
  }
      
  if (S_ISREG(stat_buf.st_mode)) 
    thread_file(file);

  flush();
  clean_up();
  clean_up_hash();

  exit(0);
}

void closedown(int i) {
 time_t now = time(NULL);

 flush();
 printf("Closed down at %s", ctime(&now));
 exit(0);
}
