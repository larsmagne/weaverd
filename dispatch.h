#ifndef DISPATCH_H
#define DISPATCH_H

#define MAX_ARGUMENTS 10

#define STRING 1
#define INT 2
#define EOA 0

typedef struct {
  char *command;
  void (*handler)();
  int type[MAX_ARGUMENTS];
} dispatcher;

#endif
