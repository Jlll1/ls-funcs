#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>

int
main(int argc, char *argv[])
{
  int32_t dirs_size = 192;
  int32_t dirs_count = 0;
  char **dirs = malloc(dirs_size * sizeof(char*));

  dirs[dirs_count++] = strdup(".");

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
        printf("%s\n", fullPath);
        free(fullPath);
      }
    }

    closedir(dir);
    free(path);
  }
}
