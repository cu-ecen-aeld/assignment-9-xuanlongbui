#include "aesdsocket.h"
#include "../aesd-char-driver/aesd_ioctl.h"
#include <time.h>
#include <string.h>

#define DEVICE_PATH     "/dev/aesdchar"
#define SEEKTO_PREFIX   "AESDCHAR_IOCSEEKTO:"

static pthread_mutex_t lock;
static int fd = 0;
#define CHUNK_SIZE 4096

void *append_timestamp(void *args)
{
#if 0
    char out_string[100];
    const char prefix[] = "timestamp:";
    unsigned lenght = 0;
    const char end_char = '\n';
    time_t now;
    struct tm *local_time;
    char buffer[50];
    unsigned int i = 0;
    now = time(NULL);
    local_time = localtime(&now);
    memset(buffer, '\0', sizeof(buffer));
    memset(out_string, '\0', 100);

    if (strftime(buffer, sizeof(buffer), "%d %b %Y %H:%M:%S", local_time) > 0)
    {
        strcat(out_string, prefix);
        strcat(out_string, buffer);
        strcat(out_string, &end_char);
        for (i = 0; i < sizeof(out_string); i++)
        {
            if (out_string[i] == end_char)
            {
                lenght = i + 1;
                break;
            }
        }
        pthread_mutex_lock(&lock);
        ssize_t bytes_written = write(fd, out_string, lenght);
        if (bytes_written < 0)
            perror("Failed to write to file");
        pthread_mutex_unlock(&lock);
    }
#endif
    return NULL;
}

int init_file(const char *file_path)
{
    fd = open(file_path, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
        return -1;
    printf("File '%s' created or opened successfully.\n", file_path);
    return 0;
}

int close_file()
{
    if (close(fd) < 0)
        return -1;
    return 0;
}

int mutex_init()
{
    if (pthread_mutex_init(&lock, NULL) != 0)
        return -1;
    return 0;
}

void mutex_destroy()
{
    pthread_mutex_destroy(&lock);
}

void write_ip_to_syslog(struct sockaddr_in in_sockaddr, char out_ip[])
{
    inet_ntop(AF_INET, &in_sockaddr.sin_addr, out_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", out_ip);
}

/* Read everything from dev_fd starting at its current f_pos and send to client_fd. */
static int send_device_content(int dev_fd, int client_fd)
{
    size_t buf_size = CHUNK_SIZE;
    char *rbuf = malloc(buf_size);
    if (!rbuf) {
        perror("malloc read buffer");
        return -1;
    }

    size_t total_read = 0;
    while (1) {
        if (total_read >= buf_size) {
            buf_size *= 2;
            char *tmp = realloc(rbuf, buf_size);
            if (!tmp) {
                perror("realloc read buffer");
                free(rbuf);
                return -1;
            }
            rbuf = tmp;
        }
        ssize_t n = read(dev_fd, rbuf + total_read, buf_size - total_read);
        if (n < 0) {
            perror("read device");
            free(rbuf);
            return -1;
        }
        if (n == 0)
            break;
        total_read += n;
    }

    if (total_read > 0) {
        ssize_t sent = send(client_fd, rbuf, total_read, 0);
        if (sent == -1)
            perror("send to client");
    }

    free(rbuf);
    return 0;
}

void *message_handler(void *args)
{
    int *c_fd = (int *)args;
    const int client_fd = *c_fd;
    char *buffer;
    size_t buffer_size = BUFFER_SIZE;
    size_t total_received = 0;

    buffer = malloc(buffer_size);
    if (!buffer) {
        perror("Failed to allocate receive buffer");
        return NULL;
    }

    while (1) {
        ssize_t bytes_received = recv(client_fd, buffer + total_received, BUFFER_SIZE, 0);
        if (bytes_received == -1) {
            perror("Receive failed");
            break;
        }
        if (bytes_received == 0) {
            printf("Client %d disconnected.\n", client_fd);
            break;
        }

        total_received += bytes_received;

        /* grow receive buffer if needed */
        if (total_received + BUFFER_SIZE > buffer_size) {
            buffer_size += BUFFER_SIZE;
            char *tmp = realloc(buffer, buffer_size);
            if (!tmp) {
                perror("Failed to reallocate receive buffer");
                break;
            }
            buffer = tmp;
        }

        /* process one complete newline-terminated packet */
        if (buffer[total_received - 1] != '\n')
            continue;

        pthread_mutex_lock(&lock);

        int dev_fd = open(DEVICE_PATH, O_RDWR);
        if (dev_fd < 0) {
            perror("Failed to open " DEVICE_PATH);
            pthread_mutex_unlock(&lock);
            break;
        }

        if (strncmp(buffer, SEEKTO_PREFIX, strlen(SEEKTO_PREFIX)) == 0) {
            /*
             * Seek command: parse X,Y, issue ioctl on dev_fd, then read from
             * the resulting f_pos using the same fd so the offset is honoured.
             */
            struct aesd_seekto seekto;
            if (sscanf(buffer + strlen(SEEKTO_PREFIX), "%u,%u",
                       &seekto.write_cmd, &seekto.write_cmd_offset) != 2) {
                fprintf(stderr, "Failed to parse seek command\n");
            } else {
                if (ioctl(dev_fd, AESDCHAR_IOCSEEKTO, &seekto) != 0)
                    perror("ioctl AESDCHAR_IOCSEEKTO");
                else
                    send_device_content(dev_fd, client_fd);
            }
        } else {
            /* Normal write: push data into driver, then read all from position 0. */
            ssize_t bytes_written = write(dev_fd, buffer, total_received);
            if (bytes_written < 0) {
                perror("Failed to write to device");
            } else {
                if (lseek(dev_fd, 0, SEEK_SET) < 0)
                    perror("lseek to 0");
                else
                    send_device_content(dev_fd, client_fd);
            }
        }

        close(dev_fd);
        pthread_mutex_unlock(&lock);

        /* reset for next packet */
        total_received = 0;
    }

    free(buffer);
    if (client_fd >= 0) {
        close(client_fd);
        printf("Closed connection with client %d\n", client_fd);
    }
    return NULL;
}
