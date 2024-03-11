#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define DELIMITERS " \t\r\n\a"
#define GREEN "\033[1;32m"
#define BLUE "\033[1;34m"
#define WHITE "\033[1;37m"
#define RED "\033[1;31m"
#define NOTHING -1
#define EXIT 0
#define SHELL_BUILTIN 1
#define EXECUTABLE_OR_ERROR 2
#define SHELL_LOG "Shell.log"

char input[MAX_INPUT_SIZE];
char *args[MAX_ARGS];
int input_type;
int background;

struct environmentVar {
    char *name;
    char *value;
};
struct  environmentVar env_vars[MAX_ARGS];

int parse_input();
void evaluate_expression();
void shell();
void execute_shell_builtin();
void execute_command();
void setup_environment();
void on_child_exit();

int main() {
    signal(SIGCHLD, on_child_exit);
    setup_environment();
    shell();
    return 0;
}

void on_child_exit() {
    int i = 0;
    while (waitpid(-1, NULL, WNOHANG) > 0) i++;
    // if (i == 0) return; // Uncomment this line if you want to ignore non zombie children
    // log the child termination
    char log_file_path[MAX_INPUT_SIZE];
    snprintf(log_file_path, sizeof(log_file_path), "%s/Desktop/%s", getenv("HOME"), SHELL_LOG);
    FILE *log_file = fopen(log_file_path, "a");
    if (log_file) {
        fprintf(log_file, "Child process terminated");
        fclose(log_file);
    }
}

int parse_input() {
    char *token;
    int i = 0;
    background = 0;
    token = strtok(input, DELIMITERS);
    while (token != NULL) {
        // check if the command is a background command
        if (strcmp(token, "&") == 0) {
            background = 1;
            break;
        }
        args[i++] = token;
        token = strtok(NULL, DELIMITERS);
    }
    args[i] = NULL;
    if (args[0] == NULL)
        return NOTHING;
    else if (strcasecmp(args[0], "exit") == 0)
        return EXIT;
    else if (strcmp(args[0], "cd") == 0 || strcmp(args[0], "export") == 0 || strcmp(args[0], "echo") == 0)
        return SHELL_BUILTIN;
    else 
        return EXECUTABLE_OR_ERROR;     
}

void evaluate_expression() {
    for (int i = 0; args[i] != NULL; i++) {
        // if the argument is an environment variable
        if (args[i][0] == '$') {
            char *env_var_name = args[i] + 1;
            char *env_var_value = NULL;
            for (int j = 0; j < MAX_ARGS; j++) {
                if (env_vars[j].name != NULL && strcmp(env_var_name, env_vars[j].name) == 0) {
                    env_var_value = env_vars[j].value;
                    break;
                }
            }
            if (env_var_value != NULL) {
                args[i] = env_var_value;
                // if the value of the variable contains spaces split it into multiple arguments
                if (strchr(env_var_value, ' ') != NULL) {
                    int j = i + 1;
                    while (args[j] != NULL) {
                        j++;
                    }
                    for (int k = j; k > i; k--) {
                        args[k] = args[k - 1];
                    }
                    char *token = strtok(env_var_value, " ");
                    while (token != NULL) {
                        args[i++] = token;
                        token = strtok(NULL, " ");
                    }
                }
            } else {
                // Variable not found in environment, make it an empty string
                args[i] = "";
            }
        }
    }
    // print the evaluated expression
    // for (int i = 0; args[i] != NULL; i++) printf("args[%d]=%s ", i, args[i]); printf("\n");
}

