#define _GNU_SOURCE
#include "../libs/markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>

typedef struct {
    pid_t client_pid;
    char fifo_c2s[64];
    char fifo_s2c[64];
} thread_data;

typedef struct client_node {
    pid_t pid;
    int fd_c2s;
    int fd_s2c;
    char fifo_c2s[64];
    char fifo_s2c[64];
    struct client_node *next;
} client_node_t;

typedef struct pending_command {
    char *user;
    char *command;
    int result;
    char *reason;
    struct pending_command *next;
} pending_command_t;

document *global_doc;

static client_node_t *client_head = NULL;
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
static pending_command_t *cmd_queue_head = NULL;
static pthread_mutex_t cmd_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t doc_mutex = PTHREAD_MUTEX_INITIALIZER;  // New global document mutex

void install_signal_handler(void);
void server_loop(void);
void *handle_client(void *arg);
void sig_handler(int sig, siginfo_t *info, void *context);
char *check_user_role(const char *username);
char *trim_whitespace(char *);
void add_client(pid_t pid, int fd_c2s, int fd_s2c, const char* fifo_c2s_name, const char* fifo_s2c_name);
void remove_client(pid_t pid);
void broadcast_document(void);
void *broadcast_thread(void *arg);
void enqueue_command(const char *user, const char *command, int result, const char *reason);
void *server_stdin_thread(void *arg);
int process_command(const char *user, const char *command, char **reason);

static size_t find_substring_in_doc(const document *doc, const char *substr) {
    char *flat = markdown_flatten(doc);
    if (!flat) return (size_t)-1;
    char *p = strstr(flat, substr);
    size_t pos = (p ? (size_t)(p - flat) : (size_t)-1);
    free(flat);
    return pos;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <time interval>\n", argv[0]);
        return 1;
    }

    int time_interval = atoi(argv[1]);

    global_doc = markdown_init();

    install_signal_handler();

    pthread_t btid;
    int *interval_arg = malloc(sizeof(int));
    *interval_arg = time_interval;
    pthread_create(&btid, NULL, broadcast_thread, interval_arg);
    pthread_detach(btid);

    pthread_t stid;
    pthread_create(&stid, NULL, server_stdin_thread, NULL);
    pthread_detach(stid);

    printf("Server PID: %d\n", getpid());
    printf("Time interval: %d seconds\n", time_interval);

    server_loop();
    
    markdown_free(global_doc);
    
    return 0;
}

// signal handler setup
void install_signal_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sig_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGRTMIN, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    // ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
}

void server_loop() {
    while (1) {
        pause();
    }
}

void sig_handler(int sig, siginfo_t *info, void *context) {
    // client connect
    (void)sig;
    (void)context;
    pid_t client_pid = info->si_pid;

    char fifo_c2s[64];
    char fifo_s2c[64];
    snprintf(fifo_c2s, sizeof(fifo_c2s), "FIFO_C2S_%d", client_pid);
    snprintf(fifo_s2c, sizeof(fifo_s2c), "FIFO_S2C_%d", client_pid);

    unlink(fifo_c2s);
    unlink(fifo_s2c);

    if (mkfifo(fifo_c2s, 0666) == -1) {
        perror("mkfifo");
        return;
    }

    if (mkfifo(fifo_s2c, 0666) == -1) {
        perror("mkfifo");
        unlink(fifo_c2s);
        return;
    }

    // notify client
    if (kill(client_pid, SIGRTMIN + 1) == -1) {
        perror("kill");
        unlink(fifo_c2s);
        unlink(fifo_s2c);
        return;
    }

    thread_data *data = malloc(sizeof(thread_data));
    if (!data) {
        perror("malloc thread_data");
        unlink(fifo_c2s);
        unlink(fifo_s2c);
        return;
    }
    data->client_pid = client_pid;
    strcpy(data->fifo_c2s, fifo_c2s);
    strcpy(data->fifo_s2c, fifo_s2c);

    // start client thread
    pthread_t thread;
    if (pthread_create(&thread, NULL, handle_client, data) != 0) {
        perror("pthread_create");
        free(data);
        unlink(fifo_c2s);
        unlink(fifo_s2c);
        return;
    }

    pthread_detach(thread);
}

