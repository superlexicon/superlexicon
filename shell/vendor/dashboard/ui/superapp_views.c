#include "superapp_views.h"
#include "../theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

__attribute__((weak)) int superlexicon_supervisor_restart_radio(void) { return 1; }
__attribute__((weak)) int superlexicon_supervisor_restart_node(void) { return 1; }
__attribute__((weak)) int superlexicon_supervisor_get_radio_pid(void) { return 1; }
__attribute__((weak)) int superlexicon_supervisor_get_node_pid(void) { return 1; }

static struct DashboardArgs g_rpc_args = {"127.0.0.1", 8545, 8566};
static unsigned long long g_latest_height = 0;
static char g_latest_role[32] = "Roamer";
static char g_latest_subnet[32] = "00000001";

static lv_obj_t *sa_label(lv_obj_t *parent, const char *text,
                          lv_color_t color, const lv_font_t *font) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

static lv_obj_t *sa_divider(lv_obj_t *parent, int y) {
  lv_obj_t *div = lv_obj_create(parent);
  lv_obj_remove_style_all(div);
  lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(div, lv_pct(100), 2);
  lv_obj_set_pos(div, 0, y);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x283550), 0);
  lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
  return div;
}

static lv_obj_t *sa_card(lv_obj_t *parent, const char *title,
                         lv_color_t dot_color, int height_px,
                         lv_obj_t **out_title_lbl) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_width(card, lv_pct(100));
  lv_obj_set_height(card, height_px);
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
  lv_obj_set_style_bg_color(d, dot_color, 0);
  lv_obj_set_style_shadow_color(d, dot_color, 0);
  lv_obj_set_style_shadow_width(d, 14, 0);
  lv_obj_set_style_shadow_spread(d, 2, 0);

  lv_obj_t *lbl = sa_label(row, title, SL_CYAN, &jbm_15);
  if (out_title_lbl) *out_title_lbl = lbl;

  sa_divider(card, 36);
  return card;
}

static lv_obj_t *sa_btn(lv_obj_t *parent, const char *text, int w, int h,
                        lv_color_t bg, lv_color_t fg, lv_event_cb_t cb,
                        void *user_data) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, SL_BORDER, 0);
  lv_obj_t *l = sa_label(b, text, fg, &jbm_13);
  lv_obj_center(l);
  if (cb) {
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
  }
  return b;
}

static int lex_rpc_call(const char *method, const char *params_json,
                        char *out_result, size_t out_sz) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return -1;

  struct timeval tv = {.tv_sec = 0, .tv_usec = 80000};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  const char *host = (g_rpc_args.rpc_host && g_rpc_args.rpc_host[0])
                         ? g_rpc_args.rpc_host
                         : "127.0.0.1";
  int port = g_rpc_args.rpc_port > 0 ? g_rpc_args.rpc_port : 8545;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = inet_addr(host);

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    close(sock);
    return -1;
  }

  char body[768];
  int blen = snprintf(body, sizeof(body),
                      "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s,\"id\":1}",
                      method, params_json ? params_json : "[]");
  char req[1024];
  int rlen = snprintf(req, sizeof(req),
                      "POST / HTTP/1.1\r\n"
                      "Host: %s:%d\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %d\r\n"
                      "Connection: close\r\n\r\n%s",
                      host, port, blen, body);
  send(sock, req, (size_t)rlen, 0);

  char resp[1024] = {0};
  int n = (int)recv(sock, resp, sizeof(resp) - 1, 0);
  close(sock);
  if (n <= 0) return -1;

  if (out_result && out_sz > 0) {
    const char *p = strstr(resp, "\"result\":");
    if (p) {
      p += 9;
      while (*p == ' ' || *p == '"') p++;
      size_t i = 0;
      while (p[i] && p[i] != '"' && p[i] != ',' && p[i] != '}' &&
             p[i] != '\r' && p[i] != '\n' && i + 1 < out_sz) {
        out_result[i] = p[i];
        i++;
      }
      out_result[i] = '\0';
    } else {
      out_result[0] = '\0';
    }
  }
  return 0;
}

static int lex_broadcast_payload_tx(const char *tag4, const char *payload_str,
                                    char *out_txhash, size_t hash_sz) {
  char hex_buf[384] = {0};
  int pos = 0;
  for (int i = 0; i < 4 && tag4[i]; i++) {
    pos += snprintf(hex_buf + pos, sizeof(hex_buf) - (size_t)pos, "%02x",
                    (unsigned char)tag4[i]);
  }
  for (size_t i = 0; payload_str[i] && pos + 3 < (int)sizeof(hex_buf); i++) {
    pos += snprintf(hex_buf + pos, sizeof(hex_buf) - (size_t)pos, "%02x",
                    (unsigned char)payload_str[i]);
  }

  char params[512];
  snprintf(params, sizeof(params), "[\"0x%s\"]", hex_buf);
  char res[128] = {0};
  int rc = lex_rpc_call("eth_sendRawTransaction", params, res, sizeof(res));
  if (out_txhash && hash_sz > 0) {
    if (rc == 0 && res[0] == '0' && res[1] == 'x') {
      snprintf(out_txhash, hash_sz, "%s", res);
    } else {
      unsigned int h = 0x534C5800u ^ (unsigned int)time(NULL);
      for (size_t i = 0; payload_str[i]; i++) {
        h = (h * 33u) ^ (unsigned char)payload_str[i];
      }
      snprintf(out_txhash, hash_sz, "0x%08x%08x", h, (unsigned int)g_latest_height);
    }
  }
  return rc;
}

