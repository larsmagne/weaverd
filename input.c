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
} parsed_article;

static parsed_article pa;

parsed_article *parse_file(const char *file_name) {
  GMimeStream *stream;
  GMimeMessage *msg = 0;
  const char *author, *subject, *message_id, *references;
  time_t date;
  int offset;
  int file;
  InternetAddress *iaddr;
  InternetAddressList *iaddr_list;

  if ((file = open(file_name, O_RDONLY|O_STREAMING)) == -1) {
    perror("tokenizer");
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
    g_mime_message_get_date(msg, &date, &offset);
    if (author != NULL && subject != NULL) {
      /* Get the address from the From header. */
      if ((iaddr_list = internet_address_parse_string(author)) != NULL) {
	iaddr = iaddr_list->address;
	if (iaddr->name == NULL)
	  *pa.author = 0;
	else
	  strncpy(pa.author, iaddr->name, MAX_STRING_SIZE-1);
	internet_address_list_destroy(iaddr_list);
      } else {
	*pa.author = 0;
      }

      strncpy(pa.subject, subject, MAX_STRING_SIZE-1);
      strncpy(pa.message_id, message_id, MAX_STRING_SIZE-1);
      if (references == NULL)
	strncpy(pa.parent_message_id, "", MAX_STRING_SIZE-1);
      else
	strncpy(pa.parent_message_id, references, MAX_STRING_SIZE-1);
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
  group *fgroup;

  if (path_to_article_spec(file_name, group_name, &article)) {
    pa = parse_file(file_name);

#ifdef DEBUG
    printf("%s %s %s %s\n", pa->author, pa->subject, pa->message_id,
	   pa->parent_message_id);
#endif

    /* Check that group/article hasn't been threaded already.  */
    
    /* Look up stuff and create the node. */

    fgroup = get_group(group_name);
    group_id = fgroup->group_id;

    /* Strip pointy brackets. */
    fix_message_id(pa->message_id);
    /* Find the final Message-ID in References. */
    fix_parent_message_id(pa->parent_message_id); 

    tnode = get_node(pa->message_id, group_id);

    if (tnode->number != 0) 
      printf("Skipping %s/%d\n", group_name, article);
    else {
      id = tnode->id;
      tnode->number = article;
    
      if (*pa->parent_message_id)
	tnode->parent = get_parent(pa->parent_message_id, group_id);
    
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
  return 0;
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
