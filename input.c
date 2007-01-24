#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#define  __USE_XOPEN2K
#include <fcntl.h>
#include <getopt.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <gmime/gmime.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <unistd.h>

#include "weaver.h"
#include "util.h"
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
  int likely_response;
} parsed_article;

void wash_string(char *string) {
  char c;

  while ((c = *string) != 0) {
    if (c == '\t' || c == '\n')
      *string = ' ';
    string++;
  }
}

int quoted_in_body_p (const char *file_name) {
  int file;
  char buf[4096];
  int read_size;
  int found = 0;

  bzero(buf, sizeof(buf));

  if ((file = open(file_name, O_RDONLY|O_STREAMING)) == -1) {
    return 0;
  }

  while ((read_size = read(file, &buf, sizeof(buf) - 1)) > 0) {
    buf[sizeof(buf)] = 0;
    if (strstr(buf, "\n>")) {
      found = 1;
      break;
    }
  }

  close(file);
  return found;
}

static parsed_article pa;

GMimeMessage *
g_mime_parser_construct_message_headers_only (GMimeStream *stream);

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
  msg = g_mime_parser_construct_message_headers_only(stream);
  g_mime_stream_unref(stream);

  if (msg != 0) {
    pa.likely_response = 0;

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
	  if (strstr(pa.author, "public.gmane.org") != NULL) {
	    if ((at = strchr(pa.author, '-')) != NULL) 
	      *at = 0;
	    else
	      *pa.author = 0;
	  } else {
	    if ((at = strchr(pa.author, '@')) != NULL) 
	      *at = 0;
	  }
	  free(address);
	}
	internet_address_list_destroy(iaddr_list);
      } else {
	*pa.author = 0;
      }

      /* Remove all leading spaces, and all leading instances of 
	 Re: from the subjects. */
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
	  pa.likely_response = 1;
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
	  strlen(original_message_id) > 0 &&
	  strstr(message_id, "gmane.org"))
	strncpy(pa.message_id, original_message_id, MAX_STRING_SIZE-1);
      else {
	char *suffix;
	strncpy(pa.message_id, message_id, MAX_STRING_SIZE-1);
	*(pa.message_id+MAX_STRING_SIZE) = 0;
	if ((suffix = strstr(pa.message_id, "__")) != NULL) {
	  char *at;
	  if ((at = strchr(suffix, '@'))) {
	    while (*at != 0)
	      *suffix++ = *at++;
	  }
	}
      }

      if (references == NULL) {
	if (g_mime_message_get_header(msg, "in-reply-to"))
	  pa.likely_response = 1;
	if (quoted_in_body_p(file_name))
	  pa.likely_response = 1;
      }
    
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

/* Convert a file name into a group/article spec. */
int path_to_article_wspec(const char *file_name, char *group, int *article) {
  char *s = news_spool;
  char *last_slash = NULL;
  char c;
  int art = 0;

  while (*s && *file_name && *s++ == *file_name++)
    ;

  if (*s || ! *file_name)
    return 0;

  /* It's common to forget to end the spool dir variable with a
     trailing slash, so we check for that here, and just ignore a
     leading slash in a group name. */
  if (*file_name == '/')
    file_name++;

  while ((c = *file_name++) != 0) {
    if (c == '/') {
      c = '.';
      last_slash = group;
    }

    *group++ = c;
  }

  *group++ = 0;

  if (! last_slash)
    return 0;

  *last_slash = 0;

  s = last_slash + 1;
  while ((c = *s++) != 0) {
    if ((c < '0') || (c > '9'))
      return 0;
    art = art * 10 + c - '0';
  }

  *article = art;

  return 1;
}

int thread_file(const char *file_name) {
  parsed_article *pa;
  node *tnode, *prev_node;
  char group_name[MAX_STRING_SIZE];
  int id, group_id, article, prev_instance;
  group *g;

  if (path_to_article_wspec(file_name, group_name, &article)) {
    g = get_group(group_name);
    if (prohibited_group_p(g)) 
      return 1;

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
    /* This variable is magically set by get_node(). */
    prev_instance = previous_instance_node;

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
	  printf("Not found '%s' '%s'\n", pa->message_id, 
		 pa->parent_message_id);
	} else {
	  printf("    Found '%s' '%s'\n", pa->message_id,
		 pa->parent_message_id);
	}
#endif
      } else if (pa->likely_response) {
	tnode->parent = get_parent_by_subject(pa->subject, group_id, pa->date);
      }
    
      /* Set next_instance in previous instances to point to us. */
      if (prev_instance) {
	prev_node = &nodes[prev_instance];
	prev_node->next_instance = id;
	write_node(prev_node);
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

void mmlock(void *area, int size) {
  if (mlock(area, size) == -1) {
    perror("weaver mlock");
    exit(1);
  }
}

void lock_and_uid(char *user) {
  struct passwd *pw;
  /*
  struct rlimit rlim;
  */

  pw = getpwnam(user);

  /*
  bzero(&rlim, sizeof(rlim));
  if (getrlimit(RLIMIT_MEMLOCK, &rlim) == -1) {
    perror("weaver");
  }
  printf("Limit for locking is %ld:%ld (%ld)\n", rlim.rlim_cur, rlim.rlim_max,
	 RLIM_INFINITY);
  rlim.rlim_cur = 2000000000;
  rlim.rlim_max = 2000000000;
  if (setrlimit(RLIMIT_MEMLOCK, &rlim) == -1) {
    perror("weaver");
  }

  if (mlockall(MCL_CURRENT) == -1) {
    perror("weaver");
    exit(1);
  }
  */

  if (mlockall(MCL_CURRENT) == -1) {
    perror("weaver");
    exit(1);
  }

  /*
  mmlock(groups, MAX_GROUPS * sizeof(group));
  mmlock(string_storage, string_storage_length);
  mmlock(nodes, nodes_length * sizeof(nodes));
  */

  setuid(pw->pw_uid);
  setgid(pw->pw_gid);
}

void read_conf_file(void) {
  FILE *conf = fopen("/etc/gmane.conf", "r");
  char buf[8192];
  char *string, *group_name, *description, *external_name, *other_names;
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
      external_name = NULL;
      other_names = NULL;
      strtok(buf, ":\n");
      while ((string = strtok(NULL, ":\n")) != NULL) {
	i++;
	if (i == 1)
	  group_name = string;
	else if (i == 3)
	  description = string;
	else if (i > 4 && strstr(string, "external=") == string)
	  external_name = string + strlen("external=");
	else if (i > 4 && strstr(string, "other-names=") == string)
	  other_names = string + strlen("other-names=");
      }

      if (group_name && description) {
	g = get_group(group_name);
	g->group_description = enter_string_storage(description);
	if (external_name) {
	  g->external_group_name = enter_string_storage(external_name);
	  enter_external_to_internal_group_name_map(external_name, group_name);
	}
	if (other_names) {
	  string = strtok(other_names, " ");
	  if (string) {
	    do {
	      enter_external_to_internal_group_name_map(string, group_name);
	    } while ((string = strtok(NULL, " ")) != NULL);
	  }
	}
      }
    }
  }

  alphabetize_groups();
}