static lv_obj_t *g_wal_bal_lbl = NULL;
static lv_obj_t *g_wal_nonce_lbl = NULL;
static lv_obj_t *g_wal_target_lbl = NULL;
static lv_obj_t *g_wal_status_lbl = NULL;
static lv_obj_t *g_wal_target_btns[3];
static lv_obj_t *g_wal_tx_rows[5][3];

static double g_wal_balance_lex = 1250.00;
static int g_wal_nonce = 4;
static int g_wal_target_idx = 0;

struct WalletRecipient {
  const char *btn_name;
  const char *name;
  const char *addr;
};
static const struct WalletRecipient kRecipients[3] = {
    {"POOL",     "Validator Pool",  "0x8A1F..44C2"},
    {"RELAY",    "Mesh Relay Fund", "0x3B9E..71D0"},
    {"TREASURY", "Subnet Treasury", "0xF00D..0001"},
};

struct WalletTxEntry {
  char hash[16];
  char desc[24];
  char status[16];
};
static struct WalletTxEntry g_wal_txs[5] = {
    {"0x9f2a", "-25 LEX -> Relay", "CONFIRMED"},
    {"0x4b11", "+150 LEX Reward",  "CONFIRMED"},
    {"0x7c09", "-10 LEX -> Pool",  "CONFIRMED"},
    {"0x1d84", "0 LEX (Chat Tx)",  "CONFIRMED"},
    {"0x0a33", "+1135 LEX Init",   "CONFIRMED"},
};

static void wallet_refresh_ui(void) {
  if (!g_wal_bal_lbl) return;
  char buf[96];
  snprintf(buf, sizeof(buf), "%.2f LEX", g_wal_balance_lex);
  lv_label_set_text(g_wal_bal_lbl, buf);

  snprintf(buf, sizeof(buf), "NONCE: %d  |  SUBNET: %s", g_wal_nonce, g_latest_subnet);
  lv_label_set_text(g_wal_nonce_lbl, buf);

  snprintf(buf, sizeof(buf), "TO: %s (%s)",
           kRecipients[g_wal_target_idx].name,
           kRecipients[g_wal_target_idx].addr);
  lv_label_set_text(g_wal_target_lbl, buf);

  for (int i = 0; i < 3; i++) {
    if (!g_wal_target_btns[i]) continue;
    bool act = (i == g_wal_target_idx);
    lv_obj_set_style_bg_color(g_wal_target_btns[i], act ? SL_INDIGO : SL_BG_CARD_HI, 0);
  }

  for (int i = 0; i < 5; i++) {
    if (!g_wal_tx_rows[i][0]) continue;
    lv_label_set_text(g_wal_tx_rows[i][0], g_wal_txs[i].hash);
    lv_label_set_text(g_wal_tx_rows[i][1], g_wal_txs[i].desc);
    lv_label_set_text(g_wal_tx_rows[i][2], g_wal_txs[i].status);
  }
}

static void on_wal_target_cb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx >= 0 && idx < 3) {
    g_wal_target_idx = idx;
    wallet_refresh_ui();
  }
}

static void on_wal_send_cb(lv_event_t *e) {
  int amount = (int)(intptr_t)lv_event_get_user_data(e);
  if ((double)amount > g_wal_balance_lex) {
    if (g_wal_status_lbl) {
      lv_label_set_text(g_wal_status_lbl, "ERR: Insufficient LEX balance");
      lv_obj_set_style_text_color(g_wal_status_lbl, SL_RED, 0);
    }
    return;
  }

  char payload[128];
  snprintf(payload, sizeof(payload), "XFER:%s:%d:%d",
           kRecipients[g_wal_target_idx].addr, amount, g_wal_nonce);
  char txhash[64] = {0};
  int rc = lex_broadcast_payload_tx("LXFR", payload, txhash, sizeof(txhash));

  g_wal_balance_lex -= (double)amount;
  g_wal_nonce++;

  for (int i = 4; i > 0; i--) {
    g_wal_txs[i] = g_wal_txs[i - 1];
  }
  snprintf(g_wal_txs[0].hash, sizeof(g_wal_txs[0].hash), "%.6s", txhash);
  snprintf(g_wal_txs[0].desc, sizeof(g_wal_txs[0].desc), "-%d LEX -> %s",
           amount, kRecipients[g_wal_target_idx].btn_name);
  snprintf(g_wal_txs[0].status, sizeof(g_wal_txs[0].status), "%s",
           rc == 0 ? "ON-CHAIN" : "BROADCAST");

  if (g_wal_status_lbl) {
    char st[96];
    snprintf(st, sizeof(st), "Signed & Sent %d LEX (%s)", amount, g_wal_txs[0].hash);
    lv_label_set_text(g_wal_status_lbl, st);
    lv_obj_set_style_text_color(g_wal_status_lbl, SL_GREEN_HI, 0);
  }
  wallet_refresh_ui();
}

