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
// record chip enable tick
static u32 ovbc_periodTick;
// record pulse num
static u32 ovbc_pulseNum;
// record pulse width(unit: ms)
static u32 ovbc_pulseWidth;

#ifndef LiuJH_DEBUG
// test only
#define OVBC_RTICK_BUF_SIZE   10
// record every R tick
static u32 ovbc_Rindex, ovbc_Rtick[OVBC_RTICK_BUF_SIZE];
// record every posEn and negEn start tick
static u32 ovbc_Pindex, ovbc_posTick[OVBC_RTICK_BUF_SIZE * 2];
static u32 ovbc_Nindex, ovbc_negTick[OVBC_RTICK_BUF_SIZE * 2];
#endif



/* private function define ******************************************/

static void ovbc_cbSmVposEn(void);


/*
  brief:
    1. NOW: we have finished pulsing work;
    2. stop tim6;
    3. 
*/
static void ovbc_smTim6Stop(void)
{
  // update status for LPM
  ovbc_status = ovbc_idle_status;

  // update status
  ovbcIsWorking = false;

  // stop tim6 working
  HAL_TIM_Base_Stop_IT(&htim6);
}

/*
  brief:
    1. NOW: we have send one pulse, start next pulse or game is over;
*/
static void ovbc_cbSmOnePulse(void)
{
  // pulse num counter
  ovbc_pulseNum--;

  // check all pulse is sending
  if(ovbc_pulseNum){
    // continue send next pulse
    ovbc_cbSmVposEn();
  }else{
    /* Game is OVER */

    // switch OFF chip
    HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_RESET);

    // reset VnegEn pin
    HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
    // Reset VposEn pin
    HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);

    // update status
    ovbc_status = ovbc_Tim6Stop_status;
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
static void ovbc_cbSmVnegEn(void)
{
#ifndef LiuJH_DEBUG
      // test only
      ovbc_negTick[ovbc_Nindex++] = HAL_GetTick();
#endif
  
    // Reset VposEn pin
    HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);
    // set VnegEn pin
    HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_SET);

    // we have send one pulse(high+low)
    ovbc_status = ovbc_onePulseEnd_status;

}

/*
  brief:
    1. Reset VnegEn pin; set VposEn pin;
    2. set timer count for VposEn pulsing time;
    3. update status;
    NOTICE:
      at first: reset vneg pin; and then set vpos pin; have some interval;
*/
static void ovbc_cbSmVposEn(void)
{
#ifndef LiuJH_DEBUG
    // test only
    ovbc_posTick[ovbc_Pindex++] = HAL_GetTick();
#endif

    // Reset VnegEn pin
    HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
    // set VposEn pin
    HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_SET);

    ovbc_status = ovbc_VnegEn_status;
}

/*
  brief:
    1. check chip enabling time is over?
    2. start pulsing;
    3. 
*/
static void ovbc_smChipEnabling(void)
{
  // test only
  if(HAL_GetTick() < ovbc_periodTick) return;

  /* start pulse interrupt */

  // update status
  ovbc_status = ovbc_VposEn_status;

  // start tim6
  HAL_TIM_Base_Start_IT(&htim6);
}

/*
  brief:
    1. Enable chip TI-TPS61096A(pin set);
    2. delay left time before VposEn;
    3. update status;
*/
static void ovbc_smChipEnable(void)
{
  if(HAL_GetTick() < ovbc_periodTick) return;

  // switch ON chip
  HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_SET);

  // init chip enable time is 15ms
  ovbc_periodTick = HAL_GetTick() + OVBC_CHIP_ENABLE_TIME - ovbc_pulseWidth;

  // update status
  ovbc_status = ovbc_chipEnabling_status;
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
  u32 tick = ecg_getRnTick();
  ppulse_config_typeDef ppulse = pulse_getConfig();
  u32 delay, Prescaler;

  // calculate time before pulsing
