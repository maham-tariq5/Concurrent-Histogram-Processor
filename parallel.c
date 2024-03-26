#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>


#define BUFFER_SIZE 1000 // using for testing
#define MAX_FILES 100 // used for testing


// prototypes for two helper functions
void sigchld(int sig);
int *Histogram(char *Data, int Size);


// global variables of the program
int pipes[MAX_FILES][2]; // 2D array for pipes
int pids[MAX_FILES];
int numChildren = 0; // keeping track of the # of children
int numTerminated = 0;  // keeping track of termination

void sigchld(int sig) {
   
    // all variables used 
    int child_status;
    int characterCounts[26];
    char filename[BUFFER_SIZE];
    char line[BUFFER_SIZE];
    pid_t child_pid;

    // Process all terminated children without blocking
    while ((child_pid = waitpid(-1, &child_status, WNOHANG)) > 0) {
        printf("Parent caught SIGCHLD from child process %d.\n", child_pid);
        numTerminated++;

        // Check if the child exited normally
        if (WIFEXITED(child_status)) {
            int curPipe = -1; // Initialize to an invalid value

            // Find the pipe associated with the exited child process
            for (int i = 0; i < MAX_FILES; i++) {
                if (child_pid == pids[i]) {
                    curPipe = i;
                    break; // Exit the loop once the correct pipe is found
                }
            }

            if (curPipe != -1) { // Check if a valid pipe was found
                // Read the histogram from the pipe into memory
                ssize_t bytesRead = read(pipes[curPipe][0], characterCounts, sizeof(characterCounts));
                if (bytesRead > 0) {
                    printf("Parent read histogram from pipe %d ", curPipe);

                    // Prepare the filename and open the file
                    sprintf(filename, "file%d.hist", child_pid);
                    int fd = open(filename, O_CREAT | O_WRONLY, 0644);
                    if (fd == -1) {
                        perror("Error opening file");
                        exit(EXIT_FAILURE);
                    }

                    // Write character counts to the file
                    for (char letter = 'a'; letter <= 'z'; letter++) {
                        int count = characterCounts[letter - 'a'];
                        sprintf(line, "%c=%d\n", letter, count);
                        write(fd, line, strlen(line));
                    }
                    close(fd);
                    printf("and saved to file %s.\n", filename);
                }
                close(pipes[curPipe][0]); // Close the read end of the pipe
            } else {
                printf("Error: Pipe for child %d not found.\n", child_pid);
            }
        } else if (WIFSIGNALED(child_status)) {
            printf("Child %d terminated abnormally.\n", child_pid);
        }
    }

    // Re-register the signal handler
    if (signal(SIGCHLD, sigchld) == SIG_ERR) {
        perror("Error re-registering SIGCHLD handler");
        exit(EXIT_FAILURE);
    }
}


int *Histogram(char *Data, int Size) {
    
    int *histogram = (int *)malloc(26 * sizeof(int));
    if (histogram == NULL) {
        // Handle memory allocation failure
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    // Initialize the histogram array to zero
    for (int i = 0; i < 26; i++) {
        histogram[i] = 0;
    }

    for (int i = 0; i < Size; i++) {
        char c = Data[i];
        if (isalpha(c)) {
            // Convert character to lowercase and increment the corresponding histogram entry
            c = tolower(c);
            histogram[c - 'a']++;
        }
    }

    // Return the pointer to the histogram array
    return histogram;
}


int main(int argc, char *argv[]) {
    
     printf("Starting program. Number of files provided: %d\n", argc - 1);

    if (argc == 1) {
        printf("Error: No input files provided.\n");
        exit(EXIT_FAILURE);
    }

    if (argc > MAX_FILES + 1) {
        printf("Error: Too many input files provided. Maximum allowed is %d.\n", MAX_FILES);
        exit(EXIT_FAILURE);
    }

    printf("Registering SIGCHLD handler...\n");
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld; // Ensure this matches your handler's name
    sigaction(SIGCHLD, &sa, NULL);

    for (int i = 1; i < argc; i++) {
        printf("Processing file/command %s...\n", argv[i]);

        if (pipe(pipes[i - 1]) < 0) {
            perror("Error creating pipe");
            exit(EXIT_FAILURE);
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("Error forking child process");
            exit(EXIT_FAILURE);
        } else if (pid == 0) { // Child process
            printf("Child process started for %s\n", argv[i]);
            close(pipes[i - 1][0]); // Close read end

            if (strcmp(argv[i], "SIG") != 0) {
                printf("Opening file: %s\n", argv[i]);
                int fileDescriptor = open(argv[i], O_RDONLY);
                if (fileDescriptor < 0) {
                    fprintf(stderr, "Error opening file %s. Exiting with 1.\n", argv[i]);
                    close(pipes[i - 1][1]); // Close write end before exiting
                    exit(1); // Exit with value 1
                }

                off_t fileSize = lseek(fileDescriptor, 0, SEEK_END);
                char *fileData = (char *)malloc(fileSize);
                if (!fileData) {
                    perror("Failed to allocate memory for file data");
                    close(pipes[i - 1][1]); // Close write end before exiting
                    exit(1); // Adjusted to exit with 1
                }

                printf("Reading file: %s\n", argv[i]);
                lseek(fileDescriptor, 0, SEEK_SET);
                read(fileDescriptor, fileData, fileSize);
                close(fileDescriptor);

                printf("Calculating histogram for file: %s\n", argv[i]);
                int *calcHisto = Histogram(fileData, fileSize);
                write(pipes[i - 1][1], calcHisto, 26 * sizeof(int));
                free(calcHisto);
                free(fileData);

                // Adjust sleep time 
                printf("Child process sleeping for %d seconds.\n", 10 + 3 * (i - 1));
                sleep(10 + 3 * (i - 1)); // Sleep for 10+3*i seconds

                printf("Child process completed for %s. Exiting with 0.\n", argv[i]);
                close(pipes[i - 1][1]); // Close write end after all operations
                exit(0); // 
            } else {
                printf("Child process (PID: %d) waiting for signal.\n", getpid());
                sleep(10); // Wait longer to ensure parent has time to send signal
            }
        } else { // Parent process
            printf("Parent process created child with PID: %d for %s\n", pid, argv[i]);
            close(pipes[i - 1][1]); // Close write end
            pids[i - 1] = pid;
            numChildren++;

            if (strcmp(argv[i], "SIG") == 0) {
                printf("Parent sending SIGINT to child %d\n", pid);
                kill(pid, SIGINT);
            }
        }
    }

    printf("Waiting for all child processes to terminate...\n");
    while (numTerminated < numChildren) {
        sleep(1); // Wait for SIGCHLD signals
    }

    printf("All child processes have terminated.\n");

    return 0;

}

