/* Test of khdays-server. It starts the
 * server on a free port and talks to it the way a host and a guest do: it
 * registers a room and lists it, checks that a list is never longer than the
 * datagram that asked for it, that both sides are introduced, that datagrams
 * are relayed only within their pair, and that a closed room is gone. */
#include "../host/net/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAGIC 0x3144484bu
#define HEADER 16
enum { K_GONE = 2, K_REGISTER = 11, K_REGISTERED, K_LIST, K_ROOMS, K_CONNECT, K_PEER, K_PUNCH, K_RELAY, K_UNREGISTER };
#define GGID 0x00800356u
#define BUILD 0x12345678u

static unsigned char reply[65536];

static void put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, unsigned int v)
{
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

static unsigned int get16(const unsigned char *p)
{
    return (unsigned int)p[0] | (unsigned int)p[1] << 8;
}

static unsigned int get32(const unsigned char *p)
{
    return get16(p) | get16(p + 2) << 16;
}

/* A datagram of `kind` from `id`, its body padded with zeros to `padded`. */
static void send_kind(KhdaysUdp *udp, const KhdaysNetAddress *to, const unsigned char *id, int kind,
                      const unsigned char *body, unsigned int size, unsigned int padded)
{
    static unsigned char bytes[2048];
    const unsigned int length = size > padded ? size : padded;
    memset(bytes, 0, HEADER + length);
    put32(bytes, MAGIC);
    bytes[4] = (unsigned char)kind;
    bytes[5] = 1;
    put16(bytes + 6, length);
    memcpy(bytes + 8, id, 6);
    if (size > 0) {
        memcpy(bytes + HEADER, body, size);
    }
    khdays_host_udp_send(udp, to, bytes, HEADER + length);
}

/* The next datagram of `kind` within `ms`: its body size, -1 if none came;
 * `got` the whole datagram's size. */
static long wait_for(KhdaysUdp *udp, int kind, int ms, long *got)
{
    const unsigned long long until = khdays_host_net_clock_ms() + (unsigned long long)ms;
    while (khdays_host_net_clock_ms() < until) {
        KhdaysNetAddress from;
        long size;
        khdays_host_udp_wait(&udp, 1, 20);
        while ((size = khdays_host_udp_receive(udp, &from, reply, sizeof(reply))) >= 0) {
            if (size >= HEADER && get32(reply) == MAGIC && reply[4] == kind) {
                if (got != NULL) {
                    *got = size;
                }
                return (long)get16(reply + 6);
            }
        }
    }
    return -1;
}

static int fail(const char *what)
{
    fprintf(stderr, "server test: %s\n", what);
    return 1;
}

static int run(const KhdaysNetAddress *server)
{
    const unsigned char host_id[6] = {2, 1, 1, 1, 1, 1}, guest_id[6] = {2, 2, 2, 2, 2, 2};
    const unsigned char info[] = "test room";
    unsigned char body[256], announce[64];
    unsigned int token;
    long size, got;
    KhdaysUdp *host = khdays_host_udp_open(0, 0), *guest = khdays_host_udp_open(0, 0);
    if (host == NULL || guest == NULL) {
        return fail("no sockets");
    }
    /* the server up: a host registers until it answers */
    put32(announce, BUILD);
    put32(announce + 4, 0);
    put16(announce + 8, khdays_host_udp_port(host));
    announce[10] = 1;
    announce[11] = 0;
    put32(announce + 12, GGID);
    put32(announce + 16, sizeof(info));
    memcpy(announce + 20, info, sizeof(info));
    size = -1;
    for (int attempt = 0; attempt < 50 && size < 0; ++attempt) {
        send_kind(host, server, host_id, K_REGISTER, announce, 20 + sizeof(info), 0);
        size = wait_for(host, K_REGISTERED, 100, NULL);
    }
    if (size != 6 || get16(reply + HEADER + 4) != khdays_host_udp_port(host)) {
        return fail("REGISTERED did not tell the host its address");
    }
    /* the list, asked with a full datagram */
    put32(body, BUILD);
    put32(body + 4, GGID);
    put16(body + 8, 0);
    send_kind(guest, server, guest_id, K_LIST, body, 10, 1200 - HEADER);
    size = wait_for(guest, K_ROOMS, 1000, &got);
    if (size < 6 || get16(reply + HEADER) != 1 || get16(reply + HEADER + 4) != 1 ||
        memcmp(reply + HEADER + 6, host_id, 6) != 0 || get16(reply + HEADER + 18) != 20 + sizeof(info) ||
        memcmp(reply + HEADER + 20, announce, 20 + sizeof(info)) != 0) {
        return fail("the room was not listed as registered");
    }
    /* ... and with a short one: never a longer answer */
    send_kind(guest, server, guest_id, K_LIST, body, 10, 0);
    size = wait_for(guest, K_ROOMS, 1000, &got);
    if (size < 6 || got > HEADER + 10 || get16(reply + HEADER) != 1 || get16(reply + HEADER + 4) != 0) {
        return fail("a short LIST had an answer longer than itself");
    }
    /* the guest asks for the room: both sides told the other */
    memset(body, 0, 16);
    memcpy(body, host_id, 6);
    send_kind(guest, server, guest_id, K_CONNECT, body, 16, 0);
    size = wait_for(guest, K_PEER, 1000, NULL);
    if (size != 16 || memcmp(reply + HEADER + 4, host_id, 6) != 0 ||
        get16(reply + HEADER + 14) != khdays_host_udp_port(host)) {
        return fail("the guest was not told the host");
    }
    token = get32(reply + HEADER);
    size = wait_for(host, K_PEER, 1000, NULL);
    if (size != 16 || get32(reply + HEADER) != token || memcmp(reply + HEADER + 4, guest_id, 6) != 0 ||
        get16(reply + HEADER + 14) != khdays_host_udp_port(guest)) {
        return fail("the host was not told the guest");
    }
    /* relayed both ways, as it came */
    put32(body, token);
    memset(body + 4, 0x5a, HEADER + 8);
    send_kind(guest, server, guest_id, K_RELAY, body, 4 + HEADER + 8, 0);
    size = wait_for(host, K_RELAY, 1000, NULL);
    if (size != 4 + HEADER + 8 || memcmp(reply + HEADER, body, (size_t)size) != 0) {
        return fail("a datagram was not relayed to the host");
    }
    send_kind(host, server, host_id, K_RELAY, body, 4 + HEADER + 8, 0);
    if (wait_for(guest, K_RELAY, 1000, NULL) != 4 + HEADER + 8) {
        return fail("a datagram was not relayed to the guest");
    }
    /* ... never with a token not given, nor from outside the pair */
    put32(body, token + 1);
    send_kind(guest, server, guest_id, K_RELAY, body, 4 + HEADER + 8, 0);
    if (wait_for(host, K_RELAY, 300, NULL) >= 0) {
        return fail("a datagram with another token was relayed");
    }
    {
        KhdaysUdp *stranger = khdays_host_udp_open(0, 0);
        put32(body, token);
        send_kind(stranger, server, guest_id, K_RELAY, body, 4 + HEADER + 8, 0);
        if (wait_for(host, K_RELAY, 300, NULL) >= 0) {
            return fail("a datagram from outside the pair was relayed");
        }
        khdays_host_udp_close(stranger);
    }
    /* the room withdrawn: listed no more, asked for in vain */
    send_kind(host, server, host_id, K_UNREGISTER, NULL, 0, 0);
    put32(body, BUILD);
    put32(body + 4, GGID);
    put16(body + 8, 0);
    for (int attempt = 0; attempt < 10; ++attempt) {
        send_kind(guest, server, guest_id, K_LIST, body, 10, 1200 - HEADER);
        size = wait_for(guest, K_ROOMS, 300, NULL);
        if (size >= 6 && get16(reply + HEADER) == 0) {
            break;
        }
    }
    if (size < 6 || get16(reply + HEADER) != 0) {
        return fail("the room was still listed after it was withdrawn");
    }
    memset(body, 0, 16);
    memcpy(body, host_id, 6);
    send_kind(guest, server, guest_id, K_CONNECT, body, 16, 0);
    if (wait_for(guest, K_GONE, 1000, NULL) != 6) {
        return fail("a guest asking for a room gone was not told");
    }
    khdays_host_udp_close(host);
    khdays_host_udp_close(guest);
    printf("server: a room registered, listed, met and relayed within its pair only; gone when withdrawn\n");
    return 0;
}

/* --- a router of this PC, for the port the server asks of it ------------- */

/* NAT-PMP's answers (external 198.51.100.9) when `natpmp_on`; SSDP's, the
 * description and the SOAP control of a UPnP router (external 203.0.113.7)
 * always; what the server asked for kept. Documentation addresses (RFC
 * 5737), nothing real. */
static struct {
    KhdaysUdp *natpmp, *ssdp;
    SOCKET http;
    unsigned short http_port;
    volatile int natpmp_on, stop;
    volatile int natpmp_mapped, upnp_mapped;
    char add_port_mapping[2048];
} router;

static void serve_http(SOCKET c)
{
    static char request[8192];
    char answer[4096];
    const char *body = NULL;
    int got = 0;
    for (;;) {
        fd_set set;
        struct timeval wait = {2, 0};
        const char *end;
        int n;
        FD_ZERO(&set);
        FD_SET(c, &set);
        if (select(0, &set, NULL, NULL, &wait) <= 0) {
            break;
        }
        n = recv(c, request + got, (int)sizeof(request) - 1 - got, 0);
        if (n <= 0) {
            break;
        }
        got += n;
        request[got] = '\0';
        end = strstr(request, "\r\n\r\n");
        if (end != NULL) {
            const char *length = strstr(request, "Content-Length: ");
            if (length == NULL || (int)(end + 4 - request) + atoi(length + 16) <= got) {
                break; /* the whole request */
            }
        }
    }
    if (strncmp(request, "GET /desc.xml", 13) == 0) {
        body = "<?xml version=\"1.0\"?><root><device><serviceList>"
               "<service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
               "<controlURL>/ctl/L3F</controlURL></service>"
               "<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
               "<controlURL>/ctl/IPConn</controlURL></service></serviceList></device></root>";
    } else if (strstr(request, "#GetExternalIPAddress\"") != NULL) {
        body = "<?xml version=\"1.0\"?><s:Envelope><s:Body><u:GetExternalIPAddressResponse>"
               "<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress>"
               "</u:GetExternalIPAddressResponse></s:Body></s:Envelope>";
    } else if (strstr(request, "#AddPortMapping\"") != NULL) {
        snprintf(router.add_port_mapping, sizeof(router.add_port_mapping), "%s", request);
        router.upnp_mapped = 1;
        body = "<?xml version=\"1.0\"?><s:Envelope><s:Body><u:AddPortMappingResponse/></s:Body></s:Envelope>";
    }
    if (body != NULL) {
        snprintf(answer, sizeof(answer),
                 "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: %u\r\n"
                 "Connection: close\r\n\r\n%s",
                 (unsigned int)strlen(body), body);
    } else {
        snprintf(answer, sizeof(answer), "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    }
    send(c, answer, (int)strlen(answer), 0);
    closesocket(c);
}

static DWORD WINAPI router_thread(void *unused)
{
    (void)unused;
    while (!router.stop) {
        unsigned char d[2048];
        KhdaysNetAddress from;
        long got;
        fd_set set;
        struct timeval wait = {0, 20000};
        KhdaysUdp *udps[2] = {router.natpmp, router.ssdp};
        khdays_host_udp_wait(udps, 2, 20);
        while ((got = khdays_host_udp_receive(router.natpmp, &from, d, sizeof(d))) >= 0) {
            unsigned char a[16];
            memset(a, 0, sizeof(a));
            if (!router.natpmp_on || got < 2 || d[0] != 0) {
                continue;
            }
            a[1] = (unsigned char)(128 + d[1]);
            if (d[1] == 0) {
                a[8] = 198, a[9] = 51, a[10] = 100, a[11] = 9;
                khdays_host_udp_send(router.natpmp, &from, a, 12);
            } else if (d[1] == 1 && got >= 12) {
                memcpy(a + 8, d + 4, 2);  /* the internal port */
                memcpy(a + 10, d + 6, 2); /* mapped as asked */
                memcpy(a + 12, d + 8, 4); /* for as long */
                router.natpmp_mapped = 1;
                khdays_host_udp_send(router.natpmp, &from, a, 16);
            }
        }
        while ((got = khdays_host_udp_receive(router.ssdp, &from, d, sizeof(d) - 1)) >= 0) {
            char answer[256];
            d[got] = '\0';
            if (strncmp((const char *)d, "M-SEARCH", 8) == 0) {
                snprintf(answer, sizeof(answer),
                         "HTTP/1.1 200 OK\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
                         "LOCATION: http://127.0.0.1:%u/desc.xml\r\n\r\n",
                         router.http_port);
                khdays_host_udp_send(router.ssdp, &from, answer, strlen(answer));
            }
        }
        FD_ZERO(&set);
        FD_SET(router.http, &set);
        if (select(0, &set, NULL, NULL, &wait) > 0) {
            const SOCKET c = accept(router.http, NULL, NULL);
            if (c != INVALID_SOCKET) {
                serve_http(c);
            }
        }
    }
    return 0;
}

static int start_router(void)
{
    struct sockaddr_in address;
    int length = sizeof(address);
    router.natpmp = khdays_host_udp_open(0, 0);
    router.ssdp = khdays_host_udp_open(0, 0);
    router.http = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (router.natpmp == NULL || router.ssdp == NULL || router.http == INVALID_SOCKET) {
        return 0;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(router.http, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(router.http, 4) != 0) {
        return 0;
    }
    getsockname(router.http, (struct sockaddr *)&address, &length);
    router.http_port = ntohs(address.sin_port);
    return CreateThread(NULL, 0, router_thread, NULL, 0, NULL) != NULL;
}

/* A server started on `port` with `arguments`, its window's words in
 * `log`. */
static int start_server(const char *exe, unsigned short port, const char *arguments, const char *log,
                        PROCESS_INFORMATION *process)
{
    char command[MAX_PATH * 2 + 64];
    STARTUPINFOA startup = {sizeof(startup)};
    DeleteFileA(log);
    snprintf(command, sizeof(command), "\"%s\" --port %u --log \"%s\" %s", exe, port, log, arguments);
    return CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, process) != 0;
}

static void stop_server(PROCESS_INFORMATION *process)
{
    TerminateProcess(process->hProcess, 0);
    WaitForSingleObject(process->hProcess, 5000);
    CloseHandle(process->hProcess);
    CloseHandle(process->hThread);
}

/* Whether the server's log has `text` within `ms`. */
static int log_says(const char *log, const char *text, int ms)
{
    const unsigned long long until = khdays_host_net_clock_ms() + (unsigned long long)ms;
    static char seen[16384];
    do {
        FILE *f = fopen(log, "rb");
        if (f != NULL) {
            const size_t got = fread(seen, 1, sizeof(seen) - 1, f);
            fclose(f);
            seen[got] = '\0';
            if (strstr(seen, text) != NULL) {
                return 1;
            }
        }
        Sleep(50);
    } while (khdays_host_net_clock_ms() < until);
    return 0;
}

static unsigned short free_port(void)
{
    KhdaysUdp *probe = khdays_host_udp_open(0, 0);
    const unsigned short port = probe != NULL ? khdays_host_udp_port(probe) : 0;
    khdays_host_udp_close(probe);
    return port;
}

/* --- a flood beside a session ------------------------- */

#define FLOODERS 64

/* The most a status line of the log says was held, and the most evicted in
 * one; 0 if none was said. */
static void read_status(const char *log, unsigned long long *held_most, unsigned long long *budget,
                        unsigned long long *evicted_most)
{
    char line[1024];
    FILE *f = fopen(log, "rb");
    *held_most = *budget = *evicted_most = 0;
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        const char *at;
        unsigned long long a, b, opened, relayed, evicted;
        if ((at = strstr(line, " KB of ")) != NULL) {
            const char *start = at;
            while (start > line && start[-1] != ' ') {
                --start;
            }
            if (sscanf(start, "%llu KB of %llu", &a, &b) == 2) {
                *held_most = a > *held_most ? a : *held_most;
                *budget = b;
            }
        }
        if ((at = strstr(line, "since the last: ")) != NULL &&
            sscanf(at, "since the last: %llu groups opened, %llu datagrams relayed, %llu evicted", &opened, &relayed,
                   &evicted) == 3) {
            *evicted_most = evicted > *evicted_most ? evicted : *evicted_most;
        }
    }
    fclose(f);
}

/* A host and a guest relaying through a server of 1 MB while 64 senders
 * flood it at a hundred times a session's rate -- groups registered by the
 * thousand, lists, introductions to groups that are not there, relays with
 * tokens never given: the session is never cut, the server holds no more
 * than its budget, and it says it evicted. */
static int flood(const char *exe, const char *log)
{
    const unsigned char host_id[6] = {2, 9, 9, 9, 9, 1}, guest_id[6] = {2, 9, 9, 9, 9, 2};
    const unsigned char info[] = "flood test";
    unsigned char body[256], announce[64];
    KhdaysNetAddress server = {0x7f000001u, free_port()};
    KhdaysUdp *host = khdays_host_udp_open(0, 0), *guest = khdays_host_udp_open(0, 0), *flooders[FLOODERS];
    PROCESS_INFORMATION process;
    unsigned int token, sent = 0, heard = 0, sequence = 0, random = 12345;
    unsigned long long start, last_heard, longest_gap = 0, registered = 0, held_most, budget, evicted_most;
    unsigned long long flooded = 0; /* the flood's datagrams sent so far */
    long size;
    int result = 0;
    for (int i = 0; i < FLOODERS; ++i) {
        flooders[i] = khdays_host_udp_open(0, 0);
    }
    if (!start_server(exe, server.port, "--router off --memory 1 --status 1", log, &process)) {
        return fail("cannot start the server for the flood");
    }
    put32(announce, BUILD);
    put32(announce + 4, 0);
    put16(announce + 8, khdays_host_udp_port(host));
    announce[10] = 1;
    announce[11] = 0;
    put32(announce + 12, GGID);
    put32(announce + 16, sizeof(info));
    memcpy(announce + 20, info, sizeof(info));
    size = -1;
    for (int attempt = 0; attempt < 50 && size < 0; ++attempt) {
        send_kind(host, &server, host_id, K_REGISTER, announce, 20 + sizeof(info), 0);
        size = wait_for(host, K_REGISTERED, 100, NULL);
    }
    memset(body, 0, 16);
    memcpy(body, host_id, 6);
    send_kind(guest, &server, guest_id, K_CONNECT, body, 16, 0);
    if (size < 0 || wait_for(guest, K_PEER, 1000, NULL) != 16) {
        stop_server(&process);
        return fail("the flood's session was not introduced");
    }
    token = get32(reply + HEADER);
    start = last_heard = khdays_host_net_clock_ms();
    while (khdays_host_net_clock_ms() - start < 6000) {
        const unsigned long long now = khdays_host_net_clock_ms();
        KhdaysNetAddress from;
        /* the session: a relayed datagram every 20 ms, the host registering every 2 s */
        put32(body, token);
        memset(body + 4, 0, HEADER + 4);
        memcpy(body + 4, "KHD1", 4);
        put32(body + 4 + HEADER, sequence++);
        send_kind(guest, &server, guest_id, K_RELAY, body, 4 + HEADER + 4, 0);
        ++sent;
        if (now - registered >= 2000) {
            send_kind(host, &server, host_id, K_REGISTER, announce, 20 + sizeof(info), 0);
            registered = now;
        }
        /* the flood: 6,400 datagrams a second (128 in each 20 ms), by the clock: Sleep(20) lasts up to 31 ms
         * where the system's timer ticks every 15.6 ms, which left the flood a third short of the budget */
        for (const unsigned long long due = (now - start) * 6400u / 1000u + 128u; flooded < due; ++flooded) {
            const int i = (int)(flooded % 128u);
            unsigned char id[6], flood_body[1200];
            KhdaysUdp *f = flooders[i % FLOODERS];
            random = random * 1664525u + 1013904223u;
            memcpy(id, &random, 4);
            id[4] = (unsigned char)i;
            id[5] = 2;
            switch (i % 4) {
            case 0: {
                unsigned char other[64];
                memcpy(other, announce, 20 + sizeof(info));
                put32(other, 0xdeadu); /* another build: not in the session's list */
                send_kind(f, &server, id, K_REGISTER, other, 20 + sizeof(info), 0);
                break;
            }
            case 1:
                put32(flood_body, BUILD);
                put32(flood_body + 4, GGID);
                put16(flood_body + 8, 0);
                send_kind(f, &server, id, K_LIST, flood_body, 10, 1200 - HEADER);
                break;
            case 2:
                memset(flood_body, 0, 16);
                memcpy(flood_body, id, 6);
                send_kind(f, &server, id, K_CONNECT, flood_body, 16, 0);
                break;
            default:
                put32(flood_body, random);
                memset(flood_body + 4, 0, HEADER + 8);
                send_kind(f, &server, id, K_RELAY, flood_body, 4 + HEADER + 8, 0);
                break;
            }
        }
        for (int i = 0; i < FLOODERS; ++i) {
            while (khdays_host_udp_receive(flooders[i], &from, reply, sizeof(reply)) >= 0) {
            }
        }
        while ((size = khdays_host_udp_receive(host, &from, reply, sizeof(reply))) >= 0) {
            if (size >= HEADER && reply[4] == K_RELAY) {
                const unsigned long long at = khdays_host_net_clock_ms();
                ++heard;
                longest_gap = at - last_heard > longest_gap ? at - last_heard : longest_gap;
                last_heard = at;
            }
        }
        while (khdays_host_udp_receive(guest, &from, reply, sizeof(reply)) >= 0) {
        }
        Sleep(20);
    }
    Sleep(1500); /* the last status line */
    stop_server(&process);
    read_status(log, &held_most, &budget, &evicted_most);
    printf("server: flood -- %u of %u relayed datagrams heard, the longest gap %llu ms; at most %llu KB of %llu "
           "held, %llu evicted in a second\n",
           heard, sent, longest_gap, held_most, budget, evicted_most);
    if (heard < sent * 9 / 10 || longest_gap > 1000) {
        result = fail("the session was cut by the flood");
    } else if (budget == 0 || held_most > budget) {
        result = fail("the server held more than its budget (or said nothing of it)");
    } else if (evicted_most == 0) {
        result = fail("the flood never reached the budget: nothing was evicted");
    }
    for (int i = 0; i < FLOODERS; ++i) {
        khdays_host_udp_close(flooders[i]);
    }
    khdays_host_udp_close(host);
    khdays_host_udp_close(guest);
    return result;
}

int main(int argc, char **argv)
{
    PROCESS_INFORMATION process;
    KhdaysNetAddress server = {0x7f000001u, 0};
    char log[MAX_PATH], variable[64], expected[128];
    int result;
    if (argc < 2) {
        return fail("give it the server's executable");
    }
    if (!khdays_host_net_start()) {
        return fail("no network");
    }
    GetTempPathA(sizeof(log) - 32, log);
    strcat(log, "khdays-server-test.log");
    /* the protocol, the router left alone */
    server.port = free_port();
    if (!start_server(argv[1], server.port, "--router off", log, &process)) {
        return fail("cannot start the server");
    }
    result = run(&server);
    stop_server(&process);
    if (result != 0) {
        return result;
    }
    /* a flood beside a session */
    result = flood(argv[1], log);
    if (result != 0) {
        return result;
    }
    /* the port asked of a router: never the PC's own -- the tests' */
    if (!start_router()) {
        return fail("no router of the test's");
    }
    snprintf(variable, sizeof(variable), "127.0.0.1:%u", khdays_host_udp_port(router.natpmp));
    SetEnvironmentVariableA("KHDAYS_SERVER_GATEWAY", variable);
    snprintf(variable, sizeof(variable), "127.0.0.1:%u", khdays_host_udp_port(router.ssdp));
    SetEnvironmentVariableA("KHDAYS_SERVER_SSDP", variable);
    for (int method = 0; method < 2; ++method) {
        /* NAT-PMP when the router answers it, else UPnP */
        router.natpmp_on = method == 0;
        server.port = free_port();
        if (!start_server(argv[1], server.port, "", log, &process)) {
            return fail("cannot start the server");
        }
        if (!log_says(log, "asking the router", 5000)) {
            stop_server(&process);
            printf("server: this machine has an address on the internet; the router's part is not tried\n");
            return 0;
        }
        snprintf(expected, sizeof(expected), "the router opened UDP port %u (%s", server.port,
                 method == 0 ? "NAT-PMP" : "UPnP");
        if (!log_says(log, expected, 10000)) {
            stop_server(&process);
            return fail(method == 0 ? "the server did not open its port through NAT-PMP"
                                    : "the server did not open its port through UPnP");
        }
        snprintf(expected, sizeof(expected), "players write %s:%u", method == 0 ? "198.51.100.9" : "203.0.113.7",
                 server.port);
        if (!log_says(log, expected, 1000)) {
            stop_server(&process);
            return fail("the server did not say the address the router gave");
        }
        stop_server(&process);
        if (method == 1) {
            char port_field[64];
            snprintf(port_field, sizeof(port_field), "<NewExternalPort>%u</NewExternalPort>", server.port);
            if (strstr(router.add_port_mapping, port_field) == NULL ||
                strstr(router.add_port_mapping, "<NewProtocol>UDP</NewProtocol>") == NULL ||
                strstr(router.add_port_mapping, "<NewInternalClient>127.0.0.1</NewInternalClient>") == NULL) {
                return fail("UPnP's AddPortMapping did not ask for the server's UDP port toward this machine");
            }
        }
    }
    router.stop = 1;
    DeleteFileA(log);
    printf("server: the port opened through NAT-PMP, and through UPnP when NAT-PMP is not there\n");
    return 0;
}
