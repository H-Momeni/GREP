/*
cd /home/hana/Desktop/code/

gcc Main4.c -o myprogram -lpthread

./myprogram /home/hana/Desktop/Test/ hello


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
#include <gtk/gtk.h>


#define MAX_THREADS 256
#define MAX_PATH 1024



// Structure to pass arguments to file processing threads
typedef struct
{
    char file_path[MAX_PATH];
    char *search_str;
} file_thread_args;

// Structure to hold the search result
typedef struct
{
    int line;
    int character;
    char file_path[MAX_PATH];
} search_result;

// Global array to store results and its count
search_result results[MAX_THREADS];
int result_count = 0;
int total_files_checked = 0;
pthread_mutex_t result_mutex;

// Function prototypes
void *search_file(void *args);
void process_directory(const char *path, const char *search_str);
void add_result(int line, int character, const char *file_path);
void on_button_clicked(GtkWidget *widget, gpointer data);


int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <directory> <string>\n", argv[0]);
        return EXIT_FAILURE;
    }

    pthread_mutex_init(&result_mutex, NULL);
    process_directory(argv[1], argv[2]);

    printf("Total files checked: %d\n", total_files_checked);
    printf("Total resault found: %d\n", result_count);

    
    printf("Occurrences of '%s':\n", argv[2]);
    for (int i = 0; i < result_count; i++)
    {
        printf("%s:%d:%d\n", results[i].file_path, results[i].line, results[i].character);
    }

    pthread_mutex_destroy(&result_mutex);
    return EXIT_SUCCESS;
}

/*void process_directory(const char *path, const char *search_str)
{
    DIR *dir = opendir(path);
    if (!dir)
    {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    pthread_t thread_ids[MAX_THREADS];
    int thread_count = 0;
    while ((entry = readdir(dir)) != NULL && thread_count < MAX_THREADS)
    {
        if (entry->d_type == DT_DIR)
        {
            char new_path[MAX_PATH];
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }
            snprintf(new_path, sizeof(new_path), "%s/%s", path, entry->d_name);
            pid_t pid = fork();
            if (pid == 0)
            { // Child process
                process_directory(new_path, search_str);
                exit(EXIT_SUCCESS);
            }
            wait(NULL); // Parent waits for child process
        }
        else if (entry->d_type == DT_REG)
        {
            char file_path[MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s/%s", path, entry->d_name);
            file_thread_args *args = malloc(sizeof(file_thread_args));
            strcpy(args->file_path, file_path);
            args->search_str = strdup(search_str);
            if (pthread_create(&thread_ids[thread_count], NULL, search_file, args) != 0)
            {
                perror("pthread_create");
                free(args->search_str);
                free(args);
                continue;
            }
            thread_count++;
        }
    }

    for (int i = 0; i < thread_count; i++)
    {
        pthread_join(thread_ids[i], NULL);
    }

    closedir(dir);
}*/

void process_directory(const char *path, const char *search_str)
{
    DIR *dir = opendir(path);
    if (!dir)
    {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    pthread_t thread_ids[MAX_THREADS];
    int thread_count = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_DIR)
        {
            char new_path[MAX_PATH];
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }
            snprintf(new_path, sizeof(new_path), "%s/%s", path, entry->d_name);
            process_directory(new_path, search_str); // Recursive call
        }
        else if (entry->d_type == DT_REG && thread_count < MAX_THREADS)
        {
            char file_path[MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s/%s", path, entry->d_name);
            file_thread_args *args = malloc(sizeof(file_thread_args));
            strcpy(args->file_path, file_path);
            args->search_str = strdup(search_str);
            if (pthread_create(&thread_ids[thread_count], NULL, search_file, args) != 0)
            {
                perror("pthread_create");
                free(args->search_str);
                free(args);
                continue;
            }
            thread_count++;
        }
    }

    // Wait for all threads to complete
    for (int i = 0; i < thread_count; i++)
    {
        pthread_join(thread_ids[i], NULL);
    }

    closedir(dir);
}


void *search_file(void *arg)
{
    file_thread_args *args = (file_thread_args *)arg;
    FILE *file = fopen(args->file_path, "r");
    if (!file)
    {
        perror("fopen");
        free(args->search_str);
        free(args);
        return NULL;
    }
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int line_number = 1;
    while ((read = getline(&line, &len, file)) != -1)
    {
        char *ptr = line;
        while ((ptr = strstr(ptr, args->search_str)) != NULL)
        {
            pthread_mutex_lock(&result_mutex);
            if (result_count < MAX_THREADS)
            {
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

void add_result(int line, int character, const char *file_path)
{
    if (result_count < MAX_THREADS)
    {
        results[result_count].line = line;
        results[result_count].character = character;
        strcpy(results[result_count].file_path, file_path);
        result_count++;
    }
}
/*
### Key Changes Made:

1. **Thread Management**: 
   - Threads for file processing are now created and stored in an array. They are joined after all have been created, allowing for parallel processing within each directory.

2. **Resource Limits**:
   - Added a check to ensure `thread_count` does not exceed `MAX_THREADS`, preventing potential buffer overflow.

3. **Error Handling**:
   - Included error handling for `pthread_create`.

4. **Result Array Protection**:
   - Ensured that `result_count` is checked before adding results to prevent array overflow.

This revised version should handle parallel processing more effectively while respecting resource limits and ensuring thread safety. Make sure to test it thoroughly with different directory structures to validate its functionality.
*/