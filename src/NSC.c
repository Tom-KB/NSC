#include "NSC.h"

Server* createServer(const char* address, int port, int connType, int ipType) {
    Server* server = (Server*)malloc(sizeof(Server)); // Create the server's structure

    // Create the server's socket
    int type = (connType == TCP) ? SOCK_STREAM : SOCK_DGRAM; // Support for TCP and UDP
    int ip = (ipType == IPv4) ? AF_INET : AF_INET6; // Support for IPv4 and IPv6

    server->socket = socket(ip, type, 0);

    // Set the socket in non-blocking mode
    #if defined (_WIN32)
        u_long nonBlocking = 1; // 1 is for non-blocking mode
        ioctlsocket(server->socket, FIONBIO, (u_long*)&nonBlocking);
    #elif defined (__linux__)
        int flags = fcntl(server->socket, F_GETFL, 0);
        fcntl(server->socket, F_SETFL, flags | O_NONBLOCK);
    #endif

    // Check if the socket was created successfully
    if (server->socket == INVALID_SOCKET) {
        fprintf(stderr, "Error creating the server's socket\n");
        return NULL;
    }

    // Set the server's information (address, port, and IP type) according to the IP type
    switch (ip) {
        case AF_INET:
            server->sin.in.sin_family = ip;
            server->sin.in.sin_port = htons(port);
            inet_pton(ip, address, &server->sin.in.sin_addr.s_addr);
            break;
        case AF_INET6:
            server->sin.in6.sin6_family = ip;
            server->sin.in6.sin6_port = htons(port);
            inet_pton(ip, address, &server->sin.in6.sin6_addr);
            break;
    }

    // Set the server's connection type and IP type
    server->connType = connType;
    server->ipType = ipType;

    switch (ipType) {
        case IPv4:
            server->recSize = sizeof(server->sin.in);
            break;
        case IPv6:
            server->recSize = sizeof(server->sin.in6);
            server->sin.in6.sin6_scope_id = 0;
            break;
    }
    

    // Initialize the server's socket set
    FD_ZERO(&server->socketSet);
    FD_SET(server->socket, &server->socketSet);

    // Set the server's maximum socket
    server->maxSocket = server->socket;

    // Create the array of clients
    server->clients = (Client*)malloc(sizeof(Client) * MaxClients);
    server->numClients = 0;

    // Bind the server's socket
    if (ipType == IPv4) {
        if (bind(server->socket, (SOCKADDR*)&server->sin.in, sizeof(server->sin.in)) == SOCKET_ERROR) {
            fprintf(stderr,"Error binding the server's socket\n");
            return NULL;
        }
    } 
    else if (ipType == IPv6) {
        if (bind(server->socket, (SOCKADDR*)&server->sin.in6, sizeof(server->sin.in6)) == SOCKET_ERROR) {
            fprintf(stderr,"Error binding the server's socket\n");
            return NULL;
        }
    }

    // Listen on the server's socket
    if (connType == TCP) {
        if (listen(server->socket, 65535) == SOCKET_ERROR) {
            fprintf(stderr,"Error listening on the server's socket\n");
            return NULL;
        }
    }

    return server;
}

void closeServer(Server* server) {
    closesocket(server->socket);
    free(server->clients);
    free(server);
}

Client* acceptClient(Server* server) {
    Client client; // Create the client's structure
    
    // Accept the connection to the server's socket
    if (server->ipType == IPv4) {
    client.recSize = sizeof(client.sin.in);
    } else if (server->ipType == IPv6) {
        client.recSize = sizeof(client.sin.in6);
    }
    client.socket = accept(server->socket, (SOCKADDR*)&client.sin, &client.recSize);

    // Check if the client was accepted successfully
    if (client.socket == INVALID_SOCKET) {
        return NULL;
    }

    // Set the client's connection type and IP type
    client.connType = server->connType;
    client.ipType = server->ipType;
    
    // Init the client's buffer
    client.bufferData.buffer = malloc(BufferSize);
    client.bufferData.len = 0;
    client.bufferData.pos = 0;

    // Add the client to the server's list of clients
    server->clients[server->numClients] = client;
    server->numClients++;

    FD_SET(client.socket, &server->socketSet); // Add the client's socket to the server's socket set
    return &server->clients[server->numClients - 1];
}

