/*
gcc forkmain.c -o myprogram7 -lpthread

./myprogram7 /home/hana/Desktop/test/ hello


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
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>

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

typedef struct {
    int total_files_checked;
    int result_count;
    search_result results[MAX_THREADS];  // Adjust the size according to your needs
} shared_data;


const char *name = "shared_total"; // Name of the shared memory object
//const int SIZE = sizeof(int)*2; // Size of the shared memory object
//int *total_files_checked; // Pointer to the shared memory object
shared_data *data; // Pointer to the shared data structure


//search_result results[MAX_THREADS];
//int result_count = 0;
//int total_files_checked = 0;
pthread_mutex_t result_mutex;
pthread_mutex_t total_mutex;


void *search_file(void *args);
void process_directory(const char *path, const char *search_str);
void process_subdirectory(const char *path, const char *search_str);
void add_result(int line, int character, const char *file_path);
void initialize_shared_memory();

int main(int argc, char *argv[]) {

    initialize_shared_memory(); // Initialize shared memory and total


    if (argc != 3) {
        fprintf(stderr, "Usage: %s <directory> <string>\n", argv[0]);
        return EXIT_FAILURE;
    }

    pthread_mutex_init(&result_mutex, NULL);
    pthread_mutex_init(&total_mutex, NULL);

    process_directory(argv[1], argv[2]);

    printf("Total files checked: %d\n", data->total_files_checked);
    printf("Total results found: %d\n", data->result_count);
    printf("Occurrences of '%s':\n", argv[2]);
    for (int i = 0; i < data->result_count; i++) {
        printf("%s:%d:%d\n", data->results[i].file_path, data->results[i].line, data->results[i].character);
    }

    pthread_mutex_destroy(&result_mutex);
    pthread_mutex_destroy(&total_mutex);

    return EXIT_SUCCESS;
}

void process_directory(const char *path, const char *search_str) {
    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            char new_path[MAX_PATH];
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            snprintf(new_path, sizeof(new_path), "%s/%s", path, entry->d_name);

            pid_t pid = fork();
            if (pid == 0) { // Child process
                process_subdirectory(new_path, search_str);
                exit(EXIT_SUCCESS); // Child process exits after processing subdirectory
            } else if (pid > 0) { // Parent process
                // Optionally wait for child processes here
            } else {
                perror("fork");
            }
        }
    }

    while (wait(NULL) > 0);

   /* while (wait(NULL) > 0){
        printf("man\n");
    } // Wait for all child processes to finish*/

    closedir(dir);
}

void process_subdirectory(const char *path, const char *search_str) {
    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    pthread_t thread_ids[MAX_THREADS];
    int thread_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && thread_count < MAX_THREADS) {
            char file_path[MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s/%s", path, entry->d_name);
            file_thread_args *args = malloc(sizeof(file_thread_args));
            strcpy(args->file_path, file_path);
            args->search_str = strdup(search_str);

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


    // while ((read = getline(&line, &len, file)) != -1) {
    //         char *ptr = line;
    //         while ((ptr = strstr(ptr, args->search_str)) != NULL){
    //             // Execute regular expression
    //             reti = regexec(&regex, line, 0, NULL, 0);
    //             if (!reti) {
    //                 pthread_mutex_lock(&result_mutex);
    //                 if (result_count < MAX_THREADS) {
    //                     printf("%s:%d:%ld\n", args->file_path , line_number, ptr - line + 1);
    //                     add_result(line_number, ptr - line + 1, args->file_path); // Match found, character position set to 1 for simplicity
    //                 }
    //                 pthread_mutex_unlock(&result_mutex);
    //                 ptr++;
    //             }
    //         }
    //         line_number++;  
    //     }

       while ((read = getline(&line, &len, file)) != -1) {
        // Execute the regular expression on the current line
        reti = regexec(&regex, line, 0, NULL, 0);
        if (!reti) { // If match found
            pthread_mutex_lock(&result_mutex); // Lock the mutex to access shared resources
            if (data->result_count < MAX_THREADS) {
                //printf("%s:%d:%d\n", args->file_path , line_number, 1);
               // data->result_count++;
                add_result(line_number, 1, args->file_path); // Add the result (character position set to 1 for simplicity)
            }
            pthread_mutex_unlock(&result_mutex); // Unlock the mutex
        }
        line_number++;
    }




    fclose(file);
    free(line);
    regfree(&regex); // Free the compiled regular expression

    pthread_mutex_lock(&result_mutex);
    data->total_files_checked++;
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

// void add_result(int line, int character, const char *file_path) {
//     pthread_mutex_lock(&total_mutex);
//     if (result_count < MAX_THREADS) {
//         results[result_count].line = line;
//         results[result_count].character = character;
//         strcpy(results[result_count].file_path, file_path);
//         result_count++;
//     }
//    pthread_mutex_unlock(&total_mutex);
// }

void add_result(int line, int character, const char *file_path) {
    //pthread_mutex_lock(&result_mutex);
    if (data->result_count < MAX_THREADS) {
        data->results[data->result_count].line = line;
        data->results[data->result_count].character = character;
        strcpy(data->results[data->result_count].file_path, file_path);
        data->result_count++;
    }
    //pthread_mutex_unlock(&result_mutex);
}

void initialize_shared_memory() {
    // Create a shared memory object
    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

   // Configure the size of the shared memory object
    const int SIZE = sizeof(shared_data);  // Size based on the shared_data structure
    ftruncate(shm_fd, SIZE);


    // Memory map the shared memory object
    data = (shared_data*) mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    // Initialize total and resa to 0
    data->total_files_checked = 0;
    data->result_count = 0;
}