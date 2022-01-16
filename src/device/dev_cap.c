#include "dev_cap.h"

#include <stdbool.h>
#include <string.h>

#include "comp_capacity.h"
#include "comp_utils.h"

/* Super capacitor */
#define CAP_FB_ID_BASE (0x211)
#define CAP_CTRL_ID_BASE (0x210)

#define CAP_RES (100) /* 电容数据分辨率 */

#define CAP_CUTOFF_VOLT 12.0f /* 电容截止电压，要高于电调最低工作电压 */

static void cap_decode(cap_feedback_t *fb, const uint8_t *raw) {
  fb->input_volt = (float)((raw[1] << 8) | raw[0]) / (float)CAP_RES;
  fb->cap_volt = (float)((raw[3] << 8) | raw[2]) / (float)CAP_RES;
  fb->input_curr = (float)((raw[5] << 8) | raw[4]) / (float)CAP_RES;
  fb->target_power = (float)((raw[7] << 8) | raw[6]) / (float)CAP_RES;

  /* 更新电容状态和百分比 */
  fb->percentage = capacity_get_capacitor_remain(fb->cap_volt, fb->input_volt,
                                                 CAP_CUTOFF_VOLT);
}

void cap_rx_callback(CAN_Raw_t raw, void *arg) {
  cap_t *cap = (cap_t *)arg;
  if (index == 0) {
    xQueueSendToBackFromISR(cap->msgq_feedback, raw.data, 0);
  }
}

err_t cap_init(cap_t *cap, const cap_param_t *param) {
  cap->msgq_control = xQueueCreate(1, sizeof(CAN_RawTx_t));
  cap->msgq_feedback = xQueueCreate(1, sizeof(CAN_Raw_t));

  cap->param = param;

  BSP_CAN_RegisterSubscriber(cap->param->can, cap->param->index,
                             cap->param->num, cap_rx_callback, cap);

  if (cap->msgq_control && cap->msgq_feedback)
    return RM_OK;
  else
    return ERR_FAIL;
}

err_t cap_update(cap_t *cap, uint32_t timeout) {
  ASSERT(cap);
  uint8_t data[8];
  while (pdPASS ==
         xQueueReceive(cap->msgq_feedback, data, pdMS_TO_TICKS(timeout))) {
    cap_decode(&(cap->feedback), data);
  }
  return RM_OK;
}

err_t cap_control(cap_t *cap, cap_control_t *output) {
  ASSERT(cap);
  ASSERT(output);

  uint16_t pwr_lim = (uint16_t)(output->power_limit * CAP_RES);

  uint8_t data[8] = {0};
  data[0] = (pwr_lim >> 8) & 0xFF;
  data[1] = pwr_lim & 0xFF;

  can_trans_packet(cap->param->can, CAP_CTRL_ID_BASE, data, &cap->mailbox);
  return RM_OK;
}

err_t cap_handle_offline(cap_t *cap) {
  ASSERT(cap);
  cap->feedback.cap_volt = 0;
  cap->feedback.input_curr = 0;
  cap->feedback.input_volt = 0;
  cap->feedback.target_power = 0;
  return RM_OK;
}

err_t cap_pack_ui(const cap_t *cap, ui_cap_t *ui) {
  ui->percentage = cap->feedback.percentage;
  return RM_OK;
}
