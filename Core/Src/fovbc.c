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
// pulse work time(2s)
static u32 fovbc_pulseTick;
// chip enable need time(15ms)
static u32 fovbc_chipEnTick;
// delay time after high+low pulse per pulse(total is 300ms)
static u32 fovbc_loopTick;
// record tim6 prescaler and counter period of cure pulse
static u32 fovbc_oldPrescaler;

#ifndef LiuJH_DEBUG
// test only
static u32 fovbc_index, fovbc_stick[8], fovbc_etick[8];
#endif

/* private function define ******************************************/


/*
  brief:
    1. NOW: we have send a pulse, start delay;
*/
static void fovbc_smPulseDelay(void)
{
  // pulsing work is over?
  if(HAL_GetTick() < fovbc_pulseTick){

    // chip enable time is coming?
    if(HAL_GetTick() >= fovbc_loopTick){
      // start pulse interrupt
      fovbc_loopTick = HAL_GetTick() + FOVBC_PULSETIME - FOVBC_PULSEWIDTH;

      // update status
      fovbc_status = fovbc_VposEn_status;

      // start tim6
      HAL_TIM_Base_Start_IT(&htim6);
    }

  }else{
    // Game is over
    fovbc_shutdown();
  }
}

/*
  brief:
    1. NOW: we have send a pulse, start delay;
    2. stop tim6;
    3. 
*/
static void fovbc_smTim6Stop(void)
{
  // return chipEnable status for next pulse
  fovbc_status = fovbc_pulseDelay_status;

  HAL_TIM_Base_Stop_IT(&htim6);
}

/*
  brief:
    1. NOW: we have send one pulse, start delay;
*/
static void fovbc_cbSmOnePulseEnd(void)
{
  // reset VnegEn pin
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
  // Reset VposEn pin
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);

  // we will stop this timer for delay time
  fovbc_status = fovbc_Tim6Stop_status;

  // Can we stop tim6 in tim6 interrupt??????
//  HAL_TIM_Base_Stop_IT(&htim6);
}

/*
  brief:
    1. Reset VposEn pin; set VnegEn pin;
    2. set timer count for VnegEn pulsing time;
    3. update status;
    NOTICE:
      at first: reset VposEn pin; and then set VnegEn pin; have some interval;
*/
static void fovbc_cbSmVnegEn(void)
{
#ifndef LiuJH_DEBUG
  // test only
  fovbc_etick[fovbc_index] = HAL_GetTick();
#endif

  // Reset VposEn pin
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);
  // set VnegEn pin
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_SET);

  // we have send one pulse(high+low)
  fovbc_status = fovbc_onePulseEnd_status;
}

/*
  brief:
    1. Reset VnegEn pin; set VposEn pin;
    2. set timer count for VposEn pulsing time;
    3. update status;
    NOTICE:
      at first: reset vneg pin; and then set vpos pin; have some interval;
*/
static void fovbc_cbSmVposEn(void)
{
#ifndef LiuJH_DEBUG
  // test only
  fovbc_stick[fovbc_index++] = HAL_GetTick();
#endif

  // Reset VnegEn pin
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
  // set VposEn pin
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_SET);
  
  fovbc_status = fovbc_VnegEn_status;
}

/*
  brief:
    1. Enable chip TI-TPS61096A(pin set);
    2. delay left time before VposEn;
    3. update status;
*/
static void fovbc_smChipEnabling(void)
{
  // test only
  if(HAL_GetTick() < fovbc_chipEnTick) return;

  // start pulse interrupt
  fovbc_loopTick = HAL_GetTick() + FOVBC_PULSETIME - FOVBC_PULSEWIDTH;

  // update status
  fovbc_status = fovbc_VposEn_status;

  // start tim6
  HAL_TIM_Base_Start_IT(&htim6);
}


/* public function define *******************************************/


/*
*/
void fovbc_cbStateMachine(void)
{
  switch(fovbc_status){
    case fovbc_VposEn_status:
      fovbc_cbSmVposEn();
      break;
    case fovbc_VnegEn_status:
      fovbc_cbSmVnegEn();
      break;
    case fovbc_onePulseEnd_status:
      fovbc_cbSmOnePulseEnd();
      break;

    default:
      break;
  }
}

/*
  1. all status proc after ovbc_startup_status working in RTC Timer Callback;
  2. purpose: Holding precise time Prevent interruption by other tasks;
  3. NOTICE: please hand move this function;
*/
void fovbc_stateMachine(void)
{
  switch(fovbc_status){
    case fovbc_chipEnabling_status:
      fovbc_smChipEnabling();
      break;
    case fovbc_Tim6Stop_status:
      fovbc_smTim6Stop();
      break;
    case fovbc_pulseDelay_status:
      fovbc_smPulseDelay();
      break;


    default:
      break;
  }
}

/*
  brief:
    1. when ble or pulse shut down, we will shut down;
    2. NOW: the rtc timer or tim6 timer is stop;
*/
void fovbc_shutdown(void)
{
  // reset VposEn and VnegEn pin
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);

  // switch OFF chip
  HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_RESET);

  // restore tim6 config of cure pulse
  htim6.Init.Prescaler = fovbc_oldPrescaler;
  HAL_TIM_Base_Init(&htim6);

  // update status
  fovbcIsWorking = false;
  // update status for LPM
  fovbc_status = fovbc_idle_status;
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

  // init tim6 overflow time is 1ms
  if(htim6.Init.Prescaler != FOVBC_TIM6_UP_PRESCA){
    // temp save old prescaler and counter period
    fovbc_oldPrescaler = htim6.Init.Prescaler;

    // update new config
    htim6.Init.Prescaler = FOVBC_TIM6_UP_PRESCA;
    htim6.Init.Period = FOVBC_TIM6_UP_PERIOD;
    HAL_TIM_Base_Init(&htim6);
  }

  // init pulse time is 2s
  fovbc_pulseTick = HAL_GetTick() + FPULSE_FOVBC_TICK;
  // init chip enable time is 15ms
  fovbc_chipEnTick = HAL_GetTick() + OVBC_CHIP_ENABLE_TIME - FOVBC_PULSEWIDTH;

  // switch ON chip
  HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_SET);

  // update status
  fovbc_status = fovbc_chipEnabling_status;
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

#ifndef LiuJH_DEBUG
  if(_width < FOVBC_PULSE_WIDTH_MIN)
    fovbc_pulseCount = FOVBC_PULSE_WIDTH_MIN;
  else if(_width > FOVBC_PULSE_WIDTH_MAX)
    fovbc_pulseCount = FOVBC_PULSE_WIDTH_MAX;
  else
    fovbc_pulseCount = _width;

#ifndef LiuJH_DEBUG
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
/*
  if(fovbc_pulseCount == FOVBC_PULSE_WIDTH_MAX)
    fovbc_pulseCount = 160;
  else
*/
    // set counter(rounding); 3ms
    fovbc_pulseCount = 48;  // 44;

  // 200bpm is 300ms per circle; 300 - 7 = 293;
  // 292.5ms * 16.384 = 4792.34
  fovbc_pulseDelay = FOVBC_PULSE_DELAY_DEFAULT_COUNT;  // 44;
#endif
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

  fovbc_oldPrescaler = FOVBC_TIM6_UP_PRESCA_5MS;

#ifndef LiuJH_DEBUG
  // test only
  fovbc_index = 0;
#endif

  fovbc_status = fovbc_inited_status;
}

