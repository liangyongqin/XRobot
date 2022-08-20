/**
 * @file launcher.c
 * @author Qu Shen (503578404@qq.com)
 * @brief 弹丸发射器模块
 * @version 1.0.0
 * @date 2021-05-04
 *
 * @copyright Copyright (c) 2021
 *
 */

#include "mod_launcher.hpp"

#include "bsp_pwm.h"
#include "comp_utils.hpp"

#define LAUNCHER_TRIG_SPEED_MAX (8191)

using namespace Module;

Launcher::Launcher(Param& param, float control_freq)
    : param_(param), mode_(Component::CMD::LAUNCHER_MODE_RELAX) {
  for (size_t i = 0; i < LAUNCHER_ACTR_TRIG_NUM; i++) {
    this->trig_actuator_[i] = (Device::PosActuator*)System::Memory::Malloc(
        sizeof(Device::PosActuator));
    new (this->trig_actuator_[i])
        Device::PosActuator(param.trig_actr[i], control_freq);

    this->trig_motor_[i] =
        (Device::RMMotor*)System::Memory::Malloc(sizeof(Device::RMMotor));

    new (this->trig_motor_[i])
        Device::RMMotor(this->param_.trig_motor[i],
                        ("launcher_trig" + std::to_string(i)).c_str());
  }

  for (size_t i = 0; i < LAUNCHER_ACTR_FRIC_NUM; i++) {
    this->fric_actuator_[i] = (Device::SpeedActuator*)System::Memory::Malloc(
        sizeof(Device::SpeedActuator));
    new (this->fric_actuator_[i])
        Device::SpeedActuator(param.fric_actr[i], control_freq);

    this->fric_motor_[i] =
        (Device::RMMotor*)System::Memory::Malloc(sizeof(Device::RMMotor));

    new (this->fric_motor_[i])
        Device::RMMotor(this->param_.fric_motor[i],
                        ("launcher_fric" + std::to_string(i)).c_str());
  }

  bsp_pwm_start(BSP_PWM_LAUNCHER_SERVO);
  bsp_pwm_set_comp(BSP_PWM_LAUNCHER_SERVO, this->param_.cover_close_duty);

  auto launcher_thread = [](void* arg) {
    Launcher* launcher = (Launcher*)arg;

    Device::Referee::Data raw_ref;

    DECLARE_TOPIC(ui_tp, launcher->ui_, "launcher_ui", true);

    DECLARE_SUBER(ref_tp, raw_ref, "referee");
    DECLARE_SUBER(cmd_tp, launcher->cmd_, "cmd_launcher");

    while (1) {
      ref_tp.DumpData();
      cmd_tp.DumpData();

      launcher->PraseRef(raw_ref);
      launcher->UpdateFeedback();
      launcher->Control();
      launcher->PackUI();

      ui_tp.Publish();

      /* 运行结束，等待下一次唤醒 */
      launcher->thread_.Sleep(2);
    }
  };

  THREAD_DECLEAR(this->thread_, launcher_thread, 384, System::Thread::Medium,
                 this);
}

void Launcher::UpdateFeedback() {
  const float last_trig_motor_angle = this->trig_motor_[0]->GetAngle();

  for (size_t i = 0; i < LAUNCHER_ACTR_FRIC_NUM; i++) {
    this->fric_motor_[i]->Update();
  }

  for (size_t i = 0; i < LAUNCHER_ACTR_TRIG_NUM; i++) {
    this->trig_motor_[i]->Update();
  }

  const float delta_motor_angle = circle_error(this->trig_motor_[0]->GetAngle(),
                                               last_trig_motor_angle, M_2PI);
  circle_add(&(this->trig_angle_),
             delta_motor_angle / this->param_.trig_gear_ratio, M_2PI);
}

