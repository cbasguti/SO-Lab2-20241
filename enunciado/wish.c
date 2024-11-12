#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>

typedef struct {
    char **data;
    int size;
    int capacity;
} Vector;

Vector PATH;

Vector create_vector() {
    Vector v;
    v.size = 0;
    v.capacity = 10;
    v.data = (char **)malloc(v.capacity * sizeof(char *));
    return v;
}

void vector_append(Vector *v, char *item) {
    if (v->size == v->capacity) {
        v->capacity *= 2;
        v->data = (char **)realloc(v->data, v->capacity * sizeof(char *));
    }
    v->data[v->size++] = item;  // Store the pointer, don't copy the string
}

char *vector_get(Vector *v, int index) {
    if (index < 0 || index >= v->size) {
        return NULL;
    }
    return v->data[index];
}

// Do not free individual strings within the vector to avoid double free
void vector_free(Vector *v) {
    free(v->data);  // Only free the array holding the pointers, not the strings themselves
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

void print_error() {
    fprintf(stderr, "An error has occurred\n");
}

int handle_builtin_commands(Vector items) {
    if (strcmp(vector_get(&items, 0), "exit") == 0) {
        if (items.size > 1) {
            // Print error message if there are any arguments passed to "exit"
            print_error();
            return 1;
        } else {
            // Exit the shell
            exit(0);
        }
    } 
    
    else if (strcmp(vector_get(&items, 0), "cd") == 0) {
        if (chdir(vector_get(&items, 1)) != 0) {
            print_error();
        }
        return 1;
    } 
    
    else if (strcmp(vector_get(&items, 0), "path") == 0) {
        if (items.size == 1) {
            vector_free(&PATH);  // Clear PATH
        } else {
            vector_free(&PATH);  // Clear the current PATH
            for (int i = 1; i < items.size; i++) {
                vector_append(&PATH, vector_get(&items, i));
            }
        }
        return 1;
    }

    return 0;
}

int handle_external_commands(Vector items) {
    char *command = vector_get(&items, 0);
    for (int i = 0; i < PATH.size; i++) {
        char *dir = (char *)malloc((strlen(vector_get(&PATH, i)) + strlen(command) + 2) * sizeof(char));
        snprintf(dir, strlen(vector_get(&PATH, i)) + strlen(command) + 2, "%s/%s", vector_get(&PATH, i), command);

        if (access(dir, X_OK) == 0) {
            // Prepare arguments for execv
            char *argv[items.size + 1];
            for (int j = 0; j < items.size; j++) {
                argv[j] = vector_get(&items, j);
            }
            argv[items.size] = NULL;

            pid_t pid = fork();
            if (pid == 0) {
                execv(dir, argv);
                print_error();
                exit(1);
            } else if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
            } else {
                print_error();
            }

            free(dir);
            return 1;
        }

        free(dir);
    }

    print_error();
    return 0;
}

int is_valid_redirection(Vector items) {
    int n = items.size;
    for (int i = 0; i < n; i++) {
        if (strcmp(">", vector_get(&items, i)) == 0) {
            if (i == 0 || i == n - 1 || n - 1 - i > 1) {
                return 0;
            }
            if (i != n - 1 && strcmp(">", vector_get(&items, i + 1)) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

Vector parse_input(char *expression) {
    Vector items = create_vector();
    int len = strlen(expression);
    char *s = NULL;
    int start = -1;
    for (int i = 0; i < len; i++) {
        if (expression[i] == '>') {
            vector_append(&items, ">");
            continue;
        }
        if (expression[i] == '&') {
            vector_append(&items, "&");
            continue;
        }
        if (!isspace(expression[i])) {
            if (i == 0 || isspace(expression[i - 1])) start = i;
            if (i == len - 1 || isspace(expression[i + 1])) {
                s = (char *)malloc((i - start + 2) * sizeof(char));
                strncpy(s, &expression[start], i - start + 1);
                s[i - start + 1] = '\0';
                vector_append(&items, s);
            }
        }
    }
    return items;
}

int main(int argc, char *argv[]) {
    int in_exec, full_cmd;
    Vector items, actual;
    char *expression = NULL;

    FILE *input_stream = NULL;

    // Verify input files
    for (int i = 1; i < argc; i++) {
        FILE *aux_file = fopen(argv[i], "r");
        if (aux_file == NULL) {
            print_error();
            exit(1);
        }

        if (input_stream == NULL) {
            input_stream = aux_file;
        } else {
            struct stat in_stat, aux_stat;
            fstat(fileno(input_stream), &in_stat);
            fstat(fileno(aux_file), &aux_stat);
            if (!(in_stat.st_dev == aux_stat.st_dev && in_stat.st_ino == aux_stat.st_ino)) {
                print_error();
                exit(1);
            }
        }
    }

    if (input_stream == NULL) {
        input_stream = stdin;
    }

    // Initializing PATH vector
    PATH = create_vector();
    vector_append(&PATH, "./");
    vector_append(&PATH, "/usr/bin/");
    vector_append(&PATH, "/bin/");

    while (1) {
        // Check if we are reading from a file or stdin
        if (input_stream == stdin) {
            printf("wish> ");
        }

        // Read input
        size_t n = 0;
        int in_len = getline(&expression, &n, input_stream);

        if (in_len == -1) {
            if (input_stream == stdin) {
                continue;
            } else {
                break;
            }
        }

        // Check for EOF
        if (feof(input_stream)) {
            break;
        }

        // Replace newline with NULL character
        expression[in_len - 1] = '\0';

        // Parse input into vector of items
        items = parse_input(expression);

        if (vector_get(&items, 0) == NULL) {
            vector_free(&items);  // Freeing vector in case of empty input
            continue;
        }

        actual = create_vector();

        int pids[items.size];
        int pid_count = 0;
        for (int i = 0; i < items.size; i++) {
            full_cmd = 0;
            if (strcmp(vector_get(&items, i), "&") == 0) {
                continue;
            } else {
                char *pos_act;
                while (!full_cmd && i < items.size) {
                    pos_act = vector_get(&items, i);
                    if (strcmp(pos_act, "&") != 0) {
                        vector_append(&actual, pos_act);
                        i++;
                    } else {
                        full_cmd = 1;
                    }
                }

                // Validate if the redirection is valid
                if (is_valid_redirection(actual)) {
                    // Attempt to execute a built-in command
                    in_exec = handle_builtin_commands(actual);
                    // If not a built-in command, attempt to execute external command
                    if (in_exec == 0) {
                        pids[pid_count] = handle_external_commands(actual);
                        pid_count++;
                    }
                } else {
                    print_error();
                }

                full_cmd = 0;
                vector_free(&actual);  // Free the actual vector after processing
                actual = create_vector();  // Reinitialize the actual vector
            }
        }

        // Wait for child processes to finish
        for (int i = 0; i < pid_count; i++) {
            waitpid(pids[i], NULL, 0);
        }

        free(expression);
        expression = NULL;
        vector_free(&items);  // Free items vector after each input
    }

    vector_free(&PATH);  // Free PATH vector when done
    return 0;
}
