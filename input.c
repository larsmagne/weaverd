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
#include <gmime/gmime.h>
#include <pwd.h>
#include <sys/types.h>

#include "weaver.h"
#include "../mdb/util.h"
#include "hash.h"
#include "config.h"

typedef struct {
  int group_id;
  char message_id[MAX_STRING_SIZE];
  char parent_message_id[MAX_STRING_SIZE];
  char subject[MAX_STRING_SIZE];
  char author[MAX_STRING_SIZE];
  time_t date;
  int ignorep;
} parsed_article;

void wash_string(char *string) {
  char c;

  while ((c = *string) != 0) {
    if (c == '\t' || c == '\n')
      *string = ' ';
    string++;
  }
}

static parsed_article pa;

parsed_article *parse_file(const char *file_name) {
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
    author = g_mime_message_get_sender(msg);
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
	if (iaddr->name != NULL)
	  strncpy(pa.author, iaddr->name, MAX_STRING_SIZE-1);
	else {
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
	    subject == strstr(subject, "eR:") ||
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
  return &pa;
}

void fix_message_id(char *id) {
  char *p = id, *q = id, c;
  while ((c = *p++) &&
	 (c != '<'))
    ;
  while (((c = *p++) != 0) &&
	 (c != '>'))
    *q++ = c;
  *q = 0;
}

void fix_parent_message_id(char *ids) {
  int len = strlen(ids);
  char *p = ids + len, *q = ids, c;

  while (p > ids &&
	 ((c = *p--) != '<'))
    ;
  p++;
  if (*p == '<')
    p++;
  while (((c = *p++) != 0) &&
	 (c != '>'))
    *q++ = c;
  *q = 0;
}

int thread_file(const char *file_name) {
  parsed_article *pa;
  node *tnode, *prev_node;
  char group_name[MAX_STRING_SIZE];
  int id, group_id, article;
  group *g;

  if (path_to_article_spec(file_name, group_name, &article)) {
    g = get_group(group_name);
    if (strlen(get_string(g->group_description)) == 0)
      return 0;

    pa = parse_file(file_name);

    if (pa == NULL)
      return 0;

    if (pa->ignorep == 1)
      return 0;

#ifdef DEBUG
    printf("%s %s %s %s\n", pa->author, pa->subject, pa->message_id,
	   pa->parent_message_id);
#endif

    /* Check that group/article hasn't been threaded already.  */
    
    /* Look up stuff and create the node. */

    group_id = g->group_id;

    /* Strip pointy brackets. */
    fix_message_id(pa->message_id);
    /* Find the final Message-ID in References. */
    if (*pa->parent_message_id) 
      fix_parent_message_id(pa->parent_message_id); 

#if 0
    printf("Doing message %s\n   %s\n", file_name, pa->message_id);
#endif
    
    tnode = get_node(pa->message_id, group_id);

    if (tnode->number != 0) 
      printf("Skipping %s/%d (prev appearance %d)\n", group_name, article,
	     tnode->number);
    else {
      id = tnode->id;
      tnode->number = article;


      if (*pa->parent_message_id) {
	tnode->parent = get_parent(pa->parent_message_id, group_id);
#if 0
	if (nodes[tnode->parent].subject == 0) {
	  printf("Not found '%s' '%s'\n", pa->message_id, pa->parent_message_id);
	} else {
	  printf("    Found '%s' '%s'\n", pa->message_id, pa->parent_message_id);
	}
#endif
      }
    
      /* Set next_instance in previous instances to point to us. */
      if (previous_instance_node) {
	prev_node = &nodes[previous_instance_node];
	prev_node->next_instance = id;
      }

      tnode->subject = enter_string_storage(pa->subject);
      tnode->author = enter_string_storage(pa->author);
      tnode->date = pa->date;
      
      thread(tnode, 1);

      write_node(tnode);
    }
  } else {
    printf("Can't find an article spec for %s\n", file_name);
  }
  return 1;
}

void lock_and_uid(char *user) {
  struct passwd *pw;

  pw = getpwnam(user);
  
  if (mlockall(MCL_FUTURE) == -1) {
    perror("we-index");
    exit(1);
  }

  setuid(pw->pw_uid);
  setgid(pw->pw_gid);
}

void read_conf_file(void) {
  FILE *conf = fopen("/etc/gmane.conf", "r");
  char buf[8192];
  char *string, *group_name, *description;
  int i = 0;
  group *g;

  while (fgets(buf, 8192, conf) != NULL) {
    if ((! strstr(buf, "removed")) &&
	(! strstr(buf, "denied")) &&
	strchr(buf, ':') &&
	(*buf != '#')) {
      i = 0;
      group_name = NULL;
      description = NULL;
      strtok(buf, ":");
      while ((string = strtok(NULL, ":")) != NULL &&
	     i < 4) {
	i++;
	if (i == 1)
	  group_name = string;
	else if (i == 3)
	  description = string;
      }

      if (group_name && description) {
	g = get_group(group_name);
	g->group_description = enter_string_storage(description);
      }
    }
  }

  alphabetize_groups();
}