void Launcher::Control() {
  this->now_ = System::Thread::GetTick();
  this->dt_ = (float)(this->now_ - this->lask_wakeup_) / 1000.0f;
  this->lask_wakeup_ = this->now_;

  this->SetMode(this->cmd_.mode);
  this->HeatLimit();

  /* 根据开火模式计算发射行为 */
  this->fire_ctrl_.fire_mode = this->cmd_.fire_mode;
  uint32_t max_burst;
  switch (this->cmd_.fire_mode) {
    case Component::CMD::FIRE_MODE_SINGLE: /* 点射开火模式 */
      max_burst = 1;
      break;
    case Component::CMD::FIRE_MODE_BURST: /* 爆发开火模式 */
      max_burst = 5;
      break;
    default:
      max_burst = 1;
      break;
  }

  switch (this->cmd_.fire_mode) {
    case Component::CMD::FIRE_MODE_SINGLE: /* 点射开火模式 */
    case Component::CMD::FIRE_MODE_BURST:  /* 爆发开火模式 */

      /* 计算是否是第一次按下开火键 */
      this->fire_ctrl_.first_pressed_fire =
          this->cmd_.fire && !this->fire_ctrl_.last_fire;
      this->fire_ctrl_.last_fire = this->cmd_.fire;

      /* 设置要发射多少弹丸 */
      if (this->fire_ctrl_.first_pressed_fire && !this->fire_ctrl_.to_launch) {
        this->fire_ctrl_.to_launch =
            MIN(max_burst,
                (this->heat_ctrl_.available_shot - this->fire_ctrl_.launched));
      }

      /* 以下逻辑保证触发后一定会打完预设的弹丸，完成爆发 */
      if (this->fire_ctrl_.launched >= this->fire_ctrl_.to_launch) {
        this->fire_ctrl_.launch_delay = UINT32_MAX;
        this->fire_ctrl_.launched = 0;
        this->fire_ctrl_.to_launch = 0;
      } else {
        this->fire_ctrl_.launch_delay = this->param_.min_launch_delay;
      }
      break;

    case Component::CMD::FIRE_MODE_CONT: { /* 持续开火模式 */
      float launch_freq = this->LimitLauncherFreq();
      this->fire_ctrl_.launch_delay =
          (launch_freq == 0.0f) ? UINT32_MAX : (uint32_t)(1000.f / launch_freq);
      break;
    }
    default:
      break;
  }

  /* 根据模式选择是否使用计算出来的值 */
  switch (this->mode_) {
    case Component::CMD::LAUNCHER_MODE_RELAX:
    case Component::CMD::LAUNCHER_MODE_SAFE:
      this->fire_ctrl_.bullet_speed = 0.0f;
      this->fire_ctrl_.launch_delay = UINT32_MAX;
    case Component::CMD::LAUNCHER_MODE_LOADED:
      break;
  }

  /* 计算摩擦轮转速的目标值 */
  this->setpoint.fric_rpm_[1] = bullet_speed_to_fric_rpm(
      this->fire_ctrl_.bullet_speed, this->param_.fric_radius,
      (this->param_.model == LAUNCHER_MODEL_17MM));
  this->setpoint.fric_rpm_[0] = -this->setpoint.fric_rpm_[1];

  /* 计算拨弹电机位置的目标值 */
  if ((this->now_ - this->fire_ctrl_.last_launch) >=
      this->fire_ctrl_.launch_delay) {
    /* 将拨弹电机角度进行循环加法，每次加(减)射出一颗弹丸的弧度变化 */
    if (this->cmd_.reverse_trig) { /* 反转拨盘，用来解决卡顿*/
      circle_add(&(this->setpoint.trig_angle_),
                 M_2PI / this->param_.num_trig_tooth, M_2PI);
    } else {
      circle_add(&(this->setpoint.trig_angle_),
                 -M_2PI / this->param_.num_trig_tooth, M_2PI);
      /* 计算已发射弹丸 */
      this->fire_ctrl_.launched++;
      this->fire_ctrl_.last_launch = this->now_;
    }
  }

  switch (this->mode_) {
    case Component::CMD::LAUNCHER_MODE_RELAX:
      for (size_t i = 0; i < LAUNCHER_ACTR_TRIG_NUM; i++) {
        this->trig_motor_[i]->Relax();
      }
      for (size_t i = 0; i < LAUNCHER_ACTR_FRIC_NUM; i++) {
        this->fric_motor_[i]->Relax();
      }
      bsp_pwm_stop(BSP_PWM_LAUNCHER_SERVO);
      break;

    case Component::CMD::LAUNCHER_MODE_SAFE:
    case Component::CMD::LAUNCHER_MODE_LOADED:
      for (int i = 0; i < LAUNCHER_ACTR_TRIG_NUM; i++) {
        /* 控制拨弹电机 */
        float trig_out = this->trig_actuator_[i]->Calculation(
            this->setpoint.trig_angle_,
            this->trig_motor_[i]->GetSpeed() / LAUNCHER_TRIG_SPEED_MAX,
            this->trig_angle_, this->dt_);

        this->trig_motor_[i]->Control(trig_out);
      }

      for (size_t i = 0; i < LAUNCHER_ACTR_FRIC_NUM; i++) {
        /* 控制摩擦轮 */
        float fric_out = this->fric_actuator_[i]->Calculation(
            this->setpoint.fric_rpm_[i], this->fric_motor_[i]->GetSpeed(),
            this->dt_);

        this->fric_motor_[i]->Control(fric_out);
      }

      /* 根据弹仓盖开关状态更新弹舱盖打开时舵机PWM占空比 */
      if (this->cmd_.cover_open) {
        bsp_pwm_start(BSP_PWM_LAUNCHER_SERVO);
        bsp_pwm_set_comp(BSP_PWM_LAUNCHER_SERVO, this->param_.cover_open_duty);
      } else {
        bsp_pwm_start(BSP_PWM_LAUNCHER_SERVO);
        bsp_pwm_set_comp(BSP_PWM_LAUNCHER_SERVO, this->param_.cover_close_duty);
      }
      break;
  }
}

