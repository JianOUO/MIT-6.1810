#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAXCOMLEN 32
#define MAXARGLEN 32
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: xargs COMMAND\n");
        exit(1);
    }
    char command[MAXCOMLEN];
    char args[MAXARG - 1][MAXARGLEN];
    char *exec_args[MAXARG];
    int arg_count = 0;
    if (strlen(argv[1]) + 1 > MAXCOMLEN) {
        printf("xargs: command %s too long\n", argv[1]);
        exit(1);
    }
    strcpy(command, argv[1]);
    //fprintf(2, "command: %s\n", command);
    strcpy(args[arg_count++], command);
    for (int i = 2; i < argc; i++) {
        if (arg_count >= MAXARG) {
            fprintf(2, "xargs: too many args\n");
            exit(1);
        }
        if (strlen(argv[i]) + 1 > MAXARGLEN) {
            fprintf(2, "xargs: arg %s too long\n", argv[i]);
            exit(1);
        }
        strcpy(args[arg_count++], argv[i]);
        //fprintf(2, "arg%d: %s\n", arg_count - 1, args[arg_count - 1]);
    }
    char c;
    int arg_len = 0;
    while (read(0, &c, sizeof(char)) == sizeof(char)) {
        //fprintf(2, "c: %c\n", c);
        if (c == '\n') {
            if (fork() == 0) {
                args[arg_count][arg_len] = 0;
                for (int i = 0; i <= arg_count; i++) {
                    exec_args[i] = args[i];
                    //fprintf(2, "exec_args[%d]: %s\n", i, exec_args[i]);
                }
                exec_args[arg_count + 1] = 0;
                exec(command, exec_args);
            } else {
                arg_count = argc - 1;
                arg_len = 0;
                wait(0);
            }
        } else if (c == ' ') {
            if (arg_count + 1 >= MAXARG) {
                fprintf(2, "xargs: too many args\n");
                exit(1);
            }
            args[arg_count][arg_len] = 0;
            fprintf(2, "arg%d: %s\n", arg_count, args[arg_count]);
            arg_count++;
            arg_len = 0;
        } else {
            if (arg_len + 1 >= MAXARGLEN) {
                fprintf(2, "xargs: arg too long\n");
                exit(1);
            }
            args[arg_count][arg_len++] = c;
            //fprintf(2, "args[%d][%d] = %c\n", arg_count, arg_len - 1, c);
        }
    }
    exit(0);
}