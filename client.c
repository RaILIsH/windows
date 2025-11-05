#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <conio.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8888
#define BUFFER_SIZE 4096

typedef struct {
    SOCKET socket;
    int stop_flag;
} ClientContext;

unsigned long socket_to_console_thread(void* param) {
    ClientContext* context = (ClientContext*)param;
    char buffer[BUFFER_SIZE];
    unsigned long bytes_written;
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    
    printf("Network->Console thread started\n");
    
    while (!context->stop_flag) {
        int bytes_read = recv(context->socket, buffer, sizeof(buffer)-1, 0);
        
        if (bytes_read > 0) {
            WriteFile(stdout_handle, buffer, bytes_read, &bytes_written, NULL);
        } else if (bytes_read == 0) {
            printf("\nServer disconnected\n");
            break;
        } else {
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
                break;
            }
            Sleep(10);
        }
    }
    
    context->stop_flag = 1;
    return 0;
}

unsigned long console_to_socket_thread(void* param) {
    ClientContext* context = (ClientContext*)param;
    char buffer[BUFFER_SIZE];
    unsigned long bytes_read;
    HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    
    printf("Console->Network thread started\n");
    printf("Connected! Type commands:\n");
    
    while (!context->stop_flag) {
        if (ReadFile(stdin_handle, buffer, sizeof(buffer)-1, &bytes_read, NULL)) {
            if (bytes_read > 0) {
                send(context->socket, buffer, bytes_read, 0);
            }
        } else {
            break;
        }
    }
    
    context->stop_flag = 1;
    return 0;
}

int main(int argc, char* argv[]) {
    WSADATA wsa_data;
    SOCKET connect_socket;
    struct sockaddr_in server_addr;
    ClientContext context = {0};
    HANDLE threads[2];
    unsigned long thread_ids[2];
    
    const char* server_ip = "127.0.0.1";
    int port = PORT;
    
    // Аргументы командной строки
    if (argc > 1) server_ip = argv[1];
    if (argc > 2) port = atoi(argv[2]);
    
    printf("Starting client...\n");
    printf("Connecting to %s:%d...\n", server_ip, port);
    
    // Инициализация сети
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("Network init failed\n");
        return 1;
    }
    
    connect_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connect_socket == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        return 1;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(port);
    
    if (connect(connect_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connection failed\n");
        closesocket(connect_socket);
        return 1;
    }
    
    printf("Connected successfully!\n");
    
    context.socket = connect_socket;
    
    // Неблокирующий режим
    unsigned long mode = 1;
    ioctlsocket(connect_socket, FIONBIO, &mode);
    
    // Запускаем потоки
    threads[0] = CreateThread(NULL, 0, socket_to_console_thread, &context, 0, &thread_ids[0]);
    threads[1] = CreateThread(NULL, 0, console_to_socket_thread, &context, 0, &thread_ids[1]);
    
    if (!threads[0] || !threads[1]) {
        printf("Thread creation failed\n");
        context.stop_flag = 1;
    }
    
    // Ожидаем завершения потоков
    WaitForMultipleObjects(2, threads, TRUE, INFINITE);
    
    printf("Client shutting down\n");
    
    // Очистка
    context.stop_flag = 1;
    shutdown(connect_socket, 2);
    closesocket(connect_socket);
    
    if (threads[0]) CloseHandle(threads[0]);
    if (threads[1]) CloseHandle(threads[1]);
    
    WSACleanup();
    
    printf("Press any key to exit...\n");
    _getch();
    
    return 0;
}