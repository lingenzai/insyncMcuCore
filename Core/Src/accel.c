/* Private includes ----------------------------------------------------------*/
#include "main.h"


/* Private variables ----------------------------------------------------*/

static accel_stateTypeDef accelStatus;
static bool accelIsworking = false;

/*
  NOTICE: The first set of data must be discarded,
          so we only get (FIFO_DATA_NUM - 1) sets of data.
*/
static uint8_t FIFO_data_buf[FIFO_DATA_NUM * 3 * 2] = {0};

// record sample time
static u32 accel_sampleTick = 0;

// record judge motion again flag
static bool accel_checkAgain;
// record motion flag and tick
static accel_motion_state_typeDef accel_motion = accel_motion_notfixed;
static u32 accel_motionTick = 0;

// double check period time
static u32 accel_DcheckTick;

/* Private code ---------------------------------------------------------*/

/*
*/
static u8 accel_readRegValue(u8 _regaddr)
{
  u8 ret = 0xFF;

  if(HAL_I2C_Master_Transmit(&hi2c1, ACCEL_CHIP_ADDR_R, &_regaddr, sizeof(_regaddr), 1000) == HAL_OK){
    HAL_I2C_Master_Receive(&hi2c1, ACCEL_CHIP_ADDR_R, &ret, sizeof(ret), 1000);
  }

  return ret;
}

/*
*/
static bool accel_writeRegValue(u8 _regaddr, u8 _value)
{
  bool ret = false;
  uint8_t buf[2] = {_regaddr, _value};

	// In blocking mode, write register.
    if(HAL_I2C_Master_Transmit(&hi2c1, ACCEL_CHIP_ADDR_W, &buf[0], sizeof(buf), 1000) == HAL_OK){
      ret = true;
    }

  return ret;
}

/*
  brief:
    1. config registers that we need;
    2. then auto start sample data;
*/
static bool accel_configReg(void)
{
#ifndef LiuJH_DEBUG
  return true;
#else

  bool ret = false;

	/* In blocking mode, config control registers. */

  // clear FIFO 32 * 4 old datas.
  if(!accel_writeRegValue(REG_ADDR_FIFO_CTRL, FIFO_CTRL_DATA_CLEAR))
    goto error;

  // need it???
//  HAL_Delay(10);

	// set INT ENABLE.
  if(!accel_writeRegValue(REG_ADDR_CTRL7, CTRL7_INT_ENABLE))
    goto error;

  // set FIFO sample data NUM threshold and FIFO mode.
  if(!accel_writeRegValue(REG_ADDR_FIFO_CTRL, FIFO_CTRL_VALUE))
    goto error;

  // route FIFO threshold to INT1 pad(notice to MCU)
  if(!accel_writeRegValue(REG_ADDR_CTRL4, CTRL4_INT1_ROUTE))
    goto error;

  // set sample freq ODR 12.5Hz.
  ret = accel_writeRegValue(REG_ADDR_CTRL1, CTRL1_ODR_12_5_HZ);

  // NOW: accel start working.

error:
  return ret;
#endif
}


/*
  brief:
    1. reset all registers to default values;
    2. 
*/
static bool accel_resetAllReg(void)
{
#ifndef LiuJH_DEBUG
  return true;
#else
  bool ret = false;

  ret = accel_writeRegValue(REG_ADDR_CTRL2, CTRL2_RESET_VALUE);
  // wait boot
//  HAL_Delay(5);

  return ret;
#endif
}

/*
  brief:
    1. boot chip(load correct trimming parameters);
    2. 
*/
static bool accel_rebootChip(void)
{
#ifndef LiuJH_DEBUG
  return true;
#else
  bool ret;

  // In blocking mode, write register.
  ret = accel_writeRegValue(REG_ADDR_CTRL2,CTRL2_BOOT_VALUE);

  // wait boot; need it???
//  HAL_Delay(5);

  return ret;
#endif
}


/*
  brief:
    1. read "Who am I" register value.
    2. 
*/
static bool accel_readDeviceID(void)
{
#ifndef LiuJH_DEBUG
  return true;
#else
  bool ret = false;
	uint8_t deviceid = 0;

	// In blocking mode, read "Who am I" register.
  deviceid = accel_readRegValue(REG_ADDR_DEVICE_ID);
  if(!(deviceid ^ DEVICE_ID_FIXED_VALUE))
    ret = true;

  return ret;
#endif
}

