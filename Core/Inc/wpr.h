/*
 * wpr.h
 *
 *  Created on: 2023年11月18日
 *      Author: Johnny
 */

#ifndef INC_WPR_H_
#define INC_WPR_H_


/* macro define *****************************************************/

// battery level update time(unit: ms)
#define WPR_BATT_UPDATE_PERIOD          TIMEOUT_5S //  TIMEOUT_15S // TIMEOUT_20S // TIMEOUT_60S
/*
  We sample 6 channels, so interval time is 4ms per point
  WPR_BATT_LEVEL_UPDATE_TICK / 4 = 1250
*/
#define WPR_BATT_UPDATE_COUNT           1250
// first sample time is 100ms
#define WPR_BATT_UPDATE_COUNT1          25

#define WPR_INVALID_VALUE               (-1)

#define WPR_ADCBUF_SIZE                 32

// ultra low battery value
#define WPR_BATT_LOW_THRESHOLD          3100  // 35% * 12 + 3000 = 3420  // 3100
// full charged battery value
#define WPR_BATT_HIGH_THRESHOLD         4200


/* adc max value(corresponding 4.2V, it is max voltage of ccm project) */
#define WPR_ADC_MAX_VALUE               2838

// adc value of 4.1V
//#define WPR_ADC_HIGH_VALUE              2768
// adc value of 3.1V
//#define WPR_ADC_LOW_VALUE               2094

/* adc min value(corresponding 3.0V, it is min voltage of ccm project) */
#define WPR_ADC_MIN_VALUE               2026
// 
#define WPR_ADC_STEP                    (WPR_ADC_MAX_VALUE - WPR_ADC_MIN_VALUE) / 100


// I2C device address of STWLC38
#define WPR_CHIP_ADDR_R                 0xC3
#define WPR_CHIP_ADDR_W                 0xC2

/* register address */
// chip id byte0
#define WPR_REG_ADDR_CHIP_ID            0x0000  // 1
#define WPR_CHIP_ID_DEFAULT_VALUE       0x26

/*
opration mode: default is 2
  1: deubg mode;
  2: Rx mode;
  3: Tx mode;
*/
#define WPR_REG_ADDR_OPRATION_MODE      0x000E  // 1

// System command
#define WPR_REG_ADDR_SYS_COMMAND        0x0020  // 1

// have four bytes [2C, 2F]
#define WPR_REG_ADDR_SYS_ERROR          0x002C  // 4

/*
  we can read once from 0x0092 to 0x0095
*/
// VRECT unit: mV two register [0x92, 0x93]
#define WPR_REG_ADDR_VRECT_MV           0x0092  // 2
// VOUT unit: mv two register [0x94, 0x95]
#define WPR_REG_ADDR_VOUT_MV            0x0094  // 2

// RX received power in mW; two register [0xA6, 0xA7]
#define WPR_REG_ADDR_POWER_RX           0x00A6  // 2

/*
  1. RX interrupts enable;
  2. three register valid [0x80, 0x82];
*/
#define WPR_REG_ADDR_RX_INT_EN          0x0080  // 3
/*
  1. RX interrupt Clear;
  2. three register valid [0x84, 0x86];
*/
#define WPR_REG_ADDR_RX_INT_CLEAR       0x0084  // 3
/*
  1. RX interrupt latch;
  2. three register valid [0x88, 0x8A];
*/
#define WPR_REG_ADDR_RX_INT_LATCH       0x0088  // 3
/*
  1. RX interrupt status;
  2. three register valid [0x8C, 0x8E];
*/
#define WPR_REG_ADDR_RX_INT_STATUS      0x008C  // 3
/*
  1. RX commands;
*/
#define WPR_REG_ADDR_RX_COMMAND1        0x0090
#define WPR_REG_ADDR_RX_COMMAND2        0x00CF
/*
  1. RX BPP VOUT SET;
  2. two register valid [0xB1, 0xB2];
  3. Example :
      VOUT SET = 5V ; 0x00B2 = 2D ; 0x00B1 [7..6] = 00
      VOUT SET = 5.075V ; 0x00B2 = 2D; 0x00B1[7..6] = 11
*/
#define WPR_REG_ADDR_RX_VOUT_SET        0x00B1  // 2
/*
  1. RX LDO configuration;
  2. seven register valid [0xC8, 0xCE];
*/
#define WPR_REG_ADDR_RX_LDO_CONFIG      0x00C8  // 7
/*
  1. Rx Power Transfer Contract;
  2. two register valid [0xAA, 0xAB];
*/
#define WPR_REG_ADDR_RX_POWER_CONTRACT  0x00AA  // 2



/* type define ******************************************************/

typedef enum{
  wpr_chargeSwitch_off,
  wpr_chargeSwitch_on,
} wpr_chargeSwitch_typeDef;

typedef enum {
  /*
    1. for LPM;
  */
  wpr_idle_status,
  /*
  */
  wpr_inited_status,
  /*
    1. startup Batt measurement ADC sample;
    2. deal with stwlc38 for charge;
    3. goto charge waiting status;
  */
  wpr_startup_status,
  /*
    1. check charge flag set by ble;
    2. goto charge state if flag set;
    3. loop waiting;
  */
  wpr_Waiting_status,
  /*
    1. start charging;
    2. goto charging status;
  */
  wpr_startCharge_status,
  /*
    1. check charge flag setting by ble;
    2. if charge flag reset, goto next state;
    3. check charging fully or not flag;
    4. if fully charged, goto next state;
  */
  wpr_charging_status,
  /*
    1. stop charge;
    2. deal with stwlc38;
    3. goto waiting state;
  */
  wpr_stopCharge_status,


  wpr_max_status
} wpr_status_typeDef;


/* export function **************************************************/

extern void wpr_adcConvCpltCB(u32 _adcvalue);
extern void wpr_stateMachine(void);
extern void wpr_init(void);
extern void wpr_startup(void);
extern void wpr_shutdown(void);
extern void wpr_setChargeSwitch(bool _isOn);
extern bool wpr_isCharging(void);
extern bool wpr_isUltraLowBattLevel(void);
extern u8 wpr_getBattPercent(void);


#endif /* INC_WPR_H_ */
