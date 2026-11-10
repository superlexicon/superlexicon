#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct DashboardArgs {
  const char *rpc_host;
  int rpc_port;
  int rpc_ws_port;
};

void dashboard_create(lv_obj_t *parent, const struct DashboardArgs *args);
void dashboard_start_timer(const struct DashboardArgs *args);
void dashboard_stop_timer(void);
void dashboard_select_tab(int tab_idx);
void dashboard_on_orientation_changed(int hor_res, int ver_res);

#ifdef __cplusplus
}
#endif