/*
    Parameters:
        - ServerEvent* events : The lists of actual events
        - int numEvents : The number of events
        - int* eventMemory : The size of the list
    Output:
        - ServerEvent* : The new address of the events' list if creation succeeded, the old address otherwise
    Description:
        This function reallocate space of the server's events list if needed.
*/
ServerEvent* eventReallocServer(ServerEvent* events, int numEvents, int* eventMemory) {
    if (numEvents >= *eventMemory) {
        *eventMemory += EventBlock;
        ServerEvent* temp = realloc(events, sizeof(ServerEvent) * *eventMemory);
        if (!temp) {
            fprintf(stderr, "Memory allocation failed for events\n");
        }
        events = temp;
    }
    return events;
}

ServerEventsList* serverListen(Server* server) {
    ServerEventsList* eventsList = (ServerEventsList*)malloc(sizeof(ServerEventsList));
    eventsList->numEvents = 0;

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 50;

    int eventMemory = EventBlock;
    eventsList->events = (ServerEvent*)malloc(sizeof(ServerEvent) * eventMemory);

    fd_set copySet = server->socketSet;
    int numReady = select(server->maxSocket + 1, &copySet, NULL, NULL, &timeout);

    if (numReady <= 0) {
        // timeout or error
        return eventsList;
    }

    if (FD_ISSET(server->socket, &copySet)) {
        if (server->connType == TCP) {
            Client* client = acceptClient(server);
            while (client != NULL) {
                eventsList->events = eventReallocServer(eventsList->events, eventsList->numEvents, &eventMemory);
                eventsList->events[eventsList->numEvents].type = Connection;
                eventsList->events[eventsList->numEvents].socket = client->socket;
                eventsList->events[eventsList->numEvents].sin = client->sin;
                eventsList->events[eventsList->numEvents].ipType = client->ipType;
                eventsList->events[eventsList->numEvents].data = NULL;
                eventsList->numEvents++;

                client = acceptClient(server);
            }
        } else if (server->connType == UDP) {
            // UDP socket is ready to receive
            char* buffer = (char*)malloc(BufferSize);
            SIN clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);

            int bytesReceived = recvfrom(server->socket, buffer, BufferSize - 1, 0, (SOCKADDR*)&clientAddr, &clientAddrLen);

            if (bytesReceived > 0) {
                bytesReceived = (bytesReceived < BufferSize) ? bytesReceived : BufferSize - 1;

                eventsList->events = eventReallocServer(eventsList->events, eventsList->numEvents, &eventMemory);
                eventsList->events[eventsList->numEvents].type = DataReceived;
                eventsList->events[eventsList->numEvents].socket = server->socket;
                eventsList->events[eventsList->numEvents].sin = clientAddr;
                eventsList->events[eventsList->numEvents].ipType = server->ipType;
                eventsList->events[eventsList->numEvents].dataSize = bytesReceived;
                eventsList->events[eventsList->numEvents].data = (char*)malloc(bytesReceived);
                memcpy(eventsList->events[eventsList->numEvents].data, buffer, bytesReceived);
                eventsList->numEvents++;
            }

            free(buffer);
        }
    }

    // Check all clients for data (TCP)
    for (int i = 0; i < server->numClients; i++) {
        if (FD_ISSET(server->clients[i].socket, &copySet)) {
            char* buffer = NULL;
            int bytesReceived = 0;

            do {
                bytesReceived = readMessage(&server->clients[i], &buffer);

                if (bytesReceived > 0) {
                    bytesReceived = (bytesReceived < BufferSize) ? bytesReceived : BufferSize - 1;

                    eventsList->events = eventReallocServer(eventsList->events, eventsList->numEvents, &eventMemory);
                    eventsList->events[eventsList->numEvents].type = DataReceived;
                    eventsList->events[eventsList->numEvents].socket = server->clients[i].socket;
                    eventsList->events[eventsList->numEvents].sin = server->clients[i].sin;
                    eventsList->events[eventsList->numEvents].ipType = server->ipType;
                    eventsList->events[eventsList->numEvents].dataSize = bytesReceived;
                    eventsList->events[eventsList->numEvents].data = (char*)malloc(bytesReceived);
                    memcpy(eventsList->events[eventsList->numEvents].data, buffer, bytesReceived);
                    eventsList->numEvents++;
                    if (buffer != NULL) free(buffer);
                    buffer = NULL;
                } else if (bytesReceived == -1) {
                    // Client disconnected
                    eventsList->events = eventReallocServer(eventsList->events, eventsList->numEvents, &eventMemory);
                    eventsList->events[eventsList->numEvents].type = Disconnection;
                    eventsList->events[eventsList->numEvents].socket = server->clients[i].socket;
                    eventsList->events[eventsList->numEvents].sin = server->clients[i].sin;
                    eventsList->events[eventsList->numEvents].ipType = server->ipType;
                    eventsList->events[eventsList->numEvents].data = NULL;
                    eventsList->numEvents++;
                    if (buffer != NULL) free(buffer);
                    clientDisconnect(server, i);
                    break;
                } else {
                    if (buffer != NULL) free(buffer);
                    break;
                }

            } while (bytesReceived > 0);
        }
    }

    return eventsList;
}

