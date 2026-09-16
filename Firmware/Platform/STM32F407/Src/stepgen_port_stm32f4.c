/**
 * @file    stepgen_port_stm32f4.c
 * @brief   STM32F407VGT6 hardware port for the STEP/DIR generator.
 *
 *      TIM8 UP ──► DMA2 Stream1 Ch7 ──► GPIOA->BSRR   (STEP X..B, PA8..PA12)
 *      CPU (boundary ISR)           ──► GPIOD->BSRR   (DIR X..B, ADR-006)
 *
 * The CPU never generates a STEP edge and never participates in STEP
 * timing. Once a ring half is committed its waveform is produced entirely
 * by the timer and the DMA controller, independent of interrupt latency,
 * Ethernet traffic and all other software activity.
 *
 * Register-level access rather than HAL: Docs/FIRMWARE-ARCHITECTURE.md §16
 * permits it where timing justifies it, and both cases here qualify - the
 * emergency-stop path must be bounded to a few hundred nanoseconds, and
 * ADR-004 explicitly rejects HAL_TIM_Base_Start_DMA() because it targets
 * the timer's own ARR rather than GPIOA->BSRR.
 */
#include "stepgen_hw_map.h"
#include "stepgen_port_stm32f4.h"
#include "stepgen_port.h"
#include "stepgen.h"

#include <stddef.h>

static stepgen_boundary_cb_t s_cb;
static uint32_t             *s_buf;
static uint32_t              s_ring;
static uint32_t              s_tick_hz;
static volatile uint32_t     s_dma_errors;
static volatile bool         s_enabled;

/* ---------------------------------------------------------------------- */

static void gpio_make_output(GPIO_TypeDef *port, uint32_t mask)
{
    for (uint32_t pin = 0; pin < 16u; pin++) {
        if ((mask & (1u << pin)) == 0u) {
            continue;
        }
        const uint32_t sh = pin * 2u;
        port->MODER   = (port->MODER   & ~(3u << sh)) | (1u << sh);  /* output */
        port->OTYPER &= ~(1u << pin);                                /* PP     */
        /* Very high speed: a 250 ns pulse must still present a clean edge
         * into the driver input. Matches the .ioc's GPIO_SPEED_FREQ_VERY_HIGH. */
        port->OSPEEDR = (port->OSPEEDR & ~(3u << sh)) | (3u << sh);
        port->PUPDR  &= ~(3u << sh);
    }
}

