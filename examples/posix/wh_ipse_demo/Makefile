# Makefile for the wolfHSM Intel IPSE demo (host client + simulated engine
# server over the Intel MEI transport)

BIN_SERVER = wh_ipse_server
BIN_CLIENT = wh_ipse_client

PROJECT_DIR  ?= .
WOLFSSL_DIR  ?= ../../../../wolfssl
WOLFHSM_DIR  ?= ../../../
BUILD_DIR    ?= $(PROJECT_DIR)/Build

INC = -I$(PROJECT_DIR) \
      -I$(WOLFSSL_DIR) \
      -I$(WOLFHSM_DIR)

DEF += -D_POSIX_C_SOURCE=200809L
DEF += -DWOLFSSL_USER_SETTINGS -DWOLFHSM_CFG

ARCHFLAGS ?=
CFLAGS_EXTRA = -Wall -Wextra -Werror -ffunction-sections -fdata-sections
CSTD    ?= -std=c99
CFLAGS  ?= $(ARCHFLAGS) $(CSTD) $(CFLAGS_EXTRA)
LDFLAGS ?= $(ARCHFLAGS) -Wl,--gc-sections
LIBS    = -lpthread

ifeq ($(DEBUG),1)
    CFLAGS  += -ggdb -g3
    LDFLAGS += -ggdb -g3
endif

CMD_ECHO ?=

## Source files
# wolfCrypt and wolfSSL (compiled in directly, wolfHSM style)
SRC_C += $(wildcard $(WOLFSSL_DIR)/wolfcrypt/src/*.c)
SRC_C += $(wildcard $(WOLFSSL_DIR)/src/*.c)

# wolfHSM core
SRC_C += $(wildcard $(WOLFHSM_DIR)/src/*.c)

# Intel MEI transport and IPSE server skeleton
SRC_C += $(wildcard $(WOLFHSM_DIR)/port/intel/mei/*.c)
SRC_C += $(wildcard $(WOLFHSM_DIR)/port/intel/ipse/*.c)

# POSIX time source
SRC_C += $(WOLFHSM_DIR)/port/posix/posix_time.c

FILENAMES_C = $(notdir $(SRC_C))
OBJS_C = $(addprefix $(BUILD_DIR)/, $(FILENAMES_C:.c=.o))
vpath %.c $(dir $(SRC_C)) $(PROJECT_DIR)

.PHONY: all clean run

all: $(BUILD_DIR) $(BUILD_DIR)/$(BIN_SERVER) $(BUILD_DIR)/$(BIN_CLIENT)
	@echo Build complete.

$(BUILD_DIR):
	$(CMD_ECHO) mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c
	@echo "Compiling C file: $(notdir $<)"
	$(CMD_ECHO) $(CC) $(CFLAGS) $(DEF) $(INC) -c -o $@ $<

$(BUILD_DIR)/$(BIN_SERVER): $(OBJS_C) $(BUILD_DIR)/wh_ipse_server.o
	@echo "Linking: $(notdir $@)"
	$(CMD_ECHO) $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(BUILD_DIR)/$(BIN_CLIENT): $(OBJS_C) $(BUILD_DIR)/wh_ipse_client.o
	@echo "Linking: $(notdir $@)"
	$(CMD_ECHO) $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

# Start the engine server in the background, run the client, stop the server
run: all
	$(BUILD_DIR)/$(BIN_SERVER) & \
	SERVER_PID=$$!; \
	sleep 1; \
	$(BUILD_DIR)/$(BIN_CLIENT); \
	CLIENT_RC=$$?; \
	kill $$SERVER_PID 2>/dev/null; \
	wait $$SERVER_PID 2>/dev/null; \
	exit $$CLIENT_RC

clean:
	rm -rf $(BUILD_DIR)
