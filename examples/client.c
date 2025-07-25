#include "NSC.h"

/*
Name : C^LI's Client
This client is a demonstration tool used to showcase and test the features of NSC.
It supports all four configuration modes: TCP/UDP over IPv4/IPv6.
This is a very basic chat application, designed to demonstrate and test what can be done with NSC.
There is a lot of confusing stuff in this code (threads, non-canonical mode, etc...).
Don't worry, it's just about me who wanted to have a "clean" chat system in the CLI without having to use a lot of libraries.
You can just focus about how NSC's working.
I hope you'll find it useful ! ^^
Author : TK
*/

#if defined(_WIN32)
    #include <windows.h>
    DWORD WINAPI listenerThread(LPVOID arg);
    #include "conio.h"
#else
    #include <pthread.h>
    #include <termios.h>
    #include <unistd.h>
    void* listenerThread(void* arg);
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
void enableImmediateInput() {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void disableImmediateInput() {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag |= ICANON | ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}
#endif

#define MAX_MSG 10
#define MAX_NAME_SIZE 20
int nbMsg = 0; // # messages receive
char* messages[MAX_MSG]; // Buffer of the messages
char inputBuffer[1500]; // Buffer to store the user's entries
uint32_t len = 0; // Len of inputBuffer
volatile int running = 1; // Variable which control the thread's loop
char name[MAX_NAME_SIZE]; // Buffer with the name of the client

// Transform the given array in a queue where the first element is pop when there is more than MAX_MSG
void addMessage(char** messages, char* newMessage) {
    if (nbMsg < MAX_MSG) {
        messages[nbMsg] = newMessage;
        nbMsg++;
    }
    else {
        free(messages[0]);
        for (int i = 1; i < MAX_MSG; ++i) {
            messages[i-1] = messages[i];
        }
        messages[MAX_MSG-1] = newMessage;
    }
}

// Clear the screen
void clearScreen() {
#if defined(_WIN32)
    system("cls");
#else
    system("clear");
#endif
}

// Draw the chat in the CLI
void drawChat() {
    clearScreen();
    printf("------------------------ C^LI ------------------------\n");
    for (int i = 0; i < MAX_MSG; ++i) {
        if (i < nbMsg) printf("%s\n", messages[i]);
        else printf("\n\n");
    }
    printf("Enter a message : %s", inputBuffer);
}

// Function in which every client's events will be processed
// Designed to be run in a thread
#if defined(_WIN32)
DWORD WINAPI listenerThread(LPVOID arg)
#else
void* listenerThread(void* arg)
#endif
{
    Client* client = (Client*)arg;

    while (running) {
        ClientEventsList* events = clientListen(client);
        if (events->numEvents == 0) {
            free(events);
            #if defined(_WIN32)
                Sleep(10);
            #else
                usleep(10000);
            #endif
            continue;
        }

        for (int i = 0; i < events->numEvents; i++) {
            ClientEvent event = events->events[i];
            if (event.type == DataReceived) {
                addMessage(messages, event.data);
                drawChat();
            }
        }
        free(events->events);
        free(events);
    }

    return 0;
}

int main() {
    #if defined(_WIN32)
        startup();
    #else
        enableImmediateInput();
    #endif

    // Client's Configuration (must match the Server)
    int usedConnType = TCP;
    int usedIpType = IPv4;
    const char* address = usedIpType == IPv4 ? "127.0.0.1" : "::1";

    // Create the client
    Client* client = createClient(address, 25565, usedConnType, usedIpType);
    if (client == NULL) {
        printf("Error creating the client\n");
        return 1;
    }

    printf("Welcome to C^LI !\nPlease enter your name : ");
    // You have to enter you desired name (max MAX_NAME_SIZE chars)
    fgets(name, MAX_NAME_SIZE, stdin);
    name[strlen(name)-1] = '\0'; // Remove the '\n'

    // Lancement du thread d'écoute
    #if defined(_WIN32)
        HANDLE thread;
        thread = CreateThread(NULL, 0, listenerThread, client, 0, NULL);
    #else
        pthread_t thread;
        pthread_create(&thread, NULL, listenerThread, client);
    #endif

    while (1) {
        char c = 'a';
        len = 0;
        inputBuffer[len] = '\0';
        drawChat();

        // Entries reading (cross-platform)
        #if defined(_WIN32)
        while (1) {
            if (_kbhit()) {
                c = _getch();
                if (c == '\r') break;
                if (c == '\b') { // Backspace
                    if (len <= 0) continue;
                    len--;
                    inputBuffer[len] = '\0';
                    drawChat();
                }
                else {
                    inputBuffer[len] = c;
                    len++;
                    inputBuffer[len] = '\0';
                    printf("%c", c);
                }
            }
        }
        #else
        while (1) {
            c = fgetc(stdin);
            if (c == '\n') break;
            if (c == 127 || c == '\b') { // DEL or Backspace
                if (len <= 0) continue;
                len--;
                inputBuffer[len] = '\0';
                drawChat();
            }
            else {
                inputBuffer[len] = c;
                len++;
                inputBuffer[len] = '\0';
                printf("%c", c);
            }
        }
        #endif
        
        // Properly disconnect with !quit
        if (!strcmp(inputBuffer, "!quit")) {
            running = 0;
            break;
        }
        if (len <= 0) continue;
        char* localBuffer = malloc(MAX_NAME_SIZE + len); // "You : " + inputBuffer
        sprintf(localBuffer, "%s : %s\n", name, inputBuffer);
        addMessage(messages, localBuffer); // Add your message

        // Send your message
        sendMessage(&client->socket, localBuffer, strlen(localBuffer)+1, usedConnType, usedIpType, &client->sin); 
    }

    // Cleaning
    #if defined(_WIN32)
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    #else
        pthread_join(thread, NULL);
    #endif

    closeClient(client);

    #if defined(_WIN32)
        cleanup();
    #else 
        disableImmediateInput();
    #endif

    return 0;
}
