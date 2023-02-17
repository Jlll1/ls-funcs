#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include "tree_sitter/api.h"

TSLanguage *tree_sitter_c();

#define TS_C_field_declarator 7
#define TS_C_field_type 23

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

int
main(int argc, char *argv[])
{
  int32_t dirs_size = 192;
  int32_t dirs_count = 0;
  char **dirs = malloc(dirs_size * sizeof(char*));

  dirs[dirs_count++] = strdup("./test");

  TSParser *tsparser = ts_parser_new();
  ts_parser_set_language(tsparser, tree_sitter_c());

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
          FILE *f = fopen(fullPath, "r");
          if (f != NULL) {
            fseek(f, 0, SEEK_END);
            long file_length = ftell(f);
            rewind(f);

            size_t file_size = (file_length * sizeof(char)) + 1;
            char *contents = calloc(1, file_size);
            if (contents != NULL) {
              fread(contents, sizeof(char), file_length, f);
              char *query_text = "((function_definition) @func)";

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
            }
            else {
              fprintf(stderr, "err: could not allocate memory to read the file %s\n", fullPath);
            }

            free(contents);
          }
          else {
            fprintf(stderr, "err: could not open file: %s\n", fullPath);
          }

          free(fullPath);
          fclose(f);
        }
      }
    }

    closedir(dir);
    free(path);
  }

  ts_parser_delete(tsparser);
}
