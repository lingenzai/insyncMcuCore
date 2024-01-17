/*
 * fovbc.c
 *
 *  Created on: 2024年1月5日
 *      Author: Johnny
 */

#include "main.h"


/* variable define **************************************************/

static fovbc_status_typeDef fovbc_status;
// fovbc is working flag
static bool fovbcIsWorking;
// fovbc pulse width
static u32 fovbc_pulseCount = FOVBC_PULSE_DEFAULT_COUNT;
// delay = (5 - 0.1 * 2) * 16.384 = 
static u32 fovbc_pulseDelay = FOVBC_PULSE_DELAY_DEFAULT_COUNT;


/* private function define ******************************************/


static void fovbc_smVposEn(void);

/*
  brief:
    1. we have sent pulse once turn;
    2. check loop num; continue pulsing or stop;
    3. update status;
*/
static void fovbc_smPulsingLoop(void)
{
  // reset VposEn and VnegEn pin
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);

  // go into next status
  fovbc_status = fovbc_VposEn_status;

  // start RTC timer
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, fovbc_pulseDelay, RTC_WAKEUPCLOCK_RTCCLK_DIV2);
}

/*
  brief:
    1. Reset VposEn pin; set VnegEn pin;
    2. set timer count for VnegEn pulsing time;
    3. update status;
    NOTICE:
      at first: reset VposEn pin; and then set VnegEn pin; have some interval;
*/
static void fovbc_smVnegEn(void)
{
  // Reset VnegEn pin
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);

  // go into next status
  fovbc_status = fovbc_pulsingLoop_status;

  // set VposEn pin
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_SET);

#ifdef LiuJH_DEBUG
    // start TIM6 timer
    HAL_TIM_Base_Start_IT(&htim6);
#else
  // start RTC timer
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, fovbc_pulseCount, RTC_WAKEUPCLOCK_RTCCLK_DIV2);
#endif
}

/*
  brief:
    1. Reset VnegEn pin; set VposEn pin;
    2. set timer count for VposEn pulsing time;
    3. update status;
    NOTICE:
      at first: reset vneg pin; and then set vpos pin; have some interval;
*/
static void fovbc_smVposEn(void)
{
  // Reset VnegEn pin
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);

  // go into next status
  fovbc_status = fovbc_VnegEn_status;

  // set VposEn pin
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_SET);

#ifdef LiuJH_DEBUG
  // start TIM6 timer
  HAL_TIM_Base_Start_IT(&htim6);
#else
  // start RTC timer
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, fovbc_pulseCount, RTC_WAKEUPCLOCK_RTCCLK_DIV2);
#endif
}

/*
  brief:
    1. Enable chip TI-TPS61096A(pin set);
    2. delay left time before VposEn;
    3. update status;
*/
static void fovbc_smChipEnable(void)
{
  // Enable chip TI-TPS61096A(pin set)
  HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_SET);

  // update status
  fovbc_status = fovbc_VposEn_status;

  // start RTC timer
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, OVBC_CHIP_EBABLE_COUNT, RTC_WAKEUPCLOCK_RTCCLK_DIV2);
}

/*
  brief:
    1. startup chipEnable immediately;
*/
static void fovbc_smStartup(void)
{
  u32 wkcount;

  // delay 1 ms to enable chip(only for maintaining the original architecture)
  wkcount = OVBC_RTC_WKUP_COUNT_UNIT;

  // update status
  fovbc_status = fovbc_chipEnable_status;

  // we can start timer
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, wkcount, RTC_WAKEUPCLOCK_RTCCLK_DIV2);

  /* all other status process will be in RTC Timer Callback */
}


/* public function define *******************************************/


