/*
 * wpr.c
 *
 *  Created on: 2023年11月18日
 *      Author: Johnny
 */

#include "main.h"


/* var define *******************************************************/


static wpr_status_typeDef wpr_status;
static bool wprIsWorking;
// record ble selected adc channel num
static u8 wpr_adcChNum = ADC_CH_NUM_BATT_MEASURE;

// record bit12 adc sample value(NOTICE: update per 4ms)
static u32 wpr_adcValue;
// adc value got freq
static u32 wpr_battUpdateTick;
// record battery value, unit: 0.1V
static u16 wpr_battValue;
// reocrd battery percent, unit: 0.01
static u8 wpr_battPercent;

// record start charge falg setting by ble
static wpr_chargeSwitch_typeDef wpr_chargeSwitch;

#ifndef LiuJH_DEBUG
static u32 adcbuf[32];
static u32 adclen;
#endif



/* private function define ******************************************/

#ifndef LiuJH_DEBUG
/*
  brief:
    1. read ONE byte from spec register;
    2. 
*/
static u8 wpr_readRegValue(u16 _regaddr)
{
  u8 ret = 0xFF;
  u8 buf[2] = {((_regaddr >> 8) & 0xFF), (_regaddr & 0xFF)};

  if(HAL_I2C_Master_Transmit(&hi2c1, WPR_CHIP_ADDR_R, buf, sizeof(_regaddr), TIMEOUT_1S) == HAL_OK){
    HAL_I2C_Master_Receive(&hi2c1, WPR_CHIP_ADDR_R, &ret, sizeof(ret), TIMEOUT_1S);
  }

  return ret;
}

/*
  brief:
    1. read _datalen bytes from _startRegister and later registers;
    2. 
*/
static bool wpr_readRegsValues(u16 _startRegaddr, u8 *_pDest, u32 _datalen)
{
  bool ret = false;
  u8 buf[2] = {((_startRegaddr >> 8) & 0xFF), (_startRegaddr & 0xFF)};

  if(HAL_I2C_Master_Transmit(&hi2c1, WPR_CHIP_ADDR_R, buf, sizeof(_startRegaddr), TIMEOUT_1S) == HAL_OK
    && HAL_I2C_Master_Receive(&hi2c1, WPR_CHIP_ADDR_R, _pDest, _datalen, TIMEOUT_5S) == HAL_OK
    ){
    ret = true;
  }

  return ret;
}

/*
  brief:
    1. write ONE byte from spec register;
*/
static bool wpr_writeRegValue(u16 _regaddr, u8 _value)
{
  bool ret = false;
  uint8_t buf[3] = {((_regaddr >> 8) & 0xFF), (_regaddr & 0xFF), _value};

	// In blocking mode, write register.
    if(HAL_I2C_Master_Transmit(&hi2c1, WPR_CHIP_ADDR_R, buf, sizeof(buf), TIMEOUT_1S) == HAL_OK){
      ret = true;
    }

  return ret;
}

/*
  brief:
    1. write _datalen bytes to spec startregister and later registers;
*/
static bool wpr_writeRegsValues(u16 _startRegaddr, u8 *_pSrc, u32 _datalen)
{
  bool ret = false;
  uint8_t buf[2] = {((_startRegaddr >> 8) & 0xFF), (_startRegaddr & 0xFF)};

	// In blocking mode, write register.
    if(HAL_I2C_Master_Transmit(&hi2c1, WPR_CHIP_ADDR_R, buf, sizeof(buf), TIMEOUT_1S) == HAL_OK
      && HAL_I2C_Master_Transmit(&hi2c1, WPR_CHIP_ADDR_R, _pSrc, _datalen, TIMEOUT_5S) == HAL_OK
      ){
      ret = true;
    }

  return ret;
}
#endif

/*
  brief:
    1. quickly get adc value;
    2. get wpr_battValue and wpr_battPercent;
*/
static u16 wpr_calculateBattLevel(void)
{
  u16 ret = 0;
  u32 adc = wpr_adcValue;
  // adc max value(4.2V); adc min value(3.0V)
  u32 max = WPR_ADC_MAX_VALUE_D;
  u32 min = WPR_ADC_MIN_VALUE_D;

#ifndef LiuJH_DEBUG
  // check wpr status, get correct max and min value
  if(wpr_status == wpr_charging_status){
    max = WPR_ADC_MAX_VALUE_E;
    min = WPR_ADC_MIN_VALUE_E;
  }
#endif

  // over flow max or min adc value?
  if(adc > max){
    adc = max;
  }else if(adc < min){
    adc = min;
  }

  // calculate batt percent
  wpr_battPercent = (u8)((adc - min) * 100 / (max - min));
  /*
    Calculate batt value(unit: mV):

      max V is 4.2V(ie. 4200mV), min V is 3.0V(ie. 3000mV);
      percent = (battvalue - 3000) / (4200 - 3000) * 100
              = (battvalue - 3000) / 12;

      So, battvalue = percent * 12 + 3000;
  */
  wpr_battValue = wpr_battPercent * 12 + 3000;

  return ret;
}

