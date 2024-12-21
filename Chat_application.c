#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <ifaddrs.h>

#define LISTEN_BACKLOG 50
#define BUFF_SIZE 256
#define MAX_CONNECTIONS 10
#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

typedef struct {
    int id;
    int socket_fd;
    char ip[INET_ADDRSTRLEN];
    int port;
} Connection;

Connection connections[MAX_CONNECTIONS];
int connection_count = 0;
int server_fd;

void display_menu() {
    printf("\n************** Chat Application **************\n");
    printf("Use the commands below:\n");
    printf("1. myip                  : Display IP address of this app\n");
    printf("2. myport                : Display listening port of this app\n");
    printf("3. connect <ip> <port>   : Connect to the app of another user\n");
    printf("4. list                  : List all the connections of this app\n");
    printf("5. terminate <id>        : Terminate a connection\n");
    printf("6. send <id> <message>   : Send a message to a connection\n");
    printf("7. exit                  : Close all connections & terminate the app\n");
    printf("**********************************************\n");
    
}

void display_ip() {
    struct ifaddrs *ifaddr, *ifa;
    char ip[INET_ADDRSTRLEN];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }

    printf("IP Addresses:\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            void *addr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, addr, ip, INET_ADDRSTRLEN);
            printf("- %s: %s\n", ifa->ifa_name, ip);
        }
    }
    freeifaddrs(ifaddr);
}

void display_port(int port) {
    printf("Listening on port: %d\n", port);
}

void list_connections() {
    if (connection_count == 0) {
        printf("No active connections.\n");
        return;
    }
    printf("ID\tIP Address\tPort\n");
    for (int i = 0; i < connection_count; i++) {
        printf("%d\t%s\t%d\n", connections[i].id, connections[i].ip, connections[i].port);
    }
}

void terminate_connection(int id) {
    if (id < 1 || id > connection_count) {
        printf("Invalid connection ID.\n");
        return;
    }

    int socket_fd = connections[id - 1].socket_fd;
    char ip[INET_ADDRSTRLEN];
    int port;
    
    // Lưu thông tin trước khi xóa
    strcpy(ip, connections[id - 1].ip);
    port = connections[id - 1].port;

    // Gửi tín hiệu terminate
    if (write(socket_fd, "terminate", strlen("terminate")) == -1) {
        perror("Error sending terminate signal");
    }

    close(socket_fd);
    printf("Terminated connection %d (%s:%d)\n", id, ip, port);

    // Cập nhật danh sách kết nối
    for (int i = id - 1; i < connection_count - 1; i++) {
        connections[i] = connections[i + 1];
        connections[i].id = i + 1;
    }
    connection_count--;
}

void send_message(int id, char *message) {
    if (id < 1 || id > connection_count) {
        printf("Invalid connection ID.\n");
        return;
    }
    int socket_fd = connections[id - 1].socket_fd;
    if (write(socket_fd, message, strlen(message)) == -1)
        perror("write()");
    else
        printf("Message sent to %s:%d - %s\n", connections[id - 1].ip, connections[id - 1].port, message);
}

// Hàm mới để xử lý khi nhận được tín hiệu terminate từ bên kia
void handle_remote_terminate(int id) {
    int socket_fd = connections[id - 1].socket_fd;
    close(socket_fd);
    printf("Connection %d (%s:%d) terminated by remote host.\n", 
           id, connections[id - 1].ip, connections[id - 1].port);

    // Cập nhật danh sách kết nối
    for (int i = id - 1; i < connection_count - 1; i++) {
        connections[i] = connections[i + 1];
        connections[i].id = i + 1;
    }
    connection_count--;
}