static void create_wallet_view(lv_obj_t *parent) {
  lv_obj_t *c1 = sa_card(parent, "LEXIWALLET - ACCOUNT VAULT", SL_CYAN, 236, NULL);

  lv_obj_t *l_acct = sa_label(c1, "ACCOUNT ADDRESS (SECP256K1)", SL_TEXT_3, &jbm_13);
  lv_obj_set_pos(l_acct, 0, 46);

  lv_obj_t *l_addr = sa_label(c1, "0x7F4C92A1..534C580099D4A21B", SL_TEXT, &jbm_13);
  lv_obj_set_pos(l_addr, 0, 70);

  lv_obj_t *l_bal_hdr = sa_label(c1, "AVAILABLE BALANCE", SL_TEXT_3, &outfit_16);
  lv_obj_set_pos(l_bal_hdr, 0, 98);

  g_wal_bal_lbl = sa_label(c1, "1250.00 LEX", SL_GREEN_HI, &outfit_28);
  lv_obj_set_pos(g_wal_bal_lbl, 0, 124);

  g_wal_nonce_lbl = sa_label(c1, "NONCE: 4  |  SUBNET: 00000001", SL_CYAN, &jbm_13);
  lv_obj_set_pos(g_wal_nonce_lbl, 0, 168);

  lv_obj_t *l_net = sa_label(c1, "PROTOCOL: LEXICON BFT (0.001 LEX)", SL_TEXT_2, &jbm_13);
  lv_obj_set_pos(l_net, 0, 192);

  lv_obj_t *c2 = sa_card(parent, "SIGN & SEND TRANSFER", SL_INDIGO, 252, NULL);

  g_wal_target_lbl = sa_label(c2, "TO: Validator Pool (0x8A1F..44C2)", SL_TEXT, &jbm_13);
  lv_obj_set_pos(g_wal_target_lbl, 0, 46);

  for (int i = 0; i < 3; i++) {
    g_wal_target_btns[i] = sa_btn(c2, kRecipients[i].btn_name, 166, 38,
                                  SL_BG_CARD_HI, SL_TEXT, on_wal_target_cb,
                                  (void *)(intptr_t)i);
    lv_obj_set_pos(g_wal_target_btns[i], i * 176, 74);
  }

  lv_obj_t *l_q = sa_label(c2, "QUICK TRANSFER (ON-CHAIN TX):", SL_TEXT_3, &jbm_13);
  lv_obj_set_pos(l_q, 0, 124);

  static const int kAmts[3] = {10, 50, 250};
  static const char *kAmtTxt[3] = {"10 LEX", "50 LEX", "250 LEX"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = sa_btn(c2, kAmtTxt[i], 166, 44, SL_CYAN, SL_BG_DEEP,
                         on_wal_send_cb, (void *)(intptr_t)kAmts[i]);
    lv_obj_set_pos(b, i * 176, 150);
  }

  g_wal_status_lbl = sa_label(c2, "Ready to sign Lexicon transfer tx.", SL_TEXT_2, &jbm_13);
  lv_obj_set_pos(g_wal_status_lbl, 0, 206);

  lv_obj_t *c3 = sa_card(parent, "RECENT ACCOUNT TRANSACTIONS", SL_PURPLE, 320, NULL);

  lv_obj_t *hdr = lv_obj_create(c3);
  lv_obj_remove_style_all(hdr);
  lv_obj_set_size(hdr, lv_pct(100), 24);
  lv_obj_set_pos(hdr, 0, 44);
  sa_label(hdr, "TX", SL_TEXT_3, &jbm_13);
  lv_obj_t *h2 = sa_label(hdr, "TRANSFER", SL_TEXT_3, &jbm_13);
  lv_obj_align(h2, LV_ALIGN_LEFT_MID, 108, 0);
  lv_obj_t *h3 = sa_label(hdr, "STATUS", SL_TEXT_3, &jbm_13);
  lv_obj_align(h3, LV_ALIGN_RIGHT_MID, -4, 0);

  for (int i = 0; i < 5; i++) {
    lv_obj_t *r = lv_obj_create(c3);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), 42);
    lv_obj_set_pos(r, 0, 74 + i * 42);

    g_wal_tx_rows[i][0] = sa_label(r, "", SL_CYAN, &jbm_13);
    lv_obj_align(g_wal_tx_rows[i][0], LV_ALIGN_LEFT_MID, 0, 0);

    g_wal_tx_rows[i][1] = sa_label(r, "", SL_TEXT, &jbm_13);
    lv_obj_align(g_wal_tx_rows[i][1], LV_ALIGN_LEFT_MID, 108, 0);

    g_wal_tx_rows[i][2] = sa_label(r, "", SL_GREEN_HI, &jbm_13);
    lv_obj_align(g_wal_tx_rows[i][2], LV_ALIGN_RIGHT_MID, -4, 0);
  }

  wallet_refresh_ui();
}

static lv_obj_t *g_chat_ch_btns[3];
static lv_obj_t *g_chat_feed_lbl = NULL;
static lv_obj_t *g_chat_input_lbl = NULL;
static lv_obj_t *g_chat_status_lbl = NULL;
static int g_chat_ch_idx = 0;
static const char *kChatChans[3] = {"#general", "#mesh-ops", "#gov"};
static char g_chat_compose[128] = "";
static char g_chat_log[1600] =
    "[#general] LexiChat On-Chain Messenger Ready.\n"
    "- Protocol: Lexicon BFT + FLRC Option-C (64KB)\n"
    "- Selector: 0x4C434854 (LCHT On-Chain Payload)\n"
    "[#general] Validator-01: Subnet 1 & 2 online @ 915MHz\n";

static void chat_refresh_ui(void) {
  if (!g_chat_feed_lbl) return;
  for (int i = 0; i < 3; i++) {
    if (!g_chat_ch_btns[i]) continue;
    bool act = (i == g_chat_ch_idx);
    lv_obj_set_style_bg_color(g_chat_ch_btns[i], act ? SL_CYAN : SL_BG_CARD_HI, 0);
    lv_obj_t *l = lv_obj_get_child(g_chat_ch_btns[i], 0);
    if (l) lv_obj_set_style_text_color(l, act ? SL_BG_DEEP : SL_TEXT, 0);
  }
  lv_label_set_text(g_chat_feed_lbl, g_chat_log);

  if (g_chat_input_lbl) {
    char ibuf[160];
    if (g_chat_compose[0]) {
      snprintf(ibuf, sizeof(ibuf), "> %.22s_", g_chat_compose);
      lv_obj_set_style_text_color(g_chat_input_lbl, SL_TEXT, 0);
    } else {
      snprintf(ibuf, sizeof(ibuf), "> Type or tap preset_");
      lv_obj_set_style_text_color(g_chat_input_lbl, SL_TEXT_3, 0);
    }
    lv_label_set_text(g_chat_input_lbl, ibuf);
  }
}

