#include <stdbool.h>
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

ssize_t read_line(int fd, char *buf, size_t maxlen);
void print_bytes(int fd, int count);
void process_line(const char *line);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <server_pid> <username>\n", argv[0]);
        return 1;
    }

    int server_pid = atoi(argv[1]);
    if (server_pid <= 0) {
        fprintf(stderr, "Invalid server PID: %s\n", argv[1]);
        return 1;
    }
    char *username = argv[2];

    // block SIGRTMIN + 1
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGRTMIN + 1);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        perror("sigprocmask");
        exit(1);
    }

    // send sigrtmin to server
    if (kill(server_pid, SIGRTMIN) < 0) {
        perror("kill(SIGRTMIN)");
        exit(1);
    }

    // wait sigrtmin + 1
    int sig;
    if (sigwait(&mask, &sig) != 0) {
        fprintf(stderr, "sigwait failed\n");
        return EXIT_FAILURE;
    }

    // create fifos path
    char fifo_c2s[256];
    char fifo_s2c[256];
    snprintf(fifo_c2s, sizeof(fifo_c2s), "FIFO_C2S_%d", getpid());
    snprintf(fifo_s2c, sizeof(fifo_s2c), "FIFO_S2C_%d", getpid());

    // open fifos
    int fd_c2s = open(fifo_c2s, O_WRONLY);
    int fd_s2c = open(fifo_s2c, O_RDONLY);
    if (fd_c2s == -1 || fd_s2c == -1) {
        perror("error open fifos");
        printf("FIFO_C2S: %s, FIFO_S2C: %s\n", fifo_c2s, fifo_s2c);
        return 1;
    }

    // send username to server
    if (write(fd_c2s, username, strlen(username)) < 0 ||
        write(fd_c2s, "\n", 1) < 0) {
        perror("write(username)");
        close(fd_c2s);
        close(fd_s2c);
        return 1;
    }

    char linebuf[256];
    ssize_t n;
    int idx = 0;
    char ch;

    while ((n = read(fd_s2c, &ch, 1)) == 1
       && ch != '\n'
       && idx < (int)sizeof(linebuf) - 1) {
        linebuf[idx++] = ch;
    }

    linebuf[idx] = '\0';
    if (idx == 0) {
        perror("failed to read/rejection from server.\n");
        close(fd_c2s);
        close(fd_s2c);
        return 1;
    }
    //check rejection
    if (strncmp(linebuf, "Reject UNAUTHORISED", 19) == 0) {
        printf("Reject UNAUTHORISED\n");
        close(fd_c2s);
        close(fd_s2c);
        return 1;
    }
    printf("Server response: %s\n", linebuf);

    // get version
    char versionbuf[32];
    idx = 0;
    while ((n = read(fd_s2c, &ch, 1)) == 1 && ch != '\n' && idx < (int)sizeof(versionbuf) - 1) {
        versionbuf[idx++] = ch;
    }
    versionbuf[idx] = '\0';
    if(idx == 0) {
        perror("failed to read version from server.\n");
        close(fd_c2s);
        close(fd_s2c);
        return 1;
    }

    // get doc length
    char lengthbuf[32];
    idx = 0;
    while ((n = read(fd_s2c, &ch, 1)) == 1 && ch != '\n' && idx < (int)sizeof(lengthbuf) - 1) {
        lengthbuf[idx++] = ch;
    }
    lengthbuf[idx] = '\0';
    if(idx == 0) {
        perror("failed to read length from server.\n");
        close(fd_c2s);
        close(fd_s2c);
        return 1;
    }
    int doc_length = atoi(lengthbuf);

    // read document content
    char *doc = malloc(doc_length + 1);
    if(!doc) {
        perror("malloc");
        close(fd_c2s);
        close(fd_s2c);
        return 1;
    }

    ssize_t total_read = 0;
    while (total_read < doc_length && 
        (n = read(fd_s2c, doc + total_read, doc_length - total_read)) > 0) {
        total_read += n;
    }
    doc[total_read] = '\0';
    printf("Document version: %s\n", versionbuf);
    printf("Document length: %s\n", lengthbuf);
    printf("Document content:\n%s\n", doc);
    free(doc);
    
    fd_set read_fds;
    int maxfd = (fd_s2c > STDIN_FILENO ? fd_s2c : STDIN_FILENO);
    bool skip_first_broadcast = true;
    char line[512];

    // main loop
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(fd_s2c, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int ready = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            perror("select");
            break;
        }
        
        // broadcast from server
        if (FD_ISSET(fd_s2c, &read_fds)) {
            // Process server broadcast
            if (read_line(fd_s2c, line, sizeof(line)) <= 0) 
                continue;
            if (strncmp(line, "VERSION", 7) != 0) 
                continue;

            // Check first broadcast
            if (skip_first_broadcast) {
                skip_first_broadcast = false;
                
                // Read peek line
                if (read_line(fd_s2c, line, sizeof(line)) > 0) {
                    if (strncmp(line, "DOC", 3) == 0) {
                        // Skip document broadcast
                        while (read_line(fd_s2c, line, sizeof(line)) > 0
                            && strcmp(line, "END\n") != 0) {
                            ;
                        }
                        continue;
                    } else {

                    }
                }
            }

            if (!skip_first_broadcast && strncmp(line, "END\n", 4) != 0) {
                process_line(line);
            }
            
            // Continue reading until END
            while (read_line(fd_s2c, line, sizeof(line)) > 0) {
                if (strcmp(line, "END\n") == 0)
                    break;
                process_line(line);
            }
        }
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            // client give command
            char cmd[1024];
            if (!fgets(cmd, sizeof(cmd), stdin)) {
                break;
            }
            size_t len = strlen(cmd);
            if (len == 0 || cmd[len - 1] != '\n') {
                int c;
                while ((c = getchar()) != '\n' && c != EOF) {}
                printf("command too long or missing newline\n");
                continue;
            }

            // send command to server
            if (write(fd_c2s, cmd, len) != (size_t)len) {
                perror("write command");
                break;
            }
            if (strncmp(cmd, "DISCONNECT", 10) == 0) {
                break;
            }
        }
    }
    close(fd_c2s);
    close(fd_s2c);
    return 0;
}

// read a line
ssize_t read_line(int fd, char *buf, size_t maxlen) {
    ssize_t n = 0, rc;
    char c;
    while (n < (ssize_t)maxlen - 1) {
        rc = read(fd, &c, 1);
        if (rc == 1) {
            buf[n++] = c;
            if (c == '\n') break;
        } else if (rc == 0) {
            break;
        } else {
            return -1;
        }
    }
    buf[n] = '\0';
    return n;
}

// print bytes
void print_bytes(int fd, int count) {
    char c;
    for (int i = 0; i < count; i++) {
        if (read(fd, &c, 1) != 1) break;
        write(STDOUT_FILENO, &c, 1);
    }
    fputc('\n', stdout);
}

// handle server line
void process_line(const char *line) {
    if (strncmp(line, "SUCCESS", 7) == 0 ||
        strncmp(line, "Reject", 6) == 0) {
        fputs(line, stdout);
        return;
    }
    // Edit notifications
    if (strncmp(line, "EDIT ", 5) == 0) {
        char who[64], op[64];
        if (sscanf(line, "EDIT %63s %63s", who, op) == 2) {
            size_t L = strlen(op);
            if (L > 0 && op[L-1] == '?') {
                return;
            }
            fputs(line, stdout);
        }
    }
}