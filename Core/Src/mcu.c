/*
 * mcuStateMachine.c
 *
 *  Created on: 2023年10月11日
 *      Author: Johnny
 */

#include "main.h"

/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ private var define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

// mcu is wakeup from LPM or power on and other reasons
mcu_resetFlag_typeDef mcu_rstFlag;
// Mcu data, time
RTC_DateTypeDef mcu_date = {0};
RTC_TimeTypeDef mcu_time = {0};

// accel redo tick
static u32 mcu_motionTick;
// accel motion timeout config
static mcu_MotionConfig_typeDef mcu_motionCfg;

static mcu_spiStatus_typeDef mcu_spiStatus;

/*
Data Format:
       Data：04 - Vout output low 
             05 - Vout output high 
             06 - Vout float status*/
static mcu_Voutset_typeDef mcu_Voutset;


//static u32 mcu_tick = 0;

static mcu_baseData_typeDef mcu_baseData;
static mcu_bpmCalmMax_typeDef mcu_bpmCalmMax;


/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv private var define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ private function define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

//static void mcu_enterStandbyMode(void);


/*
  brief:
   Init about Standby-RTC wake up for addition. 
   1. Clear standby mode flag.
   2. clear all wakeup flag.
   3. active wakeup flag we want.
*/
static void mcu_LpmInit(void)
{
  /* Enable Ultra low power mode */
  HAL_PWREx_EnableUltraLowPower();
  
  /* Enable the fast wake up from Ultra low power mode */
  HAL_PWREx_EnableFastWakeUp();

  /* Check and handle if the system was resumed from StandBy mode */
  // clear standby flag
//  if(__HAL_PWR_GET_FLAG(PWR_FLAG_SB))
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
  // Clear wakeup flags
//  if(__HAL_PWR_GET_FLAG(PWR_FLAG_WU))
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

  /* Clear the EXTI's line Flag for RTC WakeUpTimer */
  __HAL_RTC_WAKEUPTIMER_EXTI_CLEAR_FLAG();
  /* Clear the Wakeup timer interrupt pending bit */
  __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);

  // clear all RCC_CSR reset status flags
  __HAL_RCC_CLEAR_RESET_FLAGS();

  /*Disable all used wakeup sources: Pin1(PA.0)*/
  HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN1);

  /*Re-enable all used wakeup sources: Pin1(PA.0)*/
  HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1);

#ifdef LiuJH_DEBUG
  // Enables the Debug Module during STANDBY mode
  HAL_DBGMCU_EnableDBGStandbyMode();
#endif
}

/*
  brief:
    1. 
*/
static void mcu_restoreDatetime(void)
{
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  // need restore datetime?
  if(mcu_rstFlag == mcu_rstFlag_others) return;

  // restore time
  HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
  sTime.Hours = mcu_time.Hours;
  sTime.Minutes = mcu_time.Minutes;
#ifdef LiuJH_DEBUG
  // test: use 1Hz 16bit RTC timer, added 1s each wackeup, so:
  if(mcu_time.Seconds)
    mcu_time.Seconds--;
#endif
  sTime.Seconds = mcu_time.Seconds;
  HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);

  // restore date
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
  sDate.Year = mcu_date.Year;
  sDate.Month = mcu_date.Month;
  sDate.Date = mcu_date.Date;
  sDate.WeekDay = mcu_date.WeekDay;
  HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}

/*
  brief:
    1. restore key value from eeprom;
    2. DONT update mcu var, store in EE struct;
    3. 
*/
static void mcu_restoreKValue(void)
{
  ee_restoreKeyValue();
}

