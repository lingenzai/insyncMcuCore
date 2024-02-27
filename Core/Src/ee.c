/*
 * eeprom.c
 *
 *  Created on: 2023年11月8日
 *      Author: Johnny
 */

#include "main.h"

/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ public var define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv public var define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ private var define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/


/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv private var define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ private function define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

/*
  brief:
    1. 
*/
static bool ee_readData(u32 _addr, u8 *_pdata, u32 _len)
{
#ifdef LiuJH_DEBUG
  bool ret = false;
  u8 *pd = _pdata;
  __IO u8 *ps = (__IO u8 *)(_addr);
  u32 num = _len;
//  u32 tick = HAL_GetTick();

  // check param
  if(!IS_FLASH_DATA_ADDRESS(_addr) || !_pdata || 
    !IS_FLASH_DATA_ADDRESS(_addr + _len - 1))
    return ret;

  // start read bytes
  while(num){
    *pd++ = *ps++;
    num--;
  }

//  tick = HAL_GetTick() - tick;
#endif
  return true;
}

/*
  brief:
    1. 
*/
static bool ee_writeData(u32 _addr, u8 *_pdata, u32 _len)
{
  // check param
  if(!IS_FLASH_DATA_ADDRESS(_addr) || !_pdata || 
    !IS_FLASH_DATA_ADDRESS(_addr + _len - 1))
    return false;


  HAL_FLASHEx_DATAEEPROM_Unlock();
  while(_len){
    HAL_FLASHEx_DATAEEPROM_Program(ee_byte_type, _addr++, *_pdata++);
    _len--;
//    HAL_Delay(5);
  }
  HAL_FLASHEx_DATAEEPROM_Lock();

  return true;
}

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv private function define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ public function define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

/*
  brief:
    1. read all params of needed from EEPROM:
      param1: pulseConfig;
      param2: unpulsing period;
      param3: Vout_set value;
      param4: motion period config;
*/
bool ee_restoreKeyValue(void)
{
	bool ret = true;

  // Param1: pulseConfig
	ee_readOrWriteKeyValue(ee_kv_pulseConfig, true);
  // check valid
  pulse_calibrateConfig();

  // Param2:  unpulsing period
	ee_readOrWriteKeyValue(ee_kv_unpulsingPeriod, true);
  // check valid
  pulse_calibrateUnpulsingPeriod();

  // Param3: pulse Vout_set config value
	ee_readOrWriteKeyValue(ee_kv_VoutSet, true);
  // check valid
  mcu_calibrateVoutset();

  // Param4: motion period redo config
	ee_readOrWriteKeyValue(ee_kv_motionPeriod, true);
  // check valid
  mcu_calibrateMotionPeriod();

  // Param5: base data
  ee_readOrWriteKeyValue(ee_kv_baseData, true);
  // check valid
  mcu_calibrateBaseData();

  // Param6: bpm calm max threshold
  ee_readOrWriteKeyValue(ee_kv_bpmCalmMax, true);
  // check valid
  mcu_calibrateBpmCalmMax();

  return ret;
}

/*
	brief:
    1. write all params of needed into EEPROM:
      param1: pulseConfig;
      param2: unpulsing period;
      param3: Vout_set value;
      param4: motion period config;
*/
bool ee_storeKeyValue(void)
{
  bool ret = true;
	ee_keyvalue_typeDef key;

	for(key = ee_kv_pulseConfig; key < ee_kv_max; key++){
		ee_readOrWriteKeyValue(key, false);
	}

  return ret;
}

/*
  brief:
    1. store all params of needed:
        param1: pulseConfig;
        param2: 
*/
bool ee_readOrWriteKeyValue(ee_keyvalue_typeDef _key, bool _isRead)
{
  u32 addr;
  u8 *pdata;
  u32 len;
  bool ret = true;

  switch(_key){
    case ee_kv_pulseConfig:
      // Param1: pulseConfig
      addr = ee_addr_pulseConfig;
      pdata = (u8 *)pulse_getConfig();
      len = ee_addr_pulseConfig_size;
      break;
    case ee_kv_unpulsingPeriod:
      // Param2: unpulsing period
      addr = ee_addr_unpulsingPeriod;
      pdata = (u8 *)pulse_getUnpulsingPeriod();
      len = ee_addr_unpulsingPeriod_size;
      break;
    case ee_kv_VoutSet:
      // Param3: pulse Vout_set config value
      addr = ee_addr_VoutSet;
      pdata = (u8 *)mcu_getVoutset();
      len = ee_addr_VoutSet_size;
      break;
    case ee_kv_motionPeriod:
      // Param4: motion period redo config
      addr = ee_addr_motionPeriod;
      pdata = (u8 *)mcu_getMotionCfg();
      len = ee_addr_motionPeriod_size;
      break;
    case ee_kv_baseData:
      // Param5: base data
      addr = ee_addr_baseData;
      pdata = (u8 *)(mcu_getBaseData());
      len = ee_addr_baseData_size;
      break;
    case ee_kv_bpmCalmMax:
      // Param6: bpm calm max value
      addr = ee_addr_bpmCalmMax;
      pdata = (u8 *)(mcu_getBpmCalMax());
      len = ee_addr_bpmCalmMax_size;
      break;


    default:
      break;
  }

	if(_isRead)
		ee_readData(addr, pdata, len);
	else
	  ee_writeData(addr, pdata, len);

  // the last data write time delay, need it???
//  HAL_Delay(2);

  return ret;
}

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv public function define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