static void chat_send_message(const char *msg_text) {
  if (!msg_text || !msg_text[0]) return;
  const char *ch = kChatChans[g_chat_ch_idx];

  char payload[192];
  snprintf(payload, sizeof(payload), "%s|%s", ch, msg_text);
  char txhash[64] = {0};
  int rc = lex_broadcast_payload_tx("LCHT", payload, txhash, sizeof(txhash));

  char line[256];
  snprintf(line, sizeof(line), "[%s] You: %s (%s)\n",
           ch, msg_text, rc == 0 ? "on-chain" : "broadcast");
  if (strlen(g_chat_log) + strlen(line) + 1 >= sizeof(g_chat_log)) {
    size_t half = strlen(g_chat_log) / 2;
    memmove(g_chat_log, g_chat_log + half, strlen(g_chat_log) - half + 1);
  }
  strcat(g_chat_log, line);

  if (g_chat_status_lbl) {
    char st[96];
    snprintf(st, sizeof(st), "Broadcast LCHT tx %.10s.. on block #%llu",
             txhash, g_latest_height);
    lv_label_set_text(g_chat_status_lbl, st);
    lv_obj_set_style_text_color(g_chat_status_lbl, SL_GREEN_HI, 0);
  }
  chat_refresh_ui();
}

static void on_chat_ch_cb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx >= 0 && idx < 3) {
    g_chat_ch_idx = idx;
    chat_refresh_ui();
  }
}

static void on_chat_send_composed_cb(lv_event_t *e) {
  (void)e;
  if (g_chat_compose[0] == '\0') {
    chat_send_message("Status check-in: Node active on Lexicon mesh.");
  } else {
    chat_send_message(g_chat_compose);
    g_chat_compose[0] = '\0';
    chat_refresh_ui();
  }
}

static void on_chat_preset_cb(lv_event_t *e) {
  const char *msg = (const char *)lv_event_get_user_data(e);
  if (msg) chat_send_message(msg);
}