/*
  brief:
    1. check WKUP1 pin, if it is high level, return true.
    2. Check multiple times to ensure stability.
  NOTE:
    this function use time >= 10ms
*/
static bool mcu_isMagnetWakeup(void)
{
#ifndef LiuJH_DEBUG
  // only test
  return true;
//  return false;
#else
  u8 count = 0;
  u32 tick = HAL_GetTick();

  // circle 10 ms ensure it.
  while(HAL_GetTick() < tick + 10){
    /*
      this pin is pulled down(LOW) default, accept rising edge to wakeup MCU,
      so if HIGH level, must be magnet exist;
    */
    if(HAL_GPIO_ReadPin(CCM_PIN10_WKUP_INTR_GPIO_Port, CCM_PIN10_WKUP_INTR_Pin))
      count++;

    HAL_Delay(1);
  }

  return (count > 2);
#endif
}

/*
  brief:
    1. judge magnet at first;
    2. 
*/
static void mcu_judgeWkupReason(void)
{
  // we have judged wakeup reason?
  if(mcu_rstFlag != mcu_rstFlag_wakeup) return;

  /*
    Is WKUP1 PIN wakeup(magnet)?
    I can not find this status flag, so only detect the magnet(high level).
  */
  if(mcu_isMagnetWakeup()){
    mcu_rstFlag = mcu_rstFlag_MagnetHall;
//    HAL_GPIO_WritePin(CCM_PIN30_TP7_LED_GPIO_Port, CCM_PIN30_TP7_LED_Pin, GPIO_PIN_SET);
  }else{
    mcu_rstFlag = mcu_rstFlag_RtcTimer;
//    HAL_GPIO_WritePin(CCM_PIN31_TP8_LED_GPIO_Port, CCM_PIN31_TP8_LED_Pin, GPIO_PIN_SET);
  }
}

/*
  brief:
    1. BLE, pulse, charging, accel...all devices is no working;
    2. 
*/
static bool mcu_noDeviceWorking(void)
{
  if(ble_isWorking()
    || pulse_isWorking()
    || fpulse_isWorking()
    // flash need sometimes goto LPM mode, so waiting it
//    || !flash_isEnterLpm()
//    || wlc_isWorking()
//    || accel_isworking()
//    || adc_isWorking()
    ){
    // set lpm tick
//    mcu_lpmTick = HAL_GetTick();

    return false;
  }

  // no device is working, so we can enter LPM
  return true;
}

/*
  brief:
    1. We always monitor magnet for resetting RSL10(avoid mcu dont enter into LPM);
    2. conditions:
      a. Mcu is running for sometimes(ex. 10s);
      b. RSL10 MUST is LPM mode;
      c. We have found magnet for 1 second;
    3. reset MCU;
*/
static void mcu_monitorMegnet(void)
{
  // exist magnet flag
  static bool exist = false;
  // the period tick of exist magnet
  static u32 ptick;

  // Mcu is running for sometimes?
  if(HAL_GetTick() < MCU_TICK_MIN_FOR_RESET){
    exist = false;
    return;
  }

  // RSL10 is not LPM mode?
  if(!ble_Rsl10ChipIsLpm()){
    exist = false;
    return;
  }

  // check magnet for 1 second
  if(HAL_GPIO_ReadPin(CCM_PIN10_WKUP_INTR_GPIO_Port, CCM_PIN10_WKUP_INTR_Pin)){
    /* exist magnet */

    // the start time of exist magnet?
    if(exist){
      // check period somtimes
      if(HAL_GetTick() >= ptick){
        exist = false;
        // reset MCU and RSL10
        HAL_NVIC_SystemReset();
      }
    }else{
      // the first time of checking out magnet is exist
      exist = true;
      ptick = HAL_GetTick() + MCU_EXIST_MAGNET_PERIOD_TICK;
    }
  }else{
    /* no exist magnet */
    exist = false;
  }
}

/*
*/
static void mcu_redoMotionCheck(void)
{
    if(HAL_GetTick() < mcu_motionTick) return;
  
    accel_startup();

#ifndef LiuJH_DEBUG
    // test only
    mcu_motionTick = HAL_GetTick() + TIMEOUT_5S;
#else
    // set next startup timetick
    mcu_motionTick = HAL_GetTick() + mcu_motionCfg.mcu_motionPeriod * 1000;
#endif
}

