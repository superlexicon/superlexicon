#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static volatile bool g_running = true;

static void on_sig(int sig) {
    (void)sig;
    g_running = false;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock >= 0) {
        int yes = 1;
        setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
        setsockopt(udp_sock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
        setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
        struct timeval tv = {.tv_sec = 0, .tv_usec = 50000};
        setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(19444);
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        bind(udp_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    }

    FILE *rx_fp = fopen("/tmp/lora_rx_log", "a");
    if (rx_fp) {
        fprintf(rx_fp, "[radio-bridge] listening on UDP 19444\n");
        fclose(rx_fp);
    }

    while (g_running) {
        if (access("/tmp/lora_tx_cmd", F_OK) == 0) {
            FILE *fp = fopen("/tmp/lora_tx_cmd", "rb");
            if (fp) {
                char cmd[512] = {0};
                size_t n = fread(cmd, 1, sizeof(cmd) - 1, fp);
                fclose(fp);
                unlink("/tmp/lora_tx_cmd");
                if (n > 0 && udp_sock >= 0) {
                    struct sockaddr_in bcast;
                    memset(&bcast, 0, sizeof(bcast));
                    bcast.sin_family = AF_INET;
                    bcast.sin_port = htons(19444);
                    bcast.sin_addr.s_addr = inet_addr("255.255.255.255");
                    sendto(udp_sock, cmd, n, 0, (struct sockaddr *)&bcast, sizeof(bcast));
                }
            }
        }

        if (udp_sock >= 0) {
            char buf[512];
            struct sockaddr_in src;
            socklen_t slen = sizeof(src);
            ssize_t r = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&src, &slen);
            if (r > 0) {
                buf[r] = '\0';
                FILE *log_fp = fopen("/tmp/lora_rx_log", "a");
                if (log_fp) {
                    fprintf(log_fp, "[rx %s] %s\n", inet_ntoa(src.sin_addr), buf);
                    fclose(log_fp);
                }
            }
        } else {
            usleep(50000);
        }
    }

    if (udp_sock >= 0) {
        close(udp_sock);
    }
    return 0;
}
