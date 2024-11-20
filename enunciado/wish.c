#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>

typedef struct
{
    char **data;
    int size;
    int capacity;
} Vector;

Vector PATH;

Vector create_vector()
{
    Vector v;
    v.size = 0;
    v.capacity = 10;
    v.data = (char **)malloc(v.capacity * sizeof(char *));
    return v;
}

void vector_append(Vector *v, char *item)
{
    if (v->size == v->capacity)
    {
        v->capacity *= 2;
        v->data = (char **)realloc(v->data, v->capacity * sizeof(char *));
    }
    v->data[v->size++] = item; // Store the pointer, don't copy the string
}

char *vector_get(Vector *v, int index)
{
    if (index < 0 || index >= v->size)
    {
        return NULL;
    }
    return v->data[index];
}

void vector_pop(Vector *v)
{
    if (v->size > 0)
    {
        v->size--; // Reducimos el tamaño del vector para "eliminar" el último elemento
    }
}

void vector_free(Vector *v)
{
    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

void print_error()
{
    fprintf(stderr, "An error has occurred\n");
}

int handle_builtin_commands(Vector items)
{
    if (strcmp(vector_get(&items, 0), "exit") == 0)
    {
        if (items.size > 1)
        {
            print_error();
            return 1;
        }
        else
        {
            exit(0);
        }
    }

    else if (strcmp(vector_get(&items, 0), "cd") == 0)
    {
        if (chdir(vector_get(&items, 1)) != 0)
        {
            print_error();
        }
        return 1;
    }

    else if (strcmp(vector_get(&items, 0), "path") == 0)
    {
        if (items.size == 1)
        {
            vector_free(&PATH); // Clear PATH
        }
        else
        {
            vector_free(&PATH); // Clear the current PATH
            for (int i = 1; i < items.size; i++)
            {
                vector_append(&PATH, vector_get(&items, i));
            }
        }
        return 1;
    }

    return 0;
}

int handle_external_commands(Vector items)
{
    char *command = vector_get(&items, 0);
    char *output_file = NULL;
    int redirection = 0;
    int background = 0;
    pid_t pid;

    // Buscar si hay redirección de salida (>)
    for (int i = 0; i < items.size; i++)
    {
        if (strcmp(">", vector_get(&items, i)) == 0 && i + 1 < items.size)
        {
            output_file = vector_get(&items, i + 1);
            redirection = 1;
            // Eliminar ">" y el archivo de la lista de items
            vector_pop(&items);
            vector_pop(&items);
            break;
        }
        else if (strcmp("&", vector_get(&items, i)) == 0)
        {
            background = 1;
            // Eliminar el "&" de la lista de items
            vector_pop(&items);
            break;
        }
    }

    // Buscar el comando en el PATH y ejecutarlo
    for (int i = 0; i < PATH.size; i++)
    {
        char *dir = (char *)malloc((strlen(vector_get(&PATH, i)) + strlen(command) + 2) * sizeof(char));
        snprintf(dir, strlen(vector_get(&PATH, i)) + strlen(command) + 2, "%s/%s", vector_get(&PATH, i), command);

        if (access(dir, X_OK) == 0)
        {
            // Preparamos los argumentos para execv
            char *argv[items.size + 1]; // Añadimos uno más para el NULL terminator
            for (int j = 0; j < items.size; j++)
            {
                argv[j] = vector_get(&items, j);
            }
            argv[items.size] = NULL; // Añadimos el terminador NULL

            pid = fork();
            if (pid == 0)
            {
                // Si hay redirección, redirigir la salida
                if (redirection)
                {
                    int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644); // Usar O_TRUNC para sobrescribir el archivo
                    if (fd == -1)
                    {
                        print_error();
                        exit(1);
                    }
                    dup2(fd, STDOUT_FILENO); // Redirigir STDOUT al archivo
                    close(fd);
                }

                // Ejecutar el comando
                execv(dir, argv);
                print_error(); // Si execv falla
                exit(1);
            }
            else if (pid > 0)
            {
                // Si el comando está en segundo plano, no esperamos aquí
                if (background)
                {
                    continue; // El proceso sigue ejecutándose en paralelo
                }
                else
                {
                    // Si es un proceso en primer plano, esperamos aquí
                    waitpid(pid, NULL, 0); // Esperar a que termine el proceso
                }
            }
            else
            {
                print_error(); // Error en fork
            }

            free(dir);
            return pid; // Devuelve el PID para procesos en segundo plano
        }

        free(dir);
    }

    print_error();
    return 0;
}

int is_valid_redirection(Vector items)
{
    int n = items.size;
    for (int i = 0; i < n; i++)
    {
        if (strcmp(">", vector_get(&items, i)) == 0)
        {
            if (i == 0 || i == n - 1 || n - 1 - i > 1)
            {
                return 0;
            }
            if (i != n - 1 && strcmp(">", vector_get(&items, i + 1)) == 0)
            {
                return 0;
            }
        }
    }
    return 1;
}