/*
*/
static void mcu_workDriven(void)
{
  // check ble and pulse is all stop working?
  if(!ble_isWorking() && !pulse_isWorking() && !fovbc_isWorking()){
    // stop adc and ecg and accel working
    adc_stop();
    accel_stop();

    return;
  }

  /*
    NOW: ble or(and) pulse is working
  */

  // redo motion check
  mcu_redoMotionCheck();

  // always monitor megnet for resetting RSL10(avoid mcu dont enter into LPM)
  mcu_monitorMegnet();

  // 
}

/*
  brief:
    1. store all of important data into EEPROM;
    2. 
*/
static void mcu_storeKeyValue(void)
{
#ifdef LiuJH_EE
	// test only: entering into LPM may failed after writing EE
  ee_storeKeyValue();
#endif

  // store pulse total number into eeprom
  ee_readOrWriteKeyValue(ee_kv_baseData, false);
}

/*
  brief:
    1. set all not used pin into analog state.
*/
//static void GPIO_AnalogInput_config(void)
static void mcu_setGpioFloatingInput(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  // Analog Mode
//  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  // PB0 is NRESET pin of RSL10, must keep high level. So update Mode.
  // Input Floating Mode
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  // keep PA13 and PA14 for debugging, and keep PA0 for wakeup;
  GPIO_InitStruct.Pin = GPIO_PIN_All & (~CCM_PIN10_WKUP_INTR_Pin) & (~GPIO_PIN_13) & (~GPIO_PIN_14) & (~PIN30_PA9_WLC38_ON_Pin);
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  // this pin MUST set, otherwise STWLC38 is very consume power
  GPIO_InitStruct.Pin = GPIO_PIN_All & (~CCM_PIN45_VOUT_SET_Pin) & (~CCM_PIN21_BOOST_ON_Pin) & (~CCM_PIN25_MEM_CS_Pin);
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  //__HAL_RCC_GPIOA_CLK_DISABLE();
  //__HAL_RCC_GPIOB_CLK_DISABLE();

  // Johnny: Need keep RSl10_RST state??
}

/*
  brief:
    1. Wakeup timer max count value is 65535, unit is second;
      it is 18.2 hours;
    2. pulse wakeup time is 0:30-3:30, 3 hours;
    3. so we MUST wakeup twice every day;
    4. 
*/
static u32 mcu_calWakeupSecond(void)
{
	ppulse_config_typeDef p = pulse_getConfig();
	// init seconds of 18 hours
	u32 sec = RTC_WKUP_TIMER_COUNT_MAX;
	// current minute and config minute
	u32 curm, conm;
	int i;

	// no valid time in pulse config buffer?
	if(p->pulse_timeNum == 0) return sec;

	// get current time
	HAL_RTC_GetTime(&hrtc, &mcu_time, RTC_FORMAT_BIN);

	// convert current time to minute(add 1 as weight)
	curm = mcu_time.Hours * 60 + mcu_time.Minutes + 1;

	// we have only one valid pulse config time?
	if(p->pulse_timeNum == 1){
		// get the only one config time(unit: minute)
		conm = p->pulse_timeBuf[0].startTime;
	}else{
		// compare with pulse config start_time
		for(i = 0; i < p->pulse_timeNum; i++){
			if(curm < p->pulse_timeBuf[i].startTime){
				// got it
				break;
			}
		}

		// i is index we want
		if(i >= p->pulse_timeNum){
			i = 0;
		}

		// get config time
		conm = p->pulse_timeBuf[i].startTime;
	}

	// compare curm and conm
	if(curm >= conm){
		conm += MCU_MAX_MINUTE_EACH_DAY;
	}

	// get wakeup time (unit: second)
	sec = conm * 60 - ((mcu_time.Hours * 60 + mcu_time.Minutes) * 60 + mcu_time.Seconds);

  // the max counter is 0xFFFF(unit: second) in 16bits wakeup mode, is: 18 hours
  if(sec > RTC_WKUP_TIMER_COUNT_MAX){
    // we must wakeup two times(the first time is invalid wakeup)
    sec >>= 1;
    sec &= 0xFFFF;
  }

	return sec;
}