void Launcher::PackUI() {
  this->ui_.mode = this->mode_;
  this->ui_.fire = this->fire_ctrl_.fire_mode;
  this->ui_.trig = this->trig_angle_;
  for (size_t i = 0; i < LAUNCHER_ACTR_FRIC_NUM; i++) {
    if (this->setpoint.fric_rpm_[i] == 0) {
      this->ui_.fric_percent[i] = 0;
    } else {
      this->ui_.fric_percent[i] =
          this->fric_motor_[i]->GetSpeed() / this->setpoint.fric_rpm_[i];
    }
  }
}

void Launcher::SetMode(Component::CMD::LauncherMode mode) {
  if (mode == this->mode_) return;

  for (size_t i = 0; i < LAUNCHER_ACTR_FRIC_NUM; i++) {
    this->fric_actuator_[i]->Reset();
  }
  for (int i = 0; i < LAUNCHER_ACTR_TRIG_NUM; i++) {
    this->trig_actuator_[i]->Reset();
  }

  if (mode == Component::CMD::LAUNCHER_MODE_LOADED)
    this->fire_ctrl_.to_launch = 0;

  this->mode_ = mode;
}

void Launcher::HeatLimit() {
  if (this->ref_.status == Device::Referee::RUNNING) {
    /* 根据机器人型号获得对应数据 */
    if (this->param_.model == LAUNCHER_MODEL_42MM) {
      this->heat_ctrl_.heat = this->ref_.power_heat.launcher_42_heat;
      this->heat_ctrl_.heat_limit =
          this->ref_.robot_status.launcher_42_heat_limit;
      this->heat_ctrl_.speed_limit =
          this->ref_.robot_status.launcher_42_speed_limit;
      this->heat_ctrl_.cooling_rate =
          this->ref_.robot_status.launcher_42_cooling_rate;
      this->heat_ctrl_.heat_increase = GAME_HEAT_INCREASE_42MM;
    } else if (this->param_.model == LAUNCHER_MODEL_17MM) {
      this->heat_ctrl_.heat = this->ref_.power_heat.launcher_id1_17_heat;
      this->heat_ctrl_.heat_limit =
          this->ref_.robot_status.launcher_id1_17_heat_limit;
      this->heat_ctrl_.speed_limit =
          this->ref_.robot_status.launcher_id1_17_speed_limit;
      this->heat_ctrl_.cooling_rate =
          this->ref_.robot_status.launcher_id1_17_cooling_rate;
      this->heat_ctrl_.heat_increase = GAME_HEAT_INCREASE_17MM;
    }
    /* 检测热量更新后,计算可发射弹丸 */
    if ((this->heat_ctrl_.heat != this->heat_ctrl_.last_heat) ||
        this->heat_ctrl_.available_shot == 0 || (this->heat_ctrl_.heat == 0)) {
      this->heat_ctrl_.available_shot = (uint32_t)floorf(
          (this->heat_ctrl_.heat_limit - this->heat_ctrl_.heat) /
          this->heat_ctrl_.heat_increase);
      this->heat_ctrl_.last_heat = this->heat_ctrl_.heat;
    }
    this->fire_ctrl_.bullet_speed = this->heat_ctrl_.speed_limit;
  } else {
    /* 裁判系统离线，不启用热量控制 */
    this->heat_ctrl_.available_shot = 10;
    this->fire_ctrl_.bullet_speed = this->param_.default_bullet_speed;
  }
}

void Launcher::PraseRef(Device::Referee::Data& ref) {
  memcpy(&(this->ref_.power_heat), &(ref.power_heat),
         sizeof(this->ref_.power_heat));
  memcpy(&(this->ref_.robot_status), &(ref.robot_status),
         sizeof(this->ref_.robot_status));
  memcpy(&(this->ref_.launcher_data), &(ref.launcher_data),
         sizeof(this->ref_.launcher_data));
  this->ref_.status = ref.status;
}

float Launcher::LimitLauncherFreq() {
  float heat_percent = this->heat_ctrl_.heat / this->heat_ctrl_.heat_limit;
  float stable_freq =
      this->heat_ctrl_.cooling_rate / this->heat_ctrl_.heat_increase;
  if (this->param_.model == LAUNCHER_MODEL_42MM)
    return stable_freq;
  else {
    if (heat_percent > 0.9f)
      return 0.5f;
    else if (heat_percent > 0.8f)
      return 1.0f;
    else if (heat_percent > 0.6f)
      return 2.0f * stable_freq;
    else if (heat_percent > 0.2f)
      return 3.0f * stable_freq;
    else if (heat_percent > 0.1f)
      return 4.0f * stable_freq;
    else
      return 5.0f;
  }
}