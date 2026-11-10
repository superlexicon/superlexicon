#include "ws.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int fd_ = -1;
static int connected_ = 0;
static int ipc_fd_ = -1;
static uint32_t last_ipc_seq_ = 0;
static char *accum_ = NULL;
static size_t accum_len_ = 0;
static size_t accum_cap_ = 0;
static char *rx_ = NULL;
static size_t rx_cap_ = 0;
static size_t rx_len_ = 0;

static uint32_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int wait_fd(int events, int timeout_ms) {
  struct pollfd p = {.fd = fd_, .events = events};
  return poll(&p, 1, timeout_ms);
}

int send_all(const uint8_t *buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = write(fd_, buf + off, len - off);
    if (n <= 0) {
      return -1;
    }
    off += (size_t)n;
  }
  return 0;
}

void base64_key(char out[25]) {
  static const char *alpha =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint8_t raw[16];
  for (int i = 0; i < 16; i++) {
    raw[i] = (uint8_t)(rand() & 0xFF);
  }
  int o = 0;
  for (int i = 0; i < 16; i += 3) {
    uint32_t v = (uint32_t)(raw[i] << 16 | (i + 1 < 16 ? raw[i + 1] : 0) << 8 |
                            (i + 2 < 16 ? raw[i + 2] : 0));
    out[o++] = alpha[(v >> 18) & 63];
    out[o++] = alpha[(v >> 12) & 63];
    out[o++] = alpha[(v >> 6) & 63];
    out[o++] = alpha[v & 63];
  }
  out[24] = '\0';
}

void ws_init(size_t frame_cap) {
  last_ipc_seq_ = 0;
#if defined(__riscv)
  (void)frame_cap;
  return;
#endif
  if (rx_) {
    return;
  }
  rx_cap_ = frame_cap + 64;
  accum_cap_ = frame_cap + 16;
  rx_ = (char *)malloc(rx_cap_);
  accum_ = (char *)malloc(accum_cap_);
}

int ws_connect(const char *host, int port, const char *path) {
#if defined(__riscv)
  if (access("/tmp/slx_node_running", F_OK) != 0) {
    if (ipc_fd_ >= 0) {
      close(ipc_fd_);
      ipc_fd_ = -1;
    }
    last_ipc_seq_ = 0;
    return -1;
  }
#endif
  if (ipc_fd_ < 0) {
    ipc_fd_ = open("/tmp/slx_ipc.bin", O_RDWR);
  }
  if (ipc_fd_ >= 0) {
    const char *sub =
        "{\"id\":\"sub-stats\",\"method\":\"subscribe\",\"params\":[\"stats\"]}";
    ws_send_text(sub);
    return 0;
  }
#if defined(__riscv)
  return -1;
#endif
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = NULL;
  char port_s[8];
  snprintf(port_s, sizeof(port_s), "%d", port);
  if (getaddrinfo(host, port_s, &hints, &res) != 0 || !res) {
    return -1;
  }
  fd_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  fcntl(fd_, F_SETFL, O_NONBLOCK);
  int rc = connect(fd_, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  if (rc < 0) {
    struct pollfd p = {.fd = fd_, .events = POLLOUT};
    if (errno == EINPROGRESS && poll(&p, 1, 1500) > 0) {
      int so_err = 0;
      socklen_t elen = sizeof(so_err);
      getsockopt(fd_, SOL_SOCKET, SO_ERROR, &so_err, &elen);
      rc = so_err;
    }
    if (rc < 0) {
      close(fd_);
      fd_ = -1;
      return -1;
    }
  }

  char key[25];
  memset(key, 0, sizeof(key));
  base64_key(key);
  char req[512];
  int rlen = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\n"
                      "Host: %s:%d\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: %s\r\n"
                      "Sec-WebSocket-Version: 13\r\n\r\n",
                      path, host, port, key);
  if (send_all((const uint8_t *)req, (size_t)rlen) != 0) {
    close(fd_);
    fd_ = -1;
    return -1;
  }

  char resp[1024];
  size_t got = 0;
  uint32_t deadline = now_ms() + 2000;
  while (now_ms() < deadline) {
    struct pollfd p = {.fd = fd_, .events = POLLIN};
    if (poll(&p, 1, 200) <= 0) {
      continue;
    }
    ssize_t n = read(fd_, resp + got, sizeof(resp) - 1 - got);
    if (n <= 0) {
      break;
    }
    got += (size_t)n;
    resp[got] = '\0';
    if (strstr(resp, "\r\n\r\n")) {
      break;
    }
  }
  if (got == 0 || !strstr(resp, "101")) {
    close(fd_);
    fd_ = -1;
    return -1;
  }

  connected_ = 1;
  rx_len_ = 0;
  accum_len_ = 0;

  const char *sub =
      "{\"id\":\"sub-stats\",\"method\":\"subscribe\",\"params\":[\"stats\"]}";
  ws_send_text(sub);
  return 0;
}