/*
  brief:
    1. Wakeup timer max count value is 65535, unit is second;
      it is 18.2 hours;
    2. pulse wakeup time is 0:30-3:30, 3 hours;
    3. so we MUST wakeup twice every day;
    4. 
*/
static void mcu_setLpmWakeupTimer(void)
{
  u32 sec;

	// calculate wakeup time(unit: second)
	sec = mcu_calWakeupSecond();

#ifndef LiuJH_DEBUG
  // set 8s timer; ONLY test
//  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, RTC_WAKEUP_TIME_5S, RTC_WAKEUPCLOCK_RTCCLK_DIV16);
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, 8, RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
#else
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, sec, RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
#endif
}

/*
  brief:
    1. control all device enter LPM;
    2. Special: RSL10 notify 3 times per 100ms;
*/
static void mcu_enterStandbyMode(void)
{
#ifndef LiuJH_DEBUG
  // test only for LPM
	mcu_setLpmWakeupTimer();
#else

  /* NOW: we can go into LPM */

  // store our keyvalue in EEPROM (spi flash)
  mcu_storeKeyValue();

  // set all not used pin into analog state
  mcu_setGpioFloatingInput();

  /*
    NOW: we'll set wakeup timer.
    1. pulse wakeup time is 0:30-3:30;
    2. 
  */
  mcu_setLpmWakeupTimer();

  /*Enter the Standby mode*/
  HAL_PWR_EnterSTANDBYMode();
#endif
}

static void mcu_init(void)
{
  // restore mcu date and time
  mcu_restoreDatetime();

  // restore Key Value
  mcu_restoreKValue();

  // judge wakeup reason
  // HAVE 10ms delay
  mcu_judgeWkupReason();

  // Init all of LPM
  mcu_LpmInit();

  // adc symbiosis ecg and wpr
  adc_init();
  ecg_init();
  wpr_init();
  

  mcu_motionTick = 0;
}


/*
*/
static void mcu_deassertedCS(u8 _devID)
{
  switch(_devID){
    case MCU_SPI_BLE:
      BLE_CS_DEASSERTED;
      break;
    case MCU_SPI_FLASH:
      FLASH_CS_DEASSERTED;
      break;

    default:
      BLE_CS_DEASSERTED;
      FLASH_CS_DEASSERTED;
      break;
  }
}

/*
*/
static void mcu_assertedCS(u8 _devID)
{
  switch(_devID){
    case MCU_SPI_BLE:
      BLE_CS_ASSERTED;
      break;
    case MCU_SPI_FLASH:
      FLASH_CS_ASSERTED;
      break;

    default:
      break;
  }
}

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv private function define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/


/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ public function define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


/*
  1. release spi using for others device use can use it;
*/
void mcu_unlockSpi(void)
{
  mcu_spiStatus = mcu_spi_idle_status;
}

/*
  1. check and set spi using flag;
  2. if it is using return false;
*/
bool mcu_tryLockSpi(u8 _devID)
{
  if(mcu_spiStatus != mcu_spi_idle_status)
    return false;

  mcu_spiStatus = _devID;
  return true;
}

/*
  brief:
    1. 
*/
bool mcu_spiRxUnblocked(u8 _devID, u8 *_prx, u16 _len)
{
  bool ret = false;

  // spi is working?
  if(mcu_spiStatus ^ mcu_spi_idle_status)
    return ret;

  // check param valid
  if((_devID != MCU_SPI_BLE && _devID != MCU_SPI_FLASH) || !_prx || !_len)
    return ret;

  // set spi working flag
  mcu_spiStatus = _devID;

  // receive data in interrupt mode through spi
  mcu_assertedCS(_devID);
  if(HAL_SPI_Receive_IT(&hspi2, _prx, _len) == HAL_OK)
    ret = true;

  return ret;

  // we will get callback in HAL_SPI_RxCpltCallback function

}