void add_client(pid_t pid, int fd_c2s, int fd_s2c, const char* fifo_c2s_name, const char* fifo_s2c_name) {
    // add client to list
    client_node_t *cn = malloc(sizeof(client_node_t));
    if (!cn) {
        perror("malloc client_node_t");
        return;
    }
    cn->pid = pid;
    cn->fd_c2s = fd_c2s;
    cn->fd_s2c = fd_s2c;
    strncpy(cn->fifo_c2s, fifo_c2s_name, sizeof(cn->fifo_c2s) - 1);
    cn->fifo_c2s[sizeof(cn->fifo_c2s) - 1] = '\0';
    strncpy(cn->fifo_s2c, fifo_s2c_name, sizeof(cn->fifo_s2c) - 1);
    cn->fifo_s2c[sizeof(cn->fifo_s2c) - 1] = '\0';

    pthread_mutex_lock(&client_mutex);
    cn->next = client_head;
    client_head = cn;
    pthread_mutex_unlock(&client_mutex);
}

void remove_client(pid_t pid) {
    // remove client
    pthread_mutex_lock(&client_mutex);
    client_node_t **cur = &client_head;
    while (*cur) {
        if ((*cur)->pid == pid) {
            client_node_t *tmp = *cur;
            *cur = tmp->next;
            close(tmp->fd_c2s);
            close(tmp->fd_s2c);
            unlink(tmp->fifo_c2s);
            unlink(tmp->fifo_s2c);
            free(tmp);
            break;
        }
        cur = &(*cur)->next;
    }
    pthread_mutex_unlock(&client_mutex);
}

void broadcast_document() {
    // lock client/doc mutex
    pthread_mutex_lock(&client_mutex);
    pthread_mutex_lock(&doc_mutex);

    char verbuf[32];
    char lenbuf[32];
    
    snprintf(verbuf, sizeof(verbuf), "%llu\n", (unsigned long long)global_doc->version);
    snprintf(lenbuf, sizeof(lenbuf), "%zu\n", global_doc->total_length);

    char *doc_content = markdown_flatten(global_doc);

    for (client_node_t *c = client_head; c; c = c->next) {
        write(c->fd_s2c, verbuf, strlen(verbuf));
        write(c->fd_s2c, lenbuf, strlen(lenbuf));
        if (doc_content && global_doc->total_length > 0) {
            write(c->fd_s2c, doc_content, global_doc->total_length);
        }
    }

    if (doc_content) {
        free(doc_content);
    }

    pthread_mutex_unlock(&doc_mutex);
    pthread_mutex_unlock(&client_mutex);
}

void *broadcast_thread(void *arg) {
    int interval = *(int *)arg;
    free(arg);

    while (1) {
        sleep(interval);
        broadcast_document();
    }
    return NULL;
}

