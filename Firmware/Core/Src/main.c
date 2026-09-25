/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "lwip.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "stepgen.h"
#include "stepgen_selftest.h"
#include "safety_input.h"
#include "safety_selftest.h"
#include "io_outputs.h"
#include "io_selftest.h"
#include "net_selftest.h"
#include "net_udp.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* Motion engine bring-up status, readable over SWD. */
volatile bool     g_stepgen_ready;
volatile uint32_t g_stepgen_tick_hz;

/* Digital-input subsystem (M3) bring-up status, readable over SWD. */
volatile bool     g_safety_ready;

/* Output subsystem (M9 relay/LEDs, M10 spindle) bring-up status. */
volatile bool     g_io_ready;

#if CNC_RUN_SELFTEST_AT_BOOT
volatile bool     g_stepgen_selftest_pass;
volatile bool     g_net_selftest_pass;
volatile bool     g_safety_selftest_pass;
volatile bool     g_io_selftest_pass;
#endif

/* Protocol layer (Phase 3) bring-up status, readable over SWD. */
volatile bool     g_udp_ready;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM3_Init();
  MX_LWIP_Init();
  MX_TIM8_Init();
  /* USER CODE BEGIN 2 */

  /* Motion engine (Phase 1).
   *
   * stepgen_init() configures TIM8 + DMA2_Stream1 + the STEP/DIR/EN GPIOs
   * and leaves the engine in SAFE_IDLE with the drives DISABLED (PD15 at
   * the disabled level for CNC_EN_ACTIVE_HIGH). Nothing moves until something calls
   * stepgen_enable_drives() and stepgen_start(), which is deliberate: the
   * host protocol that will do so is Phase 3 work.
   *
   * If this machine's maximum STEP rate is below the 2 MHz project
   * ceiling, declare it here - the refill cost follows the base tick, not
   * the commanded speed, so a lower ceiling directly returns CPU time:
   *
   *   stepgen_configure_max_rate(1000000u);   // 1 MHz -> half the cost
   */
  g_stepgen_ready   = stepgen_init();
  g_stepgen_tick_hz = stepgen_tick_hz();

  /* Digital inputs and the E-STOP path (Phase 4 / M3).
   *
   * After stepgen_init(), never before: this samples PE2 and, if the
   * E-STOP is ALREADY asserted at power-on, drives the engine straight to
   * EMERGENCY_STOP rather than waiting for an edge that has already
   * happened. It also registers the physical-release interlock ADR-010
   * requires before an E-STOP may ever be cleared, so no packet and no
   * call path can clear one while PE2 is still down.
   *
   * Arming the EXTI lines is the last thing it does, so no input interrupt
   * can arrive before the subsystem knows the initial state of all 15. */
  g_safety_ready = safety_input_init();

  /* Relay, status LEDs and spindle PWM (Phase 5 / M9 + M10).
   *
   * After safety_input_init(), never before: this registers the
   * interrupt-time kill with the E-STOP path, and safety_input_init()
   * clears that registration as part of its own reset. Getting the order
   * wrong would leave a relay closed and a spindle turning until the
   * superloop next ran, on a machine whose relay may be the spindle
   * contactor.
   *
   * Everything comes up off: relay de-energised, PWM generator stopped,
   * both LEDs dark. Outputs stay inhibited until the host has explicitly
   * enabled the drives (ADR-016). */
  g_io_ready = io_init();

#if CNC_RUN_SELFTEST_AT_BOOT
  /* Hardware validation HV-00..HV-05 (Docs/HARDWARE-VALIDATION.md).
   * Safe to run: the drives are still disabled and the timebase is stopped
   * between tests. Inspect stepgen_selftest_results() over SWD. */
  g_stepgen_selftest_pass = stepgen_selftest_run_all();

  /* Network validation HV-20..HV-24. MX_LWIP_Init() above has already run
   * the PHY reset and brought the interface up, so all of them have
   * something to measure. HV-25 (link state) is reported but excluded from
   * the verdict: at this point auto-negotiation has had a few milliseconds,
   * so a down link here means nothing. Read it from the 100 ms poll later.
   * Inspect net_selftest_results() over SWD. */
  g_net_selftest_pass = net_selftest_run_all();

  /* Input-subsystem validation HV-40..HV-43. No E-STOP is asserted by
   * these - they check the conditions that make the E-STOP path work, and
   * HV-18 on a bench checks the path itself. HV-42 assumes the machine's
   * switches are at rest. Inspect safety_selftest_results() over SWD. */
  g_safety_selftest_pass = safety_selftest_run_all();

  /* Output validation HV-50..HV-53. None of these closes the relay or
   * turns the spindle - they check the configuration and that the
   * interlock refuses to, which at this point in the boot it must.
   * Inspect io_selftest_results() over SWD. */
  g_io_selftest_pass = io_selftest_run_all();
#endif

  /* Protocol layer (Phase 3). Binds UDP 55010 and waits. Nothing moves as a
   * result of this: the engine is still in SAFE_IDLE with the drives
   * disabled, and it stays there until a host sends CONTROL:ENABLE_DRIVES
   * and CONTROL:START. Motion blocks arriving before that are refused with
   * WRONG_STATE rather than queued. */
  g_udp_ready = net_udp_init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  MX_LWIP_Process();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Debounce filter, E-STOP release timer and chatter counters. The
     * E-STOP itself does not wait for this - it is acted on in the EXTI2
     * interrupt - so a slow loop iteration delays only the reporting of
     * ordinary inputs and the permission to clear an E-STOP, never the
     * stop. */
    safety_input_poll(HAL_GetTick());

    /* Relay, spindle and the status LEDs. Re-reads the engine state every
     * iteration and applies the safe state from it, so an output can never
     * outlive the condition that permitted it. The E-STOP does not wait
     * for this either - it kills the outputs from the EXTI2 interrupt. */
    io_poll(HAL_GetTick());

    /* Status stream and comm-timeout supervision. MX_LWIP_Process() above
     * delivers received datagrams; this drives the outbound half. */
    net_udp_poll();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
