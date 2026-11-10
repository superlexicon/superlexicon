#include "brookesia_app.hpp"
#include "ui/dashboard.h"

extern "C" {
LV_IMG_DECLARE(img_app_superlexicon);
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