void *handle_client(void *arg) {
    // handle one client
    thread_data *data = (thread_data *)arg;
    pid_t client_pid = data->client_pid;
    char client_fifo_c2s_name[64];
    char client_fifo_s2c_name[64];

    strncpy(client_fifo_c2s_name, data->fifo_c2s, sizeof(client_fifo_c2s_name) -1);
    client_fifo_c2s_name[sizeof(client_fifo_c2s_name)-1] = '\0';
    strncpy(client_fifo_s2c_name, data->fifo_s2c, sizeof(client_fifo_s2c_name)-1);
    client_fifo_s2c_name[sizeof(client_fifo_s2c_name)-1] = '\0';

    int fd_c2s = open(client_fifo_c2s_name, O_RDONLY);
    int fd_s2c = open(client_fifo_s2c_name, O_WRONLY);

    if (fd_c2s == -1 || fd_s2c == -1) {
        perror("handle_client: error open fifos");
        if(fd_c2s != -1) close(fd_c2s);
        if(fd_s2c != -1) close(fd_s2c);
        unlink(client_fifo_c2s_name); 
        unlink(client_fifo_s2c_name);
        free(data);
        return NULL;
    }

    char username[256];
    memset(username, 0, sizeof(username));
    ssize_t nread = read(fd_c2s, username, sizeof(username) - 1);

    if (nread <= 0) {
        if (nread == 0) {
            fprintf(stderr, "handle_client: client %d disconnected before sending username.\n", client_pid);
        } else {
            perror("handle_client: error reading username");
        }
        close(fd_c2s);
        close(fd_s2c);
        unlink(client_fifo_c2s_name);
        unlink(client_fifo_s2c_name);
        free(data);
        return NULL;
    }
    username[strcspn(username, "\n")] = '\0';

    // check user role
    char *role = check_user_role(username);
    if (role && strlen(role) > 0) {
        add_client(client_pid, fd_c2s, fd_s2c, client_fifo_c2s_name, client_fifo_s2c_name);

        if (write(fd_s2c, role, strlen(role)) < 0 || write(fd_s2c, "\n", 1) < 0) {
            perror("handle_client: error writing role");
            free(role);
            remove_client(client_pid);
            free(data);
            return NULL;
        }
        free(role); 

        pthread_mutex_lock(&doc_mutex);
        // 1. VERSION\n
        if (write(fd_s2c, "VERSION\n", 8) < 0) {
            pthread_mutex_unlock(&doc_mutex);
            perror("handle_client: error writing VERSION\\n");
            remove_client(client_pid);
            free(data);
            return NULL;
        }
        // 2. version
        char verbuf[32];
        snprintf(verbuf, sizeof(verbuf), "%llu\n", (unsigned long long)global_doc->version);
        if (write(fd_s2c, verbuf, strlen(verbuf)) < 0) {
            pthread_mutex_unlock(&doc_mutex);
            perror("handle_client: error writing version");
            remove_client(client_pid);
            free(data);
            return NULL;
        }
        // 3. DOC\n
        if (write(fd_s2c, "DOC\n", 4) < 0) {
            pthread_mutex_unlock(&doc_mutex);
            perror("handle_client: error writing DOC\n");
            remove_client(client_pid);
            free(data);
            return NULL;
        }
        // 4. length\n
        char lenbuf[32];
        snprintf(lenbuf, sizeof(lenbuf), "%zu\n", global_doc->total_length);
        if (write(fd_s2c, lenbuf, strlen(lenbuf)) < 0) {
            pthread_mutex_unlock(&doc_mutex);
            perror("handle_client: error writing length");
            remove_client(client_pid);
            free(data);
            return NULL;
        }
        // 5. content
        char *doc_content = markdown_flatten(global_doc);
        if (doc_content) {
            size_t written = 0;
            ssize_t ret;
            while (written < global_doc->total_length) {
                ret = write(fd_s2c, doc_content + written, global_doc->total_length - written);
                if (ret <= 0) {
                    free(doc_content);
                    pthread_mutex_unlock(&doc_mutex);
                    perror("handle_client: error writing initial document content");
                    remove_client(client_pid);
                    free(data);
                    return NULL;
                }
                written += (size_t)ret;
            }
            free(doc_content);
        }
        // 6. \nEND\n
        if (write(fd_s2c, "\nEND\n", 5) < 0) {
            pthread_mutex_unlock(&doc_mutex);
            perror("handle_client: error writing END marker");
            remove_client(client_pid);
            free(data);
            return NULL;
        }
        pthread_mutex_unlock(&doc_mutex);

        char command_buffer[1024];
        while (1) {
            nread = read(fd_c2s, command_buffer, sizeof(command_buffer)-1);
            if (nread <= 0)
                break;
            command_buffer[nread] = '\0';
            printf("[SERVER] RECV from %s (bytes=%zd): '%s'\n", username, nread, command_buffer);

            if (strncmp(command_buffer, "DISCONNECT", 10) == 0 || strncmp(command_buffer, "disconnect", 10) == 0) {
                printf("[SERVER] Client %d is disconnecting\n", client_pid);
                write(fd_s2c, "SUCCESS\n", 8);
                enqueue_command(username, command_buffer, 0, NULL);
                remove_client(client_pid);
                free(data);
                return NULL;
            }

            char *reason_str = NULL;
            printf("[SERVER] Before process_command: '%s'\n", command_buffer);
            int rc = process_command(username, command_buffer, &reason_str);
            printf("[SERVER] After process_command: result=%d, reason=%s\n", 
                   rc, reason_str ? reason_str : "NULL");

            enqueue_command(username, command_buffer, rc, reason_str);
            if (rc == 0) {
                write(fd_s2c, "SUCCESS\n", 8);
            } else {
                char reject_msg[512];
                snprintf(reject_msg, sizeof(reject_msg), "Reject %s\n", 
                         reason_str ? reason_str : "Unknown reason");
                write(fd_s2c, reject_msg, strlen(reject_msg));
            }
            if (reason_str) {
                free(reason_str);
            }
        }

        if (nread == 0) {
            printf("Client %d (PID: %d) disconnected.\n", getpid(), client_pid);
        } else if (nread < 0) {
            perror("handle_client: error reading command from client");
        }
        
        remove_client(client_pid);

    } else {
        if (role) free(role);
        const char *reject_msg = "Reject UNAUTHORISED\n";
        if(write(fd_s2c, reject_msg, strlen(reject_msg)) < 0) {
             perror("handle_client: error writing reject message");
        }
        
        close(fd_c2s);
        close(fd_s2c);
        unlink(client_fifo_c2s_name);
        unlink(client_fifo_s2c_name);
    }

    free(data);
    return NULL;
}

