/*
Network System for C (NSC)
Version 3.0
Starring : Thomas K/BIDI

Description:
Networking System C (NSC) is a lightweight, cross-platform library that simplifies socket usage with tools for server/client handling, an event system, and message framing over TCP.
It lets you create server and clients in IPv4 or IPv6 with TCP and UDP communication protocols.
You can also do domain name resolution.
*/

#ifndef NSC_H
#define NSC_H

// Includes
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

// Definition according to the OS used - to make the library cross-platform
#if defined (_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment (lib, "Ws2_32.lib")

#ifndef SHUT_RD
#define SHUT_RD SD_RECEIVE
#endif

#ifndef SHUT_WR
#define SHUT_WR SD_SEND
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif

typedef int socklen_t; // socklen_t is not defined in Windows

// Function use to start the Winsock2 library (Windows only)
static inline void startup() {
    /*
    Description:
        This function starts the Winsock2 library.
    */
    WSADATA WSAData;
    int error;
    error = WSAStartup(MAKEWORD(2, 2), &WSAData);
    if (error) {
        fprintf(stderr, "WSAStartup failed with error : %d\n", error);
        exit(1);
    }
}

static inline void cleanup() {
    /*
    Description:
        This function cleans up the Winsock2 library.
    */
    WSACleanup();
}
#elif defined (__linux__)
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

// We define the elements which doesn't exist in Linux
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(s) close(s)

typedef int SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr SOCKADDR;
#endif