Vector parse_input(char *expression)
{
    Vector items = create_vector();
    int len = strlen(expression);
    char *s = NULL;
    int start = -1;

    for (int i = 0; i < len; i++)
    {
        if (expression[i] == '>')
        {
            vector_append(&items, ">");
            continue;
        }

        if (expression[i] == '&')
        {
            if (start != -1)
            { // Si hay un comando antes de '&', agregarlo primero
                s = (char *)malloc((i - start + 1) * sizeof(char));
                strncpy(s, &expression[start], i - start);
                s[i - start] = '\0';
                vector_append(&items, s);
                start = -1;
            }
            vector_append(&items, "&");
            continue;
        }

        if (!isspace(expression[i]))
        {
            if (start == -1)
            {
                start = i;
            }
            if (i == len - 1)
            { // Último carácter
                s = (char *)malloc((i - start + 2) * sizeof(char));
                strncpy(s, &expression[start], i - start + 1);
                s[i - start + 1] = '\0';
                vector_append(&items, s);
            }
        }
        else if (start != -1)
        {
            s = (char *)malloc((i - start + 1) * sizeof(char));
            strncpy(s, &expression[start], i - start);
            s[i - start] = '\0';
            vector_append(&items, s);
            start = -1;
        }
    }

    return items;
}

int main(int argc, char *argv[])
{
    int in_exec, full_cmd;
    Vector items, actual;
    char *expression = NULL;

    FILE *input_stream = NULL;

    // Verificación de archivos de entrada
    for (int i = 1; i < argc; i++)
    {
        FILE *aux_file = fopen(argv[i], "r");
        if (aux_file == NULL)
        {
            print_error();
            exit(1);
        }

        if (input_stream == NULL)
        {
            input_stream = aux_file;
        }
        else
        {
            struct stat in_stat, aux_stat;
            fstat(fileno(input_stream), &in_stat);
            fstat(fileno(aux_file), &aux_stat);
            if (!(in_stat.st_dev == aux_stat.st_dev && in_stat.st_ino == aux_stat.st_ino))
            {
                print_error();
                exit(1);
            }
        }
    }

    if (input_stream == NULL)
    {
        input_stream = stdin;
    }

    // Inicializando el vector PATH
    PATH = create_vector();
    vector_append(&PATH, "./");
    vector_append(&PATH, "/usr/bin/");
    vector_append(&PATH, "/bin/");

    pid_t background_pids[100]; // Array para almacenar los pids de procesos en segundo plano
    int bg_pid_count = 0;

    while (1)
    {
        // Check if we are reading from a file or stdin
        if (input_stream == stdin)
        {
            printf("wish> ");
        }

        // Leer entrada
        size_t n = 0;
        int in_len = getline(&expression, &n, input_stream);

        if (in_len == -1)
        {
            if (input_stream == stdin)
            {
                continue;
            }
            else
            {
                break;
            }
        }

        // Verificar EOF
        if (feof(input_stream))
        {
            break;
        }

        // Reemplazar salto de línea por carácter NULL
        expression[in_len - 1] = '\0';

        // Parsear la entrada en un vector de elementos
        items = parse_input(expression);

        if (vector_get(&items, 0) == NULL)
        {
            vector_free(&items); // Liberar vector en caso de entrada vacía
            continue;
        }

        actual = create_vector();
        int is_background = 0;
        if (strcmp(vector_get(&items, items.size - 1), "&") == 0)
        {
            is_background = 1;  // Marcar comando como en segundo plano
            vector_pop(&items); // Eliminar '&' del comando
        }

        int pids[items.size];
        int pid_count = 0;
        for (int i = 0; i < items.size; i++)
        {
            full_cmd = 0;
            if (strcmp(vector_get(&items, i), "&") == 0)
            {
                continue;
            }
            else
            {
                char *pos_act;
                while (!full_cmd && i < items.size)
                {
                    pos_act = vector_get(&items, i);
                    if (strcmp(pos_act, "&") != 0)
                    {
                        vector_append(&actual, pos_act);
                        i++;
                    }
                    else
                    {
                        full_cmd = 1;
                    }
                }

                // Validar si la redirección es válida
                if (is_valid_redirection(actual))
                {
                    // Intentar ejecutar un comando incorporado
                    in_exec = handle_builtin_commands(actual);
                    // Si no es un comando incorporado, intentar ejecutar un comando externo
                    if (in_exec == 0)
                    {
                        int pid = handle_external_commands(actual);
                        if (pid > 0 && is_background)
                        {
                            background_pids[bg_pid_count++] = pid;
                        }
                    }
                }
                else
                {
                    print_error();
                }

                full_cmd = 0;
                vector_free(&actual);     // Liberar vector después de procesar
                actual = create_vector(); // Reinitialize the actual vector
            }
        }

        // Esperar a que los procesos en segundo plano terminen
        for (int i = 0; i < bg_pid_count; i++)
        {
            waitpid(background_pids[i], NULL, 0);
        }

        free(expression);
        expression = NULL;
        vector_free(&items); // Liberar vector de elementos después de cada entrada
    }

    vector_free(&PATH); // Liberar vector PATH cuando termine
    return 0;
}

// EOF