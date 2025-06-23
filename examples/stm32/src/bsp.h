#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TIMER_IRQ_HANDLER TIM15_IRQHandler

#ifdef __cplusplus
extern "C" {
#endif

/* Private defines -----------------------------------------------------------*/
#ifndef NVIC_PRIORITYGROUP_0
#define NVIC_PRIORITYGROUP_0         ((uint32_t)0x00000007) /*!< 0 bit  for pre-emption priority,
                                                                 4 bits for subpriority */
#define NVIC_PRIORITYGROUP_1         ((uint32_t)0x00000006) /*!< 1 bit  for pre-emption priority,
                                                                 3 bits for subpriority */
#define NVIC_PRIORITYGROUP_2         ((uint32_t)0x00000005) /*!< 2 bits for pre-emption priority,
                                                                 2 bits for subpriority */
#define NVIC_PRIORITYGROUP_3         ((uint32_t)0x00000004) /*!< 3 bits for pre-emption priority,
                                                                 1 bit  for subpriority */
#define NVIC_PRIORITYGROUP_4         ((uint32_t)0x00000003) /*!< 4 bits for pre-emption priority,
                                                                 0 bit  for subpriority */
#endif

// Initialize board to decoder (PE5 and PE6 are track inputs)
void bsp_init_decoder(void);

// Initialize board to command station (PE5 and PE5 are track outputs)
void bsp_init_command_station(void);

// Handle decoder interrupt
uint32_t bsp_decoder_irq(void);

// Handle command station interrupt
void bsp_command_station_irq(uint32_t arr);

// Set track outputs
void bsp_write_track(bool N, bool P);

// Write LEDs
void bsp_write_green_led(bool on);
void bsp_write_yellow_led(bool on);
void bsp_write_red_led(bool on);

// Delay milliseconds
void bsp_delay(uint32_t ms);

#ifdef __cplusplus
}
#endif