#ifdef __cplusplus
extern "C" {
#endif
    // Definition for the connection's type
    enum NSC_ConnType { TCP, UDP };
    enum NSC_IP_Type { IPv4, IPv6 };

    // Event definition
    enum NSC_EventType { Connection, DataReceived, Disconnection };

    // Constants
    #define MaxClients 100 // Maximum number of clients on the server
    #define ReadChunk 8192 // How many bytes we take from the socket at once (performance detail)
    #define BufferSize ReadChunk // Kept for source compatibility, and used for UDP datagrams
    #define MaxMessageSize (16 * 1024 * 1024) // Largest announced length we accept to allocate (anti-abuse policy)
    #define SendTimeoutMs 5000 // How long sendMessage waits for a saturated send buffer to drain
    #define QueueLength 65535 // Maximum length of the queue of pending connections
    #define EventBlock 8 // Block of events to allocate

    #define READMSG_NO_DATA          0   // Not enough data yet for a full message
    #define READMSG_CONN_CLOSED     -1   // Connection closed by peer (recv() == 0)
    #define READMSG_ALLOC_FAILED    -2   // Memory allocation failure
    #define READMSG_MSG_TOO_LARGE   -3   // Announced length above MaxMessageSize
    #define READMSG_SOCKET_ERROR    -4   // Socket error other than non-blocking wait
    #define READMSG_BAD_FRAME       -6   // Announced length is invalid : the stream is desynchronised

    #define SENDMSG_OK               0   // The whole message reached the send buffer
    #define SENDMSG_INVALID_ARG     -1   // NULL socket/address, or a length of 0
    #define SENDMSG_MSG_TOO_LARGE   -3   // Length above MaxMessageSize
    #define SENDMSG_SOCKET_ERROR    -4   // Socket error while sending
    #define SENDMSG_TIMEOUT         -5   // Send buffer stayed full for SendTimeoutMs

    // Union for the address
    typedef union {
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
    } SIN;

    typedef struct {
        int type; // Type of the event (Connection, DataReceived, Disconnection)
        SOCKET socket; // Socket of the client that triggered the event
        char* data; // Data received
        uint32_t dataSize;
        int ipType; // IP type (IPv4 or IPv6)
        SIN sin; // Address of the client
    } ServerEvent;

    // Structures for the network system
    // Structure that will contain the list of events that occurred on a server
    typedef struct {
        ServerEvent* events;
        int numEvents;
    } ServerEventsList;

    // ClientEvent
    typedef struct {
        int type;
        char* data;
        uint32_t dataSize;
    } ClientEvent;

    typedef struct {
        ClientEvent* events;
        int numEvents;
    } ClientEventsList;

    // Client's buffer informations
    typedef struct {
        char* buffer;
        int cap; // Allocated size of the buffer : grows on demand up to 4 + MaxMessageSize
        int len; // Number of valid bytes held in the buffer
        int pos; // Reading position inside those valid bytes
    } ClientBuffer;

    // Client's structure
    typedef struct {
        ClientBuffer bufferData;
        SOCKET socket; // The client's address
        SIN sin; // The client's address
        socklen_t recSize;
        fd_set socketSet; // File descriptor set
        int connType; // The connection type (TCP or UDP)
        int ipType; // The IP type (IPv4 or IPv6)
    } Client;

    // Server's structure
    typedef struct {
        SOCKET socket; // The server's socket
        SIN sin; // The server's address
        socklen_t recSize;

        int connType; // The connection type (TCP or UDP)
        int ipType; // The IP type (IPv4 or IPv6)
        fd_set socketSet; // Descriptor set for the server
        int maxSocket; // Value of the maximum socket
        int numClients; // Number of clients connected to the server
        Client* clients; // List of clients connected to the server
    } Server;

    /*
    Parameters:
        - char* address : The address of the server
        - int port : The port of the server
        - int connType : The connection type (TCP or UDP)
        - int ipType : The IP type (IPv4 or IPv6)
    Output:
        - Server* : The server created with the given parameters (NULL if an error occurred)
    Description:
        This function creates a server with the given parameters and returns it.
    */
    Server* createServer(const char* address, int port, int connType, int ipType);

    /*
    Parameters:
        - Server* server : The server to close
    Description:
        This function closes the server and frees the memory allocated for it.
    */
    void closeServer(Server* server);

    /*
    Parameters:
        - Server* server : The server to accept the client from
    Output:
        - Client* : The client that was accepted
    Description:
        This function accepts a client's connection to the server and returns a pointer to the client.
    */
    Client* acceptClient(Server* server);

    /*
    Parameters:
        - Server* server : The server to update
    Output:
        - ServerEventsList* : The list of events that occurred on the server
    Description:
        This function updates the server and returns the list of events that occurred on it.
    */
    ServerEventsList* serverListen(Server* server);

    /*
    Parameters:
        - Server* server : The server to disconnect the client from
        - int index : The index of the client to disconnect
    Description:
        This function disconnects the client from the server.
    */
    void clientDisconnect(Server* server, int index);

    /*
    Parameters:
        - char* address : The address of the client
        - int port : The port of the client
        - int connType : The connection type (TCP or UDP)
        - int ipType : The IP type (IPv4 or IPv6)
    Output:
        - Client* : The client created with the given parameters (NULL if an error occurred)
    Description:
        This function creates a client with the given parameters and returns it.
    */
    Client* createClient(const char* address, int port, int connType, int ipType);

    /*
    Parameters:
        - Client* client : The client to close
    Description:
        This function closes the client and frees the memory allocated for it.
    */
    void closeClient(Client* client);

    /*
    Parameters:
        - Client* client : The client on which we have to listen for events
    Output:
        - ClientEventsList* : The list of events that occurred on the client
    Description:
        This function listens for events on the client and returns the list of events that occurred.
    */
    ClientEventsList* clientListen(Client* client);

    /*
    Parameters:
        - Client* client : The client whose socket and accumulation buffer are read
        - char** out_msg : The buffer to which the data will be assigned (caller must free it)
    Output:
        - int : The length of the message on success, or one of the READMSG_* codes
    Description:
        For a TCP connexion, read the message of the following format ->
        [length : 4 bytes][message]
        and give the **msg the address of the message's buffer.
        The call never blocks : when only a part of a message has arrived it
        returns READMSG_NO_DATA and keeps the fragment in the client's buffer,
        which grows on demand up to MaxMessageSize.
        READMSG_MSG_TOO_LARGE and READMSG_BAD_FRAME mean the stream cannot be
        parsed any more : the caller has to drop the connection.
    */
    int readMessage(Client* client, char **out_msg);

    /*
    Parameters:
        - SOCKET* socket : The socket to send the data to
        - const char *msg : The data you want to send
        - uint32_t len : The length of the data
        - int connType : The type of connection on which you want to send the data
        - SOCKADDR_IN* sin : The address to send the data to
    Output:
        - int : SENDMSG_OK on success, or one of the other SENDMSG_* codes
    Description:
        This function sends the data to the given socket according to the type
        of connection you want.
        In the case of UDP the length of the message
        is not send.
        Over TCP the whole message is sent : the sockets are non-blocking, so a
        saturated send buffer is waited on rather than treated as an error.
        Truncating here would leave the length header on the wire without its
        payload and desynchronise the peer for good.
    */
    int sendMessage(SOCKET* socket, const char *msg, uint32_t len, int connType, int ipType, SIN* sin);

    /*
    Parameters:
        - const char* domainName : The domain name to resolve
    Output:
        - char* : The IP address of the domain name (caller must free the returned string)
    Description:
        This function resolves a domain name and returns the associated IP address.
    */
    char* resolveDomainName(const char* domainName);

#ifdef __cplusplus
}
#endif

#endif