/*
  brief:
    1. _psrc point sample data of current axis, number is FIFO_DATA_NUM;
    2. judge if there is D-value of over 300;
    3. return counts of in [-100, 100];
*/
static bool accel_judgeAxisMotionState(int16 *_psrc, u32 *_pcount)
{
  bool ret = false;
  int16 buf[FIFO_DATA_NUM];
  int16 *pdest = &buf[0];
  // D-value
  int16 value;

  // merge int16 data from two u8 datas of Z_L + Z_H
  // get D-value of int16 data
  for(u32 i = 0; i < FIFO_DATA_NUM; i++){
    // get Z axis sample data
    *pdest = *_psrc >> 4;

    // is not the first value?
    if(i != 0){
      // get D-value
      value = *pdest - *(pdest - 1);

      // judge motion
      if(value > (mcu_getMotionCfg()->mcu_motionThreshold)
        || value < (-mcu_getMotionCfg()->mcu_motionThreshold)){
        // dont need check others data
        accel_motion = accel_motioning;
        ret = true;
        break;
      }

      // judge motionless
      if(value >= ACCEL_MOTIONLESS_MIN_VALUE && value <= ACCEL_MOTIONLESS_MAX_VALUE){
        (*_pcount)++;
      }
    }

    // continue get next data
    pdest++;
    _psrc += 3;
  }

  return ret;
}

/*
  brief:
    1. data in the buf: fifo_data_buf;
    2. animal motion direction: Z, Y, X;
    3. 
*/
static bool accel_judgeMotionState(void)
{
  bool ret = false;
  int16 *psrc;
  // count D-value num in [100, 100]
  u32 count = 0;
  u32 tick = HAL_GetTick();

  // start from Z axis
  psrc = (int16 *)&FIFO_data_buf[0] + 2;
  if(accel_judgeAxisMotionState(psrc, &count))
    return true;

  // continue judge Y axis
  psrc = (int16 *)&FIFO_data_buf[0] + 1;
  if(accel_judgeAxisMotionState(psrc, &count))
    return true;

  // continue judge X axis
  psrc = (int16 *)&FIFO_data_buf[0];
  if(accel_judgeAxisMotionState(psrc, &count))
    return true;

  /*
    NOW:
      all these D-value is [-300, 300];
      if all is in [-100, 100], it is motionless state;
      oterwise, return false, after 5s, check again
  */
  // judge count: ALL delta value is in [-100, 100]
  if(count >= ACCEL_D_VALUE_MAX_NUM){
    accel_motion = accel_motionless;
    ret = true;
  }
  tick = HAL_GetTick() - tick;

  return ret;
}

/*
  brief:
    1. 
*/
static bool accel_powerDown(void)
{
  bool ret = false;

  if(accel_writeRegValue(REG_ADDR_FIFO_CTRL, FIFO_CTRL_DATA_CLEAR)
    && accel_writeRegValue(REG_ADDR_CTRL1, CTRL1_POWER_DOWN))
    ret = true;

  return ret;
}


/*
*/
static void accel_smDoubleDelayProc(void)
{
  // delay time has expired?
  if(HAL_GetTick() >= accel_DcheckTick){
    // start sample again

    // NOTE: init something
    memset(&FIFO_data_buf, 0, sizeof(FIFO_data_buf));

    accelStatus = accel_startup_status;
  }
}

/*
  brief:
    1. accel_isMotionless and accel_motionTick are only VALID
      when the state is accel_dataReady_status;
    2. 
*/
static void accel_smDataReadyProc(void)
{
  accelIsworking = false;
}

/*
*/
static void accel_errorProc(void)
{
  accelIsworking = false;
}