bool stepgen_port_init(stepgen_boundary_cb_t cb, uint32_t *step_buf,
                       uint32_t ring_ticks)
{
    if (cb == NULL || step_buf == NULL || ring_ticks < 4u) {
        return false;
    }
    s_cb   = cb;
    s_buf  = step_buf;
    s_ring = ring_ticks;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIODEN
                  | STEPGEN_DMA_RCC_AHB1ENR_BIT;
    RCC->APB2ENR |= STEPGEN_TIM_RCC_APB2ENR_BIT;
    (void)RCC->APB2ENR;                     /* ensure the write landed */

    /* Drive everything safe BEFORE the pins become outputs, so no transient
     * appears on a STEP or ENABLE line at power-up (§32). EN is active high
     * (Docs/PINOUT.md), so LOW is the disabled state. */
    STEPGEN_GPIO_STEP->BSRR = ((uint32_t)STEP_PINS_MASK_GPIOA) << 16;
    STEPGEN_GPIO_EN->BSRR   = ((uint32_t)EN_PIN_MASK_GPIOD) << 16;
    s_enabled = false;

    gpio_make_output(STEPGEN_GPIO_STEP, STEP_PINS_MASK_GPIOA);
    gpio_make_output(STEPGEN_GPIO_DIR,  DIR_PINS_MASK_GPIOD);
    gpio_make_output(STEPGEN_GPIO_EN,   EN_PIN_MASK_GPIOD);

    /* Timer: update event drives the DMA, no output on any pin, no IRQ. */
    STEPGEN_TIM->CR1   = 0;
    STEPGEN_TIM->CR2   = 0;
    STEPGEN_TIM->SMCR  = 0;
    STEPGEN_TIM->CCMR1 = 0;
    STEPGEN_TIM->CCMR2 = 0;
    STEPGEN_TIM->CCER  = 0;
    STEPGEN_TIM->BDTR  = 0;                 /* MOE stays off */
    STEPGEN_TIM->PSC   = 0;
    STEPGEN_TIM->DIER  = 0;
    STEPGEN_TIM->CR1  |= TIM_CR1_ARPE;      /* ADR-004: ARR preload on */

    /* DWT cycle counter, used by HV-04 to measure the refill cost. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    NVIC_SetPriority(STEPGEN_DMA_IRQn, STEPGEN_PRIO_DMA);
    NVIC_EnableIRQ(STEPGEN_DMA_IRQn);

    return stepgen_port_set_tick_hz(STEPGEN_TICK_HZ);
}

bool stepgen_port_set_tick_hz(uint32_t tick_hz)
{
    if (tick_hz == 0u || (STEPGEN_TIM->CR1 & TIM_CR1_CEN) != 0u) {
        return false;
    }
    const uint32_t div = STEPGEN_TIM_CLK_HZ / tick_hz;
    if (div < 2u || div > 65536u) {
        return false;
    }
    /* Only exact integer divisions: a fractional divider would put a
     * systematic error on every commanded feed rate. */
    if (div * tick_hz != STEPGEN_TIM_CLK_HZ) {
        return false;
    }
    STEPGEN_TIM->ARR = div - 1u;
    s_tick_hz = tick_hz;
    return true;
}

uint32_t stepgen_port_get_tick_hz(void) { return s_tick_hz; }

void stepgen_port_start(void)
{
    DMA_Stream_TypeDef *st = STEPGEN_DMA_STREAM;

    STEPGEN_TIM->CR1 &= ~(uint32_t)TIM_CR1_CEN;
    STEPGEN_TIM->DIER = 0;

    st->CR &= ~DMA_SxCR_EN;
    while ((st->CR & DMA_SxCR_EN) != 0u) { /* wait for the stream to stop */ }
    STEPGEN_DMA_IFCR = STEPGEN_DMA_CLR_ALL;

    st->PAR  = (uint32_t)(uintptr_t)&STEPGEN_GPIO_STEP->BSRR;
    st->M0AR = (uint32_t)(uintptr_t)s_buf;
    st->NDTR = s_ring;

    /* Direct mode (FIFO disabled), one transfer per TIM8_UP request, as
     * ADR-004 specifies. Direct mode also means the DMA fetches each word
     * only when the request arrives, so a refill that lands just ahead of
     * playback is still seen - there is no FIFO holding stale words. */
    st->FCR = 0;

    st->CR = (STEPGEN_DMA_CHSEL << DMA_SxCR_CHSEL_Pos)
           | (3u << DMA_SxCR_PL_Pos)        /* very high priority          */
           | (2u << DMA_SxCR_MSIZE_Pos)     /* 32-bit                      */
           | (2u << DMA_SxCR_PSIZE_Pos)     /* 32-bit, BSRR is a word      */
           | DMA_SxCR_MINC                  /* walk the ring               */
           | DMA_SxCR_CIRC                  /* wrap forever                */
           | (1u << DMA_SxCR_DIR_Pos)       /* memory -> peripheral        */
           | DMA_SxCR_TCIE | DMA_SxCR_HTIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;

    st->CR |= DMA_SxCR_EN;

    STEPGEN_TIM->CNT  = 0;
    STEPGEN_TIM->SR   = 0;
    STEPGEN_TIM->DIER = TIM_DIER_UDE;       /* update event -> DMA request */
    STEPGEN_TIM->CR1 |= TIM_CR1_CEN;
}

