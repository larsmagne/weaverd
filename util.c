#define _LARGEFILE64_SOURCE

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include "config.h"
#include "util.h"
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

static unsigned int mem_used = 0;

/* The same as strcpy, but returns a pointer to the end of the
   destination string. */
char *mstrcpy(char *dest, char *src) {
  while ((*dest++ = *src++) != 0)
    ;
  return dest;
}


/* The same as strcpy, but returns a pointer to the end of the
   source string. */
char *sstrcpy(char *dest, char *src) {
  while ((*dest++ = *src++) != 0)
    ;
  return src;
}


/* Say whether a string is all-numerical. */
int is_number(const char *string) {
  while (*string)
    if (! isdigit(*string++)) 
      return 0;
  return 1;
}


/* Return the size of a file. */
loff_t file_size(int fd) {
  struct stat64 stat_buf;
  if (fstat64(fd, &stat_buf) == -1) {
    perror("Statting a file to find out the size");
    exit(1);
  }
  return stat_buf.st_size;
}


/* The same as malloc, but returns a char*, and clears the memory. */
char *cmalloc(size_t size) {
  char *b = (char*)malloc(size);
  if (b == NULL) {
    printf("Failed to allocate %ld bytes\n", size);
    size = 0;
    size = 4 / size;
  }
  mem_used += size;
  if (size > 1000000) {
    printf("Allocating %fM\n", (float)size/(1024*1024)); 
    printf("Allocated at %lx\n", (long)b);
  }
  bzero(b, size);
  return b;
}

void crfree(void *ptr, int size) {
  mem_used -= size;
  free(ptr);
}

void *crealloc(void *ptr, size_t size, size_t old_size) {
  void *result;
  mem_used -= old_size;
  mem_used += size;
  result = realloc(ptr, size);
  if (result == NULL) {
    printf("Failed to reallocate %ld bytes to %ld\n", old_size, size);
    size = 0;
    size = 4 / size;
  }
  return result;
}

void mem_usage(void) {
  printf("Used %dMiB\n", mem_used/(1024*1024));
}


/* Write a block to a file. */
int write_from(int fp, char *buf, int size) {
  int w = 0, written = 0;

  while (written < size) {
    if ((w = write(fp, buf + written, size - written)) < 0) {
      int err;
      perror("yes...");
      err = 0;
      size = 3 / err;
      merror("Writing a block");
    }

    written += w;
  }
  return written;
}


void merror(char *error) {
  perror(error);
  exit(1);
}


/* Read a block from a file into memory. */
void read_block_1(int fd, char *block, size_t block_size) {
  size_t rn = 0;
  int ret;
  
  while (rn < block_size) {
    ret = read(fd, block + rn, block_size - rn);
    if (ret == 0) {
      fprintf(stderr, "Reached end of file (block_size: %lx).\n", block_size);
      exit(1);
    } else if (ret == -1) {
      int size;
      printf("Reading into %lx\n", (long)block);
      size = 0;
      size = 4 / size;
    }
      
    rn += ret;
  }
}

void read_block(int fd, char *block, size_t block_size) {
  size_t read = 0, max_length = 0x1000000;

  do {
    size_t size = min(max_length, block_size);
    read_block_1(fd, block + read, size);
    read += size;
    block_size -= size;
  } while (block_size > 0);
}



/* Read a block from a file at a specified offset into a in-memory
   block. */
void read_into(int fd, int block_id, char *block, size_t block_size) {
  if (lseek64(fd, (loff_t)block_id * block_size, SEEK_SET) == -1) {
    merror("Seeking before reading a block");
  }

  read_block(fd, block, block_size);
}


size_t min (size_t one, size_t two) {
  if (one < two)
    return one;
  else
    return two;
}

