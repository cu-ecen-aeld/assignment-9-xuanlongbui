#include "aesdsocket.h"
#include <time.h>
#include <string.h>
static pthread_mutex_t lock;

static int fd = 0;
#define CHUNK_SIZE 4096  // đọc từng khối 4KB

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
    now = time(NULL);             // Get the current time
    local_time = localtime(&now); // Convert to local time
    memset(buffer, '\0', sizeof(buffer));
    memset(out_string, '\0', 100);
   // printf("Timer expired, thread function called!\n");

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
        ssize_t bytes_written = write(fd, out_string, lenght); // -1 to exclude null terminator
        if (bytes_written < 0)
        {
            perror("Failed to write to file");
            // return -1;
        }
        pthread_mutex_unlock(&lock);
    }
#endif
    return NULL;
}
int init_file(const char *file_path)
{
    // Open the file, create it if it doesn't exist, with read/write permissions
    fd = open(file_path, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
    {
        // exit(EXIT_FAILURE);
        return -1;
    }
    printf("File '%s' created or opened successfully.\n", file_path);
    return 0;
}
int close_file()
{
    // Close the file
    if (close(fd) < 0)
    {
        return -1;
    }
    return 0;
}
int mutex_init()
{
    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        return -1;
    }
    return 0;
}
void mutex_destroy()
{
    pthread_mutex_destroy(&lock);
}
void write_ip_to_syslog(struct sockaddr_in in_sockaddr, char out_ip[])
{
    // Convert client IP address to string
    inet_ntop(AF_INET, &in_sockaddr.sin_addr, out_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", out_ip);
    // printf("Client connected with IP address: %s and port: %d\n",out_ip, ntohs(in_sockaddr.sin_port));
}

void *message_handler(void *args)
{
    int *c_fd = (int *)args;
    const int client_fd = *c_fd;
    ssize_t bytes_received = 0;
    char *buffer;
    size_t buffer_size = BUFFER_SIZE;
    size_t total_received = 0;
    // Dynamically allocate memory for the buffer
    buffer = (char *)malloc(buffer_size);
    if (buffer == NULL)
    {
        perror("Failed to allocate memory");
        // return -1;
    }

    while (1)
    {
        bytes_received = recv(client_fd, buffer + total_received, BUFFER_SIZE, 0);
        if (bytes_received == -1)
        {
            perror("Receive failed");
            break;
        }
        else if (bytes_received == 0)
        {
            printf("Client %d disconnected.\n", client_fd);
            close(fd);
            break;
        }

        total_received += bytes_received;

        // Check if more space is needed in the buffer
        if (total_received + BUFFER_SIZE > buffer_size)
        {
            buffer_size += BUFFER_SIZE; // Double the buffer size
            buffer = (char *)realloc(buffer, buffer_size);
            if (buffer == NULL)
            {
                perror("Failed to reallocate memory");
                close(client_fd);
                // exit(EXIT_FAILURE);
                // return -1;
            }
        }

        // Stop receiving if the client sends less than a chunk
        if (buffer[total_received - 1] == '\n')
        {
            pthread_mutex_lock(&lock);
            init_file("/dev/aesdchar");
            printf("Received from %d: %s", client_fd, buffer);
            ssize_t bytes_written = write(fd, buffer, total_received); // -1 to exclude null terminator
            if (bytes_written < 0)
            {
                perror("Failed to write to file");
                // return -1;
            }
            close(fd);
            printf("closed file descriptor %d after writing\n", fd);
            // Reinitialize the file for reading
            init_file("/dev/aesdchar");
            // Send the file content back to the client
            size_t buf_size = BUFFER_SIZE;
            char * rBuffer = (char *)malloc(BUFFER_SIZE);
            if (rBuffer == NULL)
            {
                perror("Failed to allocate memory for read buffer");
                close(client_fd);
                // return -1;
            }
            size_t total_read = 0;
            while (1)
            {
            // Expand if needed
            if (total_read >= buf_size) {
                buf_size *= 2;
                char *new_buf = realloc(rBuffer, buf_size);
                if (!new_buf) {
                    perror("realloc");
                    free(buffer);
                    close(fd);
                    return NULL;
                }
                rBuffer = new_buf;
            }

            ssize_t bytes = read(fd, rBuffer + total_read, buf_size - total_read);
            printf("Read %zd bytes from file\n", bytes);
            printf("Buffer content: %s\n", rBuffer);
            if (bytes < 0) {
                perror("read");
                free(buffer);
                close(fd);
                return NULL;
            }
            if (bytes == 0) // EOF
                break;

            total_read += bytes;
            }

            rBuffer[total_read] = '\0';
            printf("Sending back to client %s \n ", rBuffer );
            ssize_t bytesSent = send(client_fd, rBuffer, total_read, 0);
            if (bytesSent == -1)
            {
                perror("Error sending file data");
                close(client_fd);
                // return -1;
                break;
            }
            pthread_mutex_unlock(&lock);
            free(rBuffer);
            free(buffer);
            total_received = 0;
            buffer_size = BUFFER_SIZE;
            buffer = (char *)malloc(buffer_size);
        }
    }
    free(buffer);
    if (client_fd >= 0)
    {
        close(client_fd);
        printf("Closed connection with client %d\n", client_fd);
        close(fd);
        printf("Closed file descriptor %d\n", fd);
    }
    return NULL;
}