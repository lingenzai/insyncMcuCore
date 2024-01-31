/*
 * ble_rsl10.h
 *
 *  Created on: 2023年11月9日
 *      Author: Johnny
 */

#ifndef INC_BLE_H_
#define INC_BLE_H_

/* define ----------------------------------------------------------------- */

#define BLE_CS_ASSERTED     HAL_GPIO_WritePin(CCM_PIN38_BLE_CS_GPIO_Port, CCM_PIN38_BLE_CS_Pin, GPIO_PIN_RESET)
#define BLE_CS_DEASSERTED   HAL_GPIO_WritePin(CCM_PIN38_BLE_CS_GPIO_Port, CCM_PIN38_BLE_CS_Pin, GPIO_PIN_SET)

// DONT MODIFY this value, SPI1 slave of RSL10 is same value: 16.
#define SPI_SEND_NUM        16

// timout of every blocked mode txrx through spi
#define BLE_SPI_TIMEOUT           TIMEOUT_50MS
// interval time between two query command through SPI
#define BLE_QUERY_TIMEOUT         TIMEOUT_100MS
// timeout, we will enter idle status for LPM
#define BLE_WAITING_TIMEOUT       TIMEOUT_30S



/* type define ----------------------------------------------------------- */

/*
// ble key value typeDef
typedef struct{
  // 
} ble_kv_typeDef;
*/

// ble working status
typedef enum{
  /*
    1. mcu init completed, so go into this status;
    2. if magnet wakeup, so startup RSL10 chip;
    3. go into Rsl10Waiting status;
    4. otherwise, go into idle status waiting LPM;
  */
  ble_inited_status = 1,
  /*
    1. do nothing; waiting mcu go into LPM;
  */
  ble_idle_status,
  /*
    1. waiting somtimes for next status;
  */
  ble_Rsl10Waiting_status,
  /*
    1. waiting request from RSL10;
    2. read command from SPI each 100ms;
    3. if timeout(30s?), go to LPM;
    4. go into different smproc for different command;
  */
  ble_reqWaiting_status,
  /*
    1. deal with all valid command;
    2. 
  */
  ble_reqDealWith_status,
  /*
    1. wait ovbc is NOT working;
    2. set Vout_set for next pulse;
  */
  ble_setVoutSet_status,
  /*
    1. sending real time ecg data to RSl10 through SPI;
    2. if get stop instruction, goto reqWaiting status;
  */
  ble_reqRTecg_status,
  /*
    1. RTecg start working;
    2. Transfer adc data to RSl10 through SPI;
  */
  ble_RTecg_working_status,
  /*
    1. sending store ecg in spi flash to RSL10 through SPI;
    2. if sending completed, goto reqWaiting status;
  */
  ble_reqSTecg_status,
  /*
    1. enter ota status, nothing to do, but is working;
    2. for waiting rsl10 fota without LPM;
  */
  ble_reqFota_status,


  ble_max_status
} ble_status_typeDef;

/* extern public function ------------------------------------------------- */

extern void ble_spiTxRxCpltCB(SPI_HandleTypeDef *hspi);
extern void ble_adcConvCpltCB(void);
extern void ble_stateMachine(void);
extern bool ble_isWorking(void);
extern void ble_init(void);
extern bool ble_isBleAdcCh(u8 _curCh);
extern void ble_setPulseShowFlag(void);
extern void ble_resetRSL10(void);



#endif /* INC_BLE_H_ */
