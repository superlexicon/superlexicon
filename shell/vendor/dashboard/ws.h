#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ws_init(size_t frame_cap);
int ws_connect(const char *host, int port, const char *path);
int ws_service(char *buf, size_t cap);
int ws_send_text(const char *text);
int ws_is_connected(void);

#ifdef __cplusplus
}
#endif