/*
*/
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  mcu_deassertedCS(mcu_spiStatus);

  // spi enter idle status for next using
  mcu_spiStatus = mcu_spi_idle_status;
}

/*
  brief:
    1. 
*/
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  mcu_spiStatus = mcu_spi_idle_status;
  mcu_deassertedCS(mcu_spiStatus);
}

/**
  * @brief  Period elapsed callback in non-blocking mode
  * @param  htim TIM handle
  * @retval None
  */
void mcu_TIM6_periodElapsedCB(TIM_HandleTypeDef *htim)
{
  if(fovbc_isWorking())
    fovbc_cbStateMachine();
  else if(ovbc_isWorking())
    ovbc_cbStateMachine();
}

/*
  brief:
    1. spi status is idle;
    2. when spi TxRx or Tx or Rx working finished, enter into idle status;
    3. others device can use spi device when spi is idle;
*/
bool mcu_spiIsReady(void)
{
  return (!(mcu_spiStatus ^ mcu_spi_idle_status));
}

#ifndef LiuJH_DEBUG
/*
  brief:
    1. interrupt interval: (1/2097) * 4096 * 8 * 64 / 1000 = 1 second;
    2. 
*/
void HAL_WWDG_EarlyWakeupCallback(WWDG_HandleTypeDef *hwwdg)
{
  HAL_WWDG_Refresh(hwwdg);
}
#endif

/*
  brief:
    1. all mcu status process is here.
    2. this function working in main while(1);
    3. 
*/
void mcu_allStateMachine(void)
{
  // ble(RSL10) state machine
  ble_stateMachine();

  // pulse state machine process
  pulse_StateMachine();

  // ACCEL state machine process
  accel_stateMachine();

  // adc state machine process
  adc_stateMachine();

  // ecg state machine process
  ecg_stateMachine();

  // wpr state machine process
  wpr_stateMachine();

  // flash state machine process
  flash_stateMachine();

  // fpulse state machine process
  fpulse_stateMachine();
  fovbc_stateMachine();

  // mcu other work driven
  mcu_workDriven();

  // we can go into LPM?
  if(mcu_noDeviceWorking()){
    mcu_enterStandbyMode();
  }
}


/*
  because LiuJH_DEBUG defined, so:
    "ifndef": is NO sleep mode;
    "ifdef":  is sleep mode;
*/
bool mcu_noSleepTest(void)
{
#ifdef LiuJH_DEBUG
  return false;
#else
  // Test only
  return true;
#endif
}

/*
*/
bool mcu_isBleRstMode(void)
{
  if(mcu_rstFlag == mcu_rstFlag_MagnetHall)
    return true;

  return false;
}

/*
*/
bool mcu_fovbcIsWorking(void)
{
  return fovbc_isWorking();
}

/*
*/
bool mcu_ovbcIsWorking(void)
{
  return ovbc_isWorking();
}

/*
*/
void mcu_adcStartup(void)
{
  adc_startup();
}

/*
*/
bool mcu_pulseBlePulsingIsOn(void)
{
  return pulse_blePulsingIsOn();
}

/*
*/
u8 mcu_accelGetMotionState(void)
{
  return accel_getMotionState();
}

/*
*/
u8 mcu_wprGetBattPercent(void)
{
  return wpr_getBattPercent();
}

/*
*/
void mcu_pulseBleConfigPulseOn(bool _isOn)
{
  pulse_bleConfigPulseOn(_isOn);
}

/*
*/
void mcu_wprSetChargeSwitch(bool _isOn)
{
  wpr_setChargeSwitch(_isOn);
}

/*
*/
bool mcu_wprIsCharging(void)
{
  return wpr_isCharging();
}

/*
*/
bool mcu_fpulseIsWorking(void)
{
  return fpulse_isWorking();
}

