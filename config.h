#ifndef CONFIG_H
#define CONFIG_H

#undef DEBUG

#define SMALL

#define MAX_GROUPS (1024 * 64)
#define GROUP_FILE "groups.db"
#define NODE_FILE "nodes.db"
#define STRING_STORAGE_FILE "strings.db"
#define MAX_STRING_SIZE 10240
#define NEWS_SPOOL "/mirror/var/spool/news/articles/"
#define INDEX_DIR "/index/weave"

#ifdef SMALL
/* Must be powers of two. */
#define INITIAL_NODE_TABLE_LENGTH (1024 * 1024)
#define STRING_STORAGE_TABLE_LENGTH (1024 * 1024)

#define INITIAL_STRING_STORAGE_LENGTH ((unsigned int)1024 * 1024 * 24)
#define INITIAL_NODE_LENGTH (1024 * 1024)
#else
/* Must be powers of two. */
#define INITIAL_NODE_TABLE_LENGTH (1024 * 1024 * 256)
#define STRING_STORAGE_TABLE_LENGTH (1024 * 1024 * 512)

#define INITIAL_STRING_STORAGE_LENGTH ((unsigned int)1024 * 1024 * 4095) 
#define INITIAL_NODE_LENGTH (1024 * 1024 * 120)
#endif

#define GROUP_TABLE_LENGTH MAX_GROUPS

/* If you have a Linux with O_STREAMING, use the following define. */
#define O_STREAMING    04000000
/* If not, uncomment the following. */
/* #define O_STREAMING    0 */

#endif
