gcc client.c NSC.c -lws2_32 -o client
gcc server.c NSC.c -lws2_32 -o server

start "Server" server.exe
timeout 1
start "Client1" client.exe
start "Client2" client.exe