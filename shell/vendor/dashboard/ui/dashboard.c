#include "dashboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "../theme.h"
#include "../ws.h"
#include "superapp_views.h"

#define SUBNET_ROWS 8
#define SUBNET_VISIBLE_ROWS 2
#define BLOCK_ROWS 8
#define BLOCK_VISIBLE_ROWS 4
#define BLOCK_ROW_H 38
#define NODE_ROWS 8
#define VISIBLE_TABLE_ROWS 4
#define TABLE_ROW_H 42

typedef struct {
  unsigned long long height;
  long long txs;
  double confirm_ms;
  long rounds;
} recent_block_entry_t;

typedef struct {
  struct DashboardArgs args;
  lv_obj_t *role_subtitle_label;
  lv_obj_t *chain_title_label;
  lv_obj_t *phase_chips[4];
  lv_obj_t *block_rows[BLOCK_ROWS];
  lv_obj_t *block_cells[BLOCK_ROWS][3];
  lv_obj_t *node_rows[NODE_ROWS];
  lv_obj_t *node_cells[NODE_ROWS][3];
  lv_obj_t *node_dots[NODE_ROWS];
} dashboard_t;

static dashboard_t dash;
static recent_block_entry_t fallback_blocks[BLOCK_ROWS];
static int fallback_block_count = 0;
static unsigned long long fallback_last_height = 0;
static uint32_t fallback_last_height_tick = 0;

static lv_obj_t *section_divider_create(lv_obj_t *parent, int y) {
  lv_obj_t *div = lv_obj_create(parent);
  lv_obj_remove_style_all(div);
  lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(div, lv_pct(100), 2);
  lv_obj_set_pos(div, 0, y);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x283550), 0);
  lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
  return div;
}

static lv_obj_t *card_create(lv_obj_t *parent, const char *title,
                             lv_color_t dot, lv_obj_t **out_title_lbl) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_width(card, lv_pct(100));
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(card, SL_BG_CARD, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, SL_BORDER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, lv_pct(100), 34);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 10, 0);

  lv_obj_t *d = lv_obj_create(row);
  lv_obj_remove_style_all(d);
  lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(d, 14, 14);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(d, dot, 0);
  lv_obj_set_style_shadow_color(d, dot, 0);
  lv_obj_set_style_shadow_width(d, 14, 0);
  lv_obj_set_style_shadow_spread(d, 2, 0);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_font(lbl, &jbm_15, 0);
  lv_obj_set_style_text_color(lbl, SL_CYAN, 0);
  if (out_title_lbl) {
    *out_title_lbl = lbl;
  }

  section_divider_create(card, 36);

  return card;
}

static void subnets_card_create(lv_obj_t *parent);
static void chain_card_create(lv_obj_t *parent);
void dashboard_set_role(const char *role);
static void dashboard_set_substate(const char *substate);
static void dashboard_set_chain_subnet(const char *subnet_id);

static lv_obj_t *mono_label(lv_obj_t *parent, const char *text,
                            lv_color_t color, const lv_font_t *font) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

static void set_label_text_if_changed(lv_obj_t *lbl, const char *text) {
  if (!lbl || !text) return;
  const char *cur = lv_label_get_text(lbl);
  if (!cur || strcmp(cur, text) != 0) {
    lv_label_set_text(lbl, text);
  }
}

static char current_role_str[32] = "roaming";
static int last_role_idx = -1;
static int last_substate_idx = -2;

void dashboard_set_role(const char *role) {
  if (role && role[0] != '\0') {
    snprintf(current_role_str, sizeof(current_role_str), "%s", role);
  }
  int active_idx = 0;
  if (role) {
    if (strcmp(role, "probation") == 0 ||
        strcmp(role, "validator-in-probation") == 0) {
      active_idx = 1;
    } else if (strcmp(role, "validator") == 0) {
      active_idx = 2;
    } else if (strcmp(role, "joining") == 0) {
      active_idx = 3;
    } else if (strcmp(role, "repeater") == 0) {
      active_idx = 4;
    } else {
      active_idx = 0;
    }
  }
  last_role_idx = active_idx;

  static const char *role_names[5] = {"Roamer", "Validator-in-Probation",
                                      "Validator", "Joining...", "Repeater"};
  if (dash.role_subtitle_label) {
    set_label_text_if_changed(dash.role_subtitle_label, role_names[active_idx]);
    lv_obj_align(dash.role_subtitle_label, LV_ALIGN_BOTTOM_RIGHT, -8, -4);
  }
}

static void dashboard_set_substate(const char *substate) {
  int active_idx = -1;
  if (substate) {
    if (strcmp(substate, "1") == 0 || strcmp(substate, "P1") == 0) {
      active_idx = 0;
    } else if (strcmp(substate, "2") == 0 || strcmp(substate, "P2") == 0) {
      active_idx = 1;
    } else if (strcmp(substate, "3") == 0 || strcmp(substate, "P3") == 0) {
      active_idx = 2;
    } else if (strcmp(substate, "4") == 0 || strcmp(substate, "P4") == 0) {
      active_idx = 3;
    }
  }
  if (active_idx == last_substate_idx) return;
  last_substate_idx = active_idx;
  for (int i = 0; i < 4; i++) {
    if (!dash.phase_chips[i]) continue;
    bool active = (i == active_idx);
    lv_obj_set_style_bg_color(dash.phase_chips[i],
                              active ? SL_CYAN : SL_BG_CARD, 0);
    lv_obj_set_style_bg_opa(dash.phase_chips[i], LV_OPA_COVER, 0);
    lv_obj_t *lbl = lv_obj_get_child(dash.phase_chips[i], 0);
    if (lbl) {
      lv_obj_set_style_text_color(lbl, active ? SL_BG_DEEP : SL_TEXT_3, 0);
    }
  }
}

static void dashboard_set_chain_subnet(const char *subnet_id) {
  if (!dash.chain_title_label) return;
  char buf[40];
  if (subnet_id && subnet_id[0] != '\0') {
    snprintf(buf, sizeof(buf), "CHAIN - %.16s", subnet_id);
  } else {
    snprintf(buf, sizeof(buf), "CHAIN");
  }
  set_label_text_if_changed(dash.chain_title_label, buf);
}

