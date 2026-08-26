/* Framing tests for NSC 3.0 : the cases that used to lose messages silently. */
#include "NSC.h"

int sendAll(SOCKET socket, const char* data, uint32_t len); /* internal helper of NSC.c */

#define PORT 45871

static int failures = 0;
static Server* server = NULL;
static Client* client = NULL;

/* Deterministic filler so we can verify the payload came back byte for byte. */
static char* makePayload(int len) {
    char* p = (char*)malloc(len + 1);
    for (int i = 0; i < len; i++) p[i] = (char)('A' + (i % 26));
    p[len] = '\0';
    return p;
}

static void check(const char* name, int ok) {
    printf("  %-52s %s\n", name, ok ? "OK" : "FAILED");
    if (!ok) failures++;
}

/* Pump the server and collect payloads, stopping once we have what we expect. */
static int pumpServer(char** out, int* outLens, int expected, int rounds) {
    int n = 0;
    for (int r = 0; r < rounds && n < expected; r++) {
        ServerEventsList* ev = serverListen(server);
        for (int i = 0; i < ev->numEvents; i++) {
            if (ev->events[i].type == DataReceived && n < expected) {
                outLens[n] = ev->events[i].dataSize;
                out[n] = (char*)malloc(ev->events[i].dataSize + 1);
                memcpy(out[n], ev->events[i].data, ev->events[i].dataSize);
                out[n][ev->events[i].dataSize] = '\0';
                n++;
            }
            else if (ev->events[i].type == Disconnection) {
                printf("    [!] server reported a Disconnection\n");
            }
            free(ev->events[i].data);
        }
        free(ev->events);
        free(ev);
    }
    return n;
}

/*
    A payload larger than the socket buffers cannot be pushed and read by the
    same thread : the sender fills the kernel buffer and waits for a reader that
    never runs. The sends go to a worker thread and the main thread drains.
*/
typedef struct {
    const char* data;
    uint32_t len;
    int repeat;
    int status;
} SendJob;

static void sendJobRun(SendJob* job) {
    job->status = SENDMSG_OK;
    for (int i = 0; i < job->repeat; i++) {
        int st = sendMessage(&client->socket, job->data, job->len, TCP, IPv4, &client->sin);
        if (st != SENDMSG_OK) job->status = st;
    }
}

/* Just enough of a thread wrapper to keep these tests running on both systems. */
#if defined(_WIN32)
typedef HANDLE TestThread;

static DWORD WINAPI sendWorker(LPVOID param) {
    sendJobRun((SendJob*)param);
    return 0;
}

static void sendInBackground(SendJob* job, TestThread* thread) {
    *thread = CreateThread(NULL, 0, sendWorker, job, 0, NULL);
}

static void joinThread(TestThread thread) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
}
#else
#include <pthread.h>
typedef pthread_t TestThread;

static void* sendWorker(void* param) {
    sendJobRun((SendJob*)param);
    return NULL;
}

static void sendInBackground(SendJob* job, TestThread* thread) {
    pthread_create(thread, NULL, sendWorker, job);
}

static void joinThread(TestThread thread) {
    pthread_join(thread, NULL);
}
#endif

/* One message of the given size, sent alone. */
static void testSingle(int len) {
    char name[128];
    sprintf(name, "one message of %d bytes", len);

    char* payload = makePayload(len);
    SendJob job = { payload, (uint32_t)len, 1, SENDMSG_OK };
    TestThread th;
    sendInBackground(&job, &th);

    char* got[4]; int gotLens[4];
    int n = pumpServer(got, gotLens, 1, 400);
    joinThread(th);

    int ok = (job.status == SENDMSG_OK) && (n == 1) && (gotLens[0] == len) && (memcmp(got[0], payload, len) == 0);
    if (!ok) printf("    send=%d received=%d len=%d (expected %d)\n", job.status, n, n ? gotLens[0] : -1, len);
    check(name, ok);

    for (int i = 0; i < n; i++) free(got[i]);
    free(payload);
}

