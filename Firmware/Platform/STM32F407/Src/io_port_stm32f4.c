/**
 * @file    io_port_stm32f4.c
 * @brief   STM32F407VGT6 GPIOB + TIM3_CH1 port for the output subsystem.
 *
 *      PB8  relay      GPIO output, active high (Docs/PINOUT.md)
 *      PB2  run LED    GPIO output
 *      PB1  error LED  GPIO output
 *      PB4  spindle    TIM3_CH1, AF2, PWM mode 1, 10 kHz
 *
 * Register-level rather than HAL, for the same two reasons the motion and
 * input ports are: the emergency-off path is a safety property and must be
 * bounded and predictable (§16 permits register access exactly where
 * timing justifies it), and the port configures its own peripherals rather
 * than trusting MX_GPIO_Init()/MX_TIM3_Init() to have done it - a pin
 * dropped by a future .ioc regeneration would otherwise become a relay
 * that never switches.
 *
 * Every write here is idempotent and the cost is a few dozen cycles once
 * at boot.
 */
#include "io_hw_map.h"
#include "io_port.h"

static volatile bool s_running;

/* ---------------------------------------------------------------------- */
/* Level helpers - the two polarity facts live here and nowhere else.      */
/* ---------------------------------------------------------------------- */

static void write_pin(uint32_t mask, bool active, bool active_high)
{
    /* BSRR: low half sets, high half resets. One write, no read-modify. */
    if (active == active_high) {
        IO_GPIO->BSRR = mask;
    } else {
        IO_GPIO->BSRR = (mask << 16);
    }
}

/* ---------------------------------------------------------------------- */
/* Init                                                                    */
/* ---------------------------------------------------------------------- */

static void gpio_make_output(uint32_t mask)
{
    for (uint32_t pin = 0; pin < 16u; pin++) {
        if ((mask & (1u << pin)) == 0u) {
            continue;
        }
        const uint32_t sh = pin * 2u;
        IO_GPIO->MODER   = (IO_GPIO->MODER & ~(3u << sh)) | (1u << sh);
        IO_GPIO->OTYPER &= ~(1u << pin);                      /* push-pull */
        IO_GPIO->OSPEEDR = (IO_GPIO->OSPEEDR & ~(3u << sh));  /* low speed */
        IO_GPIO->PUPDR   = (IO_GPIO->PUPDR & ~(3u << sh));    /* no pull   */
    }
}

/** PWM mode 1 with the compare preloaded, counter stopped, output forced low. */
static void spindle_timer_init(void)
{
    IO_SPINDLE_TIM->CR1  = 0;                 /* stop before touching it  */
    IO_SPINDLE_TIM->CR2  = 0;
    IO_SPINDLE_TIM->SMCR = 0;
    IO_SPINDLE_TIM->DIER = 0;                 /* no interrupt, no DMA     */
    IO_SPINDLE_TIM->CCER = 0;                 /* channel off while we set */

    IO_SPINDLE_TIM->PSC  = IO_SPINDLE_PSC;
    IO_SPINDLE_TIM->ARR  = IO_SPINDLE_ARR;
    IO_SPINDLE_TIM->CCR1 = 0;

    /* Start in FORCE INACTIVE, not PWM: between here and the first real
     * duty command the pin must be low for certain, not low because the
     * compare value happens to be zero. */
    IO_SPINDLE_TIM->CCMR1 = IO_OC1M_FORCE_INACTIVE | TIM_CCMR1_OC1PE;
    IO_SPINDLE_TIM->CCER  = TIM_CCER_CC1E;    /* active high output       */
    IO_SPINDLE_TIM->CR1   = TIM_CR1_ARPE;     /* ARR preloaded, CEN clear */
    IO_SPINDLE_TIM->EGR   = TIM_EGR_UG;       /* load PSC/ARR now         */

    s_running = false;
}

bool io_port_init(void)
{
    RCC->AHB1ENR |= IO_GPIO_RCC_AHB1ENR_BIT;
    RCC->APB1ENR |= IO_SPINDLE_TIM_RCC_APB1_BIT;
    (void)RCC->APB1ENR;                       /* ensure the write landed  */

    /* Drive everything to its INACTIVE level before the pins become
     * outputs, so nothing twitches at power-up (§32). The relay is active
     * high, so that means low - and Docs/PINOUT.md requires exactly this. */
    write_pin(RELAY_PIN_MASK_GPIOB, false, CNC_RELAY_ACTIVE_HIGH);
    write_pin(LED_RUN_MASK_GPIOB,   false, CNC_LED_ACTIVE_HIGH);
    write_pin(LED_ERR_MASK_GPIOB,   false, CNC_LED_ACTIVE_HIGH);

    gpio_make_output(IO_GPIO_OUT_MASK);

    /* Timer before the pin: once PB4 is switched to the alternate
     * function it presents whatever the timer is producing, so the timer
     * must already be producing a guaranteed low. */
    spindle_timer_init();

    {
        const uint32_t pin = IO_SPINDLE_PIN;
        const uint32_t sh  = pin * 2u;
        const uint32_t ash = (pin % 8u) * 4u;

        IO_GPIO->OTYPER  &= ~(1u << pin);
        IO_GPIO->OSPEEDR  = (IO_GPIO->OSPEEDR & ~(3u << sh)) | (2u << sh);
        IO_GPIO->PUPDR    = (IO_GPIO->PUPDR & ~(3u << sh));
        IO_GPIO->AFR[0]   = (IO_GPIO->AFR[0] & ~(0xFu << ash))
                          | (IO_SPINDLE_AF << ash);
        IO_GPIO->MODER    = (IO_GPIO->MODER & ~(3u << sh)) | (2u << sh); /* AF */
    }

    return true;
}

