/*
cd /home/hana/Desktop/code/

gcc correct.c -o myprogram -lpthread

./myprogram /home/hana/Desktop/Test/ hello

gcc correct.c -o myprogram -lpthread

./myprogram /home/hana/Desktop/Test/ "[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+m[a-zA-Z0-9.-]"

*/


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <regex.h> 


#define MAX_THREADS 256
#define MAX_PATH 1024

typedef struct {
    char file_path[MAX_PATH];
    char *search_str;
    int thread_num; // Added to keep track of the thread number
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

void *search_file(void *args);
void process_directory(const char *path, const char *search_str);
void add_result(int line, int character, const char *file_path);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <directory> <string>\n", argv[0]);
        return EXIT_FAILURE;
    }

    pthread_mutex_init(&result_mutex, NULL);
    process_directory(argv[1], argv[2]);

    printf("Total files checked: %d\n", total_files_checked);
    printf("Total results found: %d\n", result_count);
    printf("Occurrences of '%s':\n", argv[2]);
    for (int i = 0; i < result_count; i++) {
        printf("%s:%d:%d\n", results[i].file_path, results[i].line, results[i].character);
    }

    pthread_mutex_destroy(&result_mutex);
    return EXIT_SUCCESS;
}

void process_directory(const char *path, const char *search_str) {
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
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            snprintf(new_path, sizeof(new_path), "%s/%s", path, entry->d_name);
            process_directory(new_path, search_str); // Recursive call
        } else if (entry->d_type == DT_REG && thread_count < MAX_THREADS) {
            char file_path[MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s/%s", path, entry->d_name);
            file_thread_args *args = malloc(sizeof(file_thread_args));
            strcpy(args->file_path, file_path);
            args->search_str = strdup(search_str);
            args->thread_num = thread_count + 1; // Assign thread number
            if (pthread_create(&thread_ids[thread_count], NULL, search_file, args) == 0) {
                thread_count++;
            } else {
                perror("pthread_create");
                free(args->search_str);
                free(args);
            }
        }
    }

    for (int i = 0; i < thread_count; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    closedir(dir);
}


void *search_file(void *arg) {
    file_thread_args *args = (file_thread_args *)arg;
    struct timeval start_time, end_time;
    regex_t regex;
    int reti;

    // Compile the regular expression
    reti = regcomp(&regex, args->search_str, REG_EXTENDED);
    if (reti) {
        fprintf(stderr, "Could not compile regex\n");
        free(args->search_str);
        free(args);
        return NULL;
    }

    gettimeofday(&start_time, NULL); // Capture start time

    printf("Thread #%ld starting regex search in file: %s\n", (long)pthread_self(), args->file_path);

    FILE *file = fopen(args->file_path, "r");
    if (!file) {
        perror("fopen");
        regfree(&regex); // Free the compiled regular expression
        free(args->search_str);
        free(args);
        return NULL;
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int line_number = 1;

    while ((read = getline(&line, &len, file)) != -1) {
        // Execute regular expression
        reti = regexec(&regex, line, 0, NULL, 0);
        if (!reti) {
            pthread_mutex_lock(&result_mutex);
            if (result_count < MAX_THREADS) {
                add_result(line_number, 1, args->file_path); // Match found, character position set to 1 for simplicity
            }
            pthread_mutex_unlock(&result_mutex);
        }
        line_number++;
    }

    fclose(file);
    free(line);
    regfree(&regex); // Free the compiled regular expression

    pthread_mutex_lock(&result_mutex);
    total_files_checked++;
    pthread_mutex_unlock(&result_mutex);

    gettimeofday(&end_time, NULL); // Capture end time

    // Calculate processing time
    long seconds = end_time.tv_sec - start_time.tv_sec;
    long microseconds = end_time.tv_usec - start_time.tv_usec;
    double elapsed = seconds + microseconds*1e-6;

    printf("Thread #%ld finished regex search in file: %s in %.6f seconds.\n", (long)pthread_self(), args->file_path, elapsed);

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
