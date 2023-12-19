/*
 * i2cprocess.h
 *
 *  Created on: 2023年8月15日
 *      Author: Johnny
 */

#ifndef INC_ACCEL_H_
#define INC_ACCEL_H_


/* Includes ------------------------------------------------------------------*/


/* Public defines -----------------------------------------------------------*/

// Accel sensor define

#define ACCEL_CHIP_ADDR_R           0x33
#define ACCEL_CHIP_ADDR_W           0x32

#define REG_ADDR_DEVICE_ID          0x0F
#define DEVICE_ID_FIXED_VALUE       0x44

#define REG_ADDR_CTRL2              0x21
#define CTRL2_BOOT_VALUE            0x84
#define CTRL2_RESET_VALUE           0x44

#define REG_ADDR_CTRL7              0x3F
#define CTRL7_INT_ENABLE            0x20

#define REG_ADDR_FIFO_CTRL          0x2E
/*  FMode[2:0] FTH[4:0]
    000: Bypass mode: FIFO turned off.
    001: FIFO mode: Stops collecting data when FIFO is full.
    01010: we need FIFO value number is 10(dont need 32). 0x2A; 0x0A;  718 ms
    01100:         FIFO value number is 12(dont need 32). 0x2C; 0x0C;  879 ms
    01101:         FIFO value number is 13(dont need 32). 0x2D; 0x0D;  958 ms
    01110:         FIFO value number is 14(dont need 32). 0x2E; 0x0E; 1038 ms
    01111:         FIFO value number is 15(dont need 32). 0x2F; 0x0F; 1117 ms
    10000:         FIFO value number is 16(dont need 32). 0x30; 0x10; 1198 ms
    10100:         FIFO value number is 20(dont need 32). 0x34; 0x14; 1517 ms
*/
#define FIFO_CTRL_VALUE             0x2E
#define FIFO_CTRL_DATA_CLEAR        0x0E

#define REG_ADDR_FIFO_SAMPLES       0x2F

#define REG_ADDR_CTRL4              0x23
//  INT1_FTH bit set to 1.
#define CTRL4_INT1_ROUTE            0x02

#define REG_ADDR_CTRL1              0x20
// power down value
#define CTRL1_POWER_DOWN            0x00
// sample value(ODR = 12.5Hz)
#define CTRL1_ODR_12_5_HZ           0x20

#define REG_ADDR_OUT_X_L            0x28

// low 5 bits is num
#define FIFO_DATA_NUM               (FIFO_CTRL_VALUE & 0x1F)

// motionless value
#define ACCEL_MOTIONLESS_MAX_VALUE  100
#define ACCEL_MOTIONLESS_MIN_VALUE  (-100)
// D-value max num
#define ACCEL_D_VALUE_MAX_NUM       ((FIFO_DATA_NUM - 1) * 3)

/* Exported types ------------------------------------------------------------*/

typedef enum{
  accel_idle_status,
  /*
  */
  accel_inited_status,
  /*
  */
  accel_startup_status,
  /*
  */
  accel_sampling_status,
  /*
  */
  accel_sampled_status,
  /*
  */
  accel_DcheckDelay_status,
  /*
  */
  accel_dataReady_status,
  /*
  */
  accel_error_status,

  accel_max_status
} accel_stateTypeDef;

typedef enum{
  accel_motion_notfixed,
  accel_motioning,
  accel_motionless
} accel_motion_state_typeDef;


/* Exported constants --------------------------------------------------------*/


/* Exported macro ------------------------------------------------------------*/


/* Exported functions prototypes ---------------------------------------------*/
extern void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);
extern void accel_stateMachine(void);
extern void accel_init(void);
extern void accel_startup(void);
extern bool accel_isworking();
extern void accel_stop(void);
extern bool accel_isMotionless(void);
extern u8 accel_getMotionState(void);




#endif /* INC_ACCEL_H_ */
