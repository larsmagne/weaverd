#ifndef CONFIG_H
#define CONFIG_H

#define MAX_GROUPS (1024 * 16)
#define GROUP_FILE "groups.db"
#define NODE_FILE "nodes.db"
#define STRING_STORAGE_FILE "strings.db"
#define MAX_STRING_SIZE 10240

#define INITIAL_STRING_STORAGE_LENGTH 1024
#define STRING_STORAGE_TABLE_LENGTH (1024 * 256)
#define GROUP_TABLE_LENGTH MAX_GROUPS

/* If you have a Linux with O_STREAMING, use the following define. */
#define O_STREAMING    04000000
/* If not, uncomment the following. */
/* #define O_STREAMING    0 */

#endif