/*
  brief:
    1. calculate and update battery level for other function using;
    2. calculate and set percentage;
    3. loop update value per minute;
    4. NOTICE: get adc value immediately;
*/
static void wpr_updateBattLevel(void)
{
  if(!wprIsWorking || wpr_adcValue == WPR_INVALID_VALUE)
    return;

  if(HAL_GetTick() < wpr_battUpdateTick)
    return;

  // calculate battery value and percentage
  wpr_calculateBattLevel();

  // for next value
  wpr_battUpdateTick = HAL_GetTick() + WPR_BATT_LEVEL_UPDATE_TICK;
}

/*
  brief:
    1. config STWLC38, but dont start charging;
    2. need it???
*/
static bool wpr_configStwlc38(void)
{
#ifdef LiuJH_DEBUG
  // test only for pulse

  // we do nothing
  return true;
#else
  bool ret = false;
  u8 buf[64];
  u8 *p = buf;
  u8 size;

  // enable chip
  HAL_GPIO_WritePin(CCM_PIN21_BOOST_ON_GPIO_Port, CCM_PIN21_BOOST_ON_Pin, GPIO_PIN_RESET);

  // clear buf
  memset(buf, 0, sizeof(buf));

  /*
    Read these registers in state of uncharge and charging; compare it
  */

  // buf[0]  default: 0x26
  *p++ = wpr_readRegValue(WPR_REG_ADDR_CHIP_ID);
  // buf[1]
  *p++ = wpr_readRegValue(WPR_REG_ADDR_OPRATION_MODE);
  // buf[2]
  *p++ = wpr_readRegValue(WPR_REG_ADDR_SYS_COMMAND);
  // buf[3, 6]
  ret = wpr_readRegsValues(WPR_REG_ADDR_SYS_ERROR, p, 4);
  p += 4;
  // buf[7, 10]  default: 2F 0F 3D 00
  ret = wpr_readRegsValues(WPR_REG_ADDR_VRECT_MV, p, 4);
  p += 4;
  // buf[11, 12]
  ret = wpr_readRegsValues(WPR_REG_ADDR_POWER_RX, p, 2);
  p += 2;
  // buf[13, 15]  default: 0x77 0x70 0x00
  ret = wpr_readRegsValues(WPR_REG_ADDR_RX_INT_EN, p, 3);
  p += 3;
  // buf[16, 18]
  ret = wpr_readRegsValues(WPR_REG_ADDR_RX_INT_CLEAR, p, 3);
  p += 3;
  // buf[19, 21]
  ret = wpr_readRegsValues(WPR_REG_ADDR_RX_INT_LATCH, p, 3);
  p += 3;
  // buf[22, 24]
  ret = wpr_readRegsValues(WPR_REG_ADDR_RX_INT_STATUS, p, 3);
  p += 3;
  // buf[25]
  *p++ = wpr_readRegValue(WPR_REG_ADDR_RX_COMMAND1);
  // buf[26]
  *p++ = wpr_readRegValue(WPR_REG_ADDR_RX_COMMAND2);
  // buf[27, 28]
  ret = wpr_readRegsValues(WPR_REG_ADDR_RX_VOUT_SET, p, 2);
  p += 2;
  // buf[29, 35]
  ret = wpr_readRegsValues(WPR_REG_ADDR_RX_LDO_CONFIG, p, 7);
  p += 7;
  // buf[36, 37]
  ret = wpr_readRegsValues(WPR_REG_ADDR_RX_POWER_CONTRACT, p, 2);
  p += 2;

  size = p - buf;

  return ret;
#endif
}

/*
  brief:
    1. 
*/
static bool wpr_isFullCharging(void)
{
  bool ret = false;

  if(wpr_battValue >= WPR_BATT_HIGH_THRESHOLD)
    ret = true;

  return ret;
}

/*
  brief:
    1. 
*/
static void wpr_smStopCharge(void)
{
  // disable chip STWLC38, set pin21 high
  HAL_GPIO_WritePin(CCM_PIN21_BOOST_ON_GPIO_Port, CCM_PIN21_BOOST_ON_Pin, GPIO_PIN_SET);

  // other working?


  // update wpr status
  wpr_status = wpr_Waiting_status;
}

