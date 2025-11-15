#include "aesdsocket.h"

unsigned int should_exit = 0;
int server_fd;
int client_fd;
void handle_signal(int sig);
TAILQ_HEAD(head_s, node) head;

int main(int argc, char *argv[])
{
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0)
        {
            printf("Runs the aesdsocket application as a daemon \n");
            pid_t p;
            p = fork();
            if (p < 0)
            {
                perror("Error forking");
                return -1;
            }
            if (p > 0)
            {
                exit(0);
            }
        }
        else
        {
        }
    }
    int  s;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    // struct sockaddr_storage  peer_addr;

    if (signal(SIGINT, handle_signal) == SIG_ERR)
    {
        perror("Error registering SIGINT handler");
        return -1;
        // exit(1);
    }
    if (signal(SIGTERM, handle_signal) == SIG_ERR)
    {
        perror("Error registering SIGTERM handler");
        return -1;
        // exit(1);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* stream socket */
    hints.ai_flags = AI_PASSIVE;     /* For wildcard IP address */
    hints.ai_protocol = 0;           /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    openlog("SyslogSocket", LOG_PID | LOG_CONS, LOG_USER);
    s = getaddrinfo(NULL, PORT, &hints, &result);
    if (s != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        // exit(EXIT_FAILURE);
        return -1;
    }
    /* getaddrinfo() returns a list of address structures.
        Try each address until we successfully bind(2).
        If socket(2) (or bind(2)) fails, we (close the socket
        and) try the next address. */

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        server_fd = socket(rp->ai_family, rp->ai_socktype,
                           rp->ai_protocol);
        if (server_fd == -1)
            continue;

        if (bind(server_fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break; /* Success */

        close(server_fd);
    }

    freeaddrinfo(result); /* No longer needed */

    if (rp == NULL)
    { /* No address succeeded */
        fprintf(stderr, "Could not bind\n");
        // exit(EXIT_FAILURE);
        return -1;
    }

    /* Read datagrams and echo them back to sender. */
    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("Listen failed");
        close(server_fd);
        // exit(EXIT_FAILURE);
        return -1;
    }
    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(client_address);

    char client_ip[INET_ADDRSTRLEN];

    TAILQ_INIT(&head); // initialize the head
    pthread_t thread;   
    struct node * e = NULL;
            
    if (mutex_init() !=0) return -1;

    // Create a timer
    timer_t timerid;
    struct sigevent sev;
    // Set up the timer to notify via SIGEV_THREAD
    sev.sigev_notify = SIGEV_THREAD;           // Specify that the timer will call a function in a new thread
    sev.sigev_notify_function = append_timestamp; // Specify the callback function
    sev.sigev_notify_attributes = NULL;        // Default attributes (new thread for callback)
    sev.sigev_value.sival_ptr = NULL;          // Optional data, can be used to pass information to the thread function

    // Create the timer
    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
        perror("timer_create");
        exit(EXIT_FAILURE);
    }
    printf("Timer ID: %p created successfully.\n", timerid);

    // Set the time for the timer
    struct itimerspec its;
    its.it_value.tv_sec = 2;                 // Timer will expire in 2 seconds
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 10;              // No repetition (one-shot timer)
    its.it_interval.tv_nsec = 0;

    // Start the timer
    if (timer_settime(timerid, 0, &its, NULL) == -1) {
        perror("timer_settime");
        // exit(EXIT_FAILURE);
        return -1;
    }

    while (!should_exit)
    {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_address_len)) < 0)
        {
            perror("Accept failed");
            close(server_fd);
            continue;
        }else
        {
            e = malloc(sizeof(struct node));
            if (e== NULL)
            {
                perror("Failed to allocate memory for new node");
                return -1;
            }
            e->client_fd=client_fd;
            write_ip_to_syslog(client_address, client_ip);
            // Create a thread and pass arguments
            if (pthread_create(&thread, NULL, message_handler, &e->client_fd) != 0) {
                perror("Failed to create thread");
                return -1;
            }
            e->tid = thread;
            // printf("The thread just created %ld \n", e->tid);
            // fflush(stdout); // Will now print everything in the stdout buffer
            TAILQ_INSERT_TAIL(&head, e, nodes);
            e = NULL;
        }
        
    }
    mutex_destroy();
    struct node * read; 
    // print the queue
    timer_delete(timerid);
    TAILQ_FOREACH(read, &head, nodes)
    {
        printf("Waiting for thread %ld \n", read->tid);
        fflush(stdout); // Will now print everything in the stdout buffer
        if (read->client_fd >= 0){
            close(read->client_fd);
        }
        pthread_join(read->tid, NULL);
    }
    while (!TAILQ_EMPTY(&head))
    {
        read = TAILQ_FIRST(&head);
        TAILQ_REMOVE(&head, read, nodes);
        free(read);
        read = NULL;
    }

    // Close the file

    closelog();
    return 0;
}

void handle_signal(int sig)
{
    printf("Received SIGINT (Ctrl+C) or SIGTERM : %d, terminating the program...\n ",sig);
    // You can do clean-up here, or use exit() to terminate the program
    should_exit = 1;
    if (server_fd >= 0)
    {
        close(server_fd);
    }
}