/* ---------------------------------------------------------------------- */
/* M9                                                                      */
/* ---------------------------------------------------------------------- */

void io_port_set_relay(bool on)
{
    write_pin(RELAY_PIN_MASK_GPIOB, on, CNC_RELAY_ACTIVE_HIGH);
}

bool io_port_get_relay(void)
{
    const bool high = (IO_GPIO->ODR & RELAY_PIN_MASK_GPIOB) != 0u;
    return (CNC_RELAY_ACTIVE_HIGH != 0) ? high : !high;
}

void io_port_set_led_run(bool on)
{
    write_pin(LED_RUN_MASK_GPIOB, on, CNC_LED_ACTIVE_HIGH);
}

void io_port_set_led_err(bool on)
{
    write_pin(LED_ERR_MASK_GPIOB, on, CNC_LED_ACTIVE_HIGH);
}

/* ---------------------------------------------------------------------- */
/* M10                                                                     */
/* ---------------------------------------------------------------------- */

void io_port_spindle_set_ccr(uint32_t ccr)
{
    /* The ceiling is ARR+1, not ARR. In PWM mode 1 the output is active
     * while CNT < CCR1 and CNT counts 0..ARR, so CCR1 == ARR leaves one
     * count of every period inactive - 99.99 % duty, with a 12 ns notch
     * every 100 us that a VFD input is entitled to see as an edge. Only
     * CCR1 == ARR+1 is a genuinely constant high. The register is 16 bit
     * and ARR+1 = 8400 fits. */
    if (ccr > (uint32_t)IO_SPINDLE_ARR + 1u) {
        ccr = (uint32_t)IO_SPINDLE_ARR + 1u;   /* the core range-checks too */
    }

    IO_SPINDLE_TIM->CCR1 = ccr;

    if (!s_running) {
        /* Leave the forced-low mode, load the compare, and start with the
         * counter at a period boundary so the first pulse is full length. */
        IO_SPINDLE_TIM->CCMR1 = (IO_SPINDLE_TIM->CCMR1 & ~(uint32_t)IO_OC1M_MASK)
                              | IO_OC1M_PWM1 | TIM_CCMR1_OC1PE;
        IO_SPINDLE_TIM->EGR   = TIM_EGR_UG;
        IO_SPINDLE_TIM->CR1  |= TIM_CR1_CEN;
        s_running = true;
    }
    /* While running the compare is preloaded, so a duty change is taken at
     * the next update event - never mid-pulse, so no short or stretched
     * pulse ever reaches a VFD. */
}

void io_port_spindle_stop(void)
{
    /* Force the output inactive FIRST. Simply writing CCR1 = 0 and halting
     * the counter is not enough: with the compare preloaded the new value
     * only lands at the next update, so stopping immediately afterwards
     * can freeze the pin HIGH for as long as the timer stays stopped.
     * Forcing the mode drives it low in this cycle, unconditionally. */
    IO_SPINDLE_TIM->CCMR1 = (IO_SPINDLE_TIM->CCMR1 & ~(uint32_t)IO_OC1M_MASK)
                          | IO_OC1M_FORCE_INACTIVE;
    IO_SPINDLE_TIM->CCR1  = 0;
    IO_SPINDLE_TIM->CR1  &= ~(uint32_t)TIM_CR1_CEN;
    s_running = false;
}

bool     io_port_spindle_running(void) { return s_running; }
uint32_t io_port_spindle_ccr(void)     { return IO_SPINDLE_TIM->CCR1; }
uint32_t io_port_spindle_period(void)  { return IO_SPINDLE_TIM->ARR + 1u; }

/* ---------------------------------------------------------------------- */

void io_port_emergency_off(void)
{
    /* Relay first - it is the one that may be holding a contactor closed,
     * and it is a single store. */
    IO_GPIO->BSRR = (CNC_RELAY_ACTIVE_HIGH != 0)
                  ? (RELAY_PIN_MASK_GPIOB << 16)
                  : RELAY_PIN_MASK_GPIOB;

    io_port_spindle_stop();
}