/*
  brief:
    1. check charge flag setting by ble;
    2. if charge flag reset, goto next state;
    3. check charging fully or not flag;
    4. if fully charged, goto next state;
*/
static void wpr_smCharging(void)
{
  if(wpr_chargeSwitch == wpr_chargeSwitch_off
    || wpr_isFullCharging()){


    wpr_chargeSwitch = wpr_chargeSwitch_off;
    // stop charge
    wpr_status = wpr_stopCharge_status;
  }
}

/*
  brief:
    1. start charging;
    2. enter next state;
*/
static void wpr_smStartCharge(void)
{
  /* start charging */

  // enable chip STWLC38, set pin21 low
  HAL_GPIO_WritePin(CCM_PIN21_BOOST_ON_GPIO_Port, CCM_PIN21_BOOST_ON_Pin, GPIO_PIN_RESET);

  // other working???


  // enter next state
  wpr_status = wpr_charging_status;
}

/*
  brief:
    1. check charge flag setting by ble;
    2. goto charge state if flag set;
    3. loop waiting;
*/
static void wpr_smWaiting(void)
{
  if(wpr_chargeSwitch == wpr_chargeSwitch_on){
    wpr_status = wpr_startCharge_status;
  }
}

/*
  brief:
    1. deal with stwlc38 for charge;
    2. goto charge waiting status;
    3. 
*/
static void wpr_smStartup(void)
{
  // 1. deal with stwlc38 for charge
  wpr_configStwlc38();

  // get batt value for the first time
  wpr_battUpdateTick = HAL_GetTick();
  // enter into next status
  wpr_status = wpr_Waiting_status;
}

/* public function define *******************************************/


void wpr_stateMachine(void)
{
  switch(wpr_status){
    case wpr_idle_status:
      wprIsWorking = false;
      break;
    case wpr_inited_status:
      // do nothing
      break;
    case wpr_startup_status:
      wpr_smStartup();
      break;
    case wpr_Waiting_status:
      wpr_smWaiting();
      break;
    case wpr_startCharge_status:
      wpr_smStartCharge();
      break;
    case wpr_charging_status:
      wpr_smCharging();
      break;
    case wpr_stopCharge_status:
      wpr_smStopCharge();
      break;

    default:
      break;
  }

  // calculate and update battery level for other function using
  wpr_updateBattLevel();
}

/*
  brief:
    1. NOTE: working in adc callback, So deal quickly and DONT block;
    2. record this data only, and process in state machine;
    3. 
*/
void wpr_adcConvCpltCB(void)
{
  // get adc sample value
  wpr_adcValue = (int32_t)(HAL_ADC_GetValue(&hadc) & 0x0FFF);
#ifndef LiuJH_DEBUG
  if(adclen < 32)
    adcbuf[adclen++] = wpr_adcValue;
  if(adclen >= 32){
    int i = 0;
    i++;
  }
#endif
}

/*
  brief:
    1. if too low battery level, dont startup pulsing work;
    2. threshold is: 
*/
bool wpr_isUltraLowBattLevel(void)
{
  bool ret = false;

  if(wprIsWorking && wpr_battValue < WPR_BATT_LOW_THRESHOLD)
    ret = true;

  return ret;
}

/*
*/
u8 wpr_getBattPercent(void)
{
  return wpr_battPercent;
}

/*
*/
bool wpr_isCharging(void)
{
  return (wpr_status == wpr_charging_status);
}

void wpr_setChargeSwitch(bool _isOn)
{
  if(_isOn)
    wpr_chargeSwitch = wpr_chargeSwitch_on;
  else
    wpr_chargeSwitch = wpr_chargeSwitch_off;
}

/*
  brief:
    1. when ble or pulse shut down, we will shut down;
*/
void wpr_shutdown(void)
{
  // pull high pin21 boost on, let stwlc38 into reset mode
  HAL_GPIO_WritePin(CCM_PIN21_BOOST_ON_GPIO_Port, CCM_PIN21_BOOST_ON_Pin, GPIO_PIN_SET);

  // other work???



  // update status
  wprIsWorking = false;

  // update status for LPM
  wpr_status = wpr_idle_status;
}

u8 wpr_getAdcChNum(void)
{
  return wpr_adcChNum;
}

/*
  brief:
    1. 
*/
void wpr_startup(void)
{
  if(wprIsWorking) return;

  wpr_init();

  wprIsWorking = true;
  wpr_status = wpr_startup_status;
}

void wpr_init(void)
{
  wprIsWorking = false;
  wpr_adcChNum = ADC_CH_NUM_BATT_MEASURE;
  wpr_adcValue = WPR_INVALID_VALUE;
  wpr_battUpdateTick = WPR_INVALID_VALUE;
  wpr_battValue = 0;
  wpr_battPercent = 0;
  wpr_chargeSwitch = wpr_chargeSwitch_off;

  wpr_status = wpr_inited_status;
}


