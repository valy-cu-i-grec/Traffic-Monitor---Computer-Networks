#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <pthread.h>

extern int errno;
int is_logged_in = 0;

void *speed_thread(void *arg)
{
    int sd = *(int *)arg;
    char sync_msg[64];

    while (1)
    {
        if (is_logged_in)
        {
            int random_speed = 30 + rand() % 51;
            snprintf(sync_msg, sizeof(sync_msg), "update-speed-%d", random_speed);

            if (write(sd, sync_msg, strlen(sync_msg) + 1) <= 0)
            {
                perror("[speed_thread] Error sending automatic speed update");
            }

            sleep(60);
        }
        else
        {
            sleep(1); // daca nu e logat, asteapta o secunda
        }
    }
    return NULL;
}

int main(int argc, char *argv[])

{
    int sd;
    struct sockaddr_in server;
    char buf[4096];
    pthread_t speed_tid;

    if (argc != 3)
    {
        printf("Sintax: %s <server_address> <port>\n", argv[0]);
        return -1;
    }

    if((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket() error.\n");
        return errno;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(atoi(argv[2]));

    if(connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("connect() error.\n");
        return errno;
    }

    srand(time(NULL));

    if(pthread_create(&speed_tid, NULL, speed_thread, &sd) != 0)
    {
        perror("pthread_create() error.\n");
        return errno;
    }

    printf("Connected to server.\nUse 'help' for command list.\n");

    while(1)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds); //stdin
        FD_SET(sd, &read_fds); //socket

        printf("[client]> ");
        fflush(stdout);

        int selret = select(sd + 1, &read_fds, NULL, NULL, NULL);

        if(selret < 0)
        {
            perror("select() error.\n");
            break;
        }

        if(FD_ISSET(sd, &read_fds))
        {
            ssize_t bytes = read(sd, buf, sizeof(buf) - 1);
            if(bytes <= 0)
            {
                printf("\n Server closed connection.\n");
                break;
            }
            buf[bytes]='\0';

            if(strncmp(buf, "exit", 4) == 0)
            {
                printf("Exiting...\n");
                break;
            }

            if(strstr(buf, "Login succeeded"))
            {
                is_logged_in = 1;
            }
            else if(strstr(buf, "Logout succesful"))
            {
                is_logged_in = 0;
            }

            printf("\n[SERVER Answer]: %s\n\n", buf);
            memset(buf, 0, sizeof(buf));
        }

        if(FD_ISSET(0, &read_fds))
        {
            ssize_t bytes = read(0, buf, sizeof(buf) - 1);
            if(bytes > 0)
            {
                buf[bytes] = '\0';
                buf[strcspn(buf, "\r\n")] = '\0';

                if(write(sd, buf, strlen(buf) + 1) <= 0)
                {
                    perror("write() error.\n");
                    break;
                }
            }
            memset(buf, 0, sizeof(buf));
        }
    }
    close(sd);
    return 0;
}

/*int port;

int main(int argc, char *argv[])
{
    int sd;
    struct sockaddr_in server;
    char buf[2048];

    if (argc != 3)
    {
        printf("Sintax: %s <server_address> <port>\n", argv[0]);
        return -1;
    }

    port = atoi(argv[2]);

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket() error.\n");
        return errno;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);

    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[client]connect() error.\n");
        return errno;
    }

    while (1)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds); //stdin
        FD_SET(sd, &read_fds); //socket

        printf("[client]> ");
        fflush(stdout);

        int selret = select(sd+1, &read_fds, NULL, NULL, NULL);

        if(selret < 0 )
        {
            perror("select() error");
            break;
        }

        if(FD_ISSET(sd, &read_fds))
        {
            ssize_t bytes = read(sd, buf, sizeof(buf) - 1);
            if(bytes <= 0)
            {
                printf("\n[client] Server closed connection.\n");
                break;
            }
            buf[bytes] = '\0';

            if(strncmp(buf, "exit", 4) == 0)
            {
                printf("\n[client] Exiting...\n");
                break;
            }

            printf("\n[SERVER Answer]: %s\n\n", buf);
            memset(buf, 0, sizeof(buf));
            continue;
        }

        if(FD_ISSET(0, &read_fds))
        {
            ssize_t bytes = read(0, buf, sizeof(buf)-1);
            if(bytes > 0)
            {
                buf[bytes] = '\0';
                buf[strcspn(buf, "\r\n")] = '\0';

                if(write(sd, buf, strlen(buf) + 1 ) <= 0)
                {
                    perror("[client] write() error");
                    break;
                }
            }
            memset(buf, 0, sizeof(buf));
        }
    }

    close(sd);
}
*/
