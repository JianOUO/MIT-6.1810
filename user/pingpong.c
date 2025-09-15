#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
     int p1[2];
     int p2[2];
     char byte[1];
     pipe(p1);
     pipe(p2);

     if (fork() == 0) {
        close(p1[1]);
        close(p2[0]);
        if (read(p1[0], byte, sizeof(byte)) < 1) {
            fprintf(3, "ping: read error\n");
            exit(1);
        }
        close(p1[0]);
        if (write(p2[1], "b", 1) < 1) {
            fprintf(3, "ping: write error\n");
            exit(1);
        }
        close(p2[1]);
        printf("%d: received ping\n", getpid());
        exit(0);
     } else {
        close(p1[0]);
        close(p2[1]);
        if (write(p1[1], "a", 1) < 1) {
            fprintf(3, "pong: write error\n");
            exit(1);
        }
        close(p1[1]);
        if (read(p2[0], byte, sizeof(byte)) < 1) {
            fprintf(3, "pong: read error\n");
            exit(1);
        }
        close(p2[1]);
        wait(0);
        printf("%d: received pong\n", getpid());
        exit(0);
     }
}