static const int SUBNET_XS[3] = {0, 148, 286};
static lv_obj_t *g_subnet_hdrs[4];
static lv_obj_t *g_block_hdrs[3];
static lv_obj_t *g_peer_hdrs[3];
static char subnet_row_ids[SUBNET_ROWS][16];
static char subnet_row_status[SUBNET_ROWS][24];
static long long subnet_row_freqs[SUBNET_ROWS];
static lv_obj_t *subnet_rows[SUBNET_ROWS];
static lv_obj_t *subnet_cells[SUBNET_ROWS][3];
static lv_obj_t *btn_join[SUBNET_ROWS];
static lv_obj_t *btn_leave[SUBNET_ROWS];

static bool node_is_validator_of_subnet(const char *st) {
  bool role_is_val =
      (strcmp(current_role_str, "validator") == 0 ||
       strcmp(current_role_str, "probation") == 0 ||
       strcmp(current_role_str, "validator-in-probation") == 0 ||
       strcmp(current_role_str, "joining") == 0);
  bool status_is_val = st && strcmp(st, "validating") == 0;
  return role_is_val && status_is_val;
}

static void subnet_row_update_buttons(int row, const char *st) {
  if (row < 0 || row >= SUBNET_ROWS || !btn_join[row] || !btn_leave[row]) {
    return;
  }
  if (subnet_row_ids[row][0] == '\0') {
    lv_obj_add_flag(btn_join[row], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_leave[row], LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (node_is_validator_of_subnet(st)) {
    lv_obj_add_flag(btn_join[row], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_leave[row], LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(btn_join[row], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_leave[row], LV_OBJ_FLAG_HIDDEN);
  }
}

static void execute_subnet_join(int row) {
  if (row < 0 || row >= SUBNET_ROWS || subnet_row_ids[row][0] == '\0') {
    return;
  }
  long long freq =
      subnet_row_freqs[row] > 0 ? subnet_row_freqs[row] : 915000000LL;

  FILE *fp = fopen("/tmp/slx_boot_flags.txt", "w");
  if (fp) {
    fprintf(fp, "--join-validator --subnet %s --freq-hz %lld\n",
            subnet_row_ids[row], freq);
    fclose(fp);
  }

  snprintf(subnet_row_status[row], sizeof(subnet_row_status[row]),
           "validating");
  dashboard_set_role("joining");
  dashboard_set_chain_subnet(subnet_row_ids[row]);
  subnet_row_update_buttons(row, "validating");

  char msg[224];
  snprintf(msg, sizeof(msg),
           "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"slx_join\",\"params\":{"
           "\"subnet\":\"%s\",\"freq_hz\":%lld,\"restart\":false}}",
           subnet_row_ids[row], freq);
  ws_send_text(msg);
}

static void execute_subnet_leave(int row) {
  if (row < 0 || row >= SUBNET_ROWS || subnet_row_ids[row][0] == '\0') {
    return;
  }

  FILE *fp = fopen("/tmp/slx_boot_flags.txt", "w");
  if (fp) {
    fprintf(fp, "--roaming\n");
    fclose(fp);
  }

  snprintf(subnet_row_status[row], sizeof(subnet_row_status[row]), "detected");
  dashboard_set_role("roaming");
  subnet_row_update_buttons(row, "detected");

  char msg[224];
  snprintf(msg, sizeof(msg),
           "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"slx_leave\",\"params\":{"
           "\"subnet\":\"%s\",\"restart\":false}}",
           subnet_row_ids[row]);
  ws_send_text(msg);
}

static void subnet_join_cb(lv_event_t *e) {
  int row = (int)(intptr_t)(long)lv_event_get_user_data(e);
  execute_subnet_join(row);
}

static void subnet_leave_cb(lv_event_t *e) {
  int row = (int)(intptr_t)(long)lv_event_get_user_data(e);
  execute_subnet_leave(row);
}

static lv_obj_t *subnet_action_button(lv_obj_t *parent, const char *label_text,
                                      lv_color_t bg_color, lv_event_cb_t cb,
                                      int row) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_size(b, 112, 38);
  lv_obj_set_ext_click_area(b, 12);
  lv_obj_set_style_bg_color(b, bg_color, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, label_text);
  lv_obj_set_style_text_font(l, &jbm_13, 0);
  lv_obj_set_style_text_color(l, SL_BG_DEEP, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)row);
  return b;
}

static void subnets_card_create(lv_obj_t *parent) {
  lv_obj_t *card = card_create(parent, "SUBNETS", SL_INDIGO, NULL);
  lv_obj_set_height(card, 182);

  lv_obj_t *hdr_row = lv_obj_create(card);
  lv_obj_remove_style_all(hdr_row);
  lv_obj_clear_flag(hdr_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hdr_row, lv_pct(100), 26);
  lv_obj_set_pos(hdr_row, 0, 44);
  const char *heads[4] = {"SUBNET", "FREQ", "HEIGHT", "ACTION"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *h = mono_label(hdr_row, heads[i], SL_TEXT_3, &jbm_13);
    g_subnet_hdrs[i] = h;
    if (i < 3) {
      lv_obj_align(h, LV_ALIGN_LEFT_MID, SUBNET_XS[i], 0);
    } else {
      lv_obj_align(h, LV_ALIGN_RIGHT_MID, -4, 0);
    }
  }

  lv_obj_t *subnet_list = lv_obj_create(card);
  lv_obj_remove_style_all(subnet_list);
  lv_obj_set_size(subnet_list, lv_pct(100), SUBNET_VISIBLE_ROWS * TABLE_ROW_H);
  lv_obj_set_pos(subnet_list, 0, 72);
  lv_obj_set_scroll_dir(subnet_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(subnet_list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(subnet_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(subnet_list, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

  for (int i = 0; i < SUBNET_ROWS; i++) {
    subnet_row_ids[i][0] = '\0';
    subnet_row_status[i][0] = '\0';
    subnet_row_freqs[i] = 0;
    lv_obj_t *row = lv_obj_create(subnet_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), TABLE_ROW_H);
    lv_obj_set_pos(row, 0, i * TABLE_ROW_H);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    subnet_rows[i] = row;
    if (i >= SUBNET_VISIBLE_ROWS) {
      lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
    for (int c = 0; c < 3; c++) {
      subnet_cells[i][c] = mono_label(row, "", SL_TEXT, &jbm_13);
      lv_obj_align(subnet_cells[i][c], LV_ALIGN_LEFT_MID, SUBNET_XS[c], 0);
    }

    btn_join[i] =
        subnet_action_button(row, "JOIN", SL_CYAN, subnet_join_cb, i);
    lv_obj_align(btn_join[i], LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_flag(btn_join[i], LV_OBJ_FLAG_HIDDEN);

    btn_leave[i] =
        subnet_action_button(row, "LEAVE", SL_AMBER, subnet_leave_cb, i);
    lv_obj_align(btn_leave[i], LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_flag(btn_leave[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static lv_obj_t *chain_height_label;
static lv_obj_t *chain_validators_label;
static lv_obj_t *chain_avg_label;

static const int BLOCK_XS[3] = {4, 210, 0};
static const int PEER_XS[3] = {4, 210, 0};

static lv_obj_t *stat_block(lv_obj_t *parent, const char *title) {
  lv_obj_t *cell = lv_obj_create(parent);
  lv_obj_remove_style_all(cell);
  lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(cell, 90);
  lv_obj_set_flex_grow(cell, 1);
  lv_obj_t *t = mono_label(cell, title, SL_TEXT_3, &outfit_16);
  lv_obj_set_pos(t, 0, 0);
  lv_obj_set_width(t, lv_pct(100));
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  return cell;
}

static lv_obj_t *status_chip_create(lv_obj_t *parent, const char *text,
                                    int width_px, int grow) {
  lv_obj_t *chip = lv_obj_create(parent);
  lv_obj_remove_style_all(chip);
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(chip, 42);
  if (width_px > 0) {
    lv_obj_set_width(chip, width_px);
  } else {
    lv_obj_set_flex_grow(chip, grow);
  }
  lv_obj_set_style_radius(chip, 6, 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_border_color(chip, SL_BORDER, 0);
  lv_obj_set_style_bg_color(chip, SL_BG_CARD, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *lbl = lv_label_create(chip);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, &outfit_16, 0);
  lv_obj_set_style_text_color(lbl, SL_TEXT_3, 0);
  lv_obj_center(lbl);
  return chip;
}

static void chain_card_create(lv_obj_t *parent) {
  lv_obj_t *card =
      card_create(parent, "CHAIN", SL_PURPLE, &dash.chain_title_label);
  lv_obj_set_height(card, 632);

  lv_obj_t *stats = lv_obj_create(card);
  lv_obj_remove_style_all(stats);
  lv_obj_clear_flag(stats, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(stats, lv_pct(100), 84);
  lv_obj_set_pos(stats, 0, 46);
  lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(stats, 6, 0);

  lv_obj_t *b;
  b = stat_block(stats, "HEIGHT");
  chain_height_label = mono_label(b, "0", SL_TEXT, &outfit_28);
  lv_obj_set_pos(chain_height_label, 0, 38);
  b = stat_block(stats, "VALIDATORS");
  chain_validators_label = mono_label(b, "0", SL_TEXT, &outfit_28);
  lv_obj_set_pos(chain_validators_label, 0, 38);
  b = stat_block(stats, "AVG. BLOCK");
  chain_avg_label = mono_label(b, "--", SL_TEXT, &outfit_28);
  lv_obj_set_pos(chain_avg_label, 0, 38);

  section_divider_create(card, 136);

  lv_obj_t *chips = lv_obj_create(card);
  lv_obj_remove_style_all(chips);
  lv_obj_clear_flag(chips, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chips, lv_pct(100), 42);
  lv_obj_set_pos(chips, 0, 148);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(chips, 8, 0);
  static const char *names[4] = {"P1", "P2", "P3", "P4"};
  for (int i = 0; i < 4; i++) {
    dash.phase_chips[i] = status_chip_create(chips, names[i], 0, 1);
  }
  last_role_idx = -1;
  dashboard_set_role("roaming");
  dashboard_set_substate("1");

  section_divider_create(card, 200);

  lv_obj_t *blocks_hdr = lv_obj_create(card);
  lv_obj_remove_style_all(blocks_hdr);
  lv_obj_clear_flag(blocks_hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(blocks_hdr, lv_pct(100), 26);
  lv_obj_set_pos(blocks_hdr, 0, 210);
  const char *block_heads[3] = {"BLOCK", "TXNS", "CONFIRM"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *h = mono_label(blocks_hdr, block_heads[i], SL_TEXT_3, &jbm_13);
    g_block_hdrs[i] = h;
    if (i < 2) {
      lv_obj_align(h, LV_ALIGN_LEFT_MID, BLOCK_XS[i], 0);
    } else {
      lv_obj_align(h, LV_ALIGN_RIGHT_MID, -4, 0);
    }
  }

  lv_obj_t *block_list = lv_obj_create(card);
  lv_obj_remove_style_all(block_list);
  lv_obj_set_size(block_list, lv_pct(100), BLOCK_VISIBLE_ROWS * BLOCK_ROW_H);
  lv_obj_set_pos(block_list, 0, 238);
  lv_obj_set_scroll_dir(block_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(block_list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(block_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(block_list, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

  for (int i = 0; i < BLOCK_ROWS; i++) {
    lv_obj_t *row = lv_obj_create(block_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), BLOCK_ROW_H);
    lv_obj_set_pos(row, 0, i * BLOCK_ROW_H);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    dash.block_rows[i] = row;
    if (i >= BLOCK_VISIBLE_ROWS) {
      lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    dash.block_cells[i][0] = mono_label(row, "", SL_CYAN, &jbm_13);
    lv_obj_align(dash.block_cells[i][0], LV_ALIGN_LEFT_MID, BLOCK_XS[0], 0);

    dash.block_cells[i][1] = mono_label(row, "", SL_PURPLE, &jbm_13);
    lv_obj_align(dash.block_cells[i][1], LV_ALIGN_LEFT_MID, BLOCK_XS[1], 0);

    dash.block_cells[i][2] = mono_label(row, "", SL_TEXT, &jbm_13);
    lv_obj_align(dash.block_cells[i][2], LV_ALIGN_RIGHT_MID, -4, 0);
  }

  section_divider_create(card, 400);

  lv_obj_t *hdr_row = lv_obj_create(card);
  lv_obj_remove_style_all(hdr_row);
  lv_obj_clear_flag(hdr_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hdr_row, lv_pct(100), 26);
  lv_obj_set_pos(hdr_row, 0, 410);
  const char *heads[3] = {"VALIDATOR", "HEIGHT", "RSSI"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *h = mono_label(hdr_row, heads[i], SL_TEXT_3, &jbm_13);
    g_peer_hdrs[i] = h;
    if (i < 2) {
      lv_obj_align(h, LV_ALIGN_LEFT_MID, PEER_XS[i], 0);
    } else {
      lv_obj_align(h, LV_ALIGN_RIGHT_MID, -4, 0);
    }
  }

  lv_obj_t *node_list = lv_obj_create(card);
  lv_obj_remove_style_all(node_list);
  lv_obj_set_size(node_list, lv_pct(100), VISIBLE_TABLE_ROWS * TABLE_ROW_H);
  lv_obj_set_pos(node_list, 0, 438);
  lv_obj_set_scroll_dir(node_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(node_list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(node_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(node_list, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

  for (int i = 0; i < NODE_ROWS; i++) {
    lv_obj_t *row = lv_obj_create(node_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), TABLE_ROW_H);
    lv_obj_set_pos(row, 0, i * TABLE_ROW_H);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    dash.node_rows[i] = row;
    if (i >= VISIBLE_TABLE_ROWS) {
      lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *d = lv_obj_create(row);
    lv_obj_remove_style_all(d);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(d, 10, 10);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, SL_TEXT_3, 0);
    lv_obj_align(d, LV_ALIGN_LEFT_MID, 84, 0);
    lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    dash.node_dots[i] = d;

    for (int c = 0; c < 3; c++) {
      dash.node_cells[i][c] = mono_label(row, "", SL_TEXT, &jbm_13);
      if (c < 2) {
        lv_obj_align(dash.node_cells[i][c], LV_ALIGN_LEFT_MID, PEER_XS[c], 0);
      } else {
        lv_obj_align(dash.node_cells[i][c], LV_ALIGN_RIGHT_MID, -4, 0);
      }
    }
  }
}

static void recent_block_row_set(int row, unsigned long long height,
                                 long long txs, double confirm_ms,
                                 long rounds) {
  if (row < 0 || row >= BLOCK_ROWS) return;
  if (height == 0) {
    for (int c = 0; c < 3; c++) {
      set_label_text_if_changed(dash.block_cells[row][c], "");
    }
    if (dash.block_rows[row] && row >= BLOCK_VISIBLE_ROWS) {
      lv_obj_add_flag(dash.block_rows[row], LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }
  if (dash.block_rows[row]) {
    lv_obj_clear_flag(dash.block_rows[row], LV_OBJ_FLAG_HIDDEN);
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "#%llu", height);
  set_label_text_if_changed(dash.block_cells[row][0], buf);

  snprintf(buf, sizeof(buf), "%lld tx", txs >= 0 ? txs : 0LL);
  set_label_text_if_changed(dash.block_cells[row][1], buf);

  if (confirm_ms > 0.0) {
    if (rounds > 0) {
      snprintf(buf, sizeof(buf), "%.1fs (%ldr)", confirm_ms / 1000.0, rounds);
    } else {
      snprintf(buf, sizeof(buf), "%.1fs", confirm_ms / 1000.0);
    }
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  set_label_text_if_changed(dash.block_cells[row][2], buf);
}

static void fallback_insert_single_block(unsigned long long height,
                                         long long txs, double confirm_ms,
                                         long rounds) {
  int ins = 0;
  while (ins < fallback_block_count && fallback_blocks[ins].height > height) {
    ins++;
  }
  if (ins >= BLOCK_ROWS) return;
  int move_cnt = fallback_block_count < BLOCK_ROWS
                     ? fallback_block_count - ins
                     : BLOCK_ROWS - 1 - ins;
  if (move_cnt > 0) {
    memmove(&fallback_blocks[ins + 1], &fallback_blocks[ins],
            (size_t)move_cnt * sizeof(recent_block_entry_t));
  }
  fallback_blocks[ins].height = height;
  fallback_blocks[ins].txs = txs >= 0 ? txs : 0;
  fallback_blocks[ins].confirm_ms = confirm_ms;
  fallback_blocks[ins].rounds = rounds > 0 ? rounds : 1;
  if (fallback_block_count < BLOCK_ROWS) {
    fallback_block_count++;
  }
}

static void fallback_record_block(unsigned long long height, long long txs,
                                  double confirm_ms, long rounds) {
  if (height == 0) return;
  for (int i = 0; i < fallback_block_count; i++) {
    if (fallback_blocks[i].height == height) {
      if (txs > fallback_blocks[i].txs) fallback_blocks[i].txs = txs;
      if (confirm_ms > 0.0) {
        fallback_blocks[i].confirm_ms = confirm_ms;
        fallback_blocks[i].rounds = rounds > 0 ? rounds : 1;
      }
      return;
    }
  }
  if (fallback_block_count > 0) {
    unsigned long long max_h = fallback_blocks[0].height;
    unsigned long long min_h = fallback_blocks[fallback_block_count - 1].height;
    if (height > max_h) {
      if (height - max_h <= BLOCK_ROWS) {
        for (unsigned long long mh = max_h + 1; mh < height; mh++) {
          fallback_insert_single_block(mh, 0, confirm_ms, 1);
        }
      } else {
        fallback_block_count = 0;
      }
    } else if (height < min_h) {
      if (!(min_h > 1 && height == min_h - 1 &&
            fallback_block_count < BLOCK_ROWS)) {
        return;
      }
    }
  }
  fallback_insert_single_block(height, txs, confirm_ms, rounds);
}

static void recent_blocks_update(const cJSON *nodes_snap,
                                 const cJSON *stats_root,
                                 unsigned long long current_height,
                                 double avg_block_ms) {
  const cJSON *rb =
      nodes_snap ? cJSON_GetObjectItem(nodes_snap, "recent_blocks") : NULL;
  if (cJSON_IsArray(rb) && cJSON_GetArraySize(rb) > 0) {
    int row = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, rb) {
      if (row >= BLOCK_ROWS) break;
      unsigned long long h = (unsigned long long)cJSON_GetNumberValue(
          cJSON_GetObjectItem(item, "height"));
      if (h == 0) continue;
      long long txs =
          (long long)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "txs"));
      double cms =
          cJSON_GetNumberValue(cJSON_GetObjectItem(item, "confirm_ms"));
      long r = (long)cJSON_GetNumberValue(cJSON_GetObjectItem(item, "rounds"));
      recent_block_row_set(row, h, txs, cms, r);
      row++;
    }
    for (; row < BLOCK_ROWS; row++) {
      recent_block_row_set(row, 0, 0, 0.0, 0);
    }
    return;
  }

  // Fallback: seed from stats_root.phaseMetricsHistory / phaseMetrics / height
  if (stats_root) {
    const cJSON *hist =
        cJSON_GetObjectItem(stats_root, "phaseMetricsHistory");
    if (cJSON_IsArray(hist)) {
      const cJSON *pm;
      cJSON_ArrayForEach(pm, hist) {
        int ph = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pm, "phase"));
        unsigned long long bh = (unsigned long long)cJSON_GetNumberValue(
            cJSON_GetObjectItem(pm, "blockHeight"));
        if (ph == 4 && bh > 0) {
          long long txs = (long long)cJSON_GetNumberValue(
              cJSON_GetObjectItem(pm, "phaseValue"));
          double cms =
              cJSON_GetNumberValue(cJSON_GetObjectItem(pm, "confirmMs"));
          long r = (long)cJSON_GetNumberValue(
              cJSON_GetObjectItem(pm, "roundsToConfirm"));
          fallback_record_block(bh, txs, cms, r);
        }
      }
    }
    const cJSON *pm = cJSON_GetObjectItem(stats_root, "phaseMetrics");
    if (cJSON_IsObject(pm)) {
      int ph = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pm, "phase"));
      unsigned long long bh = (unsigned long long)cJSON_GetNumberValue(
          cJSON_GetObjectItem(pm, "blockHeight"));
      if (ph == 4 && bh > 0) {
        long long txs = (long long)cJSON_GetNumberValue(
            cJSON_GetObjectItem(pm, "phaseValue"));
        double cms =
            cJSON_GetNumberValue(cJSON_GetObjectItem(pm, "confirmMs"));
        long r = (long)cJSON_GetNumberValue(
            cJSON_GetObjectItem(pm, "roundsToConfirm"));
        fallback_record_block(bh, txs, cms, r);
      }
    }
  }

  if (current_height > 0 && current_height != fallback_last_height) {
    uint32_t now_t = lv_tick_get();
    double cms = avg_block_ms;
    if (fallback_last_height > 0 && current_height > fallback_last_height &&
        fallback_last_height_tick > 0) {
      double dt = (double)(now_t - fallback_last_height_tick);
      unsigned long long dh = current_height - fallback_last_height;
      if (dt > 0.0 && dh > 0) {
        cms = dt / (double)dh;
      }
    }
    long long p4_txs = 0;
    if (nodes_snap) {
      const cJSON *pms = cJSON_GetObjectItem(nodes_snap, "phase_metrics");
      const cJSON *p4 = pms ? cJSON_GetObjectItem(pms, "4") : NULL;
      if (cJSON_IsObject(p4)) {
        unsigned long long p4h = (unsigned long long)cJSON_GetNumberValue(
            cJSON_GetObjectItem(p4, "height"));
        if (p4h == current_height) {
          p4_txs = (long long)cJSON_GetNumberValue(
              cJSON_GetObjectItem(p4, "value"));
        }
      }
    }
    fallback_record_block(current_height, p4_txs, cms, 1);
    fallback_last_height = current_height;
    fallback_last_height_tick = now_t;
  }

  for (int i = 0; i < BLOCK_ROWS; i++) {
    if (i < fallback_block_count) {
      recent_block_row_set(i, fallback_blocks[i].height, fallback_blocks[i].txs,
                           fallback_blocks[i].confirm_ms,
                           fallback_blocks[i].rounds);
    } else {
      recent_block_row_set(i, 0, 0, 0.0, 0);
    }
  }
}

static void subnet_row_set(int row, const char *id, long long tip,
                           const char *st, long long freq) {
  if (row < 0 || row >= SUBNET_ROWS) return;
  if (!id || id[0] == '\0') {
    for (int c = 0; c < 3; c++) {
      set_label_text_if_changed(subnet_cells[row][c], "");
    }
    subnet_row_ids[row][0] = '\0';
    subnet_row_status[row][0] = '\0';
    subnet_row_freqs[row] = 0;
    lv_obj_add_flag(btn_join[row], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_leave[row], LV_OBJ_FLAG_HIDDEN);
    if (subnet_rows[row] && row >= SUBNET_VISIBLE_ROWS) {
      lv_obj_add_flag(subnet_rows[row], LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }
  if (subnet_rows[row]) {
    lv_obj_clear_flag(subnet_rows[row], LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_style_text_color(subnet_cells[row][0], SL_TEXT, 0);
  char buf[24];
  snprintf(buf, sizeof(buf), "%.8s", id);
  set_label_text_if_changed(subnet_cells[row][0], buf);

  long long eff_freq = freq > 0 ? freq : 915000000LL;
  snprintf(buf, sizeof(buf), "%.1fM", (double)eff_freq / 1e6);
  set_label_text_if_changed(subnet_cells[row][1], buf);

  snprintf(buf, sizeof(buf), "%lld", tip);
  set_label_text_if_changed(subnet_cells[row][2], buf);

  snprintf(subnet_row_ids[row], sizeof(subnet_row_ids[row]), "%s", id);
  snprintf(subnet_row_status[row], sizeof(subnet_row_status[row]), "%s",
           st ? st : "detected");
  subnet_row_freqs[row] = eff_freq;
  subnet_row_update_buttons(row, st);
}

static void subnet_row_clear(int row) { subnet_row_set(row, "", 0, "", 0); }

static void subnets_listing_update(const cJSON *subs) {
  if (!cJSON_IsArray(subs)) {
    return;
  }
  int row = 0;
  const char *active_subnet_id = NULL;
  const cJSON *sn;
  cJSON_ArrayForEach(sn, subs) {
    if (row >= SUBNET_ROWS) break;
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(sn, "id"));
    if (!id) continue;
    long long tip =
        (long long)cJSON_GetNumberValue(cJSON_GetObjectItem(sn, "tip"));
    const char *st =
        cJSON_GetStringValue(cJSON_GetObjectItem(sn, "status"));
    long long freq =
        (long long)cJSON_GetNumberValue(cJSON_GetObjectItem(sn, "freq_hz"));
    if (!active_subnet_id || (st && strcmp(st, "validating") == 0)) {
      active_subnet_id = id;
    }
    subnet_row_set(row, id, tip, st, freq);
    row++;
  }
  if (active_subnet_id) {
    dashboard_set_chain_subnet(active_subnet_id);
  }
  for (; row < SUBNET_ROWS; row++) {
    subnet_row_clear(row);
  }
}

// fallback for older nodes: roamingStatus.roaming_subnets [{freq_hz, height}]
static void subnets_update(const cJSON *roaming_subnets) {
  if (!cJSON_IsArray(roaming_subnets)) {
    for (int i = 0; i < SUBNET_ROWS; i++) {
      subnet_row_clear(i);
    }
    return;
  }
  int row = 0;
  const cJSON *sn;
  cJSON_ArrayForEach(sn, roaming_subnets) {
    if (row >= SUBNET_ROWS) break;
    const cJSON *f = cJSON_GetObjectItem(sn, "freq_hz");
    const cJSON *ht = cJSON_GetObjectItem(sn, "height");
    long long freq =
        cJSON_IsNumber(f) ? (long long)cJSON_GetNumberValue(f) : 915000000LL;
    long long tip =
        cJSON_IsNumber(ht) ? (long long)cJSON_GetNumberValue(ht) : 0LL;
    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%08llx", freq / 1000);
    if (row == 0) {
      dashboard_set_chain_subnet(id_buf);
    }
    subnet_row_set(row, id_buf, tip, "detected", freq);
    row++;
  }
  for (; row < SUBNET_ROWS; row++) {
    subnet_row_clear(row);
  }
}

static void chain_update(const cJSON *nodes_snap, const cJSON *stats_root) {
  if (!cJSON_IsObject(nodes_snap)) {
    return;
  }
  const cJSON *subnet_item = cJSON_GetObjectItem(nodes_snap, "subnet");
  if (cJSON_IsString(subnet_item)) {
    dashboard_set_chain_subnet(cJSON_GetStringValue(subnet_item));
  }

  // Consensus phase ("1", "2", "3", "4")
  long phase =
      (long)cJSON_GetNumberValue(cJSON_GetObjectItem(nodes_snap, "phase"));
  char pbuf[8];
  snprintf(pbuf, sizeof(pbuf), "%ld", (phase >= 1 && phase <= 4) ? phase : 1L);
  dashboard_set_substate(pbuf);

  unsigned long long hv = 0ULL;
  const cJSON *h = cJSON_GetObjectItem(nodes_snap, "height");
  const cJSON *top_h =
      stats_root ? cJSON_GetObjectItem(stats_root, "height") : NULL;
  if (cJSON_IsNumber(h) || cJSON_IsNumber(top_h)) {
    hv = cJSON_IsNumber(h) ? (unsigned long long)cJSON_GetNumberValue(h) : 0ULL;
    unsigned long long thv = cJSON_IsNumber(top_h)
                                 ? (unsigned long long)cJSON_GetNumberValue(top_h)
                                 : 0ULL;
    if (thv > hv) hv = thv;
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", hv);
    set_label_text_if_changed(chain_height_label, buf);
  }

  const cJSON *validators =
      cJSON_GetObjectItem(nodes_snap, "validators");
  if (cJSON_IsNumber(validators)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", (int)cJSON_GetNumberValue(validators));
    set_label_text_if_changed(chain_validators_label, buf);
  }

  // average block time over the node's last observed intervals
  double avg_ms = 0.0;
  const cJSON *ab = cJSON_GetObjectItem(nodes_snap, "avg_block_time_ms");
  if (cJSON_IsNumber(ab)) {
    avg_ms = cJSON_GetNumberValue(ab);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1fs", avg_ms / 1000.0);
    set_label_text_if_changed(chain_avg_label, buf);
  } else if (cJSON_IsNull(ab)) {
    set_label_text_if_changed(chain_avg_label, "--");
  }

  recent_blocks_update(nodes_snap, stats_root, hv, avg_ms);
  superapp_views_tick(hv, current_role_str, NULL);
}

static lv_obj_t *g_tab_btns[4];
static lv_obj_t *g_tab_views[4];
static int g_active_tab = 0;
static lv_obj_t *g_dash_root = NULL;
static lv_obj_t *g_dash_hdr = NULL;

void dashboard_on_orientation_changed(int hor_res, int ver_res) {
  if (!g_dash_root || !lv_obj_is_valid(g_dash_root)) return;
  bool is_land = (hor_res > ver_res);
  lv_obj_t *scr = lv_obj_get_parent(g_dash_root);
  int usable_h = (ver_res > 56) ? (ver_res - 56) : ver_res;
  if (scr && lv_obj_is_valid(scr)) {
    lv_obj_set_pos(scr, 0, 56);
    lv_obj_set_size(scr, hor_res, usable_h);
  }
  lv_obj_set_pos(g_dash_root, 0, 0);
  lv_obj_set_size(g_dash_root, hor_res, usable_h);
  int h_pad = is_land ? 44 : 18;
  int b_pad = is_land ? 32 : 40;
  lv_obj_set_style_pad_left(g_dash_root, h_pad, 0);
  lv_obj_set_style_pad_right(g_dash_root, h_pad, 0);
  lv_obj_set_style_pad_bottom(g_dash_root, b_pad, 0);
  if (g_dash_hdr && lv_obj_is_valid(g_dash_hdr)) {
    lv_obj_set_height(g_dash_hdr, is_land ? 68 : 140);
    if (dash.role_subtitle_label && lv_obj_is_valid(dash.role_subtitle_label)) {
      lv_obj_align(dash.role_subtitle_label, LV_ALIGN_BOTTOM_RIGHT, -8, -4);
    }
  }
  const int s_xs[3] = {0, is_land ? 310 : 148, is_land ? 630 : 286};
  const int bp_xs[2] = {4, is_land ? 430 : 210};
  for (int c = 0; c < 3; c++) {
    if (g_subnet_hdrs[c] && lv_obj_is_valid(g_subnet_hdrs[c])) {
      lv_obj_align(g_subnet_hdrs[c], LV_ALIGN_LEFT_MID, s_xs[c], 0);
    }
    for (int r = 0; r < SUBNET_ROWS; r++) {
      if (subnet_cells[r][c] && lv_obj_is_valid(subnet_cells[r][c])) {
        lv_obj_align(subnet_cells[r][c], LV_ALIGN_LEFT_MID, s_xs[c], 0);
      }
    }
  }
  for (int c = 0; c < 2; c++) {
    if (g_block_hdrs[c] && lv_obj_is_valid(g_block_hdrs[c])) {
      lv_obj_align(g_block_hdrs[c], LV_ALIGN_LEFT_MID, bp_xs[c], 0);
    }
    if (g_peer_hdrs[c] && lv_obj_is_valid(g_peer_hdrs[c])) {
      lv_obj_align(g_peer_hdrs[c], LV_ALIGN_LEFT_MID, bp_xs[c], 0);
    }
    for (int r = 0; r < BLOCK_ROWS; r++) {
      if (dash.block_cells[r][c] && lv_obj_is_valid(dash.block_cells[r][c])) {
        lv_obj_align(dash.block_cells[r][c], LV_ALIGN_LEFT_MID, bp_xs[c], 0);
      }
    }
    for (int r = 0; r < NODE_ROWS; r++) {
      if (dash.node_cells[r][c] && lv_obj_is_valid(dash.node_cells[r][c])) {
        lv_obj_align(dash.node_cells[r][c], LV_ALIGN_LEFT_MID, bp_xs[c], 0);
      }
    }
  }
  lv_obj_update_layout(g_dash_root);
}

void dashboard_select_tab(int tab_idx) {
  if (tab_idx < 0 || tab_idx >= 4) return;
  g_active_tab = tab_idx;
  for (int i = 0; i < 4; i++) {
    if (g_tab_views[i]) {
      if (i == tab_idx) {
        lv_obj_clear_flag(g_tab_views[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(g_tab_views[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (g_tab_btns[i]) {
      bool act = (i == tab_idx);
      lv_obj_set_style_bg_color(g_tab_btns[i], act ? SL_CYAN : SL_BG_CARD, 0);
      lv_obj_set_style_border_color(g_tab_btns[i], act ? SL_CYAN : SL_BORDER, 0);
      lv_obj_t *lbl = lv_obj_get_child(g_tab_btns[i], 0);
      if (lbl) {
        lv_obj_set_style_text_color(lbl, act ? SL_BG_DEEP : SL_TEXT_2, 0);
      }
    }
  }
}

static void on_tab_btn_cb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  dashboard_select_tab(idx);
}

void dashboard_create(lv_obj_t *parent, const struct DashboardArgs *args) {
  memset(&dash, 0, sizeof(dash));
  dash.args = *args;
  last_role_idx = -1;
  last_substate_idx = -2;
  fallback_block_count = 0;
  fallback_last_height = 0;
  fallback_last_height_tick = 0;

  g_dash_root = parent;
  lv_obj_set_style_bg_color(parent, SL_BG_DEEP, 0);
  lv_obj_add_flag(parent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_scroll_dir(parent, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(parent, 8, 0);
  lv_obj_set_style_pad_row(parent, 8, 0);

  int cur_w = (int)lv_disp_get_hor_res(NULL);
  int cur_h = (int)lv_disp_get_ver_res(NULL);
  int init_h_pad = (cur_w > cur_h) ? 44 : 18;
  int init_b_pad = (cur_w > cur_h) ? 26 : 36;
  lv_obj_set_style_pad_top(parent, 10, 0);
  lv_obj_set_style_pad_left(parent, init_h_pad, 0);
  lv_obj_set_style_pad_right(parent, init_h_pad, 0);
  lv_obj_set_style_pad_bottom(parent, init_b_pad, 0);
  lv_obj_t *hdr = lv_obj_create(parent);
  g_dash_hdr = hdr;
  lv_obj_remove_style_all(hdr);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hdr, lv_pct(100), (cur_w > cur_h) ? 72 : 140);
  lv_obj_t *logo = lv_image_create(hdr);
  lv_image_set_src(logo, &img_logo_horizontal);
  lv_obj_align(logo, LV_ALIGN_LEFT_MID, 0, 0);
  dash.role_subtitle_label = mono_label(hdr, "Roamer", SL_CYAN, &outfit_16);
  lv_obj_align(dash.role_subtitle_label, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
  lv_obj_t *line = lv_obj_create(parent);
  lv_obj_remove_style_all(line);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(line, lv_pct(100), 2);
  lv_obj_set_style_bg_color(line, SL_CYAN_DIM, 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_60, 0);

  lv_obj_t *navbar = lv_obj_create(parent);
  lv_obj_remove_style_all(navbar);
  lv_obj_clear_flag(navbar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(navbar, lv_pct(100), 44);
  lv_obj_set_flex_flow(navbar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(navbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(navbar, 6, 0);

  static const char *kTabNames[4] = {"LEXICON", "WALLET", "CHAT", "LEXIAPPS"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *b = lv_button_create(navbar);
    lv_obj_set_height(b, 42);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, SL_BORDER, 0);
    lv_obj_set_style_bg_color(b, SL_BG_CARD, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_t *l = mono_label(b, kTabNames[i], SL_TEXT_2, &jbm_13);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, on_tab_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    g_tab_btns[i] = b;
  }

  for (int i = 0; i < 4; i++) {
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_remove_style_all(v);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(v, lv_pct(100));
    lv_obj_set_height(v, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(v, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(v, 8, 0);
    g_tab_views[i] = v;
  }

  subnets_card_create(g_tab_views[0]);
  chain_card_create(g_tab_views[0]);
  superapp_views_create(g_tab_views[1], g_tab_views[2], g_tab_views[3], args);

  // Seed default initial state until live WebSocket telemetry arrives
  dashboard_set_role("validator");
  dashboard_set_chain_subnet("00000001");
  subnet_row_set(0, "00000001", 1042, "validating", 915000000LL);
  subnet_row_set(1, "bb020002", 819, "detected", 916000000LL);
  set_label_text_if_changed(chain_height_label, "1042");
  set_label_text_if_changed(chain_validators_label, "4");
  set_label_text_if_changed(chain_avg_label, "5.6s");
  recent_block_row_set(0, 1042, 225, 5620.0, 1);
  recent_block_row_set(1, 1041, 240, 5580.0, 1);
  recent_block_row_set(2, 1040, 198, 5650.0, 1);
  static const char *kInitPeers[3][3] = {
      {"7f3a91c2", "1042", "-78 dBm"},
      {"4e10b804", "1042", "-84 dBm"},
      {"9c21d05e", "1042", "-89 dBm"},
  };
  for (int r = 0; r < 3; r++) {
    if (dash.node_rows[r]) {
      lv_obj_clear_flag(dash.node_rows[r], LV_OBJ_FLAG_HIDDEN);
    }
    if (dash.node_dots[r]) {
      lv_obj_set_style_bg_color(dash.node_dots[r], SL_GREEN_HI, 0);
      lv_obj_clear_flag(dash.node_dots[r], LV_OBJ_FLAG_HIDDEN);
    }
    for (int c = 0; c < 3; c++) {
      set_label_text_if_changed(dash.node_cells[r][c], kInitPeers[r][c]);
    }
  }

  dashboard_select_tab(0);
  int init_w = (int)lv_obj_get_width(parent);
  int init_h = (int)lv_obj_get_height(parent);
  if (init_w > 0 && init_h > 0) {
    dashboard_on_orientation_changed(init_w, init_h + 56);
  }
}

static void apply_stats(const cJSON *stats) {
  if (!cJSON_IsObject(stats)) {
    return;
  }
  const cJSON *nodes_snap = cJSON_GetObjectItem(stats, "nodes");
  if (cJSON_IsObject(nodes_snap)) {
    const cJSON *role = cJSON_GetObjectItem(nodes_snap, "role");
    if (cJSON_IsString(role)) {
      dashboard_set_role(cJSON_GetStringValue(role));
    }
    subnets_listing_update(cJSON_GetObjectItem(nodes_snap, "subnets"));
    chain_update(nodes_snap, stats);

    const cJSON *nodes = cJSON_GetObjectItem(nodes_snap, "nodes");
    int row = 0;
    if (cJSON_IsArray(nodes)) {
      const cJSON *n;
      cJSON_ArrayForEach(n, nodes) {
        if (row >= NODE_ROWS) break;
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(n, "id"));
        if (!id) continue;
        if (dash.node_rows[row]) {
          lv_obj_clear_flag(dash.node_rows[row], LV_OBJ_FLAG_HIDDEN);
        }
        double age = cJSON_GetNumberValue(cJSON_GetObjectItem(n, "age_ms"));
        long h = (long)cJSON_GetNumberValue(cJSON_GetObjectItem(n, "height"));
        double rssi = cJSON_GetNumberValue(cJSON_GetObjectItem(n, "rssi"));
        lv_color_t dotc = age < 5000 ? SL_GREEN_HI : SL_AMBER;
        lv_obj_set_style_bg_color(dash.node_dots[row], dotc, 0);
        lv_obj_clear_flag(dash.node_dots[row], LV_OBJ_FLAG_HIDDEN);
        char cell[24];
        snprintf(cell, sizeof(cell), "%.8s", id);
        set_label_text_if_changed(dash.node_cells[row][0], cell);
        snprintf(cell, sizeof(cell), "%ld", h);
        set_label_text_if_changed(dash.node_cells[row][1], cell);
        snprintf(cell, sizeof(cell), "%+.0f dBm", rssi);
        set_label_text_if_changed(dash.node_cells[row][2], cell);
        row++;
      }
    }
    for (; row < NODE_ROWS; row++) {
      for (int c = 0; c < 3; c++) {
        set_label_text_if_changed(dash.node_cells[row][c], "");
      }
      lv_obj_set_style_bg_color(dash.node_dots[row], SL_TEXT_3, 0);
      lv_obj_add_flag(dash.node_dots[row], LV_OBJ_FLAG_HIDDEN);
      if (dash.node_rows[row] && row >= VISIBLE_TABLE_ROWS) {
        lv_obj_add_flag(dash.node_rows[row], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  // Fallback when the snapshot carries no subnets listing (older node)
  const cJSON *rs = cJSON_GetObjectItem(stats, "roamingStatus");
  if (cJSON_IsObject(rs)) {
    if (!cJSON_IsArray(cJSON_GetObjectItem(nodes_snap, "subnets"))) {
      subnets_update(cJSON_GetObjectItem(rs, "roaming_subnets"));
    }
  }
}

static lv_timer_t *refresh_timer = NULL;

static void refresh_timer_cb(lv_timer_t *timer) {
  (void)timer;
  static char frame[32768];
  int n = ws_service(frame, sizeof(frame));
  if (n < 0) {
    ws_connect(dash.args.rpc_host ? dash.args.rpc_host : "127.0.0.1",
               dash.args.rpc_ws_port, "/");
    return;
  }
  if (n == 0) {
    return; // nothing new streamed this tick
  }
  cJSON *msg = cJSON_Parse(frame);
  if (!msg) {
    return;
  }
  const cJSON *params = cJSON_GetObjectItem(msg, "params");
  const cJSON *result = params ? cJSON_GetObjectItem(params, "result") : NULL;
  apply_stats(result);
  cJSON_Delete(msg);
}

void dashboard_start_timer(const struct DashboardArgs *args) {
  dash.args = *args;
  ws_init(256 * 1024);
  ws_connect(dash.args.rpc_host ? dash.args.rpc_host : "127.0.0.1",
             dash.args.rpc_ws_port, "/");
  if (refresh_timer) {
    lv_timer_del(refresh_timer);
  }
  refresh_timer = lv_timer_create(refresh_timer_cb, 50, NULL);
  refresh_timer_cb(NULL); // connect/pump immediately
}

void dashboard_stop_timer(void) {
  if (refresh_timer) {
    lv_timer_del(refresh_timer);
    refresh_timer = NULL;
  }
  g_dash_root = NULL;
  g_dash_hdr = NULL;
}
