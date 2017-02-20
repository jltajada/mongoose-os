/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_COMMON_PLATFORMS_ESP8266_ESP_MISSING_INCLUDES_H_
#define CS_COMMON_PLATFORMS_ESP8266_ESP_MISSING_INCLUDES_H_

#include <stdint.h>
#include <stdlib.h>

void pp_soft_wdt_init(void);
void pp_soft_wdt_stop(void);
void pp_soft_wdt_feed(void);
void pp_soft_wdt_restart(void);
void system_soft_wdt_stop(void); /* Alias for pp_soft_wdt_stop */

void uart_div_modify(int no, unsigned int freq);

void Cache_Read_Disable_2();
void Cache_Read_Enable_2();
int SPIEraseBlock(uint32_t block);

#ifndef RTOS_SDK

#include <ets_sys.h>

/* There are no declarations for these anywhere in the SDK (as of 1.2.0). */
void ets_install_putc1(void *routine);
void ets_isr_attach(int intr, void *handler, void *arg);
void ets_isr_mask(unsigned intr);
void ets_isr_unmask(unsigned intr);
void ets_timer_arm_new(ETSTimer *a, int b, int c, int isMstimer);
void ets_timer_disarm(ETSTimer *a);
void ets_timer_setfn(ETSTimer *t, ETSTimerFunc *fn, void *parg);
void ets_delay_us(unsigned us);
void system_restart_local(void);
int os_printf_plus(const char *format, ...);

void ets_wdt_init(void);
void ets_wdt_enable(uint32_t mode, uint32_t arg1, uint32_t arg2);
void ets_wdt_disable(void);
void ets_wdt_restore(uint32_t mode);
uint32_t ets_wdt_get_mode(void);

void _xtos_l1int_handler(void);
void _xtos_set_exception_handler();
void xthal_set_intenable(unsigned);

/* These are present in mem.h but are commented out. */
void *pvPortMalloc(size_t xWantedSize, const char *file, int line);
void vPortFree(void *pv, const char *file, int line);
void *pvPortZalloc(size_t size, const char *file, int line);
void *pvPortRealloc(void *pv, size_t size, const char *file, int line);

#else /* !RTOS_SDK */

#define BIT(nr) (1UL << (nr))
void system_soft_wdt_feed(void);
void system_soft_wdt_restart(void);
void ets_putc(char c);

#endif /* RTOS_SDK */

void _ResetVector(void);

#endif /* CS_COMMON_PLATFORMS_ESP8266_ESP_MISSING_INCLUDES_H_ */
