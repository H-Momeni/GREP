/*
unset GTK_PATH
gcc `pkg-config --cflags gtk+-3.0` -o GUI GUI.c `pkg-config --libs gtk+-3.0`
./GUI
*/

#define _GNU_SOURCE
#include <gtk/gtk.h>
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

// Global widgets
GtkWidget *window;
GtkWidget *select_button, *start_button;
GtkWidget *regex_entry, *results_text;
GtkFileChooserDialog *dialog;
char *folder_path = NULL; // Global variable to store the folder path

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
shared_data *data; // Pointer to the shared data structure
int proccessid[30]={0};

pthread_mutex_t result_mutex;

void *search_file(void *args);
void process_directory(const char *path, const char *search_str);
void process_subdirectory(const char *path, const char *search_str);
void add_result(int line, int character, const char *file_path);
void initialize_shared_memory();
void on_select_button_clicked(GtkWidget *widget, gpointer data);
void on_start_button_clicked(GtkWidget *widget, gpointer data);
void display_results();



void create_gui() {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "GTK+ Application");
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 200);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    select_button = gtk_button_new_with_label("Select Directory");
    g_signal_connect(select_button, "clicked", G_CALLBACK(on_select_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), select_button, FALSE, FALSE, 0);

    regex_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(regex_entry), "Enter regex pattern");
    gtk_box_pack_start(GTK_BOX(vbox), regex_entry, FALSE, FALSE, 0);

    start_button = gtk_button_new_with_label("Start Search");
    g_signal_connect(start_button, "clicked", G_CALLBACK(on_start_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), start_button, FALSE, FALSE, 0);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scrolled_window, 400, 200); // Set a size for the scrolled window

    results_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(results_text), FALSE); // Make the text view non-editable
    gtk_container_add(GTK_CONTAINER(scrolled_window), results_text);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
}

int main(int argc, char *argv[]) {

    initialize_shared_memory(); // Initialize shared memory and total

    gtk_init(&argc, &argv);

    create_gui();

    gtk_main();

    printf("-----------------------------------------------------Child Id:\n");
    int counter=0;
    while(proccessid[counter]!=0){
        printf("Child processID %d\n", proccessid[counter]);
        counter++;
    }
    printf("-----------------------------------------------------results:\n");

    printf("Total files checked: %d\n", data->total_files_checked);
    printf("Total results found: %d\n\n", data->result_count);
    printf("Occurrences of '%s':\n", argv[2]);


    for (int i = 0; i < data->result_count; i++) {
        printf("%s:%d:%d\n", data->results[i].file_path, data->results[i].line, data->results[i].character);
    }
    printf("\n");

    pthread_mutex_destroy(&result_mutex);


    

    return 0;
}


void process_directory(const char *path, const char *search_str) {
    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return;
    }
    int num=0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            char new_path[MAX_PATH];
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            snprintf(new_path, sizeof(new_path), "%s/%s", path, entry->d_name);

            pid_t pid = fork();
            if (pid == 0) { // Child process
                //printf("Child process %d: %s\n", getpid(), new_path);
               
                process_subdirectory(new_path, search_str);
                exit(EXIT_SUCCESS); // Child process exits after processing subdirectory
            } else if (pid > 0) { // Parent process
                // Optionally wait for child processes here
                 proccessid[num]=pid;
                num++;
            } else {
                perror("fork");
            }
        }
    }

    while (wait(NULL) > 0);


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
    regmatch_t pmatch[1]; // Array to hold the match positions

    while ((read = getline(&line, &len, file)) != -1) {
        int offset = 0; // Offset to start searching from in the line

        // Use a loop to find multiple occurrences in the same line
        while (regexec(&regex, line + offset, 1, pmatch, 0) == 0) {
            // The position of the match is relative to the current offset
            int match_start = pmatch[0].rm_so + offset;
            int match_end = pmatch[0].rm_eo + offset;

            pthread_mutex_lock(&result_mutex); // Lock the mutex to access shared resources
            if (data->result_count < MAX_THREADS) {
                add_result(line_number, match_start + 1, args->file_path); // Add the result
            }
            pthread_mutex_unlock(&result_mutex); // Unlock the mutex

            // Update offset to search for next occurrence, avoid infinite loop for zero-length matches
            offset = match_end > match_start ? match_end : match_end + 1;

            // Break if the end of the line has been reached
            if (line[offset] == '\0') break;
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



void add_result(int line, int character, const char *file_path) {
    
    if (data->result_count < MAX_THREADS) {
        data->results[data->result_count].line = line;
        data->results[data->result_count].character = character;
        strcpy(data->results[data->result_count].file_path, file_path);
        data->result_count++;
    }
   
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

// Callback for the "clicked" signal of the select button
void on_select_button_clicked(GtkWidget *widget, gpointer data) {
    dialog = GTK_FILE_CHOOSER_DIALOG(gtk_file_chooser_dialog_new("Select Folder", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL));

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        if (folder_path) {
            g_free(folder_path);
        }
        folder_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        // if (folder_path) {  // Check if folder_path is not NULL
        //     printf("%s\n", folder_path);  // Print the folder path
        //     fflush(stdout);  // Flush the output buffer to ensure it's displayed immediately
        // } else {
        //     printf("No folder was selected or an error occurred.\n");
        //     fflush(stdout);
        // }
        gtk_widget_destroy(GTK_WIDGET(dialog));
    } else {
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}


void on_start_button_clicked(GtkWidget *widget, gpointer data) {
    const char *regex_pattern = gtk_entry_get_text(GTK_ENTRY(regex_entry));
    if (folder_path && regex_pattern) {
        process_directory(folder_path, regex_pattern);
        display_results(); // Update the GUI with the search results
    }
}

void display_results() {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(results_text));
    GtkTextIter iter;

    // Clear the existing text in the buffer
    gtk_text_buffer_set_text(buffer, "", -1);
    gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);

    // Format the "Total files checked" string and insert it into the buffer
    char total_files_checked_str[256]; // Adjust the size based on the expected length of the string
    snprintf(total_files_checked_str, sizeof(total_files_checked_str), "Total files checked: %d\n", data->total_files_checked);
    gtk_text_buffer_insert(buffer, &iter, total_files_checked_str, -1);

    char result_count_str[256]; // Adjust the size based on the expected length of the string
    snprintf(result_count_str, sizeof(result_count_str), "Total results found: %d\n\n",  data->result_count);
    gtk_text_buffer_insert(buffer, &iter, result_count_str, -1);

    gtk_text_buffer_insert(buffer, &iter, "Results:\n", -1);
    // Format and insert each result into the buffer
    char result_str[1024]; // Adjust the size based on the expected length of result strings
    for (int i = 0; i < data->result_count; i++) {
        snprintf(result_str, sizeof(result_str), "%s:%d:%d\n", data->results[i].file_path, data->results[i].line, data->results[i].character);
        gtk_text_buffer_insert(buffer, &iter, result_str, -1);
    }

    
     for (int i = 0; i < data->result_count; i++) {
        // If file_path is dynamically allocated, free it first
        // If it's a fixed-size char array, you can skip this step or set it to an empty string
        // free(data->results[i].file_path);  // Uncomment if file_path is dynamically allocated

        // Reset the elements
        data->results[i].line = 0;
        data->results[i].character = 0;
        data->results[i].file_path[0] = '\0'; // Assuming file_path is a char array, set the first character to null terminator to make it an empty string
    }

    // Reset the total files checked and result count
    data->total_files_checked = 0;
    data->result_count = 0;
}