#ifdef LiuJH_DEBUG
  // get total delay time
  delay = ppulse->pulse_Rsvi_ms + ppulse->pulse_Rv_delay_ms;
#else
  // get total delay time
  delay = ecg_getRsvi() + ppulse->pulse_Rv_delay_ms;
#endif

	// check param
	if(HAL_GetTick() < tick){
		ovbc_shutdown();
		// dont send pulse
		return;
	}

	// get escape time(tick >= 0)
	tick = HAL_GetTick() - tick;

  // check period R peak point tick
  if(delay < OVBC_CHIP_ENABLE_TIME + tick){
    ovbc_shutdown();
    // dont send pulse
    return;
  }

	/* NOW: (delay - tick) >= OVBC_CHIP_ENABLE_TIME */

  // trim period time between curtick and Rtick
  delay -= tick;

  // calculate chip enable delay
  if(delay > OVBC_CHIP_ENABLE_TIME){
    // delay chip enable
    ovbc_periodTick = HAL_GetTick() + (delay - OVBC_CHIP_ENABLE_TIME);
  }else{
    // chip enable NOW!!! We have no time!!!
    ovbc_periodTick = HAL_GetTick();
  }

  // get pulse num and pulse width
  ovbc_pulseNum = ppulse->pulse_num;
  // get pulse width(ms)
  ovbc_pulseWidth = (ppulse->pulse_width + OVBC_PULSE_WIDTH_HALF_UNIT) / OVBC_PULSE_WIDTH_UNIT;

  // calculate prescaler of current width
  Prescaler = (ppulse->pulse_width * (FOVBC_TIM6_UP_PRESCA_1MS + 1) + OVBC_PULSE_WIDTH_HALF_UNIT) / OVBC_PULSE_WIDTH_UNIT - 1;

  // so we can reinit tim6 base on this Prescaler
  if(htim6.Init.Prescaler != Prescaler){
    htim6.Init.Prescaler = Prescaler;
    HAL_TIM_Base_Init(&htim6);
  }

  // update status
  ovbc_status = ovbc_chipEnable_status;
}


/* pulbic function define *******************************************/


/*
*/
void ovbc_cbStateMachine(void)
{
  switch(ovbc_status){
    case ovbc_VposEn_status:
      ovbc_cbSmVposEn();
      break;
    case ovbc_VnegEn_status:
      ovbc_cbSmVnegEn();
      break;
    case ovbc_onePulseEnd_status:
      ovbc_cbSmOnePulse();
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
void ovbc_stateMachine(void)
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
    case ovbc_chipEnabling_status:
      ovbc_smChipEnabling();
      break;
    case ovbc_Tim6Stop_status:
      ovbc_smTim6Stop();
      break;


    default:
      break;
  }
}

/* public function define *******************************************/



/*
  brief:
    1. when ble or pulse shut down, we will shut down;
*/
void ovbc_shutdown(void)
{
  // update status for LPM
  ovbc_status = ovbc_idle_status;

  // reset VposEn and VnegEn
  HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_RESET);

  // update status
  ovbcIsWorking = false;

  // switch OFF chip
  HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_RESET);

  // stop tim6 working
  HAL_TIM_Base_Stop_IT(&htim6);
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

#ifndef LiuJH_DEBUG
  // test only
//  if(ovbc_Rindex >= OVBC_RTICK_BUF_SIZE){
  if(ovbc_Rindex >= 1){
    ovbc_Rindex = 0;
    ovbc_Pindex = 0;
    ovbc_Nindex = 0;
  }
  ovbc_Rtick[ovbc_Rindex++] = ecg_getRnTick();
#endif

  // start state machine
//  ovbc_stateMachine();
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
  ovbc_periodTick = 0;

#ifndef LiuJH_DEBUG
  // test only
  ovbc_Rindex = ovbc_Pindex = ovbc_Nindex = 0;
#endif


  ovbc_status = ovbc_inited_status;
}