void stepgen_port_stop(void)
{
    STEPGEN_TIM->CR1 &= ~(uint32_t)TIM_CR1_CEN;
    STEPGEN_TIM->DIER = 0;
    STEPGEN_DMA_STREAM->CR &= ~DMA_SxCR_EN;
    STEPGEN_GPIO_STEP->BSRR = ((uint32_t)STEP_PINS_MASK_GPIOA) << 16;
}

void stepgen_port_halt_hold(void)
{
    /* ADR-010: a paused command stream stops STEP output but must NOT
     * de-energise the drives, or an axis could drift or drop under load. */
    STEPGEN_TIM->CR1 &= ~(uint32_t)TIM_CR1_CEN;
    STEPGEN_TIM->DIER = 0;
    STEPGEN_DMA_STREAM->CR &= ~DMA_SxCR_EN;
    STEPGEN_GPIO_STEP->BSRR = ((uint32_t)STEP_PINS_MASK_GPIOA) << 16;
}

void stepgen_port_emergency_stop(void)
{
    /* Register writes only, no loops, no calls that can block. Stopping
     * the timer first guarantees no further DMA request can raise a STEP
     * line after the pins have been forced low. */
    STEPGEN_TIM->CR1 &= ~(uint32_t)TIM_CR1_CEN;
    STEPGEN_TIM->DIER = 0;
    STEPGEN_GPIO_STEP->BSRR = ((uint32_t)STEP_PINS_MASK_GPIOA) << 16;
    STEPGEN_GPIO_EN->BSRR   = ((uint32_t)EN_PIN_MASK_GPIOD) << 16;  /* EN low */
    STEPGEN_DMA_STREAM->CR &= ~DMA_SxCR_EN;
    s_enabled = false;
}

void stepgen_port_set_enable(bool enable)
{
    /* EN is ACTIVE HIGH (Docs/PINOUT.md, owner-verified). */
    STEPGEN_GPIO_EN->BSRR = enable ? (uint32_t)EN_PIN_MASK_GPIOD
                                   : (((uint32_t)EN_PIN_MASK_GPIOD) << 16);
    s_enabled = enable;
}

bool     stepgen_port_get_enable(void) { return s_enabled; }
void     stepgen_port_write_dir(uint32_t bsrr) { STEPGEN_GPIO_DIR->BSRR = bsrr; }
uint32_t stepgen_port_ndtr(void)       { return STEPGEN_DMA_STREAM->NDTR; }
uint32_t stepgen_port_cycle_count(void){ return DWT->CYCCNT; }
uint32_t stepgen_port_dma_errors(void) { return s_dma_errors; }

/* ---------------------------------------------------------------------- */

/**
 * Ring-boundary interrupt (NVIC priority 2, ADR-004).
 *
 * Seeing the half-transfer AND transfer-complete flags pending in the same
 * entry proves this handler is at least a half-buffer late, so the DMA has
 * already re-emitted words the engine never refreshed. That is uncommanded
 * motion, not a recoverable hiccup, so it hard-faults.
 */
void STEPGEN_DMA_IRQHandler(void)
{
    const uint32_t isr = STEPGEN_DMA_ISR;
    const bool ht = (isr & STEPGEN_DMA_HTIF) != 0u;
    const bool tc = (isr & STEPGEN_DMA_TCIF) != 0u;
    const bool er = (isr & (STEPGEN_DMA_TEIF | STEPGEN_DMA_DMEIF)) != 0u;

    STEPGEN_DMA_IFCR = STEPGEN_DMA_CLR_ALL;

    if (er) {
        s_dma_errors++;
        stepgen_port_emergency_stop();
        stepgen_on_overrun();
        return;
    }
    if ((ht && tc) || (!ht && !tc)) {
        stepgen_port_emergency_stop();
        stepgen_on_overrun();
        return;
    }

    /* HT means the DMA just finished the first half; TC the second. */
    s_cb(ht ? 0u : 1u);
}