char *check_user_role(const char *username) {
    // check roles.txt
    FILE *file = fopen("roles.txt", "r");
    if (!file) {
        perror("error open roles.txt");
        return NULL;
    }

    char line[256];
    char *role = NULL;

    while (fgets(line, sizeof(line), file)) {
        char *user = line;
        char *perm = NULL;

        for (char *p = line; *p != '\0'; p++) {
            if (*p == ' ' || *p == '\n' || *p == '\t') {
                *p = '\0';
                perm = p + 1;
                break;
            }
        }
        if (!perm) {
            continue;
        }

        //delete empty chars
        user = trim_whitespace(user);
        perm = trim_whitespace(perm);
        if (strlen(user) == 0 || strlen(perm) == 0) {
            continue;
        }
        if (strcmp(user, username) == 0) {
            size_t len = strlen(perm);
            char *safe_perm = malloc(len + 1);
            if (safe_perm) {
                memcpy(safe_perm, perm, len + 1);
                safe_perm[len] = '\0';
                role = safe_perm;
            }
            break;
        }
    }
    fclose(file);
    return role;
}

char *trim_whitespace(char *s) {
    // trim spaces
    while (isspace((unsigned char)*s)) ++s;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) --end;
    end[1] = '\0';
    return s;
}

void enqueue_command(const char *user, const char *command, int result, const char *reason) {
    // add to command log
    pending_command_t *ptr = malloc(sizeof(*ptr));
    ptr->user = strdup(user);
    ptr->command = strdup(command);
    ptr->result = result;
    ptr->reason = reason ? strdup(reason) : NULL;
    ptr->next = NULL;

    pthread_mutex_lock(&cmd_queue_mutex);

    //find the end of queue
    pending_command_t **cur = &cmd_queue_head;
    while (*cur) {
        cur = &(*cur)->next;
    }
    *cur = ptr;
    pthread_mutex_unlock(&cmd_queue_mutex);

}

