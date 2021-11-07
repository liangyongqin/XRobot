/**
 * @file monitor.c
 * @author Qu Shen (503578404@qq.com)
 * @brief 监控线程
 * @version 1.0.0
 * @date 2021-04-15
 *
 * @copyright Copyright (c) 2021
 *
 * 监控系统运行情况，记录错误。
 *
 */

#include "bsp_usb.h"
#include "comp_capacity.h"
#include "dev_adc.h"
#include "dev_buzzer.h"
#include "dev_rgb.h"
#include "thd.h"

#define THD_PERIOD_MS (200)
#define THD_DELAY_TICK (pdMS_TO_TICKS(THD_PERIOD_MS))

void Thd_Monitor(void* arg) {
  Runtime_t* runtime = arg;

  uint32_t previous_wake_time = xTaskGetTickCount();

  while (1) {
    runtime->status.vbat = Volt_GetBattery(); /* ADC监测电压 */
    runtime->status.battery = Capacity_GetBatteryRemain(runtime->status.vbat);
    runtime->status.cpu_temp = Temperature_GetCPU();

    uint8_t status = 0;
    status += (uint8_t)(runtime->status.battery < 0.5f);
    status += (uint8_t)(runtime->status.cpu_temp > 50.0f);

    /* 根据检测到的状态闪烁不同的颜色 */
    if (status > 1) {
      RGB_SetColor(COLOR_HEX_RED, LED_TAGGLE);
    } else if (status > 0) {
      RGB_SetColor(COLOR_HEX_YELLOW, LED_TAGGLE);
    } else {
      RGB_SetColor(COLOR_HEX_GREEN, LED_TAGGLE);
    }

    /* 运行结束，等待下一次唤醒 */
    xTaskDelayUntil(&previous_wake_time, THD_DELAY_TICK);
  }
}