int ws_is_connected(void) {
#if defined(__riscv)
  return (access("/tmp/slx_node_running", F_OK) == 0) && (ipc_fd_ >= 0);
#else
  return (ipc_fd_ >= 0) || (fd_ >= 0 && connected_);
#endif
}

int ws_send_text(const char *text) {
  if (ipc_fd_ < 0) {
    ipc_fd_ = open("/tmp/slx_ipc.bin", O_RDWR);
  }
  if (ipc_fd_ >= 0) {
    size_t len = strlen(text);
    if (len > 0 && len < 8180) {
      uint32_t cmd_seq = 0;
      lseek(ipc_fd_, 0x3E000, SEEK_SET);
      read(ipc_fd_, &cmd_seq, 4);
      uint32_t ulen = (uint32_t)len;
      lseek(ipc_fd_, 0x3E008, SEEK_SET);
      write(ipc_fd_, &ulen, 4);
      write(ipc_fd_, text, len);
      cmd_seq++;
      if (cmd_seq == 0) {
        cmd_seq = 1;
      }
      lseek(ipc_fd_, 0x3E000, SEEK_SET);
      write(ipc_fd_, &cmd_seq, 4);
      return 0;
    }
  }
  if (fd_ < 0 || !connected_) {
    return -1;
  }
  size_t len = strlen(text);
  uint8_t hdr[10];
  size_t hlen = 2;
  hdr[0] = 0x81;
  hdr[1] = 0x80;
  uint8_t mask[4];
  for (int i = 0; i < 4; i++) {
    mask[i] = (uint8_t)(rand() & 0xFF);
  }
  if (len < 126) {
    hdr[1] |= (uint8_t)len;
  } else if (len <= 0xFFFF) {
    hdr[1] |= 126;
    hdr[hlen++] = (uint8_t)(len >> 8);
    hdr[hlen++] = (uint8_t)(len & 0xFF);
  } else {
    hdr[1] |= 127;
    for (int i = 7; i >= 0; i--) {
      hdr[hlen++] = (len >> (8 * i)) & 0xFF;
    }
  }
  for (size_t i = 0; i < 4; i++) {
    hdr[hlen++] = mask[i];
  }
  if (send_all(hdr, hlen) != 0) {
    return -1;
  }
  for (size_t i = 0; i < len; i++) {
    uint8_t m = (uint8_t)text[i] ^ mask[i % 4];
    if (send_all(&m, 1) != 0) {
      return -1;
    }
  }
  return 0;
}

static int drain_socket(void) {
  struct pollfd p = {.fd = fd_, .events = POLLIN};
  if (poll(&p, 1, 0) <= 0) {
    return 0;
  }
  ssize_t n = read(fd_, rx_ + rx_len_, rx_cap_ - rx_len_);
  if (n <= 0) {
    return -1;
  }
  rx_len_ += (size_t)n;
  return (int)n;
}

