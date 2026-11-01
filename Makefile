CC      ?= cc
CXX     ?= c++
AR      ?= ar

GCC_FIX := $(shell if [ -f /usr/lib/gcc/x86_64-pc-linux-gnu/16/cc1 ]; then echo "-B/usr/lib/gcc/x86_64-pc-linux-gnu/16/"; fi)

SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL2_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null || echo "-lSDL2")

WARN_FLAGS := -Wno-multichar -Wno-deprecated-declarations -Wno-unused-result \
              -Wno-unused-variable -Wno-format -Wno-return-type -Wno-sign-compare \
              -Wno-unused-label

INCLUDES := -Ishell \
            -Ishell/vendor \
            -Ishell/vendor/lvgl \
            -Ishell/vendor/dashboard \
            -Ishell/vendor/cjson \
            $(SDL2_CFLAGS)

CFLAGS   := $(GCC_FIX) -O2 -std=gnu11 $(WARN_FLAGS) $(INCLUDES)
CXXFLAGS := $(GCC_FIX) -O2 -std=gnu++20 $(WARN_FLAGS) $(INCLUDES)
LDFLAGS  := $(GCC_FIX) $(SDL2_LIBS) -lpthread -lm

BUILD_DIR := build
BIN_DIR   := bin

LVGL_CSRCS := $(shell find shell/vendor/lvgl/src -type f -name '*.c')
LVGL_OBJS  := $(patsubst %.c,$(BUILD_DIR)/%.o,$(LVGL_CSRCS))
LVGL_LIB   := $(BUILD_DIR)/liblvgl.a

APP_CSRCS := \
    shell/vendor/dashboard/ui/dashboard.c \
    shell/vendor/dashboard/ui/superapp_views.c \
    shell/vendor/dashboard/ui/logo_horizontal.c \
    shell/vendor/dashboard/ui/app_icon.c \
    shell/vendor/dashboard/ws.c \
    shell/vendor/dashboard/fonts/outfit_28.c \
    shell/vendor/dashboard/fonts/outfit_16.c \
    shell/vendor/dashboard/fonts/jbm_13.c \
    shell/vendor/dashboard/fonts/jbm_15.c \
    shell/vendor/cjson/cJSON.c

APP_CXXSRCS := \
    shell/main.cpp \
    shell/supervisor.cpp

APP_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_CSRCS)) \
            $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(APP_CXXSRCS))

.PHONY: all clean run headless-test

all: $(BIN_DIR)/lexicon-radio-bridge $(BIN_DIR)/superlexicon $(BIN_DIR)/lexicon-node

$(LVGL_LIB): $(LVGL_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(BIN_DIR)/superlexicon: $(APP_OBJS) $(LVGL_LIB)
	@mkdir -p $(BIN_DIR)
	$(CXX) -o $@ $(APP_OBJS) $(LVGL_LIB) $(LDFLAGS)

$(BIN_DIR)/lexicon-radio-bridge: radio-bridge/lexicon_radio_bridge.cpp
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< -lpthread

$(BIN_DIR)/lexicon-node:
	@mkdir -p $(BIN_DIR)
	@if [ -f ../lexicon/target/release/lexicon-node ]; then \
		cp ../lexicon/target/release/lexicon-node $(BIN_DIR)/lexicon-node && \
		chmod +x $(BIN_DIR)/lexicon-node; \
	elif [ -f ../lexicon/target/release/superlexicon-app ]; then \
		cp ../lexicon/target/release/superlexicon-app $(BIN_DIR)/lexicon-node && \
		chmod +x $(BIN_DIR)/lexicon-node; \
	fi

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	./$(BIN_DIR)/superlexicon

headless-test: all
	./$(BIN_DIR)/superlexicon --tab lexicon --headless 45 --screenshot $(BUILD_DIR)/tab_lexicon.bmp
	./$(BIN_DIR)/superlexicon --no-daemons --tab wallet --headless 45 --screenshot $(BUILD_DIR)/tab_lexiwallet.bmp
	./$(BIN_DIR)/superlexicon --no-daemons --tab chat --headless 45 --screenshot $(BUILD_DIR)/tab_lexichat.bmp
	./$(BIN_DIR)/superlexicon --no-daemons --tab apps --headless 45 --screenshot $(BUILD_DIR)/tab_lexiapps.bmp

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)/superlexicon $(BIN_DIR)/lexicon-radio-bridge
