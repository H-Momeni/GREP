/*
cd /home/hana/Desktop/code/

gcc debug.c -o myprogram2 -lpthread

./myprogram2 /home/hana/Desktop/Test/ hello


*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#define MAX_THREADS 256
#define MAX_PATH 1024

typedef struct {
    char file_path[MAX_PATH];
    char *search_str;
} file_thread_args;

typedef struct {
    int line;
    int character;
    char file_path[MAX_PATH];
} search_result;

search_result results[MAX_THREADS];
int result_count = 0;
int total_files_checked = 0;
pthread_mutex_t result_mutex;

// Function prototypes
void *search_file(void *args);
void process_directory(const char *path, const char *search_str, int is_child);
void add_result(int line, int character, const char *file_path);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <directory> <string>\n", argv[0]);
        return EXIT_FAILURE;
    }

    pthread_mutex_init(&result_mutex, NULL);
    process_directory(argv[1], argv[2], 0);  // Main process

    printf("Total files checked: %d\n", total_files_checked);
    printf("Total result found: %d\n", result_count);
    printf("Occurrences of '%s':\n", argv[2]);
    for (int i = 0; i < result_count; i++) {
        printf("%s:%d:%d\n", results[i].file_path, results[i].line, results[i].character);
    }

    pthread_mutex_destroy(&result_mutex);
    return EXIT_SUCCESS;
}

void process_directory(const char *path, const char *search_str, int is_child) {
    if (is_child) {
        printf("Child process %d: %s\n", getpid(), path);
    }

    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    pthread_t thread_ids[MAX_THREADS];
    int thread_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            char new_path[MAX_PATH];
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            snprintf(new_path, sizeof(new_path), "%s/%s", path, entry->d_name);
            pid_t pid = fork();
            if (pid == 0) {
                process_directory(new_path, search_str, 1); // Child process
                exit(EXIT_SUCCESS);
            } else if (pid > 0) {
                wait(NULL); // Parent waits for child process
            } else {
                perror("fork");
            }
        } else if (entry->d_type == DT_REG && thread_count < MAX_THREADS) {
            char file_path[MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s/%s", path, entry->d_name);
            file_thread_args *args = malloc(sizeof(file_thread_args));
            strcpy(args->file_path, file_path);
            args->search_str = strdup(search_str);
            if (pthread_create(&thread_ids[thread_count], NULL, search_file, args) != 0) {
                perror("pthread_create");
                free(args->search_str);
                free(args);
                continue;
            }
            thread_count++;
        }
    }

    for (int i = 0; i < thread_count; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    closedir(dir);
}

void *search_file(void *arg) {
    printf("Thread %ld: %s\n", (long)pthread_self(), ((file_thread_args *)arg)->file_path);

    file_thread_args *args = (file_thread_args *)arg;
    FILE *file = fopen(args->file_path, "r");
    if (!file) {
        perror("fopen");
        free(args->search_str);
        free(args);
        return NULL;
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int line_number = 1;
    while ((read = getline(&line, &len, file)) != -1) {
        char *ptr = line;
        while ((ptr = strstr(ptr, args->search_str)) != NULL) {
            pthread_mutex_lock(&result_mutex);
            if (result_count < MAX_THREADS) {
                add_result(line_number, ptr - line + 1, args->file_path);
            }
            pthread_mutex_unlock(&result_mutex);
            ptr++;
        }
        line_number++;
    }

    pthread_mutex_lock(&result_mutex);
    total_files_checked++;
    pthread_mutex_unlock(&result_mutex);

    free(line);
    fclose(file);
    free(args->search_str);
    free(args);
    return NULL;
}

void add_result(int line, int character, const char *file_path) {
    if (result_count < MAX_THREADS) {
        results[result_count].line = line;
        results[result_count].character = character;
        strcpy(results[result_count].file_path, file_path);
        result_count++;
    }
}
