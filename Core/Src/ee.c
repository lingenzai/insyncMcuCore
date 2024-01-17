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
#ifdef LiuJH_DEBUG
  // check param
  if(!IS_FLASH_DATA_ADDRESS(_addr) || !_pdata || 
    !IS_FLASH_DATA_ADDRESS(_addr + _len - 1))
    return false;


  HAL_FLASHEx_DATAEEPROM_Unlock();
  while(_len){
    HAL_FLASHEx_DATAEEPROM_Program(ee_byte_type, _addr++, *_pdata++);
    _len--;
    HAL_Delay(5);
  }
  HAL_FLASHEx_DATAEEPROM_Lock();

  return true;
#else
  bool ret = false;
  u32 *pd4 = (u32 *)_addr;
  u16 *pd2;
  u32 *ps4 = (u32 *)_pdata;
  u16 *ps2;
  u32 num = _len;
//  u32 tick = HAL_GetTick();

  // check param
  if(!IS_FLASH_DATA_ADDRESS(_addr) || !_pdata || 
    !IS_FLASH_DATA_ADDRESS(_addr + _len - 1))
    return ret;

  // loop word write
  num = _len >> 2;
  while(num){
    ee_writeBaseType(ee_word_type, (u32)pd4++, *ps4++);
    num--;
  }

  // half word write
  num = (_len & 0x03) > 1;
  if(num){
    pd2 = (u16 *)pd4;
    ps2 = (u16 *)ps4;
    ee_writeBaseType(ee_half_word_type, (u32)pd2++, (u32)*ps2++);
  }

  // byte write
  num = (_len & 0x01);
  if(num){
    ee_writeBaseType(ee_byte_type, (u32)pd2, (u32)*((u8 *)ps2));
  }

//  tick = HAL_GetTick() - tick;
  return true;
#endif
}

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv private function define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/



/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ public function define start ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

/*
  brief:
    1. read all params of needed from EEPROM:
      param1: pulseConfig;
      param2: unpulsing period;
      param3: Vout_set value;
*/
bool ee_restoreKeyValue(void)
{
  u32 addr;
  u8 *pdata;
  u32 len;
  bool ret = true;

  // Param1: pulseConfig
  addr = ee_addr_pulseConfig;
  pdata = (u8 *)pulse_getConfig();
  len = ee_addr_pulseConfig_size;
  ee_readData(addr, pdata, len);
  // check valid
  pulse_calibrateConfig();

  // Param2:  unpulsing period
  addr = ee_addr_unpulsingPeriod;
  pdata = (u8 *)pulse_getUnpulsingPeriod();
  len = ee_addr_unpulsingPeriod_size;
  ee_readData(addr, pdata, len);
  // check valid
  pulse_calibrateUnpulsingPeriod();

  // Param3: pulse Vout_set config value
  addr = ee_addr_VoutSet;
  pdata = (u8 *)mcu_getVoutset();
  len = ee_addr_VoutSet_size;
  ee_readData(addr, pdata, len);
  // check valid
  mcu_calibrateVoutset();

  // Param4: motion period redo config
  addr = ee_addr_motionPeriod;
  pdata = (u8 *)mcu_getMotionCfg();
  len = ee_addr_motionPeriod_size;
  ee_readData(addr, pdata, len);
  // check valid
  mcu_calibrateMotionPeriod();


  return ret;
}

/*
  brief:
    1. store all params of needed:
        param1: pulseConfig;
        param2: 
*/
bool ee_storeKeyValue(ee_keyvalue_typeDef _key)
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


    default:
      break;
  }

  ee_writeData(addr, pdata, len);

  // the last data write time delay, need it???
  HAL_Delay(2);

  return ret;
}

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv public function define end vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/