void clientDisconnect(Server* server, int index) {
    FD_CLR(server->clients[index].socket, &server->socketSet); // Remove the client's socket from the server's socket set
    closesocket(server->clients[index].socket); // Close the client's socket

    // replace the disconnected client with the last client in the list
    server->clients[index] = server->clients[server->numClients - 1];

    server->numClients--; // Decrement the number of clients connected to the server
}

Client* createClient(const char* address, int port, int connType, int ipType) {
    Client* client = (Client*)calloc(1, sizeof(Client)); // Create the client's structure
    if (!client) return NULL;

    client->recSize = sizeof(client->sin);

    // Create the client's socket
    int type = (connType == TCP) ? SOCK_STREAM : SOCK_DGRAM; // Support for TCP and UDP
    int ip = (ipType == IPv4) ? AF_INET : AF_INET6; // Support for IPv4 and IPv6

    client->socket = socket(ip, type, 0);
    if (client->socket == INVALID_SOCKET) {
        free(client);
        return NULL;
    }

    client->connType = connType;
    client->ipType = ipType;

     // Init the client's buffer
    client->bufferData.buffer = malloc(BufferSize);
    if (!client->bufferData.buffer) {
        closesocket(client->socket);
        free(client);
        return NULL;
    }
    client->bufferData.len = 0;
    client->bufferData.pos = 0;

    int status = 0;
    // Set the client's information
    switch (ip) {
        case AF_INET:
            client->sin.in.sin_family = ip;
            client->sin.in.sin_port = htons(port);
            status = inet_pton(ip, address, &client->sin.in.sin_addr.s_addr);
            break;
        case AF_INET6:
            client->sin.in6.sin6_family = ip;
            client->sin.in6.sin6_port = htons(port);
            status = inet_pton(ip, address, &client->sin.in6.sin6_addr);
            break;
    }

    if (status != 1) {
        fprintf(stderr, "Invalid IP address\n");
        free(client->bufferData.buffer);
        closesocket(client->socket);
        free(client);
        return NULL;
    }

    // Connect the client's socket
    if (connect(client->socket, (SOCKADDR*)&client->sin, client->recSize) == SOCKET_ERROR) {
        fprintf(stderr, "Error connecting the client's socket\n");
        free(client->bufferData.buffer);
        closesocket(client->socket);
        free(client);
        return NULL;
    }

    // Add the client to the set of sockets to listen to
    FD_ZERO(&client->socketSet);
    FD_SET(client->socket, &client->socketSet);

    return client;
}

