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

#define MAX_FILE_NAME 10000

typedef struct {
  int group_id;
  char message_id[MAX_STRING_SIZE];
  char parent_message_id[MAX_STRING_SIZE];
  char subject[MAX_STRING_SIZE];
  char author[MAX_STRING_SIZE];
  time_t date;
  int ignorep;
} parsed_article;

static parsed_article pa;

parsed_article *parse_simple_file(const char *file_name) {
  GMimeStream *stream;
  GMimeMessage *msg = 0;
  const char *author = NULL, *subject = NULL, *message_id = NULL,
    *references = NULL, *original_message_id = NULL, *xref = NULL;
  time_t date;
  int offset;
  int file;
  InternetAddress *iaddr;
  InternetAddressList *iaddr_list;
  int washed_subject = 0;
  char *address, *at;

  if ((file = open(file_name, O_RDONLY|O_STREAMING)) == -1) {
    printf("Can't open %s\n", file_name);
    perror("weaver");
    return NULL;
  }

  stream = g_mime_stream_fs_new(file);
  msg = g_mime_parser_construct_message(stream);
  g_mime_stream_unref(stream);

  if (msg != 0) {
    author = g_mime_message_get_header(msg, "From");
    subject = g_mime_message_get_subject(msg);
    message_id = g_mime_message_get_message_id(msg);
    references = g_mime_message_get_header(msg, "references");
    xref = g_mime_message_get_header(msg, "xref");
    original_message_id = 
      g_mime_message_get_header(msg, "original-message-id");
    g_mime_message_get_date(msg, &date, &offset);
    if (author != NULL && subject != NULL) {
      /* Get the address from the From header. */
      if ((iaddr_list = internet_address_parse_string(author)) != NULL) {
	iaddr = iaddr_list->address;
	if (iaddr->name != NULL) {
	  strncpy(pa.author, iaddr->name, MAX_STRING_SIZE-1);

	  /* There's a bug in gmimelib that may leave a closing paren in
	     the name field. */
	  if (strrchr(pa.author, ')') == pa.author + strlen(pa.author) - 1) 
	    *strrchr(pa.author, ')') = 0;
	} else {
	  address = internet_address_to_string(iaddr, FALSE);
	  strncpy(pa.author, address, MAX_STRING_SIZE-1);
	  if ((at = strchr(pa.author, '@')) != NULL) 
	    *at = 0;
	  free(address);
	}
	internet_address_list_destroy(iaddr_list);
      } else {
	*pa.author = 0;
      }

      while (! washed_subject) {
	washed_subject = 1;
	while (*subject == ' ') {
	  subject++;
	  washed_subject = 0;
	}
	if (subject == strstr(subject, "re:") ||
	    subject == strstr(subject, "Re:") ||
	    subject == strstr(subject, "rE:") ||
	    subject == strstr(subject, "RE:")) {
	  subject += 3;
	  washed_subject = 0;
	}
      }

      if (xref != NULL &&
	  strstr(xref, "gmane.spam.detected") != NULL)
	pa.ignorep = 1;
      else
	pa.ignorep = 0;

      strncpy(pa.subject, subject, MAX_STRING_SIZE-1);
      if (strlen(pa.subject) > 70) 
	*(pa.subject+70) = 0;

      if (original_message_id != NULL &&
	  strstr(message_id, "gmane.org"))
	strncpy(pa.message_id, original_message_id, MAX_STRING_SIZE-1);
      else
	strncpy(pa.message_id, message_id, MAX_STRING_SIZE-1);

      if (references == NULL)
	*(pa.parent_message_id) = 0;
      else
	strncpy(pa.parent_message_id, references, MAX_STRING_SIZE-1);

      wash_string(pa.subject);
      wash_string(pa.author);

      pa.date = date;
      g_mime_object_unref(GMIME_OBJECT(msg));
    }

  }
  close(file);

  //dummy = malloc(2045);
  //strcpy(dummy, pa.author);

  //printf("Author: %s\n", pa.author);

  return &pa;
}

void input_directory(const char* dir_name) {
  DIR *dirp;
  struct dirent *dp;
  char file_name[MAX_FILE_NAME];
  struct stat stat_buf;

  printf("%s\n", dir_name); 

  if ((dirp = opendir(dir_name)) == NULL)
    return;
    
  while ((dp = readdir(dirp)) != NULL) {

    snprintf(file_name, sizeof(file_name), "%s/%s", dir_name,
	     dp->d_name);

    if (strcmp(dp->d_name, ".") &&
	strcmp(dp->d_name, "..")) {
    
      if (stat(file_name, &stat_buf) == -1) {
	perror("tokenizer");
	break;
      }
    
      if (S_ISDIR(stat_buf.st_mode)) {
	//input_directory(file_name);
      } else if (is_number(dp->d_name)) {
	parse_simple_file(file_name);
      }
    }
  }
  closedir(dirp);
}

int main(int argc, char **argv)
{
  g_mime_init(GMIME_INIT_FLAG_UTF8);

  input_directory("/mirror/var/spool/news/articles/gmane/discuss");

  exit(0);
}
