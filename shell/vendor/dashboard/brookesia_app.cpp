#include "brookesia_app.hpp"
#include "ui/dashboard.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern "C" {
LV_IMG_DECLARE(img_app_superlexicon);

static int read_pid_file(const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp) return -1;
  char buf[32] = {0};
  int pid = -1;
  if (fgets(buf, sizeof(buf), fp)) {
    pid = atoi(buf);
  }
  fclose(fp);
  return pid > 0 ? pid : 1;
}

int superlexicon_supervisor_restart_radio(void) {
  int fd = open("/tmp/slx_start_radio", O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd >= 0) {
    write(fd, "1\n", 2);
    close(fd);
    return 1;
  }
  return -1;
}

int superlexicon_supervisor_restart_node(void) {
  int sfd = open("/tmp/slx_node_starting", O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (sfd >= 0) close(sfd);
  int fd = open("/tmp/slx_start_node", O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd >= 0) {
    write(fd, "1\n", 2);
    close(fd);
    return 1;
  }
  return -1;
}

int superlexicon_supervisor_get_radio_pid(void) {
  return read_pid_file("/tmp/slx_radio_running");
}

int superlexicon_supervisor_get_node_pid(void) {
  return read_pid_file("/tmp/slx_node_running");
}
}

SuperLexiconApp::SuperLexiconApp(bool use_status_bar, bool use_navigation_bar)
    : ESP_Brookesia_PhoneApp(
          ESP_BROOKESIA_CORE_APP_DATA_DEFAULT("SuperLexicon",
                                              &img_app_superlexicon, true),
          ESP_BROOKESIA_PHONE_APP_DATA_DEFAULT(
              &img_app_superlexicon, use_status_bar, use_navigation_bar)) {}

SuperLexiconApp::~SuperLexiconApp() {}

bool SuperLexiconApp::run(void) {
  lv_area_t area = getVisualArea();
  int w = area.x2 - area.x1 + 1;
  int h = area.y2 - area.y1 + 1;

  lv_obj_t *scr = lv_scr_act();
  if (scr) {
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(scr, area.x1, area.y1);
    lv_obj_set_size(scr, w, h);
  }

  lv_obj_t *root = lv_obj_create(scr);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, w, h);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

  struct DashboardArgs args = {
      .rpc_host = "127.0.0.1",
      .rpc_port = 8545,
      .rpc_ws_port = 8546,
  };
  dashboard_create(root, &args);
  dashboard_start_timer(&args);
  return true;
}

bool SuperLexiconApp::back(void) {
  notifyCoreClosed();
  return true;
}

bool SuperLexiconApp::close(void) {
  dashboard_stop_timer();
  return true;
}

bool SuperLexiconApp::init(void) { return true; }
