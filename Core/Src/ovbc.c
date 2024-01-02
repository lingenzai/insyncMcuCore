/*
 * ovbc.c
 *
 *  Created on: Nov 23, 2023
 *      Author: Johnny
 */

#include "main.h"

/* var define *******************************************************/

// ovbc status
static ovbc_status_typeDef ovbc_status;
// ovbc is working flag
static bool ovbcIsWorking;
// record VposENtick
static u32 ovbc_VposEnTick;
// loop num
static u8 ovbc_pulsingLoopNum;



/* private function define ******************************************/

static void ovbc_smVposEn(void);


/*
  brief:
    1. we have sent pulse once turn;
    2. check loop num; continue pulsing or stop;
    3. update status;
*/
static void ovbc_smPulsingLoop(void)
{
  ovbc_pulsingLoopNum++;

  // loop over?
  if(ovbc_pulsingLoopNum >= pulse_getConfig()->pulse_num){
    ovbc_shutdown();

    // wait for next pulsing
    ovbc_status = ovbc_inited_status;
  }else{
    /* continue next loop of this pulse */

    // NOTICE: it is not NEST!!! Stauts is different
    ovbc_smVposEn();
  }
}

/*
  brief:
    1. Reset VposEn pin; set VnegEn pin;
    2. set timer count for VnegEn pulsing time;
    3. update status;
    NOTICE:
      at first: reset VposEn pin; and then set VnegEn pin; have some interval;
*/
static void ovbc_smVnegEn(void)
{
  ppulse_config_typeDef ppulse = pulse_getConfig();
  u32 count;

  // Reset VnegEn pin
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);

  // delay VnegEn pulsing time
  count = ppulse->pulse_width * OVBC_RTC_WKUP_COUNT_UNIT / OVBC_PULSE_WIDTH_UNIT;

  // go into next status
  ovbc_status = ovbc_pulsingLoop_status;

  // set VposEn pin
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_SET);

  // start RTC timer
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, count, RTC_WAKEUPCLOCK_RTCCLK_DIV2);
}

/*
  brief:
    1. Reset VnegEn pin; set VposEn pin;
    2. set timer count for VposEn pulsing time;
    3. update status;
    NOTICE:
      at first: reset vneg pin; and then set vpos pin; have some interval;
*/
static void ovbc_smVposEn(void)
{
  ppulse_config_typeDef ppulse = pulse_getConfig();
  u32 count;

  // Reset VnegEn pin
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);

  // delay VposEn pulsing time
  count = ppulse->pulse_width * OVBC_RTC_WKUP_COUNT_UNIT / OVBC_PULSE_WIDTH_UNIT;

  // go into next status
  ovbc_status = ovbc_VnegEn_status;

  // set VposEn pin
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_SET);

  // start RTC timer
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, count, RTC_WAKEUPCLOCK_RTCCLK_DIV2);
}

/*
  brief:
    1. Enable chip TI-TPS61096A(pin set);
    2. delay left time before VposEn;
    3. update status;
*/
static void ovbc_smChipEnable(void)
{
  u32 count;

  // Enable chip TI-TPS61096A(pin set)
  HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_SET);

  // set rtc timer for next job: VposEn
  count = (ovbc_VposEnTick - HAL_GetTick()) * OVBC_RTC_WKUP_COUNT_UNIT;

  // update status
  ovbc_status = ovbc_VposEn_status;

  // start RTC timer
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, count, RTC_WAKEUPCLOCK_RTCCLK_DIV2);
}

/*
  brief:
    1. pulse config already calibrate when mcu init; so is valid;
    2. RTC Timer:
      use config: 37K Hz; DIV2;
      so, count is: 37/2  digit/ms;
*/
static void ovbc_smStartup(void)
{
  u32 ChipEnTick;
  u32 wkcount;

#ifdef LiuJH_DEBUG
  // VposEnTick
  ovbc_VposEnTick = ecg_getRnTick() + pulse_getConfig()->pulse_Rsvi_ms + pulse_getConfig()->pulse_Rv_delay_ms;
#else
  // VposEnTick
  ovbc_VposEnTick = ecg_getRnTick() + ecg_getRsvi() + pulse_getConfig()->pulse_Rv_delay_ms;
#endif

  // chip start up need 15ms
  ChipEnTick = ovbc_VposEnTick - OVBC_CHIP_ENABLE_TIME;

  // So the ChipEn delay is chiptick - current tick
  // and So count is ...
  wkcount = (ChipEnTick - HAL_GetTick()) * OVBC_RTC_WKUP_COUNT_UNIT;

  // update status
  ovbc_status = ovbc_chipEnable_status;

  // we can start timer
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, wkcount, RTC_WAKEUPCLOCK_RTCCLK_DIV2);

  /* all other status process will be in RTC Timer Callback */

}

/*
  1. all status proc after ovbc_startup_status working in RTC Timer Callback;
  2. purpose: Holding precise time Prevent interruption by other tasks;
  3. NOTICE: please hand move this function;
*/
static void ovbc_stateMachine(void)
{
  switch(ovbc_status){
    case ovbc_inited_status:
      // do noting
      break;
    case ovbc_startup_status:
      ovbc_smStartup();
      break;
    case ovbc_chipEnable_status:
      ovbc_smChipEnable();
      break;
    case ovbc_VposEn_status:
      ovbc_smVposEn();
      break;
    case ovbc_VnegEn_status:
      ovbc_smVnegEn();
      break;
    case ovbc_pulsingLoop_status:
      ovbc_smPulsingLoop();
      break;


    default:
      break;
  }
}

/* public function define *******************************************/


/*
  brief:
    1. Wakeup Timer callback;
    2. 
*/
void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc)
{
  ovbc_stateMachine();
}

/*
  brief:
    1. when ble or pulse shut down, we will shut down;
*/
void ovbc_shutdown(void)
{
  // reset VposEn and VnegEn
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);

  // disable RTC timer
  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);

  /* Clear the EXTI's line Flag for RTC WakeUpTimer */
  __HAL_RTC_WAKEUPTIMER_EXTI_CLEAR_FLAG();

  // update status
  ovbcIsWorking = false;

  // update status for LPM
  ovbc_status = ovbc_idle_status;

  // switch OFF chip
  HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_RESET);
}


/*
*/
void ovbc_startup(void)
{
  // is working?
  if(ovbcIsWorking) return;

  // start up workings
  ovbc_init();
  ovbcIsWorking = true;

  // update status
  ovbc_status = ovbc_startup_status;

  // start state machine
  ovbc_stateMachine();
}

/*
*/
bool ovbc_isWorking(void)
{
  return ovbcIsWorking;
}

/*
*/
void ovbc_init(void)
{
  ovbcIsWorking = false;
  ovbc_VposEnTick = 0;
  ovbc_pulsingLoopNum = 0;


  ovbc_status = ovbc_inited_status;
}

