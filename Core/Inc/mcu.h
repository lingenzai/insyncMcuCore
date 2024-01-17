/*
 * mcuStateMachine.h
 *
 *  Created on: 2023年10月11日
 *      Author: Johnny
 */

#ifndef INC_MCU_H_
#define INC_MCU_H_

/* If we use wakeup config is DIV16 */
// 37 * 1000 / 16 = 2368
//#define RTC_WAKEUP_TIME         2368
// 32768 / 16 = 2048
#define RTC_WAKEUP_TIME         2048
// DONT add ()
#define RTC_WAKEUP_TIME_1ms     RTC_WAKEUP_TIME / 1000
#define RTC_WAKEUP_COUNT_1S     (1 * RTC_WAKEUP_TIME)
#define RTC_WAKEUP_TIME_2S      (2 * RTC_WAKEUP_TIME)
#define RTC_WAKEUP_TIME_3S      (3 * RTC_WAKEUP_TIME)
#define RTC_WAKEUP_TIME_5S      (5 * RTC_WAKEUP_TIME)
#define RTC_WAKEUP_TIME_10S     (10 * RTC_WAKEUP_TIME)
#define RTC_WAKEUP_TIME_20S     (20 * RTC_WAKEUP_TIME)

#define TIMEOUT_10MS            10
#define TIMEOUT_20MS            20
#define TIMEOUT_50MS            50
#define TIMEOUT_100MS           100
#define TIMEOUT_150MS           150
#define TIMEOUT_180MS           180
///*
#define TIMEOUT_200MS           200
#define TIMEOUT_350MS           350
#define TIMEOUT_500MS           500
#define TIMEOUT_600MS           600
//*/
#define TIMEOUT_800MS           800
#define TIMEOUT_1S              1000
#define TIMEOUT_2S              2000
#define TIMEOUT_2P5S            2500
#define TIMEOUT_5S              5000
#define TIMEOUT_10S             10000
#define TIMEOUT_15S             15000
#define TIMEOUT_20S             20000   // AnimalTest release version value
#define TIMEOUT_30S             30000
// one minute timeout
//#define TIMEOUT_60S             60000
// 2 minutes timeout
//#define TIMEOUT_120S            120000
// 10 minutes timeout
#define TIMEOUT_600S            600000

#define BUFFER_SIZE_1KB         1024
#define BUFFER_SIZE_512B        512
#define BUFFER_SIZE_256B        256

// ask RSL10 enter LPM times
#define ASK_RSL10_LPM_NUM           3
// RTC wakeup timer max second time(18 hours)
#define RTC_WKUP_TIMER_COUNT_MAX    (64800) // (18 * 60 * 60)
// max seconds of each day(24 * 60 * 60)
#define MCU_MAX_SECOND_EACH_DAY     (86400) // (24 * 60 * 60)
// max minutes of each day(24 * 60)
#define MCU_MAX_MINUTE_EACH_DAY     (1440)  // (24 * 60)

// data struct stored in EEPROM or spi flash is valid flag value
#define MCU_DATA_STRUCT_VALID_VALUE 0x4F  // ascii code of "O" of "OK"

#define MCU_SPI_BLE             mcu_spi_bleSending_status
#define MCU_SPI_FLASH           mcu_spi_flashSending_status

// accel redo period default value(15S)
#define MCU_MOTION_PERIOD_DEFAULT     15
// accel motion and motionless threshold default value
#define MCU_MOTION_THRESHOLD_DEFAULT  300

/*
*/
typedef enum{
  mcu_rstFlag_others,

  // donot make sure reason(RTC or Magnet?)
  mcu_rstFlag_wakeup,
  mcu_rstFlag_RtcTimer,
  mcu_rstFlag_MagnetHall,

  mcu_rstFlag_max
} mcu_resetFlag_typeDef;

/*
*/
typedef enum{
  /*
  */
  mcu_spi_idle_status,
  /*
  */
  mcu_spi_bleSending_status,
  /*
  */
  mcu_spi_flashSending_status,


  mcu_spi_max_status
} mcu_spiStatus_typeDef;

/*
Data Format:
       Data：04 - Vout output low 
             05 - Vout output high 
             06 - Vout float status
*/
typedef enum{
  mcu_Vos_OutL_status = 4,
  mcu_Vos_OutH_status,
  mcu_Vos_Input_status
} mcu_VoutsetStatus_typeDef;

/*
*/
typedef struct{
  // this struct is valid flag
  u8 isValid;

  mcu_VoutsetStatus_typeDef value;
} mcu_Voutset_typeDef;

/*
*/
typedef struct{
  u8 isValid;

  // accel chip redo period(unit: second, default 15)
  u16 mcu_motionPeriod;

  // motion and motionless threshold(default: 300)
  u16 mcu_motionThreshold;  
} mcu_MotionConfig_typeDef;


extern SPI_HandleTypeDef hspi2;
extern ADC_HandleTypeDef hadc;
extern RTC_HandleTypeDef hrtc;
extern I2C_HandleTypeDef hi2c1;
//extern WWDG_HandleTypeDef hwwdg;
extern TIM_HandleTypeDef htim6;

extern mcu_resetFlag_typeDef mcu_rstFlag;
extern RTC_DateTypeDef mcu_date;
extern RTC_TimeTypeDef mcu_time;


extern void mcu_sysInit(void);
extern void mcu_deviceInit(void);
extern void mcu_startup(void);
extern void mcu_allStateMachine(void);
extern void mcu_RtcTimerWkupCB(RTC_HandleTypeDef *hrtc);
extern bool mcu_spiRxUnblocked(u8 _devID, u8 *_prx, u16 _len);
extern bool mcu_spiIsReady(void);
extern void mcu_unlockSpi(void);
extern bool mcu_tryLockSpi(u8 _devID);
extern mcu_Voutset_typeDef *mcu_getVoutset(void);
extern mcu_MotionConfig_typeDef *mcu_getMotionCfg(void);
extern void mcu_setVoutsetPin(u8 _setvalue);
extern void mcu_calibrateVoutset(void);
extern void mcu_calibrateMotionPeriod(void);


#endif /* INC_MCU_H_ */
