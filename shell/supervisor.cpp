#include "supervisor.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>

static bool g_daemons_enabled = true;
static char g_bin_dir[512] = "./bin";
static pid_t g_radio_pid = -1;
static pid_t g_node_pid = -1;
static bool g_node_external = false;

static bool is_tcp_port_open(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    struct timeval tv = {.tv_sec = 0, .tv_usec = 40000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bool ok = (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    close(sock);
    return ok;
}

static bool resolve_executable(const char *name, char *out_path, size_t out_sz) {
    const char *candidates[4];
    char c0[600], c1[600], c2[600];
    snprintf(c0, sizeof(c0), "%s/%s", g_bin_dir, name);
    snprintf(c1, sizeof(c1), "./bin/%s", name);
    snprintf(c2, sizeof(c2), "/sdcard/%s.elf", name);
    candidates[0] = c0;
    candidates[1] = c1;
    candidates[2] = c2;
    candidates[3] = nullptr;

    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], X_OK) == 0) {
            snprintf(out_path, out_sz, "%s", candidates[i]);
            return true;
        }
    }
    return false;
}

extern "C" int superlexicon_supervisor_restart_radio(void) {
    if (g_radio_pid > 0) {
        kill(g_radio_pid, SIGTERM);
        usleep(50000);
        waitpid(g_radio_pid, nullptr, WNOHANG);
        g_radio_pid = -1;
    }

    char exe[512];
    if (!resolve_executable("lexicon-radio-bridge", exe, sizeof(exe))) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        int log_fd = open("/tmp/lexicon_radio_bridge.log", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }
        char *const argv[] = {exe, nullptr};
        execv(exe, argv);
        _exit(127);
    } else if (pid > 0) {
        g_radio_pid = pid;
    }
    return (int)g_radio_pid;
}

extern "C" int superlexicon_supervisor_restart_node(void) {
    int rpc_port = getenv("LEXICON_RPC_PORT") ? atoi(getenv("LEXICON_RPC_PORT")) : 8545;

    if (g_node_pid > 0 && !g_node_external) {
        kill(g_node_pid, SIGTERM);
        usleep(80000);
        waitpid(g_node_pid, nullptr, WNOHANG);
        g_node_pid = -1;
    }

    if (is_tcp_port_open(rpc_port)) {
        g_node_external = true;
        return 1;
    }
    g_node_external = false;

    char exe[512];
    if (!resolve_executable("lexicon-node", exe, sizeof(exe))) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        int log_fd = open("/tmp/lexicon_node.log", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }
        setenv("SLX_NO_TUI", "1", 1);
        if (!getenv("SLX_RADIO_PORT")) {
            if (access("/dev/ttyACM0", R_OK | W_OK) == 0) {
                setenv("SLX_RADIO_PORT", "/dev/ttyACM0", 0);
            }
        }
        char *const argv[] = {exe, nullptr};
        execv(exe, argv);
        _exit(127);
    } else if (pid > 0) {
        g_node_pid = pid;
    }
    return (int)g_node_pid;
}

extern "C" void superlexicon_supervisor_init(bool enable_daemons, const char *self_exe_path) {
    g_daemons_enabled = enable_daemons;
    if (self_exe_path && self_exe_path[0]) {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s", self_exe_path);
        char *dir = dirname(tmp);
        if (dir && dir[0]) {
            snprintf(g_bin_dir, sizeof(g_bin_dir), "%s", dir);
        }
    }
    mkdir("./apps", 0755);
    if (!g_daemons_enabled) {
        return;
    }
    superlexicon_supervisor_restart_radio();
    superlexicon_supervisor_restart_node();
}

extern "C" void superlexicon_supervisor_poll(void) {
    if (g_radio_pid > 0) {
        int st = 0;
        pid_t r = waitpid(g_radio_pid, &st, WNOHANG);
        if (r == g_radio_pid || r < 0) {
            g_radio_pid = -1;
        }
    }
    if (g_node_pid > 0 && !g_node_external) {
        int st = 0;
        pid_t r = waitpid(g_node_pid, &st, WNOHANG);
        if (r == g_node_pid || r < 0) {
            g_node_pid = -1;
        }
    }
}

extern "C" void superlexicon_supervisor_shutdown_all(void) {
    if (g_node_pid > 0 && !g_node_external) {
        kill(g_node_pid, SIGTERM);
        waitpid(g_node_pid, nullptr, WNOHANG);
        g_node_pid = -1;
    }
    if (g_radio_pid > 0) {
        kill(g_radio_pid, SIGTERM);
        waitpid(g_radio_pid, nullptr, WNOHANG);
        g_radio_pid = -1;
    }
}

extern "C" int superlexicon_supervisor_get_radio_pid(void) {
    superlexicon_supervisor_poll();
    return (int)g_radio_pid;
}

extern "C" int superlexicon_supervisor_get_node_pid(void) {
    superlexicon_supervisor_poll();
    if (g_node_pid > 0) return (int)g_node_pid;
    int rpc_port = getenv("LEXICON_RPC_PORT") ? atoi(getenv("LEXICON_RPC_PORT")) : 8545;
    if (is_tcp_port_open(rpc_port)) return 1;
    return -1;
}
