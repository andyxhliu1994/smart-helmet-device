CMD_DBG=
RK_SDK_ROOT ?=
RK_MEDIA_CROSS ?= $(RK_SDK_ROOT)/buildroot/output/rockchip_rv1126b/host/bin/aarch64-buildroot-linux-gnu
RK_MEDIA_OUTPUT ?= $(RK_SDK_ROOT)/buildroot/output/rockchip_rv1126b/host/aarch64-buildroot-linux-gnu/sysroot/usr
RK_MEDIA_CHIP=rv1126b
ARCH=arm64

RK_MEDIA_OPTS += -Wl,-rpath-link,${RK_MEDIA_OUTPUT}/lib:$(RK_MEDIA_OUTPUT)/root/usr/lib
PKG_CONF_OPTS += -DRKPLATFORM=ON
PKG_CONF_OPTS += -DARCH64=ON

SIMPLE_CC := $(RK_MEDIA_CROSS)-g++
SIMPLE_AR := $(RK_MEDIA_CROSS)-ar
SIMPLE_STRIP := $(RK_MEDIA_CROSS)-strip

CURRENT_DIR := $(shell pwd)
SIMPLE_PKG_CONF_OPTS += -DRKPLATFORM=ON
SIMPLE_LD_FLAGS += -DRV1126B

SIMPLE_OPTS += -Wl,-rpath-link,${RK_MEDIA_OUTPUT}/lib
INC_FLAGS += -I$(CURRENT_DIR)/include
INC_FLAGS += -I$(CURRENT_DIR)/include/websocket
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rknn
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rga
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/uAPI2
SIMPLE_LD_FLAGS += -DUAPI2
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/common
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/xcore
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/algos
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/iq_parser
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/iq_parser_v2
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/rkaiq/smartIr
INC_FLAGS += -I$(RK_MEDIA_OUTPUT)/include/freetype2

SIMPLE_CFLAGS += -g -Wall $(INC_FLAGS) $(SIMPLE_PKG_CONF_OPTS) $(RK_MEDIA_CROSS_CFLAGS)

SIMPLE_LD_FLAGS += $(SIMPLE_OPTS) -L$(CURRENT_DIR)/lib -L$(RK_MEDIA_OUTPUT)/lib -L$(RK_MEDIA_OUTPUT)/root/usr/lib \
					-lpthread -lm -lrockit -lrkaiq -lrkaudio -lrkdemuxer -lrkmuxer -lrtsp -lrockiva -lrknnrt \
					-lrockchip_mpp -DRKAIQ \
					-lrga -lfreetype -lnanomsg -lcurl -lmbedcrypto -lmbedtls -lmbedx509 -lwebsockets
SIMPLE_LD_FLAGS += -Wl,--gc-sections -Wl,--as-needed

SIMPLE_CFLAGS += -Wl,-rpath-link,${RK_MEDIA_OUTPUT}/lib -Wl,-rpath-link,$(RK_MEDIA_OUTPUT)/root/usr/lib

EXEC = helmet
SRC  = $(wildcard *.c)

# log common osd
INC_FLAGS += -I$(CURRENT_DIR)/log
INC_FLAGS += -I$(CURRENT_DIR)/common
INC_FLAGS += -I$(CURRENT_DIR)/osd
INC_FLAGS += -I$(CURRENT_DIR)/ivs
INC_FLAGS += -I$(CURRENT_DIR)/vi
INC_FLAGS += -I$(CURRENT_DIR)/ai
INC_FLAGS += -I$(CURRENT_DIR)/ao
INC_FLAGS += -I$(CURRENT_DIR)/iva
INC_FLAGS += -I$(CURRENT_DIR)/rknn
INC_FLAGS += -I$(CURRENT_DIR)/rtmp
INC_FLAGS += -I$(CURRENT_DIR)/httpc
INC_FLAGS += -I$(CURRENT_DIR)/lwsc
INC_FLAGS += -I$(CURRENT_DIR)/key

# INC_FLAGS += -I $(RK_SDK_ROOT)/external/rknpu2/examples/3rdparty/opencv/opencv-linux-aarch64/include/
SRC += $(wildcard log/*.c)
SRC += $(wildcard osd/*.c)
SRC += $(wildcard common/*.c)
SRC += $(wildcard ivs/*.c)
SRC += $(wildcard vi/*.c)
SRC += $(wildcard ai/*.c)
SRC += $(wildcard ao/*.c)
SRC += $(wildcard iva/*.c)
SRC += $(wildcard rknn/*.c)
SRC += $(wildcard rtmp/*.c)
SRC += $(wildcard httpc/*.c)
SRC += $(wildcard lwsc/*.c)
SRC += $(wildcard key/*.c)

all: clean $(EXEC)

$(EXEC): $(SRC)
	$(CMD_DBG)$(SIMPLE_CC) $^ -o $@ $(SIMPLE_CFLAGS) $(SIMPLE_LD_FLAGS) 
# 	$(SIMPLE_STRIP) $(EXEC)

clean:
	$(CMD_DBG)@echo "clean"
	$(CMD_DBG)-rm -rf $(EXEC) *.o