int ws_service(char *buf, size_t cap) {
#if defined(__riscv)
  if (access("/tmp/slx_node_running", F_OK) != 0) {
    if (ipc_fd_ >= 0) {
      close(ipc_fd_);
      ipc_fd_ = -1;
    }
    last_ipc_seq_ = 0;
    return -1;
  }
#endif
  if (ipc_fd_ < 0) {
    ipc_fd_ = open("/tmp/slx_ipc.bin", O_RDWR);
  }
  if (ipc_fd_ >= 0) {
    uint32_t hdr[2] = {0, 0};
    lseek(ipc_fd_, 0x20000, SEEK_SET);
    if (read(ipc_fd_, hdr, 8) == 8) {
      uint32_t seq1 = hdr[0];
      uint32_t len = hdr[1];
      if (seq1 == 0) {
        last_ipc_seq_ = 0;
        return 0;
      }
      if (seq1 != 0 && seq1 != last_ipc_seq_ && len > 0 && len < 122872) {
        size_t n = len < cap - 1 ? len : cap - 1;
        lseek(ipc_fd_, 0x20008, SEEK_SET);
        ssize_t got = read(ipc_fd_, buf, n);
        if (got == (ssize_t)n) {
          uint32_t seq2 = 0;
          lseek(ipc_fd_, 0x20000, SEEK_SET);
          if (read(ipc_fd_, &seq2, 4) == 4 && seq2 == seq1) {
            buf[got] = '\0';
            last_ipc_seq_ = seq1;
            unlink("/tmp/slx_node_starting");
            return (int)got;
          }
        }
      }
    }
    return 0;
  }
#if defined(__riscv)
  return -1;
#endif
  if (fd_ < 0 || !connected_) {
    return -1;
  }
  int added = drain_socket();
  if (added < 0) {
    connected_ = 0;
    close(fd_);
    fd_ = -1;
    return -1;
  }

  int newest_text = 0;
  size_t pos = 0;
  while (pos < rx_len_) {
    if (rx_len_ - pos < 2) {
      break;
    }
    uint8_t b0 = (uint8_t)rx_[pos];
    uint8_t b1 = (uint8_t)rx_[pos + 1];
    int fin = b0 & 0x80;
    uint8_t opcode = b0 & 0x0F;
    int masked = b1 & 0x80;
    uint64_t len = b1 & 0x7F;
    size_t hdr = 2;
    if (len == 126) {
      if (rx_len_ - pos < hdr + 2) {
        break;
      }
      len = ((uint64_t)(uint8_t)rx_[pos + 2] << 8) | (uint8_t)rx_[pos + 3];
      hdr += 2;
    } else if (len == 127) {
      if (rx_len_ - pos < hdr + 8) {
        break;
      }
      len = 0;
      for (int i = 0; i < 8; i++) {
        len = (len << 8) | (uint8_t)rx_[pos + 2 + i];
      }
      hdr += 8;
    }
    size_t mask_len = masked ? 4 : 0;
    if (rx_len_ - pos < hdr + mask_len + len) {
      break;
    }
    const uint8_t *payload = (const uint8_t *)rx_ + pos + hdr + mask_len;
    const uint8_t *mk = (const uint8_t *)rx_ + pos + hdr;

    if (opcode == 0x1 && fin) {
      size_t n = len < accum_cap_ - 1 ? len : accum_cap_ - 1;
      if (masked) {
        for (size_t i = 0; i < n; i++) {
          accum_[i] = (char)(payload[i] ^ mk[i % 4]);
        }
      } else {
        memcpy(accum_, payload, n);
      }
      accum_[n] = '\0';
      snprintf(buf, cap, "%s", accum_);
      newest_text = (int)n;
    } else if (opcode == 0x9 && masked && len <= 125) {
      uint8_t pong[2 + 4 + 125];
      pong[0] = 0x8A;
      pong[1] = 0x80 | (uint8_t)len;
      memcpy(pong + 2, mk, 4);
      for (size_t i = 0; i < len; i++) {
        pong[6 + i] = payload[i] ^ mk[i % 4];
      }
      send_all(pong, 6 + len);
    }

    pos += hdr + mask_len + len;
  }

  if (pos > 0) {
    memmove(rx_, rx_ + pos, rx_len_ - pos);
    rx_len_ -= pos;
  }

  if (newest_text > 0) {
    size_t n = strlen(accum_);
    if (n >= cap) {
      n = cap - 1;
    }
    memcpy(buf, accum_, n);
    buf[n] = '\0';
    return (int)n;
  }
  return 0;
}
