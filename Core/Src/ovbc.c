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
//static u32 ovbc_periodTick;
// prescaler of chip enable delay
static u32 ovbc_timPreWillVponEn;
// prescaler of chip enabling period
static u32 ovbc_timPreBeingVponEn;
// prescaler of pulse width
static u32 ovbc_timPrePulse;
// record pulse num
static u32 ovbc_pulseNum;
// record pulse width(unit: ms)
//static u32 ovbc_pulseWidth;

#ifdef LiuJH_OVBC
// test only
#define OVBC_RTICK_BUF_SIZE   10
// record every R tick
static u32 ovbc_Rindex, ovbc_Rtick[OVBC_RTICK_BUF_SIZE];
// record every posEn and negEn start tick
static u32 ovbc_Pindex, ovbc_posTick[OVBC_RTICK_BUF_SIZE * 2];
static u32 ovbc_Nindex, ovbc_negTick[OVBC_RTICK_BUF_SIZE * 2];
#endif



/* private function define ******************************************/


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
    /* continue send next pulse */

#ifdef LiuJH_OVBC
		// test only
		ovbc_posTick[ovbc_Pindex++] = HAL_GetTick();
#endif

		// Reset VnegEn pin
		HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
		// set VposEn pin
		HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_SET);

    // update status
    ovbc_status = ovbc_VnegEn_status;
  }else{
    /* Game is OVER */
		ovbc_shutdown();
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
#ifdef LiuJH_OVBC
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
#ifdef LiuJH_OVBC
    // test only
    ovbc_posTick[ovbc_Pindex++] = HAL_GetTick();
#endif

    // Reset VnegEn pin
    HAL_GPIO_WritePin(CCM_PIN33_VNEG_EN_GPIO_Port, CCM_PIN33_VNEG_EN_Pin, GPIO_PIN_RESET);
    // set VposEn pin
    HAL_GPIO_WritePin(CCM_PIN32_VPOS_EN_GPIO_Port, CCM_PIN32_VPOS_EN_Pin, GPIO_PIN_SET);

		// update tim6 prescaler and reinit tim6 for pulse
		htim6.Init.Prescaler = ovbc_timPrePulse;
		HAL_TIM_Base_Init(&htim6);

		// update status
		ovbc_status = ovbc_VPonEnabling_status;
		
		// startup tim6 timer
		HAL_TIM_Base_Start_IT(&htim6);
}

/*
  brief:
    1. Enable chip TI-TPS61096A(pin set);
    2. delay left time before VposEn;
    3. update status;
*/
static void ovbc_cbSmChipEnable(void)
{
  // switch ON chip
  HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_SET);

	// update tim6 prescaler for chip enable period
  htim6.Init.Prescaler = ovbc_timPreBeingVponEn;
  HAL_TIM_Base_Init(&htim6);

  // update status
  ovbc_status = ovbc_chipEnabling_status;

	// startup tim6 timer
	HAL_TIM_Base_Start_IT(&htim6);
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
  u32 delay;

  // calculate time before pulsing
#ifdef LiuJH_DEBUG
  // get total delay time
  delay = ppulse->pulse_Rsvi_ms + ppulse->pulse_Rv_delay_ms;	//  - OVBC_CODE_RUNNING_WEIGHT;	// 
#else
  // get total delay time
  delay = ecg_getRsvi() + ppulse->pulse_Rv_delay_ms;
#endif

  // get pulse num and pulse width
  ovbc_pulseNum = ppulse->pulse_num;

  // calculate prescaler of current width
  ovbc_timPrePulse = (ppulse->pulse_width * (FOVBC_TIM6_UP_PRESCA_1MS + 1) + OVBC_PULSE_WIDTH_HALF_UNIT) / OVBC_PULSE_WIDTH_UNIT - 1;

	// calculate prescaler of chip enable period
#ifndef LiuJH_DEBUG
	// test only
	ovbc_timPreBeingVponEn = (OVBC_CHIP_ENABLE_TIME - OVBC_CODE_RUNNING_WEIGHT) * (FOVBC_TIM6_UP_PRESCA_1MS + 1) - 1;
#else
	ovbc_timPreBeingVponEn = OVBC_CHIP_ENABLE_TIME * (FOVBC_TIM6_UP_PRESCA_1MS + 1) - 1;
#endif

	// check param
	if(HAL_GetTick() < tick
		// time is over?
		|| delay + tick < HAL_GetTick() + OVBC_CHIP_ENABLE_TIME){
		ovbc_shutdown();
		// dont send pulse
		return;
	}

	// get escape time(tick >= 0)
	tick = HAL_GetTick() - tick;

	/* NOW: (delay - tick) >= OVBC_CHIP_ENABLE_TIME */

  // trim period time between curtick and Rtick
  delay -= tick;

	/* NOW: @delay is total time we have before pulsing */

  // calculate chip enable delay
  if(delay > OVBC_CHIP_ENABLE_TIME){
		// delay chip enable
		delay -= OVBC_CHIP_ENABLE_TIME;

		// wait some times before chip enable
		ovbc_status = ovbc_willChipEnable_status;
  }else{
    // chip enable NOW!!! We have no time!!!

		// this value is nouse
		delay -= OVBC_CHIP_ENABLE_TIME;

		ovbc_status = ovbc_chipEnable_status;
  }
	// calculate time prescaler
	ovbc_timPreWillVponEn = delay * (FOVBC_TIM6_UP_PRESCA_1MS + 1) - 1;

  // so we can reinit tim6 base on this Prescaler
  htim6.Init.Prescaler = ovbc_timPreWillVponEn;
  HAL_TIM_Base_Init(&htim6);

	// startup tim6 timer
	HAL_TIM_Base_Start_IT(&htim6);
}


/* pulbic function define *******************************************/


/*
*/
void ovbc_cbStateMachine(void)
{
  switch(ovbc_status){
		case ovbc_willChipEnable_status:
			// only update status
			ovbc_status = ovbc_chipEnable_status;
			break;
		case ovbc_chipEnable_status:
			ovbc_cbSmChipEnable();
			break;
		case ovbc_chipEnabling_status:
			// only update status
			ovbc_status = ovbc_VposEn_status;
			break;
    case ovbc_VposEn_status:
      ovbc_cbSmVposEn();
      break;
		case ovbc_VPonEnabling_status:
			// only update status
			ovbc_status = ovbc_VnegEn_status;
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
static void ovbc_stateMachine(void)
{
  switch(ovbc_status){
    case ovbc_inited_status:
      // do noting
      break;
    case ovbc_startup_status:
      ovbc_smStartup();
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

#ifdef LiuJH_OVBC
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

#ifndef LiuJH_DEBUG
  // test only
  ovbc_Rindex = ovbc_Pindex = ovbc_Nindex = 0;
#endif


  ovbc_status = ovbc_inited_status;
}

