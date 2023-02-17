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
      char *query_text = "((function_definition) @func)";

      TSParser *tsparser = ts_parser_new();
      ts_parser_set_language(tsparser, tree_sitter_c());
      TSTree *tree = ts_parser_parse_string(tsparser, NULL, contents, file_length);
      TSNode root = ts_tree_root_node(tree);

      uint32_t error_offset;
      TSQueryError errorType;
      TSQuery *query = ts_query_new(tree_sitter_c(), query_text, strlen(query_text), &error_offset, &errorType);
      if (errorType == TSQueryErrorNone) {
        TSQueryCursor *cursor = ts_query_cursor_new();
        ts_query_cursor_exec(cursor, query, root);

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
      }

      ts_query_delete(query);
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
} FilesCollection;

int32_t running = 1;

void *
process_files_async(void *arg)
{
  FilesCollection *files = (FilesCollection *)arg;

  while (running) {
    if (files->count > 0) {
      char *path = files->data[--files->count];
      process_file(path);
      free(path);
    }
  }

  pthread_exit(NULL);
}


int
main(int argc, char *argv[])
{
  int32_t dirs_size = 192;
  int32_t dirs_count = 0;
  char **dirs = calloc(1, dirs_size * sizeof(char*));
  if (dirs == NULL) {
    exit(1);
  }
  dirs[dirs_count++] = strdup(".");

  int32_t files_size = 256;
  char **fs = calloc(1, files_size * sizeof(char*));
  if (fs == NULL) {
    exit(1);
  }
  FilesCollection files = { fs, 0 };

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
          if (files.count + 1 > files_size) {
            files_size *= 2;
            files.data = realloc(files.data, files_size * sizeof(char *));
            if (files.data == NULL) {
              fprintf(stderr, "err: could not allocate memory for directories\n");
              exit(1);
            }
          }
          files.data[files.count++] = fullPath;
        }
      }
    }

    closedir(dir);
    free(path);
  }

  // wait for all files to be processed
  while (files.count > 0);
  running = 0;

  for (int i = 0; i < NUM_THREADS; i++) {
    err = pthread_join(threads[i], NULL);
    if (err) {
      fprintf(stderr, "err: failed joining thread\n");
    }
  }
}
