#ifndef CONFIG_H
#define CONFIG_H

#undef DEBUG


#define MAX_GROUPS (1024 * 16)
#define GROUP_FILE "groups.db"
#define NODE_FILE "nodes.db"
#define STRING_STORAGE_FILE "strings.db"
#define MAX_STRING_SIZE 10240
#define MAX_NODES 25000000
#define NEWS_SPOOL "/mirror/var/spool/news/articles/"
#define INDEX_DIR "/index/weave"

#define INITIAL_STRING_STORAGE_LENGTH (1024 * 1024 * 1100)
#define GROUP_TABLE_LENGTH MAX_GROUPS
#define INITIAL_NODE_LENGTH (1024 * 1024 * 21)

/* Must be powers of two. */
#define INITIAL_NODE_TABLE_LENGTH (1024 * 1024 * 32)
#define STRING_STORAGE_TABLE_LENGTH (1024 * 1024 * 64)


/* If you have a Linux with O_STREAMING, use the following define. */
#define O_STREAMING    04000000
/* If not, uncomment the following. */
/* #define O_STREAMING    0 */

#endif
