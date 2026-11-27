#if defined(_WIN32)
    #ifndef __USE_MINGW_ANSI_STDIO
        #define __USE_MINGW_ANSI_STDIO 1
    #endif
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601     
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int addrlen_t;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <signal.h>
    typedef int SOCKET;
    typedef socklen_t addrlen_t;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define closesocket(s) close(s)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DEFAULT_PORT 27015
#define BACKLOG      5
#define BUFFER_SIZE  64

static void error_handler(const char *msg) {
    fprintf(stderr, "Errore: %s\n", msg);
}

static int sockets_init(void) {
#if defined(_WIN32)
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    signal(SIGPIPE, SIG_IGN);
    return 1;
#endif
}

static void sockets_cleanup(void) {
#if defined(_WIN32)
    WSACleanup();
#endif
}

static int send_all(SOCKET s, const char *data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, data + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

static int recv_all(SOCKET s, char *buf, int len) {
    int got = 0;
    while (got < len) {
        int n = recv(s, buf + got, len - got, 0);
        if (n <= 0) return -1;   /* 0 = il client ha chiuso, <0 = errore */
        got += n;
    }
    return 0;
}

static int send_string(SOCKET s, const char *str) {
    return send_all(s, str, (int)strlen(str) + 1);
}

static void handle_client(SOCKET cs) {
    char reply[BUFFER_SIZE];
    uint32_t net[3];

    if (send_string(cs, "connessione avvenuta") < 0) {
        error_handler("invio del messaggio di benvenuto fallito");
        return;
    }

    for (;;) {
        if (recv_all(cs, (char *)net, (int)sizeof net) < 0) {
            printf("Il client ha chiuso la connessione.\n");
            return;
        }
        int32_t n1 = (int32_t)ntohl(net[0]);
        int32_t n2 = (int32_t)ntohl(net[1]);
        int32_t n3 = (int32_t)ntohl(net[2]);

        long long res = (long long)n1 + (long long)n2 - (long long)n3;   /* 64 bit: niente overflow */
        printf("la somma di %d+%d-%d=%lld\n", (int)n1, (int)n2, (int)n3, res);

        if (res == 100) {
            if (send_string(cs, "QUIT") < 0)
                error_handler("invio di QUIT fallito");
            return;
        }

        snprintf(reply, sizeof reply, "%lld", res);
        if (send_string(cs, reply) < 0) {
            error_handler("invio del risultato fallito");
            return;
        }
    }
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    SOCKET listen_sock;
    struct sockaddr_in sad;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 1) {
        port = atoi(argv[1]);
        if (port < 1 || port > 65535) {
            fprintf(stderr, "Uso: %s [porta 1-65535]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!sockets_init()) {
        error_handler("inizializzazione Winsock fallita");
        return EXIT_FAILURE;
    }

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        error_handler("creazione della socket fallita");
        sockets_cleanup();
        return EXIT_FAILURE;
    }

#if !defined(_WIN32)
    {
        int yes = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    }
#endif

    memset(&sad, 0, sizeof sad);
    sad.sin_family      = AF_INET;
    sad.sin_addr.s_addr = htonl(INADDR_ANY);   /* accetta connessioni su tutte le interfacce */
    sad.sin_port        = htons((unsigned short)port);

    if (bind(listen_sock, (struct sockaddr *)&sad, sizeof sad) == SOCKET_ERROR) {
        error_handler("bind() fallita (porta gia' in uso?)");
        closesocket(listen_sock);
        sockets_cleanup();
        return EXIT_FAILURE;
    }

    if (listen(listen_sock, BACKLOG) == SOCKET_ERROR) {
        error_handler("listen() fallita");
        closesocket(listen_sock);
        sockets_cleanup();
        return EXIT_FAILURE;
    }

    printf("Server in ascolto sulla porta %d \n", port);

    for (;;) {
        struct sockaddr_in cad;
        addrlen_t cad_len = sizeof cad;
        char client_ip[INET_ADDRSTRLEN];
        SOCKET client_sock;

        client_sock = accept(listen_sock, (struct sockaddr *)&cad, &cad_len);
        if (client_sock == INVALID_SOCKET) {
            error_handler("accept() fallita");
            continue;
        }

        if (inet_ntop(AF_INET, &cad.sin_addr, client_ip, sizeof client_ip) == NULL)
            strcpy(client_ip, "sconosciuto");
        printf("connesso con il client il cui nome IP: %s\n", client_ip);

        handle_client(client_sock);

        closesocket(client_sock);
        printf("Connessione con %s terminata. Server in attesa di nuovi client...\n", client_ip);
    }
}