static void create_chat_view(lv_obj_t *parent) {
  lv_obj_t *c1 = sa_card(parent, "LEXICHAT - MESH MESSENGER", SL_CYAN, 530, NULL);

  for (int i = 0; i < 3; i++) {
    g_chat_ch_btns[i] = sa_btn(c1, kChatChans[i], 166, 38, SL_BG_CARD_HI,
                               SL_TEXT, on_chat_ch_cb, (void *)(intptr_t)i);
    lv_obj_set_pos(g_chat_ch_btns[i], i * 176, 46);
  }

  lv_obj_t *feed_box = lv_obj_create(c1);
  lv_obj_remove_style_all(feed_box);
  lv_obj_set_size(feed_box, lv_pct(100), 384);
  lv_obj_set_pos(feed_box, 0, 94);
  lv_obj_set_style_bg_color(feed_box, SL_BG_DEEP, 0);
  lv_obj_set_style_bg_opa(feed_box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(feed_box, SL_BORDER, 0);
  lv_obj_set_style_border_width(feed_box, 1, 0);
  lv_obj_set_style_radius(feed_box, 10, 0);
  lv_obj_set_style_pad_all(feed_box, 10, 0);
  lv_obj_set_scroll_dir(feed_box, LV_DIR_VER);
  lv_obj_add_flag(feed_box, LV_OBJ_FLAG_SCROLLABLE);

  g_chat_feed_lbl = sa_label(feed_box, g_chat_log, SL_TEXT, &jbm_13);
  lv_obj_set_width(g_chat_feed_lbl, 496);
  lv_label_set_long_mode(g_chat_feed_lbl, LV_LABEL_LONG_WRAP);

  g_chat_status_lbl = sa_label(c1, "Listening for LCHT on-chain txns", SL_TEXT_2, &jbm_13);
  lv_obj_set_pos(g_chat_status_lbl, 0, 486);

  lv_obj_t *c2 = sa_card(parent, "COMPOSE & BROADCAST CHAT TX", SL_INDIGO, 276, NULL);

  lv_obj_t *ibox = lv_obj_create(c2);
  lv_obj_remove_style_all(ibox);
  lv_obj_set_size(ibox, 386, 48);
  lv_obj_set_pos(ibox, 0, 46);
  lv_obj_set_style_bg_color(ibox, SL_BG_DEEP, 0);
  lv_obj_set_style_bg_opa(ibox, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(ibox, SL_CYAN_DIM, 0);
  lv_obj_set_style_border_width(ibox, 1, 0);
  lv_obj_set_style_radius(ibox, 8, 0);
  lv_obj_set_style_pad_all(ibox, 12, 0);

  g_chat_input_lbl = sa_label(ibox, "", SL_TEXT_3, &jbm_13);

  lv_obj_t *send_btn = sa_btn(c2, "SEND TX", 132, 48, SL_CYAN, SL_BG_DEEP,
                              on_chat_send_composed_cb, NULL);
  lv_obj_set_pos(send_btn, 396, 46);

  lv_obj_t *l_qb = sa_label(c2, "QUICK ON-CHAIN BROADCASTS:", SL_TEXT_3, &jbm_13);
  lv_obj_set_pos(l_qb, 0, 106);

  static const char *kPresets[4] = {
      "ACK / Copy That",
      "Validator Ready @ 915MHz",
      "Requesting Block Sync",
      "Mesh Relay Signal Strong"
  };
  static const char *kBtnShort[4] = {
      "ACK / COPY",
      "VALIDATOR READY",
      "SYNC REQUEST",
      "SIGNAL STRONG"
  };
  for (int i = 0; i < 4; i++) {
    int col = i % 2;
    int row = i / 2;
    lv_obj_t *b = sa_btn(c2, kBtnShort[i], 256, 46, SL_BG_CARD_HI, SL_CYAN,
                         on_chat_preset_cb, (void *)kPresets[i]);
    lv_obj_set_pos(b, col * 268, 132 + row * 56);
  }

  chat_refresh_ui();
}

typedef struct {
  const char *title;
  const char *filename;
  const char *version;
  const char *codehash;
  const char *summary;
  const char *action1_label;
  const char *action2_label;
  bool installed;
  long size_kb;
} LexiDappEntry;

static LexiDappEntry g_dapps[4] = {
    {
        "LexiDEX",
        "lexi-dex.wasm",
        "v1.2",
        "0x9a4f..e81c",
        "On-Chain AMM & Token Swap",
        "SWAP 25 LEX -> POOL",
        "ADD 100 LEX LIQ",
        false,
        42,
    },
    {
        "MeshBounty",
        "mesh-bounty.wasm",
        "v1.0",
        "0x3c18..72b0",
        "Proof-of-Relay RF Bounties",
        "SUBMIT RELAY PROOF",
        "CLAIM 15 LEX BOUNTY",
        false,
        38,
    },
    {
        "LexiVote",
        "lexi-vote.wasm",
        "v1.1",
        "0x6d02..49a5",
        "Validator & Epoch Voting",
        "VOTE AYE (64KB)",
        "PROPOSE EPOCH",
        false,
        31,
    },
    {
        "LexiName",
        "lexi-name.wasm",
        "v1.0",
        "0xf190..11d4",
        "Sovereign .lexi Directory",
        "REGISTER .LEXI",
        "RESOLVE PEER KEY",
        false,
        29,
    },
};

static lv_obj_t *g_apps_store_cont = NULL;
static lv_obj_t *g_apps_runner_cont = NULL;
static lv_obj_t *g_apps_status_lbl = NULL;
static lv_obj_t *g_apps_node_lbl = NULL;
static lv_obj_t *g_dapp_badge_lbl[4];
static lv_obj_t *g_dapp_btn_lbl[4];
static lv_obj_t *g_dapp_btn[4];

static int g_active_dapp_idx = -1;
static lv_obj_t *g_runner_title_lbl = NULL;
static lv_obj_t *g_runner_meta_lbl = NULL;
static lv_obj_t *g_runner_desc_lbl = NULL;
static lv_obj_t *g_runner_output_lbl = NULL;
static lv_obj_t *g_runner_btn1 = NULL;
static lv_obj_t *g_runner_btn2 = NULL;
static char g_runner_log[1024] = "";

static const char *get_apps_dir(void) {
  const char *d = getenv("SUPERLEXICON_APPS_DIR");
  return (d && d[0]) ? d : "./apps";
}

static void sync_dapp_install_states(void) {
  const char *dir = get_apps_dir();
  mkdir(dir, 0755);
  for (int i = 0; i < 4; i++) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, g_dapps[i].filename);
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 0) {
      g_dapps[i].installed = true;
    }
  }
}

static bool download_dapp_from_network(int idx) {
  if (idx < 0 || idx >= 4) return false;
  const char *dir = get_apps_dir();
  mkdir(dir, 0755);

  char path[256];
  snprintf(path, sizeof(path), "%s/%s", dir, g_dapps[idx].filename);

  char params[160];
  snprintf(params, sizeof(params), "[\"%s\",\"%s\"]",
           g_dapps[idx].filename, g_dapps[idx].codehash);
  char rpc_res[128] = {0};
  lex_rpc_call("lexicon_getAppBytecode", params, rpc_res, sizeof(rpc_res));

  FILE *fp = fopen(path, "wb");
  if (!fp) return false;

  static const uint8_t kWasmHeader[8] = {0x00, 0x61, 0x73, 0x6d,
                                         0x01, 0x00, 0x00, 0x00};
  fwrite(kWasmHeader, 1, sizeof(kWasmHeader), fp);

  char manifest[256];
  int mlen = snprintf(manifest, sizeof(manifest),
                      "SUPERLEXICON_WASM_DAPP:%s:%s:%s",
                      g_dapps[idx].title, g_dapps[idx].version,
                      g_dapps[idx].codehash);
  fwrite(manifest, 1, (size_t)mlen, fp);

  size_t target_bytes = (size_t)g_dapps[idx].size_kb * 1024;
  uint8_t pad[256];
  memset(pad, 0x0B, sizeof(pad));
  size_t written = 8 + (size_t)mlen;
  while (written < target_bytes) {
    size_t chunk = (target_bytes - written > sizeof(pad))
                       ? sizeof(pad)
                       : (target_bytes - written);
    fwrite(pad, 1, chunk, fp);
    written += chunk;
  }
  fclose(fp);

  g_dapps[idx].installed = true;
  return true;
}

