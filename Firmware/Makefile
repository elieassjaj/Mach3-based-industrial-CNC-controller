# CNC5AX-ETH motion and network subsystem build.
#
#   make test    host unit tests for the portable motion, safety and net cores
#   make bench   host benchmark of the ring refill cost
#   make arm     cross-compile both subsystems + their STM32F407 ports
#
# The ARM target is a compile check against the real CMSIS, HAL and lwIP
# headers. The flashable image is built by STM32CubeIDE from the project in
# this directory; see README.md for integration.

MOTION_INC := -ICore/Inc -IMotion/Inc
MOTION_SRC := Motion/Src/stepgen_core.c \
              Motion/Src/stepgen.c \
              Motion/Src/motion_segment_queue.c

NET_INC    := -INet/Inc
NET_SRC    := Net/Src/net_link.c \
              Net/Src/cnc_protocol.c \
              Net/Src/cnc_session.c

SAFETY_INC := -ISafety/Inc
SAFETY_SRC := Safety/Src/safety_input.c

WARN := -Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion \
        -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes \
        -Wundef -Wdouble-promotion

BUILD := build

HOST_CC    := gcc
HOST_INC   := $(MOTION_INC) -IPlatform/Host/Src -ITests
HOST_SRC   := $(MOTION_SRC) \
              Platform/Host/Src/stepgen_port_sim.c \
              Platform/Host/Src/sim_trace.c
HOST_FLAGS := -std=c11 -O2 -g $(WARN) $(HOST_INC)

# The safety core reaches the motion engine through stepgen.h, so anything
# that links it also links the real engine on its simulation port. That is
# the point: a test asserting PE2 must be able to check that the ENGINE
# ended up in EMERGENCY_STOP, not that a mock recorded a call.
SAFETY_HOST_SRC := $(SAFETY_SRC) Platform/Host/Src/safety_port_sim.c

.PHONY: all test test-motion test-net test-proto test-safety bench arm clean

all: test bench arm

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/test_stepgen: $(HOST_SRC) Tests/test_stepgen.c | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) $^ -o $@

# net_link.c alone - the link observer has no motion dependency, and
# keeping this target narrow means a break there cannot hide behind the
# motion engine failing to link.
$(BUILD)/test_net_link: Net/Src/net_link.c Tests/test_net_link.c | $(BUILD)
	$(HOST_CC) -std=c11 -O2 -g $(WARN) $(NET_INC) -ITests $^ -o $@

# The protocol suite drives the session against the REAL motion engine on
# its simulation port, so backpressure and abort are exercised against the
# real queue rather than a mock. It links the safety core too, since the
# status packet now carries the input word.
$(BUILD)/test_protocol: $(NET_SRC) $(HOST_SRC) $(SAFETY_HOST_SRC) \
                        Tests/test_protocol.c | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) $(NET_INC) $(SAFETY_INC) $^ -o $@

$(BUILD)/test_safety: $(SAFETY_HOST_SRC) $(HOST_SRC) Tests/test_safety.c \
                      | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) $(SAFETY_INC) $^ -o $@

test: test-motion test-safety test-net test-proto

test-motion: $(BUILD)/test_stepgen
	@./$(BUILD)/test_stepgen

test-safety: $(BUILD)/test_safety
	@./$(BUILD)/test_safety

test-net: $(BUILD)/test_net_link
	@./$(BUILD)/test_net_link

test-proto: $(BUILD)/test_protocol
	@./$(BUILD)/test_protocol

bench: $(BUILD)/bench_fill
	@./$(BUILD)/bench_fill

# ----------------------------------------------------------------- arm ----
ARM_CC    := arm-none-eabi-gcc
# The CubeIDE project's own CMSIS copy; tools/fetch_cmsis.sh provides a
# standalone one for CI or a bare checkout.
CMSIS_DIR ?= Drivers/CMSIS
ARM_INC   := $(MOTION_INC) $(NET_INC) $(SAFETY_INC) -IPlatform/STM32F407/Inc \
             -I$(CMSIS_DIR)/Device/ST/STM32F4xx/Include \
             -I$(CMSIS_DIR)/Include
# The network port also needs the HAL, the PHY driver and lwIP, since it
# bridges to the generated project rather than replacing it.
ARM_NET_INC := -IDrivers/STM32F4xx_HAL_Driver/Inc \
               -IDrivers/BSP/Components/lan8742 \
               -IMiddlewares/Third_Party/LwIP/src/include \
               -IMiddlewares/Third_Party/LwIP/system \
               -ILWIP/App -ILWIP/Target
ARM_SRC   := $(MOTION_SRC) \
             Platform/STM32F407/Src/stepgen_port_stm32f4.c \
             Platform/STM32F407/Src/stepgen_selftest_stm32f4.c \
             $(NET_SRC) \
             Platform/STM32F407/Src/net_port_stm32f4.c \
             Platform/STM32F407/Src/net_glue_stm32f4.c \
             Platform/STM32F407/Src/net_selftest_stm32f4.c \
             Platform/STM32F407/Src/net_udp_stm32f4.c \
             $(SAFETY_SRC) \
             Platform/STM32F407/Src/safety_port_stm32f4.c \
             Platform/STM32F407/Src/safety_selftest_stm32f4.c
ARM_FLAGS := -std=c11 -O2 -g -mcpu=cortex-m4 -mthumb \
             -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
             -ffunction-sections -fdata-sections \
             -DSTM32F407xx $(WARN) $(ARM_INC) \
             -DSTEPGEN_BUFFERS_SECTION='".stepgen_ram"'