void *handle_client_messages(void *conn_id) {
    int id = *(int *)conn_id;
    free(conn_id);
    int original_id = id; // Lưu ID gốc

    char recvbuff[BUFF_SIZE];
    int socket_fd = connections[id].socket_fd;

    while (1) {
        memset(recvbuff, 0, BUFF_SIZE);
        int num_read = read(socket_fd, recvbuff, BUFF_SIZE);
        
        // Kiểm tra xem connection này có còn tồn tại không
        int still_exists = 0;
        for (int i = 0; i < connection_count; i++) {
            if (connections[i].socket_fd == socket_fd) {
                still_exists = 1;
                id = i; // Cập nhật ID mới nếu vị trí thay đổi
                break;
            }
        }
        
        if (!still_exists) {
            break; 
        }

        if (num_read <= 0) {
            printf("Connection %d (%s:%d) closed unexpectedly.\n", 
                   id + 1, connections[id].ip, connections[id].port);
            handle_remote_terminate(id + 1);
            break;
        }

        recvbuff[num_read] = '\0';

        // Kiểm tra tín hiệu terminate
        if (strcmp(recvbuff, "terminate") == 0) {
            handle_remote_terminate(id + 1);
            break;
        }

        printf("Message from %s:%d: %s\n", 
               connections[id].ip, connections[id].port, recvbuff);
    }
    return NULL;
}

void connect_to_server(char *ip, int port) {
    if (connection_count >= MAX_CONNECTIONS) {
        printf("Connection limit reached!\n");
        return;
    }

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1)
        handle_error("socket()");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
        handle_error("inet_pton()");

    if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(client_fd);
        return;
    }

    connections[connection_count].id = connection_count + 1;
    connections[connection_count].socket_fd = client_fd;
    strcpy(connections[connection_count].ip, ip);
    connections[connection_count].port = port;

    printf("Connected to %s:%d\n", ip, port);

    // Tạo luồng để nhận tin nhắn từ kết nối mới
    int *new_conn_id = malloc(sizeof(int));
    *new_conn_id = connection_count;
    pthread_t tid;
    if (pthread_create(&tid, NULL, handle_client_messages, new_conn_id) != 0) {
        perror("pthread_create");
        free(new_conn_id);
    }
    pthread_detach(tid);

    connection_count++;
}

void *start_server(void *port_ptr) {
    int port = *(int *)port_ptr;
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
        handle_error("socket()");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        handle_error("bind()");

    if (listen(server_fd, LISTEN_BACKLOG) == -1)
        handle_error("listen()");

    printf("Server is listening on port: %d\n", port);

    while (1) {
        int *new_socket_fd = malloc(sizeof(int));
        *new_socket_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (*new_socket_fd == -1) {
            free(new_socket_fd);
            handle_error("accept()");
        }

        printf("New connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        connections[connection_count].id = connection_count + 1;
        connections[connection_count].socket_fd = *new_socket_fd;
        strcpy(connections[connection_count].ip, inet_ntoa(client_addr.sin_addr));
        connections[connection_count].port = ntohs(client_addr.sin_port);

        // Tạo luồng để xử lý tin nhắn từ client
        int *conn_id = malloc(sizeof(int));
        *conn_id = connection_count;
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client_messages, conn_id) != 0) {
            perror("pthread_create");
            free(conn_id);
        }
        pthread_detach(tid);
        free(new_socket_fd);
        connection_count++;
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, start_server, &port) != 0)
        handle_error("pthread_create");

    char command[256], ip[INET_ADDRSTRLEN], message[BUFF_SIZE];
    int id, port_num;
    display_menu();

    while (1) {
        printf("Enter your command: ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            perror("fgets");
            continue;
        }
        command[strcspn(command, "\n")] = 0; 

        if (strcmp(command, "myip") == 0) {
            display_ip();
        } else if (strcmp(command, "myport") == 0) {
            display_port(port);
        } else if (sscanf(command, "connect %s %d", ip, &port_num) == 2) {
            connect_to_server(ip, port_num);
        } else if (strcmp(command, "list") == 0) {
            list_connections();
        } else if (sscanf(command, "terminate %d", &id) == 1) {
            terminate_connection(id);
        } else if (sscanf(command, "send %d %[^\n]", &id, message) == 2) {
            send_message(id, message);
        } else if (strcmp(command, "exit") == 0) {
            printf("Exiting application...\n");
            for (int i = 0; i < connection_count; i++) {
                // close(connections[i].socket_fd);
                terminate_connection(i + 1);
            }
            close(server_fd);
            pthread_cancel(server_thread);
            break;
        } else {
            printf("Invalid command. Please try again.\n");
        }
    }
    return 0;
}