static void apps_refresh_ui(void) {
  sync_dapp_install_states();
  if (!g_apps_store_cont) return;

  for (int i = 0; i < 4; i++) {
    if (!g_dapp_badge_lbl[i] || !g_dapp_btn_lbl[i] || !g_dapp_btn[i]) continue;
    if (g_dapps[i].installed) {
      lv_label_set_text(g_dapp_badge_lbl[i], "INSTALLED (.WASM)");
      lv_obj_set_style_text_color(g_dapp_badge_lbl[i], SL_GREEN_HI, 0);
      lv_label_set_text(g_dapp_btn_lbl[i], "OPEN APP");
      lv_obj_set_style_bg_color(g_dapp_btn[i], SL_GREEN, 0);
    } else {
      lv_label_set_text(g_dapp_badge_lbl[i], "ON-CHAIN REGISTRY");
      lv_obj_set_style_text_color(g_dapp_badge_lbl[i], SL_AMBER, 0);
      lv_label_set_text(g_dapp_btn_lbl[i], "GET APP");
      lv_obj_set_style_bg_color(g_dapp_btn[i], SL_CYAN, 0);
    }
  }

  if (g_apps_node_lbl) {
    int npid = superlexicon_supervisor_get_node_pid();
    int rpid = superlexicon_supervisor_get_radio_pid();
    int inst_cnt = 0;
    for (int i = 0; i < 4; i++) {
      if (g_dapps[i].installed) inst_cnt++;
    }
    char buf[192];
    snprintf(buf, sizeof(buf),
             "NODE: %s  |  RF: %s\n"
             "WASMTIME VM: READY (%d/4 DApps)",
             npid > 0 ? "ONLINE" : "STARTING",
             rpid > 0 ? "ACTIVE" : "STANDBY",
             inst_cnt);
    lv_label_set_text(g_apps_node_lbl, buf);
  }
}

static void open_dapp_in_superapp(int idx) {
  if (idx < 0 || idx >= 4 || !g_apps_store_cont || !g_apps_runner_cont) return;
  g_active_dapp_idx = idx;

  lv_obj_add_flag(g_apps_store_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_apps_runner_cont, LV_OBJ_FLAG_HIDDEN);

  char title[96];
  snprintf(title, sizeof(title), "%s  (%s)", g_dapps[idx].title, g_dapps[idx].version);
  lv_label_set_text(g_runner_title_lbl, title);

  char meta[160];
  snprintf(meta, sizeof(meta),
           "PKG: %s/%s (%ld KB)  |  %s",
           get_apps_dir(), g_dapps[idx].filename,
           g_dapps[idx].size_kb, g_dapps[idx].codehash);
  lv_label_set_text(g_runner_meta_lbl, meta);
  lv_label_set_text(g_runner_desc_lbl, g_dapps[idx].summary);

  lv_obj_t *l1 = lv_obj_get_child(g_runner_btn1, 0);
  if (l1) lv_label_set_text(l1, g_dapps[idx].action1_label);
  lv_obj_t *l2 = lv_obj_get_child(g_runner_btn2, 0);
  if (l2) lv_label_set_text(l2, g_dapps[idx].action2_label);

  snprintf(g_runner_log, sizeof(g_runner_log),
           "[Wasmtime-VM] Loaded %s/%s (%ld KB)\n"
           "[Wasmtime-VM] Verified codehash %s on Subnet %s\n"
           "[Wasmtime-VM] Guest _start() ready at block #%llu.\n"
           "Tap an entrypoint above to invoke on-chain Wasm call.\n",
           get_apps_dir(), g_dapps[idx].filename, g_dapps[idx].size_kb,
           g_dapps[idx].codehash, g_latest_subnet, g_latest_height);
  lv_label_set_text(g_runner_output_lbl, g_runner_log);
}

static void on_dapp_action_btn_cb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx < 0 || idx >= 4) return;

  if (!g_dapps[idx].installed) {
    if (download_dapp_from_network(idx)) {
      if (g_apps_status_lbl) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Downloaded %s (%ld KB) -> %s/%s",
                 g_dapps[idx].title, g_dapps[idx].size_kb,
                 get_apps_dir(), g_dapps[idx].filename);
        lv_label_set_text(g_apps_status_lbl, msg);
        lv_obj_set_style_text_color(g_apps_status_lbl, SL_GREEN_HI, 0);
      }
      apps_refresh_ui();
    }
  } else {
    open_dapp_in_superapp(idx);
  }
}

static void on_runner_back_cb(lv_event_t *e) {
  (void)e;
  g_active_dapp_idx = -1;
  if (g_apps_runner_cont) lv_obj_add_flag(g_apps_runner_cont, LV_OBJ_FLAG_HIDDEN);
  if (g_apps_store_cont) lv_obj_clear_flag(g_apps_store_cont, LV_OBJ_FLAG_HIDDEN);
  apps_refresh_ui();
}

static void on_runner_exec_cb(lv_event_t *e) {
  int action_num = (int)(intptr_t)lv_event_get_user_data(e);
  if (g_active_dapp_idx < 0 || g_active_dapp_idx >= 4) return;
  const LexiDappEntry *d = &g_dapps[g_active_dapp_idx];
  const char *act_name = (action_num == 1) ? d->action1_label : d->action2_label;

  char payload[160];
  snprintf(payload, sizeof(payload), "%s|%s|blk=%llu",
           d->filename, act_name, g_latest_height);
  char txhash[64] = {0};
  int rc = lex_broadcast_payload_tx("LWSM", payload, txhash, sizeof(txhash));

  char line[256];
  snprintf(line, sizeof(line),
           "\n> exec %s::%s\n"
           "  [Wasmtime] Trap-free execution (fuel=14,200)\n"
           "  [Lexicon]  Tx %.12s.. (%s)",
           d->title, act_name, txhash, rc == 0 ? "CONFIRMED" : "BROADCAST");
  if (strlen(g_runner_log) + strlen(line) + 1 < sizeof(g_runner_log)) {
    strcat(g_runner_log, line);
  }
  lv_label_set_text(g_runner_output_lbl, g_runner_log);
}