void closeClient(Client* client) {
    free(client->bufferData.buffer);
    closesocket(client->socket);
    free(client);
}

/*
    Parameters:
        - ClientEvent* events : The lists of actual events
        - int numEvents : The number of events
        - int* eventMemory : The size of the list
    Output:
        - ClientEvent* : The new address of the events' list if creation succeeded, the old address otherwise
    Description:
        This function reallocate space of the client's events list if needed.
*/
ClientEvent* eventReallocClient(ClientEvent* events, int numEvents, int* eventMemory) {
    if (numEvents >= *eventMemory) {
        *eventMemory += EventBlock;
        ClientEvent* temp = realloc(events, sizeof(ClientEvent) * *eventMemory);
        if (!temp) {
            fprintf(stderr, "Memory allocation failed for events\n");
        }
        events = temp;
    }
    return events;
}

ClientEventsList* clientListen(Client* client) {
    ClientEventsList* eventsList = (ClientEventsList*)malloc(sizeof(ClientEventsList)); // Create the list of events

    eventsList->numEvents = 0; // Initialize the number of events to 0

    int eventMemory = EventBlock;
    eventsList->events = (ClientEvent*)malloc(sizeof(ClientEvent) * eventMemory); // Create the array of events

    while (1) {
        // Define the timeout for the select function
        struct timeval timeout;
        timeout.tv_sec = 0; // 0 seconds
        timeout.tv_usec = 50; // 50 microseconds

        // Copy the client's socket set
        fd_set copySet = client->socketSet;

        // Select the sockets that are ready for reading
        int numReady = select(client->socket + 1, &copySet, NULL, NULL, &timeout);

        if (numReady <= 0) {
            break; // Timeout or error
        }

        // Check if the client's socket is ready for reading
        if (FD_ISSET(client->socket, &copySet)) {
            // Handling data received from the server
            char* buffer = NULL;
            int bytesReceived = 0;
            if (client->connType == UDP) {
                buffer = (char*)malloc(BufferSize);
                bytesReceived = recvfrom(client->socket, buffer, BufferSize-1, 0, (SOCKADDR*)&client->sin, &client->recSize);
            }
            else if (client->connType == TCP) {
                // Receive the data from the server
                bytesReceived = readMessage(client, &buffer);
            }

            // Check if the server disconnected
            if (bytesReceived == -1 && client->connType == TCP) {
                eventsList->events = eventReallocClient(eventsList->events, eventsList->numEvents, &eventMemory);

                // Add the event to the list
                eventsList->events[eventsList->numEvents].type = Disconnection;
                eventsList->events[eventsList->numEvents].data = NULL;

                eventsList->numEvents++; // Increment the number of events
            }
            else if (bytesReceived > 0) {
                // Data received
                // Limit the number of bytes received to the buffer size
                bytesReceived = (bytesReceived < BufferSize) ? bytesReceived : BufferSize - 1;
                
                eventsList->events = eventReallocClient(eventsList->events, eventsList->numEvents, &eventMemory);
                
                // Add the event to the list
                eventsList->events[eventsList->numEvents].type = DataReceived;
                eventsList->events[eventsList->numEvents].dataSize = bytesReceived;
                eventsList->events[eventsList->numEvents].data = (char*)malloc(bytesReceived * sizeof(char));

                // Copy the data received to the event
                memcpy(eventsList->events[eventsList->numEvents].data, buffer, bytesReceived);
                if (buffer != NULL) free(buffer);

                eventsList->numEvents++; // Increment the number of events
            }
            else {
                if (buffer != NULL) free(buffer);
            }
        }
    }
    
    return eventsList;
}

