#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pwd.h>
#include <signal.h>
#include <getopt.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <time.h>
#include "include/talk.h"

#define DEFAULT_BACKLOG 1
#define BUFFER_SIZE 512
#define LOGIN_NAME_MAX 256
#define YESNO_LEN 4
#define REQ_FMT "Mytalk request from %s@%s. Accept (y/n)? "

int main(int argc, char* argv[]) {

    int opt;
    int verbosity = 0;
    int auto_accept = 0;
    int no_ncurses = 0;

    char* hostname = NULL;
    int port = 0;

    /*mode selection*/
    while ((opt = getopt(argc, argv, "vaN")) != -1) {
        switch (opt) {
        case 'v':
            verbosity++;
            break;
        case 'a':
            auto_accept = 1;
            break;
        case 'N':
            no_ncurses = 1;
            break;
        default:
            fprintf(stderr,
             "Usage: %s [-v] [-a] [-N] [hostname] port\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /*handles the modes and choose whether server or client*/
    if (optind < argc) {
        hostname = argv[optind++];
        if (optind < argc) {
            port = atoi(argv[optind]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Invalid port number.\n");
                exit(EXIT_FAILURE);
            }
            client_start(hostname, port, verbosity, auto_accept, no_ncurses);
        } else {
            port = atoi(hostname);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Invalid port number.\n");
                exit(EXIT_FAILURE);
            }
            server_start(port, verbosity, auto_accept, no_ncurses);
        }
    } else {
        fprintf(stderr, "Port number missing.\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}

void server_start(int port, int verbosity, int auto_accept, int no_ncurses) {
    int client_disconnected = 0;
    /* Create necessary variables */
    int sockfd, newsockfd, num_bytes;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    char remoteUsername[LOGIN_NAME_MAX + 1];
    struct pollfd fds[2];

    set_verbosity(verbosity); 
    /*Set verbosity level*/

    /* Create socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("server: socket");
        stop_windowing();
        exit(EXIT_FAILURE);
    }

    /* Prepare the sockaddr_in structure */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* Bind the socket to the address */
    if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("server: bind");
        close(sockfd);
        stop_windowing();
        exit(EXIT_FAILURE);
    }

    /* Listen for incoming connections */
    if (listen(sockfd, DEFAULT_BACKLOG) == -1) {
        printf("errno: %d\n", errno);
        perror("server: listen");  
        /*Print error message*/
        close(sockfd);
        stop_windowing();
        exit(EXIT_FAILURE);
    }

    /* Accept new connection */
    newsockfd = accept(sockfd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (newsockfd == -1) {
        perror("server: accept");
        close(sockfd);
        stop_windowing();
        exit(EXIT_FAILURE);
    }

    /* Get the hostname from the client */
    char remoteHost[256];
    if (getnameinfo((struct sockaddr *)&client_addr, sizeof client_addr, 
        remoteHost, sizeof remoteHost, NULL, 0, NI_NAMEREQD) == 0) {
    } else {
        perror("getnameinfo");
        exit(EXIT_FAILURE);
    }

    /* Prompt for request acceptance */
    char yesno[YESNO_LEN + 1];
    if (!auto_accept) {
        printf(REQ_FMT, remoteUsername, remoteHost);
        fgets(yesno, YESNO_LEN + 1, stdin);
        yesno[strcspn(yesno, "\n")] = '\0';
    } else {
        strncpy(yesno, "y", YESNO_LEN); 
        /*If auto_accept flag is set, automatically respond with "yes"*/
    }

    if (strcasecmp(yesno, "y") == 0) {
        if(!no_ncurses){
            start_windowing();
        }
        char yes_response[] = "YES";
        if (send(newsockfd, yes_response, strlen(yes_response), 0) == -1) {
            perror("server: send");
            close(newsockfd);
            close(sockfd);
            stop_windowing();
            exit(EXIT_FAILURE);
        }
    } else {
        printf("Connection declined.\n");
        close(newsockfd);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Get username from client */
    num_bytes = recv(newsockfd, remoteUsername, sizeof(remoteUsername) - 1, 0);
    if (num_bytes <= 0) {
        perror("server: recv");
        close(newsockfd);
        close(sockfd);
        stop_windowing();
        exit(EXIT_FAILURE);
    }
    remoteUsername[num_bytes] = '\0';

    /* Set up the array of file descriptors to poll */
    fds[0].fd = newsockfd;
    fds[0].events = POLLIN;
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;

    /* Keep the connection open until the client disconnects */
    while (1) {
        int poll_ret = poll(fds, 2, -1);
        if (poll_ret == -1) {
            perror("server: poll");
            break;
        }

        if (!client_disconnected && fds[0].revents & POLLIN) {  
            /* data received from client */
            num_bytes = recv(newsockfd, buffer, sizeof(buffer) - 1, 0);
            if (num_bytes <= 0) {
                if (num_bytes == 0) {
                fprint_to_output("\nClient closed connection. ^C to terminate.\n");
                client_disconnected = 1;
            } else {
                perror("server: recv");
                break;
            }
            } else {
                buffer[num_bytes] = '\0';
                fprint_to_output("%s", buffer);
            }
        }

        if (fds[1].revents & POLLIN) {  
            /* data typed in by user */
            num_bytes = read_from_input(buffer, sizeof(buffer));
            if (num_bytes <= 0) {
                if (num_bytes == 0) {
                    fprint_to_output("\nClient closed connection. ^C to terminate.\n");
                    client_disconnected = 1;
                } else {
                    perror("server: send");
                }
                break;
            }
            buffer[num_bytes - 1] = '\0'; 
             /* remove newline */
            if (!client_disconnected) {
                num_bytes = send(newsockfd, buffer, num_bytes, 0);
                if (num_bytes <= 0) {
                    break;
                }
            }
        }
    }

    close(newsockfd);
    close(sockfd);
    stop_windowing();
}

void client_start(char* hostname, int port, int verbosity, int auto_accept, int no_ncurses) {
    char username[LOGIN_NAME_MAX + 1];
    struct addrinfo hints, *servinfo, *p;
    int sockfd, rv;
    char s[INET6_ADDRSTRLEN];
    struct pollfd fds[2];
    int server_disconnected = 0;


    set_verbosity(verbosity);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portStr[6];
    sprintf(portStr, "%d", port);

    /* Get address info for the given hostname and port */
    if ((rv = getaddrinfo(hostname, portStr, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return;
    }

    /* Iterate through the address info list and create a socket and connect to the server */
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    /* If no valid address info found, print an error message and return */
    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return;
    }

    printf("client: connecting to %s\n", inet_ntoa(((struct sockaddr_in*)p->ai_addr)->sin_addr));

    freeaddrinfo(servinfo);

    /* Get the current username */
    struct passwd *pw = getpwuid(getuid());
    if (pw == NULL) {
        perror("client: getpwuid");
        exit(EXIT_FAILURE);
    }
    strncpy(username, pw->pw_name, sizeof(username) - 1);
    username[sizeof(username) - 1] = '\0';

    /* Send the username to the server */
    if (send(sockfd, username, strlen(username), 0) == -1) {
        perror("client: send");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Send the hostname to the server */
    if (send(sockfd, hostname, strlen(hostname), 0) == -1) {
        perror("client: send");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE + 1];
    printf("Waiting for response from %s.\n", hostname);

    /* Receive the response from the server */
    int num_bytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
    if (num_bytes == -1) {
        perror("client: recv");
        exit(EXIT_FAILURE);
    }

    buffer[num_bytes] = '\0';

    /* Check if the server accepted the connection */
    if (strcmp(buffer, "YES") == 0) {
        if(!no_ncurses){
            start_windowing();
        }
    } else {
        printf("Server declined connection.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    fds[0].fd = sockfd;
    fds[0].events = POLLIN;
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;

    while (1) {
        /* Poll for events on the sockets */
        int polled = poll(fds, 2, -1);
        if (polled == -1) {
            perror("client: poll");
            break;
        }

        static int server_disconnected = 0;
        static int message_printed = 0;

        if (fds[0].revents & POLLIN) { 
            /* data received from server */
            num_bytes = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
            if (num_bytes <= 0) {
                /*client's main loop */
                if (num_bytes == 0) {
                    if(!server_disconnected) {
                        fprint_to_output("\nServer closed connection. ^C to terminate.\n");
                        server_disconnected = 1;
                        fds[0].events = 0;  
                        /* Stop polling the server socket */
                    }
                } else {
                    perror("client: recv");
                }
            } else {
                buffer[num_bytes] = '\n';
                buffer[num_bytes + 1] = '\0';
                write_to_output(buffer, strlen(buffer));
            }
        }

        if (fds[1].revents & POLLIN && !server_disconnected) { 
            /* data typed in by user */
            update_input_buffer();
            while(has_whole_line()) {
                char line[BUFFER_SIZE + 1];
                read_from_input(line, BUFFER_SIZE);
                num_bytes = send(sockfd, line, strlen(line), 0);
                if (num_bytes <= 0) {
                    if (num_bytes == 0 && !message_printed) {
                        fprint_to_output("\n");
                        fprint_to_output("Server closed connection. ^C to terminate.\n");
                        server_disconnected = 1;
                        message_printed = 1;
                    } else if(num_bytes < 0) {
                        perror("client: send");
                        break;
                    }
                }
            }
        }
    }

    stop_windowing();
    close(sockfd);
}