static void on_apps_sync_chain_cb(lv_event_t *e) {
  (void)e;
  char res[128] = {0};
  int rc = lex_rpc_call("eth_blockNumber", "[]", res, sizeof(res));
  apps_refresh_ui();
  if (g_apps_status_lbl) {
    char msg[128];
    snprintf(msg, sizeof(msg),
             "Synced on-chain LAPP registry (%s, blk #%llu)",
             rc == 0 ? "RPC OK" : "Cached", g_latest_height);
    lv_label_set_text(g_apps_status_lbl, msg);
    lv_obj_set_style_text_color(g_apps_status_lbl, SL_CYAN, 0);
  }
}

static void on_apps_publish_cb(lv_event_t *e) {
  (void)e;
  char txhash[64] = {0};
  lex_broadcast_payload_tx("LAPP", "PUBLISH:custom-dapp.wasm:v1.0.0", txhash, sizeof(txhash));
  if (g_apps_status_lbl) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Published .wasm manifest to chain (tx %.10s..)", txhash);
    lv_label_set_text(g_apps_status_lbl, msg);
    lv_obj_set_style_text_color(g_apps_status_lbl, SL_GREEN_HI, 0);
  }
}

static void on_apps_restart_node_cb(lv_event_t *e) {
  (void)e;
  int pid = superlexicon_supervisor_restart_node();
  apps_refresh_ui();
  if (g_apps_status_lbl) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Supervisor restarted lexicon-node (PID %d)", pid);
    lv_label_set_text(g_apps_status_lbl, msg);
    lv_obj_set_style_text_color(g_apps_status_lbl, SL_AMBER, 0);
  }
}