void *server_stdin_thread(void *arg) {
    // handle server stdin
    (void)arg;
    char buf[256];
    while (fgets(buf, sizeof(buf), stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        if (strcmp(buf, "DOC?") == 0) {
            //pthread_mutex_lock(&doc_mutex);
            printf("[SERVER] Current document (version %llu, length %zu):\n", 
                    (unsigned long long)global_doc->version, 
                    global_doc->total_length);
            
            char *doc_content = markdown_flatten(global_doc);
            if (doc_content && global_doc->total_length > 0) {
                fwrite(doc_content, 1, global_doc->total_length, stdout);
                free(doc_content);
            }
            printf("\n");
            //pthread_mutex_unlock(&doc_mutex);
        } else if (strcmp(buf, "LOG?") == 0) {
            printf("[SERVER] Current commands log (pending queue):\n");
            for (pending_command_t *p = cmd_queue_head; p; p = p->next) {
                printf("EDIT %s %s %s %s\n", 
                p->user, 
                p->command, 
                p->result == 0 ? "SUCCESS" : "Reject",
                p->reason == 0 ? "" : p->reason);
            }
        } else if (strcmp(buf, "QUIT") == 0) {
            int has_clients = (client_head != NULL);
            if (has_clients) {
                int count = 0;
                for (client_node_t *c = client_head; c; c = c->next) count++;
                printf("QUIT rejected, %d clients still connected.\n", count);
            } else {
                printf("[SERVER] Received QUIT command. Exiting...\n");
                //pthread_mutex_lock(&doc_mutex);
                char *content = markdown_flatten(global_doc);
                FILE *out = fopen("doc.md", "w");
                if (out) {
                    fwrite(content, 1, global_doc->total_length, out);
                    fclose(out);
                }
                free(content);
                //pthread_mutex_unlock(&doc_mutex);
                markdown_free(global_doc);
                exit(0);
            }
        }
    }
    return NULL;
}

int process_command(const char *user, const char *command, char **reason) {
    (void)user;
    
    char *cmd_copy = strdup(command);
    if (!cmd_copy) {
        *reason = strdup("Memory allocation failed");
        return -1;
    }
    
    size_t len = strlen(cmd_copy);
    if (len > 0 && cmd_copy[len-1] == '\n') {
        cmd_copy[len-1] = '\0';
        len--;
    }
    
    if (len > 0 && cmd_copy[len-1] == '\r') {
        cmd_copy[len-1] = '\0';
    }
    
    printf("[SERVER] Trimmed command: '%s'\n", cmd_copy);
    
    pthread_mutex_lock(&doc_mutex);
    int result = -1;
    uint64_t cur_version = global_doc->version;
    
    // handle each command
    if (strncmp(cmd_copy, "INSERT", 6) == 0) {
        const char *p = cmd_copy + 7;
        char *endptr;
        long position = strtol(p, &endptr, 10);
        if (endptr == p || (*endptr != ' ' && *endptr != '\t')) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid INSERT format. Expected: INSERT pos text");
            return -1;
        }
        size_t pos = (size_t)position;
        p = endptr;
        while (*p == ' ' || *p == '\t') p++;
        printf("[SERVER] Inserting at pos %zu: '%s'\n", pos, p);
        result = markdown_insert(global_doc, cur_version, pos, p);
        if (result == 0) {
            markdown_commit(global_doc);
            printf("[SERVER] Document updated to version %llu, length %zu\n", 
                   (unsigned long long)global_doc->version, global_doc->total_length);
        }
        pthread_mutex_unlock(&doc_mutex);
        free(cmd_copy);
        if (result != 0) {
            *reason = strdup("Insert operation failed");
            return -1;
        }
        return 0;
    }
    if (strncmp(cmd_copy, "NEWLINE", 7) == 0) {
        const char *p = cmd_copy + 8;
        char *endptr;
        long position = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid NEWLINE format. Expected: NEWLINE pos");
            return -1;
        }
        size_t pos = (size_t)position;
        result = markdown_newline(global_doc, cur_version, pos);
        if (result == 0) {
            markdown_commit(global_doc);
            printf("[SERVER] Document updated to version %llu, length %zu\n", 
                   (unsigned long long)global_doc->version, global_doc->total_length);
            pthread_mutex_unlock(&doc_mutex);
            broadcast_document();
            free(cmd_copy);
            return 0;
        } else {
            *reason = strdup("INVALID_POSITION");
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            return -1;
        }
    }
    if (strncmp(cmd_copy, "HEADING", 7) == 0) {
        const char *p = cmd_copy + 8;
        char *endptr;
        long level = strtol(p, &endptr, 10);
        if (endptr == p || level < 1 || level > 6) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid HEADING level. Expected: HEADING level pos");
            return -1;
        }
        p = endptr + 1;
        while (*p == ' ' || *p == '\t') p++;
        long position = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid HEADING position. Expected: HEADING level pos");
            return -1;
        }
        size_t pos = (size_t)position;
        result = markdown_heading(global_doc, cur_version, (size_t)level, pos);
        if (result == 0) {
            markdown_commit(global_doc);
            printf("[SERVER] Document updated to version %llu, length %zu\n", 
                   (unsigned long long)global_doc->version, global_doc->total_length);
            pthread_mutex_unlock(&doc_mutex);
            broadcast_document();
            free(cmd_copy);
            return 0;
        } else {
            *reason = strdup("INVALID_POSITION");
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            return -1;
        }
    }
    if (strncmp(cmd_copy, "BOLD", 4) == 0) {
        const char *p = cmd_copy + 5;
        char *endptr;
        long start = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid BOLD format. Expected: BOLD start end");
            return -1;
        }
        p = endptr;
        while (*p == ' ' || *p == '\t') p++;
        long end = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid BOLD format. Expected: BOLD start end");
            return -1;
        }
        result = markdown_bold(global_doc, cur_version, (size_t)start, (size_t)end);
        if (result == 0) {
            markdown_commit(global_doc);
            printf("[SERVER] Document updated to version %llu, length %zu\n", 
                   (unsigned long long)global_doc->version, global_doc->total_length);
            pthread_mutex_unlock(&doc_mutex);
            broadcast_document();
            free(cmd_copy);
            return 0;
        } else {
            *reason = strdup("INVALID_POSITION");
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            return -1;
        }
    }
    if (strncmp(cmd_copy, "DELETE", 6) == 0) {
        const char *p = cmd_copy + 6;
        while (*p == ' ' || *p == '\t') p++;
        char *endptr;
        long position = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid DELETE format. Expected: DELETE pos len");
            return -1;
        }
        p = endptr;
        while (*p == ' ' || *p == '\t') p++;
        long length = strtol(p, &endptr, 10);
        if (endptr == p || length < 0) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid DELETE length");
            return -1;
        }
        size_t pos = (size_t)position;
        size_t len_del = (size_t)length;
        result = markdown_delete(global_doc, cur_version, pos, len_del);
        if (result == 0) {
            markdown_commit(global_doc);
            printf("[SERVER] Document updated to version %llu, length %zu\n", 
                   (unsigned long long)global_doc->version, global_doc->total_length);
            pthread_mutex_unlock(&doc_mutex);
            broadcast_document();
            free(cmd_copy);
            return 0;
        } else {
            *reason = strdup("INVALID_POSITION");
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            return -1;
        }
    }
    if (strncmp(cmd_copy, "ORDERED_LIST", 12) == 0) {
        const char *p = cmd_copy + 13;
        char *endptr;
        long position = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid ORDERED_LIST format. Expected: ORDERED_LIST pos");
            return -1;
        }
        size_t pos = (size_t)position;
        result = markdown_ordered_list(global_doc, cur_version, pos);
        if (result == 0) {
            markdown_commit(global_doc);
            printf("[SERVER] Document updated to version %llu, length %zu\n", 
                   (unsigned long long)global_doc->version, global_doc->total_length);
            pthread_mutex_unlock(&doc_mutex);
            broadcast_document();
            free(cmd_copy);
            return 0;
        } else {
            *reason = strdup("INVALID_POSITION");
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            return -1;
        }
    }

    if (strncmp(cmd_copy, "UNORDERED_LIST", 14) == 0) {
        const char *p = cmd_copy + 15;
        while (*p == ' ' || *p == '\t') p++;
        char *endptr;
        long position = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid UNORDERED_LIST format. Expected: UNORDERED_LIST pos");
            return -1;
        }
        size_t pos = (size_t)position;

        result = markdown_unordered_list(global_doc, cur_version, pos);
        if (result == 0) {
            markdown_commit(global_doc);
            pthread_mutex_unlock(&doc_mutex);
            broadcast_document();
            free(cmd_copy);
            return 0;
        } else {
            *reason = strdup("INVALID_POSITION");
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            return -1;
        }
    }

    if (strncmp(cmd_copy, "ITALIC", 6) == 0) {
        const char *p = cmd_copy + 7;
        char *endptr;
        long start = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid ITALIC format. Expected: ITALIC start end");
            return -1;
        }
        p = endptr + 1;
        while (*p == ' ' || *p == '\t') p++;
        long end = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid ITALIC format. Expected: ITALIC start end");
            return -1;
        }
        result = markdown_italic(global_doc, cur_version, (size_t)start, (size_t)end);
        if (result == 0) {
            markdown_commit(global_doc);
            printf("[SERVER] Document updated to version %llu, length %zu\n", 
                   (unsigned long long)global_doc->version, global_doc->total_length);
            pthread_mutex_unlock(&doc_mutex);
            broadcast_document();
            free(cmd_copy);
            return 0;
        } else {
            *reason = strdup("INVALID_POSITION");
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            return -1;
        }
    }
    if (strncmp(cmd_copy, "CODE", 4) == 0) {
        const char *p = cmd_copy + 5;
        char *endptr;
        long start = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid CODE format. Expected: CODE start end");
            return -1;
        }
        p = endptr + 1;
        while (*p == ' ' || *p == '\t') p++;
        long end = strtol(p, &endptr, 10);
        if (endptr == p) {
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            *reason = strdup("Invalid CODE format. Expected: CODE start end");
            return -1;
        }
        result = markdown_code(global_doc, cur_version, (size_t)start, (size_t)end);
        if (result == 0) {
            markdown_commit(global_doc);
            printf("[SERVER] Document updated to version %llu, length %zu\n", 
                   (unsigned long long)global_doc->version, global_doc->total_length);
            pthread_mutex_unlock(&doc_mutex);
            broadcast_document();
            free(cmd_copy);
            return 0;
        } else {
            *reason = strdup("INVALID_POSITION");
            pthread_mutex_unlock(&doc_mutex);
            free(cmd_copy);
            return -1;
        }
    }
    pthread_mutex_unlock(&doc_mutex);
    free(cmd_copy);
    *reason = strdup("Unknown command");
    return -1;
}