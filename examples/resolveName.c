#include "NSC.h"

/*
Name : Domain Name Resolution example
This is a very small example on how to use the resolveDomainName function.
Author : TK
*/

int main() {
    #if defined (_WIN32)
        startup();
    #endif

    char *domain = "example.com";
    char *ip = resolveDomainName(domain);

    if (ip != NULL) {
        printf("IP Address: %s\n", ip);
        free(ip); // Free the IP string allocated in resolveDomainName
    } else {
        printf("Failed to resolve domain name.\n");
    }

    #if defined (_WIN32)
        cleanup();
    #endif

    return 0;
}
