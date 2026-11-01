#pragma once

#include "lvgl.h"
#include "esp_brookesia.hpp"

class SuperLexiconApp : public ESP_Brookesia_PhoneApp {
public:
  SuperLexiconApp(bool use_status_bar = true, bool use_navigation_bar = true);
  ~SuperLexiconApp();

  bool run(void) override;
  bool back(void) override;
  bool close(void) override;
  bool init(void) override;
};
