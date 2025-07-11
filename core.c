#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#define MAX_BUFFER_SIZE 2048
#define AUTH_CODE_LENGTH 6
#define MAX_COMMANDS 10
#define MAX_COMMAND_LENGTH 256

const char* ALLOWED_COMMANDS[MAX_COMMANDS] = {
    "ls",
    "pwd",
    "whoami",
    "date",
    "uptime",
    "df -h",
    "free -h",
    "ps aux",
    "netstat -tuln",
    "uname -a"
};

void generate_auth_code(char* code) {
    srand(time(NULL));
    for (int i = 0; i < AUTH_CODE_LENGTH; i++) {
        code[i] = '0' + (rand() % 10);
    }
    code[AUTH_CODE_LENGTH] = '\0';
}

int is_command_allowed(const char* command) {
    for (int i = 0; i < MAX_COMMANDS; i++) {
        if (ALLOWED_COMMANDS[i] != NULL && strncmp(command, ALLOWED_COMMANDS[i], strlen(ALLOWED_COMMANDS[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

void execute_safe_command(int socket_fd, const char* command) {
    FILE* fp;
    char result[MAX_BUFFER_SIZE] = {0};
    char temp_buffer[256];
    
    if (!is_command_allowed(command)) {
        const char* error_msg = "ERROR: Command not allowed\n";
        send(socket_fd, error_msg, strlen(error_msg), 0);
        return;
    }
    
    fp = popen(command, "r");
    if (fp == NULL) {
        const char* error_msg = "ERROR: Failed to execute command\n";
        send(socket_fd, error_msg, strlen(error_msg), 0);
        return;
    }
    
    while (fgets(temp_buffer, sizeof(temp_buffer), fp) != NULL) {
        strncat(result, temp_buffer, sizeof(result) - strlen(result) - 1);
    }
    
    pclose(fp);
    
    send(socket_fd, result, strlen(result), 0);
}

int main(int argc, const char *argv[]) {
    int SERVER_FD, NEW_SOCK;
    struct sockaddr_in ADDR;
    int OPT = 1;
    int ADDRLEN = sizeof(ADDR);
    char BUFFER[MAX_BUFFER_SIZE] = {0};
    char AUTH_CODE[AUTH_CODE_LENGTH + 1];
    char RECEIVED_CODE[AUTH_CODE_LENGTH + 1] = {0};
    int PORT = 3131;
    int BACK_LOG = 2;

    if (argc > 1) PORT = atoi(argv[1]);

    if ((SERVER_FD = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Failed to open socket!\nExiting...\n");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(SERVER_FD, SOL_SOCKET, SO_REUSEADDR, &OPT, sizeof(OPT))) {
        perror("Failed to set socket operator 'ReUseADDRESS'!\nExiting...\n");
        exit(EXIT_FAILURE);
    }

    ADDR.sin_family = AF_INET;
    ADDR.sin_addr.s_addr = INADDR_ANY;
    ADDR.sin_port = htons(PORT);

    if (bind(SERVER_FD, (struct sockaddr*)&ADDR, ADDRLEN) < 0) {
        perror("Failed to bind server socket!\nExiting...\n");
        exit(EXIT_FAILURE);
    }

    if (listen(SERVER_FD, BACK_LOG) < 0) {
        perror("Failed to listen to connections!\nExiting...\n");
        exit(EXIT_FAILURE);
    }

    generate_auth_code(AUTH_CODE);
    printf("=== REMOTE SHELL SERVER ===\n");
    printf("Listening on port %d\n", PORT);
    printf("Authentication Code: %s\n", AUTH_CODE);
    printf("==============================\n");

    if ((NEW_SOCK = accept(SERVER_FD, (struct sockaddr*)&ADDR, (socklen_t*)&ADDRLEN)) < 0) {
        perror("Failed to accept connection!\nExiting...\n");
        exit(EXIT_FAILURE);
    }

    printf("New connection accepted from %s\n", inet_ntoa(ADDR.sin_addr));

    //send(NEW_SOCK, AUTH_CODE, AUTH_CODE_LENGTH, 0);

    memset(BUFFER, 0, sizeof(BUFFER));
    int bytes_received = read(NEW_SOCK, BUFFER, sizeof(BUFFER));
    
    if (bytes_received <= 0) {
        printf("No authentication code received\n");
        close(NEW_SOCK);
        close(SERVER_FD);
        exit(EXIT_FAILURE);
    }

    strncpy(RECEIVED_CODE, BUFFER, AUTH_CODE_LENGTH);
    RECEIVED_CODE[AUTH_CODE_LENGTH] = '\0';

    if (strcmp(AUTH_CODE, RECEIVED_CODE) != 0) {
        printf("Authentication failed! Expected: %s, Received: %s\n", AUTH_CODE, RECEIVED_CODE);
        const char* auth_fail_msg = "AUTH_FAILED";
        send(NEW_SOCK, auth_fail_msg, strlen(auth_fail_msg), 0);
	close(NEW_SOCK);
    }

    printf("Authentication successful!\n");
    const char* auth_success_msg = "AUTH_SUCCESS";
    send(NEW_SOCK, auth_success_msg, strlen(auth_success_msg), 0);

    while (1) {
        memset(BUFFER, 0, sizeof(BUFFER));
        bytes_received = read(NEW_SOCK, BUFFER, sizeof(BUFFER));
        
        if (bytes_received <= 0) {
            printf("Connection closed by client\n");
            break;
        }

        BUFFER[strcspn(BUFFER, "\r\n")] = '\0';

        printf("Received command: '%s'\n", BUFFER);

        if (strcmp(BUFFER, "exit") == 0) {
            printf("Exit command received\n");
            break;
        }

        execute_safe_command(NEW_SOCK, BUFFER);
    }

    close(NEW_SOCK);
    close(SERVER_FD);
    printf("Server shutdown\n");
    return 0;
}