/* Two messages pushed back to back, without letting the peer read in between. */
static void testBackToBack(int lenA, int lenB) {
    char name[128];
    sprintf(name, "%d + %d bytes back to back", lenA, lenB);

    char* a = makePayload(lenA);
    char* b = makePayload(lenB);

    int sa = sendMessage(&client->socket, a, lenA, TCP, IPv4, &client->sin);
    int sb = sendMessage(&client->socket, b, lenB, TCP, IPv4, &client->sin);

    char* got[4]; int gotLens[4];
    int n = pumpServer(got, gotLens, 2, 200);

    int ok = (sa == SENDMSG_OK) && (sb == SENDMSG_OK) && (n == 2)
             && (gotLens[0] == lenA) && (memcmp(got[0], a, lenA) == 0)
             && (gotLens[1] == lenB) && (memcmp(got[1], b, lenB) == 0);
    if (!ok) printf("    sendA=%d sendB=%d received=%d\n", sa, sb, n);
    check(name, ok);

    for (int i = 0; i < n; i++) free(got[i]);
    free(a); free(b);
}

/* Many messages in a row, summing far above the read chunk. */
static void testBurst(int count, int len) {
    char name[128];
    sprintf(name, "%d messages of %d bytes in a burst", count, len);

    char* payload = makePayload(len);
    SendJob job = { payload, (uint32_t)len, count, SENDMSG_OK };
    TestThread th;
    sendInBackground(&job, &th);

    char* got[512]; int gotLens[512];
    int n = pumpServer(got, gotLens, count, 800);
    joinThread(th);

    int ok = (job.status == SENDMSG_OK) && (n == count);
    if (ok) {
        for (int i = 0; i < n; i++) {
            if (gotLens[i] != len || memcmp(got[i], payload, len) != 0) { ok = 0; break; }
        }
    }
    if (!ok) printf("    sendStatus=%d received=%d (expected %d)\n", job.status, n, count);
    check(name, ok);

    for (int i = 0; i < n; i++) free(got[i]);
    free(payload);
}

/* A length header split across two TCP reads. */
static void testSplitHeader(void) {
    int len = 5000;
    char* payload = makePayload(len);

    uint32_t lenNet = htonl((uint32_t)len);
    sendAll(client->socket, (const char*)&lenNet, 2);   /* half the header */

    char* got[4]; int gotLens[4];
    int early = pumpServer(got, gotLens, 1, 10);        /* nothing should surface yet */

    sendAll(client->socket, ((const char*)&lenNet) + 2, 2);
    sendAll(client->socket, payload, (uint32_t)len);

    int n = pumpServer(got, gotLens, 1, 200);

    int ok = (early == 0) && (n == 1) && (gotLens[0] == len) && (memcmp(got[0], payload, len) == 0);
    if (!ok) printf("    early=%d received=%d\n", early, n);
    check("length header split across two reads", ok);

    for (int i = 0; i < n; i++) free(got[i]);
    free(payload);
}

/* A message delivered in two halves with a pause in the middle. */
static void testSplitBody(void) {
    int len = 12000;
    char* payload = makePayload(len);

    uint32_t lenNet = htonl((uint32_t)len);
    sendAll(client->socket, (const char*)&lenNet, 4);
    sendAll(client->socket, payload, 6000);

    char* got[4]; int gotLens[4];
    int early = pumpServer(got, gotLens, 1, 20);        /* half a message is not a message */

    sendAll(client->socket, payload + 6000, (uint32_t)(len - 6000));
    int n = pumpServer(got, gotLens, 1, 200);

    int ok = (early == 0) && (n == 1) && (gotLens[0] == len) && (memcmp(got[0], payload, len) == 0);
    if (!ok) printf("    early=%d received=%d\n", early, n);
    check("message split across two reads with a pause", ok);

    for (int i = 0; i < n; i++) free(got[i]);
    free(payload);
}

/* Lengths the library has to refuse without allocating. */
static void testInvalidLengths(void) {
    char* payload = makePayload(16);

    check("sendMessage refuses a length of 0",
          sendMessage(&client->socket, payload, 0, TCP, IPv4, &client->sin) == SENDMSG_INVALID_ARG);
    check("sendMessage refuses MaxMessageSize + 1",
          sendMessage(&client->socket, payload, MaxMessageSize + 1, TCP, IPv4, &client->sin) == SENDMSG_MSG_TOO_LARGE);

    free(payload);
}