int readMessage(Client* client, char **msg) {
    ClientBuffer* bfData = &client->bufferData;
    int haveLen = 0;
    // Loop until we read a complete message or a disconnection/error
    while (1) {
        // In this case we verify if we already have the 4 bytes containing the message's length
        if (bfData->len - bfData->pos >= 4) {
            uint32_t lenNet;
            memcpy(&lenNet, bfData->buffer + bfData->pos, 4);
            uint32_t msgLen = ntohl(lenNet); // Convert message length from network byte order
            haveLen = 1;
            // Case when the message length is longer than the buffer's size
            // Have to be handled by the developper at emission or by changing the BufferSize macro
            if (msgLen > BufferSize - 4) {
                return -1;
            }

            // Copying of the message
            if (bfData->len - bfData->pos - 4 >= (int)msgLen) {
                *msg = malloc(msgLen + 1); // Allocate space for message (+1 for null terminator)
                if (!*msg) return -1; // malloc did not work

                memcpy(*msg, bfData->buffer + bfData->pos + 4, msgLen); // Copy message data from buffer
                (*msg)[msgLen] = '\0'; // Null-terminate the string

                bfData->pos += 4 + msgLen; // Move buffer position past the current message

                return msgLen; // Return number of bytes read (excluding header's length)
            }
        }

        // If there's remaining unread data in the buffer, compact it to the start
        if (bfData->len > 0 && bfData->pos < bfData->len) {
            memmove(bfData->buffer, bfData->buffer + bfData->pos, bfData->len - bfData->pos);
            bfData->len -= bfData->pos;
            bfData->pos = 0;
        }

        // Read more data from the socket into the buffer
        int n = recv(client->socket, bfData->buffer + bfData->len, BufferSize - bfData->len, 0);
        if (n < 0) {
            // Handle non-blocking socket error (platform-dependent)
            // Continue to loop if the error is linked to the lack of data in the buffer
            // i.d. wait for the remaining data to arrive
            #ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                if (!haveLen) return 0; // No partial message
                else continue; // No data yet, continue loop
            }
            #else
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if (!haveLen) return 0; // No partial message
                else continue; // No data yet, continue loop
            }
            #endif
            return -1; // Other socket error
        } else if (n == 0) {
            return -1; // Connection closed by peer
        }

        bfData->len += n; // Update buffer length 
    }
}

void sendMessage(SOCKET* socket, const char *msg, uint32_t len, int connType, int ipType, SIN* sin) {
    if (connType == TCP) {
        uint32_t len_net = htonl(len);
        int sent = send(*socket, (char *)&len_net, 4, 0);
        sent = send(*socket, msg, len, 0);
    }
    else if (connType == UDP) {
        if (ipType == IPv4) {
            sendto(*socket, msg, len, 0, (SOCKADDR*)&sin->in, sizeof(sin->in));
        }
        else if (ipType == IPv6) {
            sendto(*socket, msg, len, 0, (SOCKADDR*)&sin->in6, sizeof(sin->in6));
        }
    }
}

char* resolveDomainName(const char* domainName) {
    struct addrinfo hints, *res, *p;
    int status;
    char *ipstr = NULL;
    char buf[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // Support both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP

    status = getaddrinfo(domainName, NULL, &hints, &res);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo error");
        return NULL;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        void *addr;

        if (p->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
        } else if (p->ai_family == AF_INET6) { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
        } else {
            continue; // Unsupported address family
        }

        if (inet_ntop(p->ai_family, addr, buf, sizeof(buf)) == NULL) {
            fprintf(stderr, "inet_ntop failed.\n");
            freeaddrinfo(res);
            return NULL;
        }

        ipstr = malloc(strlen(buf) + 1);
        if (ipstr == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            freeaddrinfo(res);
            return NULL;
        }

        strcpy(ipstr, buf);
        break; // Take the first valid result
    }

    freeaddrinfo(res);
    return ipstr;
}