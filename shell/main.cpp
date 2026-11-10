#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "lvgl/lvgl.h"
#include "ui/dashboard.h"
#include "ui/superapp_views.h"
#include "supervisor.hpp"

static const int kScreenW = 568;
static const int kScreenH = 1232;

static uint32_t g_framebuf[kScreenW * kScreenH];
static SDL_Window *g_window = nullptr;
static SDL_Renderer *g_renderer = nullptr;
static SDL_Texture *g_texture = nullptr;

static volatile bool g_quit = false;
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static bool g_mouse_pressed = false;

static void on_signal(int sig) {
    (void)sig;
    g_quit = true;
}

static void sdl_disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    int32_t x1 = area->x1 < 0 ? 0 : area->x1;
    int32_t y1 = area->y1 < 0 ? 0 : area->y1;
    int32_t x2 = area->x2 >= kScreenW ? kScreenW - 1 : area->x2;
    int32_t y2 = area->y2 >= kScreenH ? kScreenH - 1 : area->y2;
    int32_t w = x2 - x1 + 1;

    for (int32_t y = y1; y <= y2; y++) {
        memcpy(&g_framebuf[y * kScreenW + x1], color_p, (size_t)w * sizeof(uint32_t));
        color_p += w;
    }

    if (lv_disp_flush_is_last(disp_drv) && g_renderer && g_texture) {
        SDL_UpdateTexture(g_texture, NULL, g_framebuf, kScreenW * (int)sizeof(uint32_t));
        SDL_RenderClear(g_renderer);
        SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
        SDL_RenderPresent(g_renderer);
    }

    lv_disp_flush_ready(disp_drv);
}

static void sdl_pointer_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    (void)indev_drv;
    data->point.x = (lv_coord_t)g_mouse_x;
    data->point.y = (lv_coord_t)g_mouse_y;
    data->state = g_mouse_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static int write_bmp_screenshot(const char *path, const uint32_t *fb, int w, int h) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    uint32_t row_stride = (uint32_t)(w * 3);
    uint32_t img_size = row_stride * (uint32_t)h;
    uint32_t file_size = 54 + img_size;

    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    memcpy(&hdr[2], &file_size, 4);
    uint32_t data_off = 54;
    memcpy(&hdr[10], &data_off, 4);
    uint32_t dib_size = 40;
    memcpy(&hdr[14], &dib_size, 4);
    int32_t bw = w;
    int32_t bh = -h;
    memcpy(&hdr[18], &bw, 4);
    memcpy(&hdr[22], &bh, 4);
    uint16_t planes = 1;
    uint16_t bpp = 24;
    memcpy(&hdr[26], &planes, 2);
    memcpy(&hdr[28], &bpp, 2);
    memcpy(&hdr[34], &img_size, 4);
    fwrite(hdr, 1, sizeof(hdr), fp);

    uint8_t *row = (uint8_t *)malloc(row_stride);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t px = fb[y * w + x];
            row[x * 3 + 0] = (uint8_t)(px & 0xFF);
            row[x * 3 + 1] = (uint8_t)((px >> 8) & 0xFF);
            row[x * 3 + 2] = (uint8_t)((px >> 16) & 0xFF);
        }
        fwrite(row, 1, row_stride, fp);
    }
    free(row);
    fclose(fp);
    return 0;
}

static void poll_sdl_events(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                g_quit = true;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    g_mouse_x = ev.button.x;
                    g_mouse_y = ev.button.y;
                    g_mouse_pressed = true;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    g_mouse_x = ev.button.x;
                    g_mouse_y = ev.button.y;
                    g_mouse_pressed = false;
                }
                break;
            case SDL_MOUSEMOTION:
                g_mouse_x = ev.motion.x;
                g_mouse_y = ev.motion.y;
                break;
            case SDL_TEXTINPUT:
                for (int i = 0; ev.text.text[i] != '\0'; i++) {
                    uint8_t ch = (uint8_t)ev.text.text[i];
                    if (ch >= 32 && ch < 127) {
                        superapp_views_handle_key((uint32_t)ch);
                    }
                }
                break;
            case SDL_KEYDOWN: {
                SDL_Keycode sym = ev.key.keysym.sym;
                if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
                    superapp_views_handle_key('\n');
                } else if (sym == SDLK_BACKSPACE) {
                    superapp_views_handle_key('\b');
                } else if (sym == SDLK_F1) {
                    dashboard_select_tab(0);
                } else if (sym == SDLK_F2) {
                    dashboard_select_tab(1);
                } else if (sym == SDLK_F3) {
                    dashboard_select_tab(2);
                } else if (sym == SDLK_F4) {
                    dashboard_select_tab(3);
                }
                break;
            }
            default:
                break;
        }
    }
}

static int parse_tab_name(const char *name) {
    if (!name || !name[0]) return 0;
    if (strcasecmp(name, "lexicon") == 0 || strcasecmp(name, "chain") == 0 || strcmp(name, "0") == 0) return 0;
    if (strcasecmp(name, "wallet") == 0 || strcasecmp(name, "lexiwallet") == 0 || strcmp(name, "1") == 0) return 1;
    if (strcasecmp(name, "chat") == 0 || strcasecmp(name, "lexichat") == 0 || strcmp(name, "2") == 0) return 2;
    if (strcasecmp(name, "apps") == 0 || strcasecmp(name, "lexiapps") == 0 || strcmp(name, "3") == 0) return 3;
    return 0;
}

