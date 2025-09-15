#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

void find(int fd, char *pathname, char *filename) {
    struct dirent de;
    struct stat st;
    char *buf, *p;
    int new_fd;

    if ((buf = (char *)malloc(sizeof(char) * 256)) == 0) {
        fprintf(2, "find: buf malloc failed\n");
        exit(1);
    }
    if (strlen(pathname) + 1 + DIRSIZ + 1 > 256) {
        printf("find: path %s too long\n", pathname);
        exit(0);
    }
    strcpy(buf, pathname);
    p = buf + strlen(pathname);
    *p++ = '/';
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)  {
            continue;
        }
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if (stat(buf, &st) < 0) {
            fprintf(2, "find: cannot stat %s\n", buf);
            continue;
        }
        if (st.type == T_FILE) {
            if (strcmp(p, filename) == 0) {
                printf("%s\n", buf);
            }
        } else if (st.type == T_DIR) {
            if ((new_fd = open(buf, O_RDONLY)) < 0) {
                fprintf(2, "find: cannot open %s\n", buf);
                exit(1); 
            }
            find(new_fd, buf, filename);
            close(new_fd);
        }
    }
    close(fd);
    free(buf);
}
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(2, "Usage: find PATHNAME FILENAME\n");
        exit(1);
    }
    char *pathname = argv[1];
    char *filename = argv[2];
    int fd;
    struct stat st;
    if ((fd = open(pathname, O_RDONLY)) < 0) {
        fprintf(2, "find: cannot open %s\n", pathname);
        exit(1);
    }
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", pathname);
        exit(1);
    }
    if (st.type != T_DIR) {
        fprintf(2, "find: %s is not directory\n", pathname);
        exit(1);
    }

    find(fd, pathname, filename);
    exit(0);
}