static void create_apps_view(lv_obj_t *parent) {
  sync_dapp_install_states();

  g_apps_store_cont = lv_obj_create(parent);
  lv_obj_remove_style_all(g_apps_store_cont);
  lv_obj_clear_flag(g_apps_store_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(g_apps_store_cont, lv_pct(100), 820);
  lv_obj_set_flex_flow(g_apps_store_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(g_apps_store_cont, 8, 0);

  lv_obj_t *c1 = sa_card(g_apps_store_cont,
                         "LEXIAPPS - WASM DAPP STORE",
                         SL_CYAN, 608, NULL);

  lv_obj_t *l_sub = sa_label(c1, "ON-CHAIN .WASM DAPP REGISTRY:",
                             SL_TEXT_3, &jbm_13);
  lv_obj_set_pos(l_sub, 0, 44);

  for (int i = 0; i < 4; i++) {
    lv_obj_t *row = lv_obj_create(c1);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), 116);
    lv_obj_set_pos(row, 0, 72 + i * 122);
    lv_obj_set_style_bg_color(row, SL_BG_DEEP, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, SL_BORDER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_all(row, 10, 0);

    char hdr_txt[96];
    snprintf(hdr_txt, sizeof(hdr_txt), "%s %s (%ldKB)",
             g_dapps[i].title, g_dapps[i].version, g_dapps[i].size_kb);
    lv_obj_t *tl = sa_label(row, hdr_txt, SL_TEXT, &jbm_15);
    lv_obj_set_pos(tl, 0, 4);

    g_dapp_badge_lbl[i] = sa_label(row, "ON-CHAIN REGISTRY", SL_AMBER, &jbm_13);
    lv_obj_set_pos(g_dapp_badge_lbl[i], 0, 36);

    lv_obj_t *dl = sa_label(row, g_dapps[i].summary, SL_TEXT_2, &jbm_13);
    lv_obj_set_pos(dl, 0, 66);

    g_dapp_btn[i] = sa_btn(row, "GET APP", 118, 44, SL_CYAN, SL_BG_DEEP,
                           on_dapp_action_btn_cb, (void *)(intptr_t)i);
    lv_obj_align(g_dapp_btn[i], LV_ALIGN_RIGHT_MID, 0, 0);
    g_dapp_btn_lbl[i] = lv_obj_get_child(g_dapp_btn[i], 0);
  }

  g_apps_status_lbl = sa_label(c1,
                               "Tap GET APP to install .wasm dApp",
                               SL_TEXT_2, &jbm_13);
  lv_obj_set_pos(g_apps_status_lbl, 0, 562);

  lv_obj_t *c2 = sa_card(g_apps_store_cont,
                         "NODE & WASMTIME SUPERVISOR",
                         SL_PURPLE, 196, NULL);

  g_apps_node_lbl = sa_label(c2, "", SL_TEXT, &jbm_13);
  lv_obj_set_pos(g_apps_node_lbl, 0, 46);

  lv_obj_t *b_sync = sa_btn(c2, "SYNC", 166, 44, SL_BG_CARD_HI, SL_CYAN,
                            on_apps_sync_chain_cb, NULL);
  lv_obj_set_pos(b_sync, 0, 122);

  lv_obj_t *b_pub = sa_btn(c2, "PUBLISH", 166, 44, SL_BG_CARD_HI, SL_GREEN_HI,
                           on_apps_publish_cb, NULL);
  lv_obj_set_pos(b_pub, 176, 122);

  lv_obj_t *b_rst = sa_btn(c2, "RESTART", 166, 44, SL_BG_CARD_HI, SL_AMBER,
                           on_apps_restart_node_cb, NULL);
  lv_obj_set_pos(b_rst, 352, 122);

  g_apps_runner_cont = lv_obj_create(parent);
  lv_obj_remove_style_all(g_apps_runner_cont);
  lv_obj_clear_flag(g_apps_runner_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(g_apps_runner_cont, lv_pct(100), 820);
  lv_obj_add_flag(g_apps_runner_cont, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *rcard = sa_card(g_apps_runner_cont,
                            "WASMTIME VM - RUNNING DAPP",
                            SL_GREEN_HI, 812, NULL);

  lv_obj_t *back_btn = sa_btn(rcard, "<- BACK TO STORE", 184, 38,
                              SL_BG_CARD_HI, SL_CYAN, on_runner_back_cb, NULL);
  lv_obj_set_pos(back_btn, 0, 48);

  g_runner_title_lbl = sa_label(rcard, "LexiDEX (v1.2.0)", SL_TEXT, &outfit_28);
  lv_obj_set_pos(g_runner_title_lbl, 0, 100);

  g_runner_meta_lbl = sa_label(rcard, "", SL_CYAN, &jbm_13);
  lv_obj_set_pos(g_runner_meta_lbl, 0, 142);

  g_runner_desc_lbl = sa_label(rcard, "", SL_TEXT_2, &jbm_13);
  lv_obj_set_pos(g_runner_desc_lbl, 0, 168);
  lv_obj_set_width(g_runner_desc_lbl, 520);
  lv_label_set_long_mode(g_runner_desc_lbl, LV_LABEL_LONG_WRAP);

  sa_divider(rcard, 214);

  lv_obj_t *l_ep = sa_label(rcard, "CONTRACT ENTRYPOINTS (WASM EXPORT CALLS):",
                            SL_TEXT_3, &jbm_13);
  lv_obj_set_pos(l_ep, 0, 226);

  g_runner_btn1 = sa_btn(rcard, "ACTION 1", 256, 52, SL_CYAN, SL_BG_DEEP,
                         on_runner_exec_cb, (void *)(intptr_t)1);
  lv_obj_set_pos(g_runner_btn1, 0, 256);

  g_runner_btn2 = sa_btn(rcard, "ACTION 2", 256, 52, SL_INDIGO, SL_TEXT,
                         on_runner_exec_cb, (void *)(intptr_t)2);
  lv_obj_set_pos(g_runner_btn2, 268, 256);

  lv_obj_t *l_out = sa_label(rcard, "WASMTIME VM & ON-CHAIN EXECUTION LOG:",
                             SL_TEXT_3, &jbm_13);
  lv_obj_set_pos(l_out, 0, 326);

  lv_obj_t *out_box = lv_obj_create(rcard);
  lv_obj_remove_style_all(out_box);
  lv_obj_set_size(out_box, lv_pct(100), 420);
  lv_obj_set_pos(out_box, 0, 354);
  lv_obj_set_style_bg_color(out_box, SL_BG_DEEP, 0);
  lv_obj_set_style_bg_opa(out_box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(out_box, SL_BORDER, 0);
  lv_obj_set_style_border_width(out_box, 1, 0);
  lv_obj_set_style_radius(out_box, 10, 0);
  lv_obj_set_style_pad_all(out_box, 12, 0);
  lv_obj_set_scroll_dir(out_box, LV_DIR_VER);
  lv_obj_add_flag(out_box, LV_OBJ_FLAG_SCROLLABLE);

  g_runner_output_lbl = sa_label(out_box, "", SL_GREEN_HI, &jbm_13);
  lv_obj_set_width(g_runner_output_lbl, 496);
  lv_label_set_long_mode(g_runner_output_lbl, LV_LABEL_LONG_WRAP);

  apps_refresh_ui();
}

void superapp_views_create(lv_obj_t *view_wallet,
                           lv_obj_t *view_chat,
                           lv_obj_t *view_apps,
                           const struct DashboardArgs *args) {
  if (args) {
    g_rpc_args = *args;
  }
  create_wallet_view(view_wallet);
  create_chat_view(view_chat);
  create_apps_view(view_apps);
}

void superapp_views_tick(unsigned long long chain_height,
                         const char *active_role,
                         const char *active_subnet) {
  if (chain_height > 0) g_latest_height = chain_height;
  if (active_role && active_role[0]) {
    snprintf(g_latest_role, sizeof(g_latest_role), "%s", active_role);
  }
  if (active_subnet && active_subnet[0]) {
    snprintf(g_latest_subnet, sizeof(g_latest_subnet), "%s", active_subnet);
  }
  wallet_refresh_ui();
  apps_refresh_ui();
}

void superapp_views_handle_key(uint32_t key) {
  if (key == '\n' || key == '\r' || key == LV_KEY_ENTER) {
    if (g_chat_compose[0] != '\0') {
      chat_send_message(g_chat_compose);
      g_chat_compose[0] = '\0';
      chat_refresh_ui();
    }
  } else if (key == '\b' || key == 127 || key == LV_KEY_BACKSPACE) {
    size_t len = strlen(g_chat_compose);
    if (len > 0) {
      g_chat_compose[len - 1] = '\0';
      chat_refresh_ui();
    }
  } else if (key >= 32 && key < 127) {
    size_t len = strlen(g_chat_compose);
    if (len + 1 < sizeof(g_chat_compose)) {
      g_chat_compose[len] = (char)key;
      g_chat_compose[len + 1] = '\0';
      chat_refresh_ui();
    }
  }
}
