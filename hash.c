static char *string_storage = NULL;
static int string_storage_length = INITIAL_STRING_STORAGE_LENGTH;
static int next_string = 0;
static int string_storage_file = 0;

static int *string_storage_table = NULL;
static int string_storage_table_length = STRING_STORAGE_TABLE_LENGTH;

static int inhibit_file_write = 0;

#define HASH_GRANULARITY 1024

/* one_at_a_time_hash() */

unsigned int hash(const char *key, unsigned int len, unsigned int table_length)
{
  unsigned int   hash, i;
  for (hash=0, i=0; i<len; ++i)
  {
    hash += key[i];
    hash += (hash << 10);
    hash ^= (hash >> 6);
  }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return (hash & (table_length - 1));
} 

unsigned int get_group_id(const char *group) {
  
}

unsigned int enter_string_storage(const char *string) {
  int string_length = strlen(string);
  int search = hash(string, string_length, string_storage_table_length);
  int offset;
  char *candidate;

  while (true) {
    offset = string_storage_table[search];
    if (! offset)
      break;
    else if (offset && 
	     ! strcmp(string, *(string_storage + offset)))
      break;
    if (search++ >= string_storage_table_length)
      search = 0;
  }

  if (! offset) {
    strcpy((string_storage + next_string), string);
    if (! inhibit_file_write)
      fwrite(string, string_length + 1, 1, string_storage_file);
    string_storage_table[search] = next_string;
    offset = next_string;
    next_string += string_length + 1;
    
    if (next_string > string_storage_length)
      extend_string_storage();
  }

  return offset;
}

void populate_string_table_from_file(int *fd) {
  loff_t file_size = file_size(string_storage_file);
  loff_t bytes_read = 0;
  char *buffer[MAX_STRING_SIZE], *c;

  inhibit_file_write = 1;

  while (bytes_read < file_size) {
    c = buffer;
    while (read64(fd, *c, 1)) {
      if (*c == 0) 
	break;
      c++;
    }
    enter_string_storage(buffer);
  }

  inhibit_file_write = 0;
}

void init (void) {
  string_storage = cmalloc(string_storage_length);
  string_storage_table = cmalloc(STRING_STORAGE_TABLE_LENGTH * sizeof(int));

  if ((instance_file = open64(STRING_STORAGE_INSTANCE_FILE,
			      O_RDWR|O_CREAT, 0644)) == -1)
    merror("Opening the string storage file.");

  populate_string_table_from_file(instance_file);
  
}