/*
  1. all status proc after ovbc_startup_status working in RTC Timer Callback;
  2. purpose: Holding precise time Prevent interruption by other tasks;
  3. NOTICE: please hand move this function;
*/
void fovbc_stateMachine(void)
{
  switch(fovbc_status){
    case fovbc_inited_status:
      // do noting
      break;
    case fovbc_startup_status:
      fovbc_smStartup();
      break;
    case fovbc_chipEnable_status:
      fovbc_smChipEnable();
      break;
    case fovbc_VposEn_status:
      fovbc_smVposEn();
      break;
    case fovbc_VnegEn_status:
      fovbc_smVnegEn();
      break;
    case fovbc_pulsingLoop_status:
      fovbc_smPulsingLoop();
      break;


    default:
      break;
  }
}


/**
  * @brief  Period elapsed callback in non-blocking mode
  * @param  htim TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
#ifdef LiuJH_DEBUG
  HAL_TIM_Base_Stop_IT(&htim6);
#endif
  fovbc_stateMachine();
}


/*
  brief:
    1. when ble or pulse shut down, we will shut down;
*/
void fovbc_shutdown(void)
{
  // update status for LPM
  fovbc_status = fovbc_idle_status;
  
#ifdef LiuJH_DEBUG
  HAL_TIM_Base_Stop_IT(&htim6);
#endif

  // disable RTC timer
  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);

  /* Clear the EXTI's line Flag for RTC WakeUpTimer */
  __HAL_RTC_WAKEUPTIMER_EXTI_CLEAR_FLAG();

  // reset VposEn and VnegEn pin
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);

  // update status
  fovbcIsWorking = false;

  // switch OFF chip
  HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_RESET);
}

/*
*/
void fovbc_startup(void)
{
  // is working?
  if(fovbcIsWorking) return;

  // start up workings
  fovbc_init();
  fovbcIsWorking = true;

#ifdef LiuJH_DEBUG
  // update status
  fovbc_status = fovbc_chipEnable_status;
#else
  // update status
  fovbc_status = fovbc_startup_status;
#endif

  // start state machine
  fovbc_stateMachine();
}

/*
  brief:
    1. _width is [0, 255], and the unit is 10us;
    2. We want 0.1ms - 1ms width, that is: value is 10 - 100 of _width;
    3. so ths rtc timer counter is: _width * 16.384 / 100;
    4. NOTICE: To achieve maximum accuracy, used rounding method;
    5. So the rtc timer counter is: (_width * 16.384 + 50) / 100, that is:
      (_width * 1024 + 3125) / 6250
*/
void fovbc_setPulseWidth(u8 _width)
{
  if(fovbcIsWorking) return;

  if(_width < FOVBC_PULSE_WIDTH_MIN)
    fovbc_pulseCount = FOVBC_PULSE_WIDTH_MIN;
  else if(_width > FOVBC_PULSE_WIDTH_MAX)
    fovbc_pulseCount = FOVBC_PULSE_WIDTH_MAX;
  else
    fovbc_pulseCount = _width;

#ifdef LiuJH_DEBUG
  // Using RTC timer as delay timer
  fovbc_pulseDelay = FOVBC_200HZ_COUNT - (fovbc_pulseCount << 10) / 3125 - 5;

  /*
    For TIM6:
      1. clock is 2097KHz; div is 1; so counter is 2097 per 1 ms;
      2. So counter is: _width * 2097 / 100;
  */
  if(fovbc_pulseCount == FOVBC_PULSE_WIDTH_MAX)
//    fovbc_pulseCount = fovbc_pulseCount * 2097 / 100;
    fovbc_pulseCount = 2097;
  else{
    fovbc_pulseCount = 30;
    fovbc_pulseDelay = 65;
  }

  // reinit tim6
  htim6.Init.Period = fovbc_pulseCount;
  HAL_TIM_Base_Init(&htim6);
#else
  // set counter(rounding)
  fovbc_pulseCount = ((fovbc_pulseCount << 10) + 3125) / 6250;

  fovbc_pulseDelay = FOVBC_200HZ_COUNT - (fovbc_pulseCount << 1);
#endif
}

/*
*/
bool fovbc_isWorking(void)
{
  return fovbcIsWorking;
}


void fovbc_init(void)
{
  fovbcIsWorking = false;


  fovbc_status = fovbc_inited_status;
}






