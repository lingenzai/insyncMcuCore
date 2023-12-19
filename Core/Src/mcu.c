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

static u32 mcu_lpmTick;
/*
Data Format:
       Data：04 - Vout output low 
             05 - Vout output high 
             06 - Vout float status*/
static mcu_Voutset_typeDef mcu_Voutset;


/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv private var define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ private function define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/



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
  
  // clear all RCC_CSR reset status flags
  __HAL_RCC_CLEAR_RESET_FLAGS();

  /*Disable all used wakeup sources: Pin1(PA.0)*/
//  HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN1);

  /*Re-enable all used wakeup sources: Pin1(PA.0)*/
//  HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1);

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
/*
  HAL_Delay(1000 - HAL_GetTick());
*/
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
#ifdef LiuJH_DEBUG
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

  return (count > 3);
#endif
}

/*
  brief:
    1. judge magnet at first;
    2. 
*/
static void mcu_judgeWkupReason(void)
{
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
    // flash need sometimes goto LPM mode, so waiting it
//    || !flash_isEnterLpm()
//    || wlc_isWorking()
//    || accel_isworking()
//    || adc_isWorking()
    ){
    // set lpm tick
    mcu_lpmTick = HAL_GetTick();

    return false;
  }

  // no device is working, so we can enter LPM
  return true;
}

/*
*/
static void mcu_workDriven(void)
{
  // check ble and pulse is all stop working?
  if(!ble_isWorking() && !pulse_isWorking()){
    // stop adc and ecg and accel working
    adc_stop();
    accel_stop();

    return;
  }

  /*
    NOW: ble or(and) pulse is working
  */

  // redo motion check
  if(HAL_GetTick() < mcu_motionTick) return;

  accel_startup();
  // set next startup timetick
  mcu_motionTick = HAL_GetTick() + mcu_motionCfg.mcu_motionPeriod * 1000;
}

/*
  brief:
    1. store all of important data in EEPROM;
    2. 
*/
static void mcu_storeKeyValue(void)
{
  ee_storeKeyValue();
}

/*
  brief:
    1. set all not used pin into analog state.
*/
//static void GPIO_AnalogInput_config(void)
static void mcu_setGpioFloatingInput(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /*Set all GPIO in analog state to reduce power consumption*/

//  __HAL_RCC_GPIOA_CLK_ENABLE();
//  __HAL_RCC_GPIOB_CLK_ENABLE();
//  __HAL_RCC_GPIOC_CLK_ENABLE();

  // Analog Mode
//  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  // PB0 is NRESET pin of RSL10, must keep high level. So update Mode.
  // Input Floating Mode
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
#ifdef LiuJH_DEBUG
  /*
    NOTE:
      During debugging, PA13 and PA14 are debugging pins and cannot be adjusted;
      No problem when the version was released;
  */
  GPIO_InitStruct.Pin = GPIO_PIN_All & (~GPIO_PIN_13) & (~GPIO_PIN_14);
#else
  GPIO_InitStruct.Pin = GPIO_PIN_All;
#endif

  HAL_GPIO_Init(GPIOA,&GPIO_InitStruct);

  // this pin MUST set, otherwise STWLC38 is very consume power
  GPIO_InitStruct.Pin = GPIO_PIN_All & (~CCM_PIN21_BOOST_ON_Pin) & (~CCM_PIN25_MEM_CS_Pin)&(~CCM_PIN45_VOUT_SET_Pin);
  HAL_GPIO_Init(GPIOB,&GPIO_InitStruct);
//  GPIO_InitStruct.Pin = GPIO_PIN_All;
//  HAL_GPIO_Init(GPIOC,&GPIO_InitStruct);

  __HAL_RCC_GPIOA_CLK_DISABLE();
  __HAL_RCC_GPIOB_CLK_DISABLE();
//  __HAL_RCC_GPIOC_CLK_DISABLE();

  // Johnny: Restore WKUP1,BLE_CS,RSl10_RST origin state??
//  MX_GPIO_Init();
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
  u32 startsec;
  // seconds of 18 hours
//  u32 secmax = 18 * 60 * 60;

  // get current time
  HAL_RTC_GetTime(&hrtc, &mcu_time, RTC_FORMAT_BIN);

  // current time convert seconds
  sec = (mcu_time.Hours * 60 + mcu_time.Minutes) * 60 + mcu_time.Seconds;
  // get config pulse start time, convert seconds
  startsec = pulse_getConfig()->pulse_start_time * 60;

  // compare these, make sure wakeup time
  if(sec > startsec)
    startsec += MCU_MAX_SECOND_EACH_DAY;
  sec = startsec - sec;

  // the max counter is 0xFFFF(unit: second) in 16bits wakeup mode, is: 18 hours
  if(sec > RTC_WKUP_TIMER_COUNT_MAX){
    // we must wakeup two times(the first time is invalid wakeup)
    sec >>= 1;
    sec &= 0xFFFF;
  }

#ifndef LiuJH_DEBUG
  // set 3s timer; ONLY test
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, RTC_WAKEUP_TIME_3S, RTC_WAKEUPCLOCK_RTCCLK_DIV16);
#else
//  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, sec, RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, 30, RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
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

#else
  static u8 count = 0;

  if(HAL_GetTick() < mcu_lpmTick) return;

  count++;

  /* ask rsl10 enter LPM */
  if(!ble_askRsl10EnterLpm()){

    mcu_lpmTick = HAL_GetTick() + BLE_QUERY_TIMEOUT;

    // ask RSL10 three times, so give up
    if(count < 3) return;
  }

  count = 0;

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
  mcu_lpmTick = 0;
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

  // mcu other work driven
  mcu_workDriven();

  // we can go into LPM?
  if(mcu_noDeviceWorking()){
    mcu_enterStandbyMode();
  }
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
  }

  // NOTICE: config init this pin again
  mcu_setVoutsetPin((u8)p->value);
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
  brief:
    1. start up all device base mcu, such as accel;
    2. 
*/
void mcu_startup(void)
{
  // startup accel, check motion state;
  // NOTE: this process have 10ms delay;
  accel_startup();

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

#ifndef LiuJH_DEBUG
  // Note these code for test, dont enter LPM when power on

  // Is wakeup from standby mode?
  if(!__HAL_PWR_GET_FLAG(PWR_FLAG_SB) ||
     !__HAL_PWR_GET_FLAG(PWR_FLAG_WU)){
	  return;
  }
#endif

  // set wakup flag
  mcu_rstFlag = mcu_rstFlag_wakeup;

#ifndef LiuJH_DEBUG
  // this method dont work

  // Is RTC Timer wakeup?
  if(__HAL_RTC_WAKEUPTIMER_GET_FLAG(&rtc, RTC_FLAG_WUTF)){
    mcu_rstFlag = mcu_rstFlag_RtcTimer;
  }
#endif

  /*
    NOW: start temp store some data
  */

  // 1. date&time temp store before MX init
  HAL_RTC_GetTime(&rtc, &mcu_time, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&rtc, &mcu_date, RTC_FORMAT_BIN);

  // 2. 

}

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv public function define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