int main(int argc, char **argv) {
    bool headless = false;
    int headless_ticks = 45;
    const char *screenshot_path = nullptr;
    const char *initial_tab = nullptr;
    bool spawn_daemons = false;
    double user_scale = 0.0;
    const char *rpc_host = getenv("LEXICON_RPC_HOST") ? getenv("LEXICON_RPC_HOST") : "127.0.0.1";
    int rpc_port = getenv("LEXICON_RPC_PORT") ? atoi(getenv("LEXICON_RPC_PORT")) : 8545;
    int ws_port  = getenv("LEXICON_WS_PORT")  ? atoi(getenv("LEXICON_WS_PORT"))  : 8566;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) {
            headless = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                headless_ticks = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if ((strcmp(argv[i], "--tab") == 0 || strcmp(argv[i], "--open-app") == 0) && i + 1 < argc) {
            initial_tab = argv[++i];
        } else if (strcmp(argv[i], "--no-daemons") == 0) {
            spawn_daemons = false;
        } else if (strcmp(argv[i], "--daemons") == 0) {
            spawn_daemons = true;
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            user_scale = atof(argv[++i]);
        } else if (strcmp(argv[i], "--rpc-host") == 0 && i + 1 < argc) {
            rpc_host = argv[++i];
        } else if (strcmp(argv[i], "--rpc-port") == 0 && i + 1 < argc) {
            rpc_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ws-port") == 0 && i + 1 < argc) {
            ws_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options]\n"
                   "  --tab <lexicon|wallet|chat|apps>  Select initial view\n"
                   "  --scale <0.5..1.5>                Window scale factor\n"
                   "  --no-daemons                      Do not spawn background services\n"
                   "  --rpc-host / --rpc-port / --ws-port\n"
                   "  --headless [ticks]                Run offscreen\n"
                   "  --screenshot <path.bmp>           Save 568x1232 BMP screenshot\n",
                   argv[0]);
            return 0;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    superlexicon_supervisor_init(spawn_daemons, argv[0]);

    if (headless) {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        headless = true;
    }

    if (!headless) {
        double scale = user_scale;
        if (scale <= 0.1) {
            SDL_DisplayMode dm;
            if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.h > 0) {
                double max_h = (double)dm.h * 0.86;
                scale = max_h / (double)kScreenH;
                if (scale > 1.0) scale = 1.0;
                if (scale < 0.55) scale = 0.55;
            } else {
                scale = 0.78;
            }
        }
        int win_w = (int)(kScreenW * scale);
        int win_h = (int)(kScreenH * scale);

        g_window = SDL_CreateWindow(
            "SuperLexicon",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            win_w, win_h,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (g_window) {
            g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!g_renderer) {
                g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
            }
            if (g_renderer) {
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
                SDL_RenderSetLogicalSize(g_renderer, kScreenW, kScreenH);
                g_texture = SDL_CreateTexture(
                    g_renderer, SDL_PIXELFORMAT_ARGB8888,
                    SDL_TEXTUREACCESS_STREAMING, kScreenW, kScreenH);
            }
            SDL_StartTextInput();
        }
    }

    lv_init();

    static lv_disp_draw_buf_t s_draw_buf;
    static lv_color_t s_buf1[kScreenW * 120];
    static lv_color_t s_buf2[kScreenW * 120];
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, kScreenW * 120);

    static lv_disp_drv_t s_disp_drv;
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = kScreenW;
    s_disp_drv.ver_res = kScreenH;
    s_disp_drv.flush_cb = sdl_disp_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    static lv_indev_drv_t s_pointer_drv;
    lv_indev_drv_init(&s_pointer_drv);
    s_pointer_drv.type = LV_INDEV_TYPE_POINTER;
    s_pointer_drv.read_cb = sdl_pointer_read_cb;
    lv_indev_drv_register(&s_pointer_drv);

    struct DashboardArgs args = {
        .rpc_host = rpc_host,
        .rpc_port = rpc_port,
        .rpc_ws_port = ws_port,
    };
    dashboard_create(lv_scr_act(), &args);
    dashboard_start_timer(&args);

    if (initial_tab) {
        dashboard_select_tab(parse_tab_name(initial_tab));
    }

    int tick_count = 0;
    while (!g_quit) {
        if (!headless) {
            poll_sdl_events();
        }
        superlexicon_supervisor_poll();

        lv_tick_inc(16);
        lv_task_handler();

        if (headless) {
            tick_count++;
            if (tick_count >= headless_ticks) {
                break;
            }
            usleep(5000);
        } else {
            usleep(12000);
        }
    }

    if (screenshot_path && screenshot_path[0]) {
        write_bmp_screenshot(screenshot_path, g_framebuf, kScreenW, kScreenH);
    }

    dashboard_stop_timer();
    superlexicon_supervisor_shutdown_all();

    if (g_texture) SDL_DestroyTexture(g_texture);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
