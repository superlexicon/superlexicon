#pragma once

#include <lvgl.h>
#include "dashboard.h"

#ifdef __cplusplus
extern "C" {
#endif

void superapp_views_create(lv_obj_t *view_wallet,
                           lv_obj_t *view_chat,
                           lv_obj_t *view_apps,
                           const struct DashboardArgs *args);

void superapp_views_tick(unsigned long long chain_height,
                         const char *active_role,
                         const char *active_subnet);

void superapp_views_handle_key(uint32_t key);

#ifdef __cplusplus
}
#endif