void execute_shell_builtin() {
    if (strcmp(args[0], "cd") == 0) {
        // if no argument is passed go to home
        if (args[1] == NULL) {
            chdir(getenv("HOME"));
        }
        // replace ~ with home
        else if (args[1][0] == '~') {
            char *home = getenv("HOME");
            char *new_path = malloc(strlen(home) + strlen(args[1]) + 1);
            strcpy(new_path, home);
            strcat(new_path, args[1] + 1);
            if (chdir(new_path) == -1) {
                printf("%sError: %s%s\n", RED, strerror(errno), WHITE);
            }
            free(new_path);
        } else{
            if (chdir(args[1]) == -1) {
                printf("%sError: %s%s\n", RED, strerror(errno), WHITE);
            }
        }
    }
    else if (strcmp(args[0], "echo") == 0) {
        for (int i = 1; args[i] != NULL; i++) {
            printf("%s ", args[i]);
        }
        printf("\n");
    } else if (strcmp(args[0], "export") == 0) {
        char *env_var_name = strtok(args[1], "=");
        char *env_var_value = strtok(NULL, "=");
        // if no value is passed
        if (env_var_value == NULL) {
            printf("%sError: Invalid syntax%s\n", RED, WHITE);
            return;
        }
        env_var_value = strdup(env_var_value);
        // if there is " in the start remove it
        bool is_quoted = false;
        if (env_var_value[0] == '"') {
            env_var_value = strdup(env_var_value + 1);
            is_quoted = true;
        }
        // if there are more than one word in the value
        if (is_quoted) {
            for (int i = 2; args[i] != NULL; i++) {
                strcat(env_var_value, " ");
                strcat(env_var_value, args[i]);
            }
        }
        // if there is " in the end remove it
        if (env_var_value[strlen(env_var_value) - 1] == '"') {
            env_var_value[strlen(env_var_value) - 1] = '\0';
        }
        // store the environment variable in the array
        for (int i = 0; i < MAX_ARGS; i++) {
            if (env_vars[i].name == NULL) {
                env_vars[i].name = strdup(env_var_name);
                env_vars[i].value = strdup(env_var_value);
                break;
            }
            // if the variable already exists update its value
            else if (strcmp(env_vars[i].name, env_var_name) == 0) {
                env_vars[i].value = strdup(env_var_value);
                break;
            }
        }
    }
    else execute_command();
}

void execute_command() {
    int pid = fork();
    if (pid == -1) {
        printf("%sError: %s%s\n", RED, strerror(errno), WHITE);
    } else if (pid == 0) {
        if (execvp(args[0], args) == -1) {
            // if the command is not found
            printf("%sError: %s%s\n", RED, strerror(errno), WHITE);
        }
        exit(0);
    } else if (background == 0) {
        // wait for the child to finish
        waitpid(pid, NULL, 0);
    }
}

void shell() {
    do {
        char *cwd = getcwd(NULL, 0);
        char *home = getenv("HOME");
        // replace home with ~
        if (strncmp(cwd, home, strlen(home)) == 0) {
            printf("%sShell@AbdElRahmanOsama%s:%s~%s%s$ ", GREEN, WHITE, BLUE, cwd + strlen(home), WHITE);
        } else {
            printf("%sShell@AbdElRahmanOsama%s:%s%s%s$ ", GREEN, WHITE, BLUE, cwd, WHITE);
        }
        free(cwd);
        fgets(input, MAX_INPUT_SIZE, stdin);
        input_type = parse_input();
        evaluate_expression();
        switch (input_type){
            case SHELL_BUILTIN:
                execute_shell_builtin();
                break;
            case EXECUTABLE_OR_ERROR:
                execute_command();
                break;
        }
    } while (input_type != EXIT);
}

void setup_environment() {
    // go to Desktop = cd ~/Desktop
    char *home = getenv("HOME");
    char *desktop = malloc(strlen(home) + strlen("/Desktop") + 1);
    strcpy(desktop, home);
    strcat(desktop, "/Desktop");
    chdir(desktop);
    free(desktop);
    // clear the screen
    int pid = fork();
    if (pid == -1) {
        printf("%sError: %s%s\n", RED, strerror(errno), WHITE);
    } else if (pid == 0) {
        char *clear[] = {"clear", NULL};
        if (execvp(clear[0], clear) == -1) {
            printf("%sError: %s%s\n", RED, strerror(errno), WHITE);
        }
        exit(0);
    } else {
        waitpid(pid, NULL, 0);
    }
    // create a log file and if it exists clear it
    FILE *log_file = fopen(SHELL_LOG, "w");
    if (log_file) {
        fclose(log_file);
    }
}