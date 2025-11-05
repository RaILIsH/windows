# Remote Console (SSH-like System)

Проект реализует SSH-подобную систему для удаленного выполнения команд через сокеты. Сервер создает виртуальную командную строку (`cmd.exe`) для каждого подключенного клиента и перенаправляет ввод/вывод через сетевые сокеты.

gcc server.c -o server.exe -lws2_32
gcc client.c -o client.exe -lws2_32

server.exe [bind_ip] [port]
client.exe [server_ip] [port]

# Окно 1 - Сервер (принимает подключения с любого IP)
server.exe
# Или явно указать:
server.exe 0.0.0.0 8888

# Окно 2 - Клиент (подключается к localhost)
client.exe
# Или явно указать:
client.exe 127.0.0.1 8888