arm: | $(BUILD)
	@test -d "$(CMSIS_DIR)/Include" || { \
	  echo "CMSIS headers not found at $(CMSIS_DIR)."; \
	  echo "Run tools/fetch_cmsis.sh, or pass CMSIS_DIR=<path>."; exit 1; }
	@mkdir -p $(BUILD)/arm
	@for f in $(ARM_SRC); do \
	  o=$(BUILD)/arm/$$(echo $$f | tr / _ | sed 's/\.c$$/.o/'); \
	  echo "  CC(arm) $$f"; \
	  $(ARM_CC) $(ARM_FLAGS) $(ARM_NET_INC) -c $$f -o $$o || exit 1; \
	done
	@arm-none-eabi-size -t $(BUILD)/arm/*.o | tail -1

clean:
	rm -rf $(BUILD)

# ------------------------------------------------------------ firmware ----
# Whole-image verification build: compiles and links everything, including
# the CubeMX-generated files that only USER CODE blocks separate from ours,
# so a broken edit there fails a command instead of waiting for someone to
# open the IDE.
#
# This is NOT the flashable deliverable. STM32CubeIDE owns that, and its
# build configurations differ (optimisation level, C library, linker flags),
# so the section sizes below will not match the IDE's byte for byte. What
# this target establishes is that the code compiles, links, fits, and places
# its buffers in the regions ADR-012 requires.
FW_INC := -ICore/Inc -IMotion/Inc $(NET_INC) $(SAFETY_INC) -IPlatform/STM32F407/Inc \
          -IDrivers/STM32F4xx_HAL_Driver/Inc \
          -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy \
          -IDrivers/BSP/Components/lan8742 \
          -I$(CMSIS_DIR)/Device/ST/STM32F4xx/Include -I$(CMSIS_DIR)/Include \
          -ILWIP/App -ILWIP/Target \
          -IMiddlewares/Third_Party/LwIP/src/include \
          -IMiddlewares/Third_Party/LwIP/system

FW_DEF := -DSTM32F407xx -DUSE_HAL_DRIVER \
          -DSTEPGEN_BUFFERS_SECTION='".stepgen_ram"'

FW_ARCH := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
FW_BASE := -std=gnu11 -O2 -g3 $(FW_ARCH) -ffunction-sections -fdata-sections \
           $(FW_DEF) $(FW_INC)

# Project-owned code carries the strict warning set; ST's HAL, CMSIS and
# lwIP do not compile clean under it and are not ours to change.
FW_OURS   := $(FW_BASE) $(WARN)
FW_VENDOR := $(FW_BASE) -Wall

FW_SRC_OURS   := $(MOTION_SRC) $(NET_SRC) \
                 Platform/STM32F407/Src/stepgen_port_stm32f4.c \
                 Platform/STM32F407/Src/stepgen_selftest_stm32f4.c \
                 Platform/STM32F407/Src/net_port_stm32f4.c \
                 Platform/STM32F407/Src/net_glue_stm32f4.c \
                 Platform/STM32F407/Src/net_selftest_stm32f4.c \
                 Platform/STM32F407/Src/net_udp_stm32f4.c \
                 $(SAFETY_SRC) \
                 Platform/STM32F407/Src/safety_port_stm32f4.c \
                 Platform/STM32F407/Src/safety_selftest_stm32f4.c

FW_SRC_VENDOR := $(wildcard Core/Src/*.c) \
                 $(wildcard Drivers/STM32F4xx_HAL_Driver/Src/*.c) \
                 Drivers/BSP/Components/lan8742/lan8742.c \
                 LWIP/App/lwip.c LWIP/Target/ethernetif.c \
                 $(wildcard Middlewares/Third_Party/LwIP/src/core/*.c) \
                 $(wildcard Middlewares/Third_Party/LwIP/src/core/ipv4/*.c) \
                 Middlewares/Third_Party/LwIP/src/netif/ethernet.c

FW_LD := -TSTM32F407VGTX_FLASH.ld --specs=nosys.specs --specs=nano.specs \
         -Wl,--gc-sections -Wl,-Map=$(BUILD)/fw/CNC5AX-ETH.map

.PHONY: firmware

firmware: | $(BUILD)
	@test -d "$(CMSIS_DIR)/Include" || { \
	  echo "CMSIS headers not found at $(CMSIS_DIR)."; \
	  echo "Run tools/fetch_cmsis.sh, or pass CMSIS_DIR=<path>."; exit 1; }
	@mkdir -p $(BUILD)/fw
	@for f in $(FW_SRC_OURS); do \
	  o=$(BUILD)/fw/$$(echo $$f | tr / _ | sed 's/\.c$$/.o/'); \
	  $(ARM_CC) $(FW_OURS) -c $$f -o $$o || exit 1; \
	done
	@for f in $(FW_SRC_VENDOR); do \
	  o=$(BUILD)/fw/$$(echo $$f | tr / _ | sed 's/\.c$$/.o/'); \
	  $(ARM_CC) $(FW_VENDOR) -c $$f -o $$o || exit 1; \
	done
	@$(ARM_CC) $(FW_BASE) -c Core/Startup/startup_stm32f407vgtx.s \
	  -o $(BUILD)/fw/startup.o
	@$(ARM_CC) $(FW_ARCH) $(FW_LD) $(BUILD)/fw/*.o -o $(BUILD)/fw/CNC5AX-ETH.elf
	@arm-none-eabi-size $(BUILD)/fw/CNC5AX-ETH.elf