/*
*/
ppulse_config_typeDef mcu_pulseGetConfig(void)
{
  return pulse_getConfig();
}

/*
  7B_DATE Data Format:
  Data：17 0B 10 0A 14 0F 04  
  Note：year+month+day+hour+minute+second+week
        2023.11.16.10：30：15 Thursday
*/
u8 *mcu_bleReqReadDateTime(u8 *_pdata)
{
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  if(!_pdata)
    return _pdata;

  // get current time
  HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
  // get current date
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

  // fill in buf
  *_pdata++ = sDate.Year;
  *_pdata++ = sDate.Month;
  *_pdata++ = sDate.Date;
  *_pdata++ = sTime.Hours;
  *_pdata++ = sTime.Minutes;
  *_pdata++ = sTime.Seconds;
  *_pdata++ = sDate.WeekDay;

  return _pdata;
}

/*
*/
void mcu_bleReqWriteDateTime(u8 *_pdata)
{
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  if(!_pdata) return;

  // NOW: got date and time, set mcu date&time
  // format: year month day hour minute second weekday; total 7 bytes
  sDate.Year = *_pdata++;
  sDate.Month = *_pdata++;
  sDate.Date = *_pdata++;
  sTime.Hours = *_pdata++;
  sTime.Minutes = *_pdata++;
  sTime.Seconds = *_pdata++;
  sDate.WeekDay = *_pdata++;
  
  HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
  HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}

/*
*/
bool mcu_ecgGetRsviAbout(u8 *_pdata)
{
  return ecg_getRsviAbout(_pdata);
}

/*
*/
ppulse_unpulsing_period_typeDef mcu_pulseGetUnpulsingPeriod(void)
{
  return pulse_getUnpulsingPeriod();
}

/*
*/
void mcu_ecgGetAdcPeakValue(u16 *_pmax, u16 *_pmin)
{
  ecg_getAdcPeakValue(_pmax, _pmin);
}

/*
*/
void mcu_fpulseStartupPulsing(u8 _width)
{
  fpulse_startupPulsing(_width);
}

/*
*/
ecg_AdcWeightGain_typeDef *mcu_ecgGetAdcWeightGain(void)
{
  return ecg_getAdcWeightGain();
}

/*
*/
u8 mcu_ecgGetStatus(void)
{
  return ecg_getStatus();
}

/*
*/
u8 mcu_ecgGetBpm(void)
{
  return ecg_getBpm();
}

/*
*/
bool mcu_halSpiTransmitReceive(u8 *_ptx, u8 *_prx, u16 _len, u32 _timeout)
{
  if(HAL_SPI_TransmitReceive(&hspi2, _ptx, _prx, _len, _timeout) == HAL_OK)
    return true;

  return false;
}

/*
*/
bool mcu_halSpiTransmit(u8 *_pdata, u16 _len, u32 _timeout)
{
  if(HAL_SPI_Transmit(&hspi2, _pdata, _len, _timeout) == HAL_OK)
    return true;

  return false;
}

