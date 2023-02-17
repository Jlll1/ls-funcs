#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <pthread.h>
#include "tree_sitter/api.h"

TSLanguage *tree_sitter_c();

#define TS_C_field_declarator 7
#define TS_C_field_type 23
#define NUM_THREADS 6

int32_t g_running = 1;
TSQuery *g_query;

char *
get_node_text(TSNode node, char *source, uint32_t *contents_length)
{
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  *contents_length = end - start + 1;
  char *contents = calloc(1, *contents_length);
  if (contents != NULL) {
    strncpy(contents, &source[start], *contents_length - 1);
  }

  return contents;
}


void
process_file(char *path)
{
  FILE *f = fopen(path, "r");
  if (f != NULL) {
    fseek(f, 0, SEEK_END);
    long file_length = ftell(f);
    rewind(f);

    size_t file_size = (file_length * sizeof(char)) + 1;
    char *contents = calloc(1, file_size);
    if (contents != NULL) {
      fread(contents, sizeof(char), file_length, f);

      TSParser *tsparser = ts_parser_new();
      ts_parser_set_language(tsparser, tree_sitter_c());
      TSTree *tree = ts_parser_parse_string(tsparser, NULL, contents, file_length);
      TSNode root = ts_tree_root_node(tree);

      TSQueryCursor *cursor = ts_query_cursor_new();
      ts_query_cursor_exec(cursor, g_query, root);

      TSQueryMatch match;
      while(ts_query_cursor_next_match(cursor, &match)) {
        for (int i = 0; i < match.capture_count; i++) {
          TSNode node = match.captures[i].node;

          TSNode type = ts_node_child_by_field_id(node, TS_C_field_type);
          uint32_t type_text_length;
          char *type_text = get_node_text(type, contents, &type_text_length);
          if (type_text != NULL) {
            printf("type: %s\n", type_text);
          }

          TSNode declarator = ts_node_child_by_field_id(node, TS_C_field_declarator);
          uint32_t declarator_text_length;
          char *declarator_text = get_node_text(declarator, contents, &declarator_text_length);
          if (declarator_text != NULL) {
            printf("declarator: %s\n\n", declarator_text);
          }

          free(type_text);
          free(declarator_text);
        }
      }

      ts_tree_delete(tree);
      ts_parser_delete(tsparser);
    }
    else {
      fprintf(stderr, "err: could not allocate memory to read the file %s\n", path);
    }

    free(contents);
  }
  else {
    fprintf(stderr, "err: could not open file: %s\n", path);
  }

  fclose(f);
}

typedef struct {
  char **data;
  int32_t count;
  int32_t size;
  pthread_mutex_t lock;
} FilesCollection;

void
push_file(FilesCollection *fc, char *path)
{
  pthread_mutex_lock(&fc->lock);
  if (fc->count + 1 > fc->size) {
    fc->size *= 2;
    fc->data = realloc(fc->data, fc->size * sizeof(char *));
    if (fc->data == NULL) {
      exit(1);
    }
  }
  fc->data[fc->count++] = path;
  pthread_mutex_unlock(&fc->lock);
}

char *
pop_file(FilesCollection *fc)
{
  pthread_mutex_lock(&fc->lock);
  char *data = NULL;
  if (fc->count > 0) {
    data = fc->data[--fc->count];
  }
  pthread_mutex_unlock(&fc->lock);

  return data;
}

void *
process_files_async(void *arg)
{
  FilesCollection *files = (FilesCollection *)arg;

  while (g_running) {
    char *path = pop_file(files);
    if (path != NULL) {
      process_file(path);
      free(path);
    }
  }

  pthread_exit(NULL);
}

int
main(int argc, char *argv[])
{
  uint32_t error_offset;
  TSQueryError errorType;
  char *query_text = "((function_definition) @func)";
  g_query = ts_query_new(tree_sitter_c(), query_text, strlen(query_text), &error_offset, &errorType);
  if (errorType != TSQueryErrorNone) {
    exit(1);
  }

  int32_t dirs_size = 192;
  int32_t dirs_count = 0;
  char **dirs = calloc(1, dirs_size * sizeof(char*));
  if (dirs == NULL) {
    exit(1);
  }
  dirs[dirs_count++] = strdup(".");

  FilesCollection files;
  files.count = 0;
  files.size = 256;
  files.data = calloc(1, files.size * sizeof(char*));
  if (files.data == NULL) {
    exit(1);
  }
  pthread_mutex_init(&files.lock, NULL);

  pthread_t threads[NUM_THREADS];
  int err = 0;
  for (int i = 0; i < NUM_THREADS; i++) {
    err = pthread_create(&threads[i], NULL, process_files_async, &files);
    if (err) {
      exit(1);
    }
  }

  while (dirs_count > 0) {
    char *path = dirs[--dirs_count];

    DIR *dir = opendir(path);
    if (dir == NULL) {
      fprintf(stderr, "err: could not open directory: %s\n", path);
      continue;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      size_t fullPathLength = strlen(entry->d_name) + strlen(path) + 2;
      char *fullPath = calloc(1, fullPathLength);
      if (fullPath == NULL) {
        fprintf(stderr, "err: could not allocate memory for file path\n");
        exit(1);
      }

      sprintf(fullPath, "%s/%s", path, entry->d_name);

      if (entry->d_type == DT_DIR) {
        if (dirs_count + 1 > dirs_size) {
          dirs_size *= 2;
          dirs = realloc(dirs, dirs_size * sizeof(char *));
          if (dirs == NULL) {
            fprintf(stderr, "err: could not allocate memory for directories\n");
            exit(1);
          }
        }
        dirs[dirs_count++] = fullPath;
      }
      else {
        char *ext = strrchr(fullPath, '.');
        if (ext != NULL && strcmp(ext, ".c") == 0) {
          push_file(&files, fullPath);
        }
      }
    }

    closedir(dir);
    free(path);
  }

  // wait for all files to be processed
  while (files.count > 0);
  g_running = 0;

  for (int i = 0; i < NUM_THREADS; i++) {
    err = pthread_join(threads[i], NULL);
    if (err) {
      fprintf(stderr, "err: failed joining thread\n");
    }
  }

  ts_query_delete(g_query);
}