/*
    The other direction : clientListen used to go back through select() between
    two messages, so anything already sitting in the buffer stayed unread until
    the next byte happened to arrive.
*/
static void testClientDirection(int lenA, int lenB) {
    char name[128];
    sprintf(name, "server -> client, %d + %d bytes in one read", lenA, lenB);

    char* a = makePayload(lenA);
    char* b = makePayload(lenB);

    SOCKET peer = server->clients[0].socket;
    int sa = sendMessage(&peer, a, lenA, TCP, IPv4, &server->clients[0].sin);
    int sb = sendMessage(&peer, b, lenB, TCP, IPv4, &server->clients[0].sin);

    int received = 0, okPayload = 1;
    for (int r = 0; r < 60 && received < 2; r++) {
        ClientEventsList* ev = clientListen(client);
        for (int i = 0; i < ev->numEvents; i++) {
            if (ev->events[i].type == DataReceived) {
                const char* expect = (received == 0) ? a : b;
                int expectLen = (received == 0) ? lenA : lenB;
                if ((int)ev->events[i].dataSize != expectLen ||
                    memcmp(ev->events[i].data, expect, expectLen) != 0) okPayload = 0;
                received++;
            }
            free(ev->events[i].data);
        }
        free(ev->events);
        free(ev);
    }

    int ok = (sa == SENDMSG_OK) && (sb == SENDMSG_OK) && (received == 2) && okPayload;
    if (!ok) printf("    sendA=%d sendB=%d received=%d payloadOk=%d\n", sa, sb, received, okPayload);
    check(name, ok);

    free(a); free(b);
}

/* An absurd announced length must be reported, not allocated. */
static void testAbsurdAnnouncedLength(void) {
    Client* rogue = createClient("127.0.0.1", PORT, TCP, IPv4);
    char* got[4]; int gotLens[4];
    pumpServer(got, gotLens, 1, 20); /* let the server accept it */

    uint32_t bogus = htonl(0xFFFFFFF0u);
    sendAll(rogue->socket, (const char*)&bogus, 4);

    int sawDisconnection = 0;
    for (int r = 0; r < 60 && !sawDisconnection; r++) {
        ServerEventsList* ev = serverListen(server);
        for (int i = 0; i < ev->numEvents; i++) {
            if (ev->events[i].type == Disconnection) sawDisconnection = 1;
            free(ev->events[i].data);
        }
        free(ev->events);
        free(ev);
    }

    check("an absurd announced length drops the connection", sawDisconnection);
    closeClient(rogue);
}

int main(void) {
#if defined(_WIN32)
    startup();
#endif

    server = createServer("127.0.0.1", PORT, TCP, IPv4);
    if (!server) { printf("cannot create the server\n"); return 1; }

    client = createClient("127.0.0.1", PORT, TCP, IPv4);
    if (!client) { printf("cannot create the client\n"); return 1; }

    /* Let the server accept the connection. */
    char* got[4]; int gotLens[4];
    pumpServer(got, gotLens, 1, 20);
    if (server->numClients != 1) { printf("the client was not accepted\n"); return 1; }

    printf("\nNSC 3.0 framing tests (ReadChunk = %d, MaxMessageSize = %d)\n\n", ReadChunk, MaxMessageSize);

    printf(" Sizes around the old 8192 limit:\n");
    testSingle(ReadChunk - 5);      /* largest size the old code accepted */
    testSingle(ReadChunk - 4);
    testSingle(ReadChunk);
    testSingle(ReadChunk + 1);      /* symptom 1 : used to vanish and freeze the loop */
    testSingle(9679);               /* the size seen on CustomBoardGame */

    printf("\n Well past the old limit:\n");
    testSingle(64 * 1024);
    testSingle(1024 * 1024);

    printf("\n Several messages at once:\n");
    testBackToBack(4816, 4863);     /* symptom 2 */
    testBackToBack(6000, 6000);
    testBurst(200, 700);            /* sums far above the read chunk */
    testClientDirection(3000, 3000);/* both messages inside a single recv */
    testClientDirection(4816, 4863);

    printf("\n Fragmented arrivals:\n");
    testSplitHeader();
    testSplitBody();

    printf("\n Invalid lengths:\n");
    testInvalidLengths();
    testAbsurdAnnouncedLength();

    printf("\n%s (%d failure%s)\n\n", failures ? "FAILURES" : "ALL TESTS PASSED",
           failures, failures == 1 ? "" : "s");

    closeClient(client);
    closeServer(server);
#if defined(_WIN32)
    cleanup();
#endif
    return failures ? 1 : 0;
}
