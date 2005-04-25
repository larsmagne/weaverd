#ifndef UTIL_H
#define UTIL_H

#if defined(__FreeBSD__)
#define loff_t off_t
#endif
char *mstrcpy(char *dest, char *src);
char *sstrcpy(char *dest, char *src);
int is_number(const char *string);
loff_t file_size (int fd);
char *cmalloc(int size);
int write_from(int fp, char *buf, int size);
void merror(char *error);
void read_into(int fd, int block_id, char *block, int block_size);
void read_block(int fd, char *block, int block_size);
int path_to_article_spec(const char *file_name, char *group, int *article);
void *crealloc(void *ptr, size_t size, size_t old_size);
void crfree(void *ptr, int size);
void mem_usage(void);
int min(int, int);

extern char *news_spool;

#endif
