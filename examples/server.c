#include "NSC.h"

/*
Name : C^LI's Server
This server is a demonstration tool used to showcase and test the features of NSC.
It supports all four configuration modes: TCP/UDP over IPv4/IPv6.
In TCP : the server echoes to all others clients the message.
In UDP : the server echoes back to the sender.
This is a very basic chat application, designed to demonstrate and test what can be done with NSC.
Yeah, this example is a bit verbose... but hey, it works, and I value my free time.
I hope you'll find it useful ! ^^
Author : TK
*/

int main() {
    #if defined (_WIN32)
        startup();
    #endif
    
    // Server's Configuration (must match the clients)
    int usedConnType = TCP;
    int usedIpType = IPv4;
    const char* address = usedIpType == IPv4 ? "127.0.0.1" : "::1";

    // Create the server
    Server* server = createServer(address, 25565, usedConnType, usedIpType);
    if (server == NULL) {
        printf("Error creating the server\n");
        return 1;
    }
    
    while (1) {
        ServerEventsList* events = serverListen(server);
        for (int i = 0; i < events->numEvents; i++) {
            ServerEvent event = events->events[i];
            if (event.type == Connection) {
                switch (event.ipType) {
                    case IPv4:
                        printf("Connection from %s:%d\n", inet_ntoa(event.sin.in.sin_addr), ntohs(event.sin.in.sin_port));
                        break;
                    case IPv6:
                        char ip[INET6_ADDRSTRLEN];
                        inet_ntop(AF_INET6, &event.sin.in6.sin6_addr, ip, INET6_ADDRSTRLEN);
                        printf("Connection from %s:%d\n", ip, ntohs(event.sin.in6.sin6_port));
                        break;
                }
            } else if (event.type == DataReceived) {
                if (!strcmp("", event.data)) continue;
                switch (event.ipType) {
                    case IPv4:
                        printf("Data received from %s:%d: %s\n", inet_ntoa(event.sin.in.sin_addr), ntohs(event.sin.in.sin_port), event.data);
                        break;
                    case IPv6:
                        char ip[INET6_ADDRSTRLEN];
                        inet_ntop(AF_INET6, &event.sin.in6.sin6_addr, ip, INET6_ADDRSTRLEN);
                        printf("Data received from %s:%d: %s\n", ip, ntohs(event.sin.in6.sin6_port), event.data);
                        break;
                }
                // Echo the data to all the other clients
                if (usedConnType == TCP) {
                    for (int i = 0; i < server->numClients; ++i) {
                        if (server->clients[i].socket != event.socket) {
                            sendMessage(&server->clients[i].socket, event.data, event.dataSize, usedConnType, usedIpType, &server->clients[i].sin);
                        }
                    }
                }
                else {
                    // In UDP we have an echo back system for this example
                    sendMessage(&event.socket, event.data, event.dataSize, usedConnType, usedIpType, &event.sin);
                }
                
                free(event.data);
            } else if (event.type == Disconnection) {
                switch (event.ipType) {
                    case IPv4:
                        printf("Disconnection from %s:%d\n", inet_ntoa(event.sin.in.sin_addr), ntohs(event.sin.in.sin_port));
                        break;
                    case IPv6:
                        char ip[INET6_ADDRSTRLEN];
                        inet_ntop(AF_INET6, &event.sin.in6.sin6_addr, ip, INET6_ADDRSTRLEN);
                        printf("Disconnection from %s:%d\n", ip, ntohs(event.sin.in6.sin6_port));
                        break;
                }
            }
        }
        free(events->events);
        free(events);
    }

    #if defined (_WIN32)
        cleanup();
    #endif

    return 0;
}