/*
  brief:
    1. get data from accel chip;
    2. judge status of motion and set flag;
    3. power down accel chip for LMP;
    4. update accel status;
    5. 
*/
static void accel_smSampledProc(void)
{
  uint8_t reg_addr = REG_ADDR_OUT_X_L;  // REG_ADDR_FIFO_SAMPLES;

	// In blocking mode, read x_l, x_h, y_l, y_h, z_l, z_h, loops read these registers num times.
	if(HAL_I2C_Master_Transmit(&hi2c1, ACCEL_CHIP_ADDR_R, &reg_addr, sizeof(reg_addr), 1000) == HAL_OK
    && HAL_I2C_Master_Receive(&hi2c1, ACCEL_CHIP_ADDR_R, &FIFO_data_buf[0], sizeof(FIFO_data_buf), 10000) == HAL_OK){

    // record sampled tick
    accel_sampleTick = HAL_GetTick();

    // 2. judge status of motion and set flag;
    if(accel_judgeMotionState()){
      /*
        NOW: we set motion or motionless state,
        so we dont need this chip working
      */
      accel_motionTick = accel_sampleTick;

      // 3. power down accel chip for LMP;
      accel_powerDown();
      // 4. update accel status;
      accelStatus = accel_dataReady_status;
    }else{
      /*
        NOW: D-value is [100, 300] or [-300, -100],
        so we will sample and judge again only one time;
      */
      if(!accel_checkAgain){
        // delay som times and check again
        accel_DcheckTick = HAL_GetTick() + TIMEOUT_2S;
        accelStatus = accel_DcheckDelay_status;
      }else{
        // current check is the second check, so think it is in motionless stae
        accel_motion =  accel_motionless;
        accel_motionTick = accel_sampleTick;

        accelStatus = accel_dataReady_status;
      }
    }

    // only check two times
    accel_checkAgain = true;
	}else{
    accel_powerDown();
    accelStatus = accel_error_status;
	}
}

/*
  brief:
    1. 
*/
static void accel_smSamplingProc(void)
{
}

/*
  brief:
    1. config registers;
    2. start sampling motion data;
*/
static bool accel_smStartupProc(void)
{
  bool ret = false;

  // 1. read device ID
  if(!accel_readDeviceID())
    goto error;

  // 2. boot chip(load correct trimming parameters)
  // delay 5ms
  if(!accel_rebootChip())
    goto error;

  // 3. reset all registers to default values
  // delay 5ms
  if(!accel_resetAllReg())
    goto error;

  // 4. config registers that we need.
  if(!accel_configReg())
    goto error;

  ret = true;

  // update status
  accelStatus = accel_sampling_status;

  // start time of sample
//  accel_sampleTick = HAL_GetTick();

error:
  if(!ret){
    /* power down accel chip */
    accel_powerDown();
    accelStatus = accel_error_status;
  }

  return ret;
}

/*
  brief:
    1. status stay here until someone start up;
    2. 
*/
static void accel_smInitedProc(void)
{
}


/* Public code ---------------------------------------------------------*/

u8 accel_getMotionState(void)
{
  return (u8)accel_motion;
}

/*
*/
bool accel_isMotionless(void)
{
  bool ret = false;

  // Has got motion state and is motionless
  if(accel_motionTick && accel_motion == accel_motionless)
    ret = true;

  return ret;
}

/*
*/
bool accel_isworking(void)
{
  return accelIsworking;
}

/*
*/
void accel_stop(void)
{
  if(accelIsworking){
    accelIsworking = false;

    /* power down accel chip */
    accel_powerDown();

    accelStatus = accel_idle_status;
  }
}

/*
*/
void accel_startup(void)
{
  if(accelIsworking) return;

  memset(&FIFO_data_buf, 0, sizeof(FIFO_data_buf));
  accel_checkAgain = false;
  accel_DcheckTick = 0;

  accelIsworking = true;
  accelStatus = accel_startup_status;
}

/*
  brief:
    1. 
*/
void accel_init(void)
{
  memset(&FIFO_data_buf, 0, sizeof(FIFO_data_buf));
  accel_checkAgain = false;
  accel_DcheckTick = 0;
  accelIsworking = false;

  // update accel status
  accelStatus = accel_inited_status;
}

/**
  * @brief  EXTI line detection callbacks.
  * @param  GPIO_Pin Specifies the pins connected to the EXTI line.
  * @retval None
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  // is accel interrupt?
  // is current status?
  if(GPIO_Pin == CCM_PIN22_ACCEL_INT_Pin && accelStatus == accel_sampling_status)
    // update accel status
    accelStatus = accel_sampled_status;
}

/*
  brief:
    1. ACCEL state machine process;
    2. execute in while loop of main;
    3. 
*/
void accel_stateMachine(void)
{
  switch(accelStatus){
    case accel_idle_status:
      break;
    case accel_inited_status:
      accel_smInitedProc();
      break;
    // status stay here until someone start up
    case accel_startup_status:
      accel_smStartupProc();
      break;
    case accel_sampling_status:
      accel_smSamplingProc();
      break;
    case accel_sampled_status:
      accel_smSampledProc();
      break;
    case accel_dataReady_status:
      accel_smDataReadyProc();
      break;
    case accel_DcheckDelay_status:
      accel_smDoubleDelayProc();
      break;
    case accel_error_status:
      accel_errorProc();
      break;

    default:
      break;
  }
}