/*
*/
void mcu_resetRsl10(void)
{
  // use NRESET pin of RSL10 chip to wakeup RSL10 if magnethall exist(Rising edge)
  HAL_GPIO_WritePin(CCM_PIN18_RSL10_RST_GPIO_Port, CCM_PIN18_RSL10_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(5);
  HAL_GPIO_WritePin(CCM_PIN18_RSL10_RST_GPIO_Port, CCM_PIN18_RSL10_RST_Pin, GPIO_PIN_SET);
}

/*
*/
void mcu_bleCsDeasserted(void)
{
  HAL_GPIO_WritePin(CCM_PIN38_BLE_CS_GPIO_Port, CCM_PIN38_BLE_CS_Pin, GPIO_PIN_SET);
}

/*
*/
void mcu_bleCsAsserted(void)
{
  HAL_GPIO_WritePin(CCM_PIN38_BLE_CS_GPIO_Port, CCM_PIN38_BLE_CS_Pin, GPIO_PIN_RESET);
}

/*
*/
bool mcu_eeReadOrWriteKeyValue(u8 _key, bool _isRead)
{
  return ee_readOrWriteKeyValue((ee_keyvalue_typeDef)_key, _isRead);
}

/*
*/
uint32 mcu_getAdcValue(void)
{
  return HAL_ADC_GetValue(&hadc);
}

/*
*/
bool mcu_adcIsWorking(void)
{
  return adc_isWorking();
}

/*
*/
void mcu_setEcgPulsingFlag(void)
{
  pulse_setEcgPulsingFlag();
}


/*
  brief:
    1. config CCM_PIN45_VOUT_SET_Pin to:
      input mode(6);
      output high mode(5);
      output low mode(4);
*/
void mcu_setVoutsetPin(u8 _setvalue)
{
  mcu_VoutsetStatus_typeDef value = (mcu_VoutsetStatus_typeDef)_setvalue;
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = CCM_PIN45_VOUT_SET_Pin;

  // config pin
  switch(value){
    case mcu_Vos_OutL_status:
      HAL_GPIO_WritePin(CCM_PIN45_VOUT_SET_GPIO_Port, CCM_PIN45_VOUT_SET_Pin, GPIO_PIN_RESET);
      GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
      GPIO_InitStruct.Pull = GPIO_PULLDOWN;
      GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
      break;
    case mcu_Vos_OutH_status:
      HAL_GPIO_WritePin(CCM_PIN45_VOUT_SET_GPIO_Port, CCM_PIN45_VOUT_SET_Pin, GPIO_PIN_SET);
      GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
      GPIO_InitStruct.Pull = GPIO_PULLUP;
      GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
      break;
    case mcu_Vos_Input_status:
      GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
      GPIO_InitStruct.Pull = GPIO_NOPULL;
      break;

    default:
      break;
  }
  HAL_GPIO_Init(CCM_PIN45_VOUT_SET_GPIO_Port, &GPIO_InitStruct);

  // set status
  mcu_Voutset.value = value;
}

/*
*/
void mcu_calibrateMotionPeriod(void)
{
  mcu_MotionConfig_typeDef *p = &mcu_motionCfg;

  // unvalid? set default
  if(p->isValid ^ MCU_DATA_STRUCT_VALID_VALUE){
    p->mcu_motionPeriod = MCU_MOTION_PERIOD_DEFAULT;
    p->mcu_motionThreshold = MCU_MOTION_THRESHOLD_DEFAULT;

    // set valid
    p->isValid = MCU_DATA_STRUCT_VALID_VALUE;

		// store this key value into EEPROM
		ee_readOrWriteKeyValue(ee_kv_motionPeriod, false);
  }
}

/*
  brief:
    1. check valid or not;
    2. if unvalid, set default value: output High;
 */
void mcu_calibrateVoutset(void)
{
  mcu_Voutset_typeDef *p = &mcu_Voutset;

  // unvalid? set default
  if(p->isValid ^ MCU_DATA_STRUCT_VALID_VALUE){
    p->value = mcu_Vos_OutH_status;

    // set valid
    p->isValid = MCU_DATA_STRUCT_VALID_VALUE;

		// store this key value into EEPROM
		ee_readOrWriteKeyValue(ee_kv_VoutSet, false);
  }

  // NOTICE: config init this pin again
  mcu_setVoutsetPin((u8)p->value);
}

/*
*/
void mcu_calibrateBaseData(void)
{
  mcu_baseData_typeDef *p = &mcu_baseData;

  // unvalid? set default
  if(p->isValid ^ MCU_DATA_STRUCT_VALID_VALUE){
    p->mcu_pulseTotalNum = 0;

    p->isValid = MCU_DATA_STRUCT_VALID_VALUE;

    // store this key value into EEPROM
    ee_readOrWriteKeyValue(ee_kv_baseData, false);
  }
}

/*
*/
void mcu_calibrateBpmCalmMax(void)
{
  mcu_bpmCalmMax_typeDef *p = &mcu_bpmCalmMax;

  // unvalid? set default
  if(p->isValid ^ MCU_DATA_STRUCT_VALID_VALUE){
    p->mcu_bpmCalmMax = PULSING_BPM_CALM_MAX;

    p->isValid = MCU_DATA_STRUCT_VALID_VALUE;

    // store this key value into EEPROM
    ee_readOrWriteKeyValue(ee_kv_bpmCalmMax, false);
  }
}

/*
*/
mcu_MotionConfig_typeDef *mcu_getMotionCfg(void)
{
  return &mcu_motionCfg;
}


/*
Data Format:
       Data：04 - Vout output low 
             05 - Vout output high 
             06 - Vout float status*/
mcu_Voutset_typeDef *mcu_getVoutset(void)
{
  return &mcu_Voutset;
}

/*
*/
mcu_baseData_typeDef *mcu_getBaseData(void)
{
  return &mcu_baseData;
}

/*
*/
mcu_bpmCalmMax_typeDef *mcu_getBpmCalMax(void)
{
  return &mcu_bpmCalmMax;
}

/*
  brief:
    1. start up all device base mcu, such as accel;
    2. 
*/
void mcu_startup(void)
{
  // startup accel, check motion state;
  // NOTE: this process have 10ms delay;
  accel_startup();

//  HAL_TIM_Base_Start_IT(&htim6);

  // set next startup timetick
  mcu_motionTick = HAL_GetTick() + mcu_motionCfg.mcu_motionPeriod * 1000;
}

/*
  brief:
    1. include all user init;
    2. 
*/
void mcu_deviceInit(void)
{
  // mcu init itself
  mcu_init();

  // flash init
//  flash_init();

  // accel chip init
  accel_init();

  // RSL10 chip init
  ble_init();

  // init ecg Rwave setting
  pulse_init();

  // init fpulse and fovbc
  fpulse_init();

#ifdef LiuJH_DEBUG
  // test only(run this statement, code will fly......)
//  HAL_GPIO_WritePin(CCM_PIN46_VPON_GPIO_Port, CCM_PIN46_VPON_Pin, GPIO_PIN_SET);
//  HAL_GPIO_WritePin(PIN30_PA9_WLC38_ON_GPIO_Port, PIN30_PA9_WLC38_ON_Pin, GPIO_PIN_RESET);
#endif
}

/*
  brief:
    1. If wakeup from LPM, set flag for used it later;
    2. Such as:
      RTC Timer wakeup;
      Wakeup1 pin wakeup;
    3. store some important values(ex. date&time) before MX init(will clear all);
    4. 
*/
void mcu_sysInit(void)
{
  RTC_HandleTypeDef rtc;
  rtc.Instance = RTC;

  mcu_rstFlag = mcu_rstFlag_others;

  // Note these code for test, dont enter LPM when power on

  // Is wakeup from standby mode?
  if(!__HAL_PWR_GET_FLAG(PWR_FLAG_SB) ||
     !__HAL_PWR_GET_FLAG(PWR_FLAG_WU)){

    // enter into LPM immediately when power on or rset
	//return;

	  // define power on as BLE mode startup
#ifdef LiuJH_DEBUG
    mcu_rstFlag = mcu_rstFlag_MagnetHall;
#else
    // test only
	  mcu_rstFlag = mcu_rstFlag_wakeup;
#endif
  }else{
    // if set wakeup flag, judge ble or pulse mode later
#ifndef LiuJH_DEBUG
    // test only
    mcu_rstFlag = mcu_rstFlag_MagnetHall;
#else
    mcu_rstFlag = mcu_rstFlag_wakeup;
#endif
  }

  /*
    NOW: start temp store some data
  */

  // 1. date&time temp store before MX init
  HAL_RTC_GetTime(&rtc, &mcu_time, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&rtc, &mcu_date, RTC_FORMAT_BIN);

  // 2. 

}


/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv public function define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



