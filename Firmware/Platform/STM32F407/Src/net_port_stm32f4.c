/**
 * @file    net_port_stm32f4.c
 * @brief   STM32F407VGT6 Ethernet hardware port (see net_port_stm32f4.h).
 *
 * ADR-013's PHY reset pulse, plus the introspection HV-20..HV-22 need.
 */
#include "net_port_stm32f4.h"

#include <stddef.h>

static bool     s_reset_done;
static uint32_t s_assert_cycles;

/* ---------------------------------------------------------------------- */

static void dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
    /* CYCCNT is deliberately not zeroed: stepgen_port_init() owns that, and
     * every use here is a difference of two reads. */
}

void net_port_delay_us(uint32_t us)
{
    dwt_enable();

    const uint32_t cycles = us * (SystemCoreClock / 1000000u);
    const uint32_t t0     = DWT->CYCCNT;
    while ((DWT->CYCCNT - t0) < cycles) {
        __NOP();
    }
}

bool net_port_phy_hw_reset(void)
{
    RCC->AHB1ENR |= NET_PHY_NRST_RCC_AHB1ENR;
    (void)RCC->AHB1ENR;                      /* ensure the write landed */

    /* Configure PB0 explicitly rather than trusting MX_GPIO_Init() to have
     * run first and to still be configuring this pin after the next
     * regeneration. Internal pull-up on, backing the module's own 4.7k, so
     * the line is never floating while it is an input at reset.            */
    const uint32_t sh = NET_PHY_NRST_PIN * 2u;
    NET_PHY_NRST_GPIO->MODER   = (NET_PHY_NRST_GPIO->MODER   & ~(3u << sh)) | (1u << sh);
    NET_PHY_NRST_GPIO->OTYPER &= ~NET_PHY_NRST_MASK;                     /* push-pull */
    NET_PHY_NRST_GPIO->OSPEEDR = (NET_PHY_NRST_GPIO->OSPEEDR & ~(3u << sh));
    NET_PHY_NRST_GPIO->PUPDR   = (NET_PHY_NRST_GPIO->PUPDR   & ~(3u << sh)) | (1u << sh);

    dwt_enable();

    /* Assert: nRST is active low. */
    NET_PHY_NRST_GPIO->BSRR = NET_PHY_NRST_MASK << 16;

    const uint32_t t0 = DWT->CYCCNT;
    net_port_delay_us(NET_PHY_RESET_ASSERT_US);
    s_assert_cycles = DWT->CYCCNT - t0;

    /* Release. Datasheet Note 5-21: deassertion must be monotonic - a single
     * BSRR write on a push-pull output is exactly that. */
    NET_PHY_NRST_GPIO->BSRR = NET_PHY_NRST_MASK;

    net_port_delay_us(NET_PHY_RESET_SETTLE_US);

    s_reset_done = true;

    /* Read the pin, not the output register: if something external is
     * holding nRST down, ODR would still say "high" while the PHY stays in
     * reset and every later MDIO read fails for no visible reason. */
    return (NET_PHY_NRST_GPIO->IDR & NET_PHY_NRST_MASK) != 0u;
}

bool net_port_phy_reset_done(void)
{
    return s_reset_done;
}

uint32_t net_port_phy_reset_assert_cycles(void)
{
    return s_assert_cycles;
}

uint32_t net_port_irq_priority(IRQn_Type irq)
{
    /* NVIC_GetPriority() returns the priority in the same units
     * HAL_NVIC_SetPriority() and the .ioc use, given the 4-bit preemption
     * grouping this project runs (NVIC_PRIORITYGROUP_4). */
    return NVIC_GetPriority(irq);
}

bool net_port_addr_in_sram1(const void *p, uint32_t len)
{
    if (p == NULL || len == 0u) {
        return false;
    }

    const uint32_t start = (uint32_t)(uintptr_t)p;
    const uint32_t end   = start + len;          /* exclusive */

    if (end < start) {                           /* wrapped */
        return false;
    }

    return (start >= NET_SRAM1_BASE)
        && (end   <= (NET_SRAM1_BASE + NET_SRAM1_SIZE));
}
