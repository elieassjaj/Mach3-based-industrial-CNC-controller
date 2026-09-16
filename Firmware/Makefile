# CNC5AX-ETH motion subsystem build.
#
#   make test    host unit tests for the portable motion core
#   make bench   host benchmark of the ring refill cost
#   make arm     cross-compile the motion core + STM32F407 port
#
# The ARM target is a compile check of the motion subsystem against the
# real CMSIS device header. The flashable image is built by STM32CubeIDE
# from the project in this directory; see README.md for integration.

MOTION_INC := -ICore/Inc -IMotion/Inc
MOTION_SRC := Motion/Src/stepgen_core.c \
              Motion/Src/stepgen.c \
              Motion/Src/motion_segment_queue.c

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

.PHONY: all test bench arm clean

all: test bench arm

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/test_stepgen: $(HOST_SRC) Tests/test_stepgen.c | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) $^ -o $@

$(BUILD)/bench_fill: Motion/Src/stepgen_core.c Motion/Src/motion_segment_queue.c Tests/bench_fill.c | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) $^ -o $@

test: $(BUILD)/test_stepgen
	@./$(BUILD)/test_stepgen

bench: $(BUILD)/bench_fill
	@./$(BUILD)/bench_fill

# ----------------------------------------------------------------- arm ----
ARM_CC    := arm-none-eabi-gcc
# The CubeIDE project's own CMSIS copy; tools/fetch_cmsis.sh provides a
# standalone one for CI or a bare checkout.
CMSIS_DIR ?= Drivers/CMSIS
ARM_INC   := $(MOTION_INC) -IPlatform/STM32F407/Inc \
             -I$(CMSIS_DIR)/Device/ST/STM32F4xx/Include \
             -I$(CMSIS_DIR)/Include
ARM_SRC   := $(MOTION_SRC) \
             Platform/STM32F407/Src/stepgen_port_stm32f4.c \
             Platform/STM32F407/Src/stepgen_selftest_stm32f4.c
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
	  $(ARM_CC) $(ARM_FLAGS) -c $$f -o $$o || exit 1; \
	done
	@arm-none-eabi-size -t $(BUILD)/arm/*.o | tail -1

clean:
	rm -rf $(BUILD)
