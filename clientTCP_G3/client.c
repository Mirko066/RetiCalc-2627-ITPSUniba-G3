#if defined(_WIN32)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define closesocket(s) close(s)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BUFFER_SIZE 64

static void error_handler(const char *msg) {
    fprintf(stderr, "Errore: %s\n", msg);
}

static int sockets_init(void) {
#if defined(_WIN32)
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
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

static int recv_string(SOCKET s, char *buf, int size) {
    int i;
    for (i = 0; i < size; i++) {
        if (recv(s, &buf[i], 1, 0) <= 0) return -1;
        if (buf[i] == '\0') return i;
    }
    return -1;
}

static int read_line(const char *prompt, char *buf, int size) {
    printf("%s", prompt);
    fflush(stdout);
    return fgets(buf, size, stdin) != NULL;   /* 0 = EOF */
}

static int read_host(char *host) {
    char line[128];
    for (;;) {
        if (!read_line("Inserisci l'IP del server (127.0.0.1): ", line, sizeof line))
            return 0;
        if (sscanf(line, "%63s", host) == 1)
            return 1;
        printf("Valore non valido, riprova.\n");
    }
}

static int read_port(int *port) {
    char line[64], extra;
    long p;
    for (;;) {
        if (!read_line("Inserisci la porta del server: ", line, sizeof line))
            return 0;
        if (sscanf(line, "%ld %c", &p, &extra) == 1 && p >= 1 && p <= 65535) {
            *port = (int)p;
            return 1;
        }
        printf("Porta non valida (1-65535), riprova.\n");
    }
}

static int read_three_ints(int32_t *n1, int32_t *n2, int32_t *n3) {
    char line[128], extra;
    long long a, b, c;
    for (;;) {
        if (!read_line("Inserisci tre interi n1 n2 n3 (separati da spazio): ", line, sizeof line))
            return 0;
        if (sscanf(line, "%lld %lld %lld %c", &a, &b, &c, &extra) == 3
            && a >= INT32_MIN && a <= INT32_MAX
            && b >= INT32_MIN && b <= INT32_MAX
            && c >= INT32_MIN && c <= INT32_MAX) {
            *n1 = (int32_t)a;
            *n2 = (int32_t)b;
            *n3 = (int32_t)c;
            return 1;
        }
        printf("Input non valido: servono esattamente tre interi a 32 bit.\n");
    }
}

static SOCKET connect_to_server(const char *host, int port) {
    struct addrinfo hints, *res = NULL;
    char port_str[16];
    SOCKET s;

    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    snprintf(port_str, sizeof port_str, "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL) {
        error_handler("indirizzo del server non valido");
        return INVALID_SOCKET;
    }

    s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        error_handler("creazione della socket fallita");
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }

    if (connect(s, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        error_handler("connessione al server fallita");
        closesocket(s);
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }

    freeaddrinfo(res);
    return s;
}

int main(void) {
    char host[64];
    char buf[BUFFER_SIZE];
    int port;
    SOCKET s;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (!sockets_init()) {
        error_handler("inizializzazione Winsock fallita");
        return EXIT_FAILURE;
    }

    if (!read_host(host) || !read_port(&port)) {
        error_handler("input interrotto");
        sockets_cleanup();
        return EXIT_FAILURE;
    }

    s = connect_to_server(host, port);
    if (s == INVALID_SOCKET) {
        sockets_cleanup();
        return EXIT_FAILURE;
    }
    printf("Connesso al server %s:%d\n", host, port);

    if (recv_string(s, buf, sizeof buf) < 0) {
        error_handler("ricezione del messaggio di benvenuto fallita");
        closesocket(s);
        sockets_cleanup();
        return EXIT_FAILURE;
    }
    printf("Server: %s\n", buf);

    for (;;) {
        int32_t n1, n2, n3;
        uint32_t net[3];

        if (!read_three_ints(&n1, &n2, &n3)) {
            printf("Input terminato: chiudo la connessione.\n");
            break;
        }
        net[0] = htonl((uint32_t)n1);
        net[1] = htonl((uint32_t)n2);
        net[2] = htonl((uint32_t)n3);
        if (send_all(s, (const char *)net, (int)sizeof net) < 0) {
            error_handler("invio dei numeri fallito");
            break;
        }

        if (recv_string(s, buf, sizeof buf) < 0) {
            error_handler("il server ha chiuso la connessione o ricezione fallita");
            break;
        }
        printf("Stringa ricevuta dal server: %s\n", buf);

        if (strcmp(buf, "QUIT") == 0) {
            printf("Ricevuto QUIT: chiudo la connessione.\n");
            break;
        }
        printf("Risultato res = %s\n", buf);
    }

    closesocket(s);
    sockets_cleanup();
    return EXIT_SUCCESS;
}
