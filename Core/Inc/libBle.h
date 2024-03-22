/*
 * libBle.h
 *
 *  Created on: Mar 19, 2024
 *      Author: Johnny
 */

#ifndef CORE_LIBBLE_H_
#define CORE_LIBBLE_H_


/* define ----------------------------------------------------------------- */

#define BUFFER_SIZE_512B              512
#define BUFFER_SIZE_256B              256


#define BLE_CS_ASSERTED               (mcu_bleCsAsserted())
#define BLE_CS_DEASSERTED             (mcu_bleCsDeasserted())

// DONT MODIFY this value, SPI1 slave of RSL10 is same value: 16.
#define SPI_SEND_NUM                  16

// timout of every blocked mode txrx through spi
#define BLE_SPI_TIMEOUT               TIMEOUT_50MS
// interval time between two query command through SPI
#define BLE_QUERY_TIMEOUT             TIMEOUT_100MS
// timeout, we will enter idle status for LPM
#define BLE_WAITING_TIMEOUT           TIMEOUT_30S


#define BLE_INVALID_BYTE              (0xFF)




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

extern void ble_adcConvCpltCB(void);
extern void ble_stateMachine(void);
extern bool ble_Rsl10ChipIsLpm(void);
extern bool ble_isWorking(void);
extern void ble_init(void);
extern bool ble_isBleAdcCh(u8 _curCh);
extern void ble_setPulseShowFlag(void);


/* called function ************************************************/
// function used in lib

/*
  NOTE:
    HAL_GPIO_WritePin(CCM_PIN38_BLE_CS_GPIO_Port, CCM_PIN38_BLE_CS_Pin, GPIO_PIN_RESET)
*/
void mcu_bleCsAsserted(void);
/*
  NOTE:
    HAL_GPIO_WritePin(CCM_PIN38_BLE_CS_GPIO_Port, CCM_PIN38_BLE_CS_Pin, GPIO_PIN_SET)
*/
void mcu_bleCsDeasserted(void);
/*
  NOTE:
    // use NRESET pin of RSL10 chip to wakeup RSL10 if magnethall exist(Rising edge)
    HAL_GPIO_WritePin(CCM_PIN18_RSL10_RST_GPIO_Port, CCM_PIN18_RSL10_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(CCM_PIN18_RSL10_RST_GPIO_Port, CCM_PIN18_RSL10_RST_Pin, GPIO_PIN_SET);
*/
void mcu_resetRsl10(void);
/*
  NOTE:
    HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout)
*/
bool mcu_halSpiTransmit(u8 *_pdata, u16 _len, u32 _timeout);
/*
  NOTE:
    HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi, uint8_t *pTxData, uint8_t *pRxData, uint16_t Size, uint32_t Timeout)
    if reuturn HAL_OK update to true, otherwise return false;
*/
bool mcu_halSpiTransmitReceive(u8 *_ptx, u8 *_prx, u16 _len, u32 _timeout);
/*
  NOTE:
    ecg_getBpm()
*/
u8 mcu_ecgGetBpm(void);
/*
  NOTE:
    ecg_getStatus()
*/
u8 mcu_ecgGetStatus(void);
/*
  NOTE:
    ecg_getAdcWeightGain()
*/
ecg_AdcWeightGain_typeDef *mcu_ecgGetAdcWeightGain(void);
/*
  NOTE:
    void fpulse_startupPulsing(u8 _width)
*/
void mcu_fpulseStartupPulsing(u8 _width);
/*
  NOTE:
    void ecg_getAdcPeakValue(u16 *_pmax, u16 *_pmin);
*/
void mcu_ecgGetAdcPeakValue(u16 *_pmax, u16 *_pmin);
/*
  NOTE:
    ppulse_unpulsing_period_typeDef pulse_getUnpulsingPeriod(void)
*/
ppulse_unpulsing_period_typeDef mcu_pulseGetUnpulsingPeriod(void);
/*
  NOTE:
    bool ecg_getRsviAbout(u8 *_pdata);
*/
bool mcu_ecgGetRsviAbout(u8 *_pdata);
/*
  NOTE:
    void ble_reqWriteDateTime(u8 _reqId);
*/
void mcu_bleReqWriteDateTime(u8 *_pdata);
/*
  NOTE:
    void ble_reqReadDateTime(u8 _reqId);
*/
u8 *mcu_bleReqReadDateTime(u8 *_pdata);
/*
  NOTE:
    ppulse_config_typeDef pulse_getConfig(void);
*/
ppulse_config_typeDef mcu_pulseGetConfig(void);
/*
  NOTE:
    bool fpulse_isWorking(void);
*/
bool mcu_fpulseIsWorking(void);
/*
  NOTE:
    bool wpr_isCharging(void);
*/
bool mcu_wprIsCharging(void);
/*
  NOTE:
    void wpr_setChargeSwitch(bool _isOn);
*/
void mcu_wprSetChargeSwitch(bool _isOn);
/*
  NOTE:
    void pulse_bleConfigPulseOn(bool _isOn);
*/
void mcu_pulseBleConfigPulseOn(bool _isOn);
/*
  NOTE:
    u8 wpr_getBattPercent(void);
*/
u8 mcu_wprGetBattPercent(void);
/*
  NOTE:
    u8 accel_getMotionState(void);
*/
u8 mcu_accelGetMotionState(void);
/*
  NOTE:
    bool pulse_blePulsingIsOn(void);
*/
bool mcu_pulseBlePulsingIsOn(void);
/*
  NOTE:
    void adc_startup(void);
*/
void mcu_adcStartup(void);
/*
  NOTE:
    bool ovbc_isWorking(void);
*/
bool mcu_ovbcIsWorking(void);
/*
  NOTE:
    bool fovbc_isWorking(void);
*/
bool mcu_fovbcIsWorking(void);
/*
  NOTE:
    
*/
bool mcu_isBleRstMode(void);
bool mcu_noSleepTest(void);


#endif /* CORE_LIBBLE_H_ */
