/*
 * flash.h
 *
 *  Created on: 2023年11月18日
 *      Author: Johnny
 */

#ifndef INC_FLASH_H_
#define INC_FLASH_H_

/* macro define *****************************************************/


#define FLASH_SPI_TIMEOUT     TIMEOUT_10MS


#define FLASH_CS_ASSERTED     HAL_GPIO_WritePin(CCM_PIN25_MEM_CS_GPIO_Port, CCM_PIN25_MEM_CS_Pin, GPIO_PIN_RESET)
#define FLASH_CS_DEASSERTED   HAL_GPIO_WritePin(CCM_PIN25_MEM_CS_GPIO_Port, CCM_PIN25_MEM_CS_Pin, GPIO_PIN_SET)

#define FLASH_SPI_BUF_SIZE                  4
#define FLASH_INVALID_VALUE                 (-1)

#define FLASH_SIZE_8KB                      (8 * 1024)
#define FALSH_SIZE_32KB                     (32 * 1024)
// flash max address
#define FLASH_MAX_ADDRESS                   (0x07FFFFFF)
#define FLASH_ONE_PAGE_SIZE                 256

// 1. sysinfo addr (sector 9(8KB))
#define FLASH_SYS_INFO_START_ADDR           (0x07A000)
#define FLASH_SYS_INFO_END_ADDR             (0x07BFFF)
#define FLASH_SYS_INFO_SIZE                 FLASH_SIZE_8KB
/* R septum sector 0, 1, 2 */
#define FLASH_RS_BLOCK32_NUM                6
// 2. R septum 1
#define FLASH_RS1_START_ADDR                (0x000000)
#define FLASH_RS1_END_ADDR                  (0x007FFF)
// 3. R septum 2
#define FLASH_RS2_START_ADDR                (0x008000)
#define FLASH_RS2_END_ADDR                  (0x00FFFF)
// 4. R septum 3
#define FLASH_RS3_START_ADDR                (0x010000)
#define FLASH_RS3_END_ADDR                  (0x017FFF)
// 5. R septum 4
#define FLASH_RS4_START_ADDR                (0x018000)
#define FLASH_RS4_END_ADDR                  (0x01FFFF)
// 6. R septum 5
#define FLASH_RS5_START_ADDR                (0x020000)
#define FLASH_RS5_END_ADDR                  (0x027FFF)
// 7. R septum 6
#define FLASH_RS6_START_ADDR                (0x028000)
#define FLASH_RS6_END_ADDR                  (0x02FFFF)
#define FLASH_RS_BLOCK_SIZE                 FALSH_SIZE_32KB
/* RV sector 3, 4, 5 */
#define FLASH_RV_BLOCK32_NUM                6
// 8. RV 1
#define FLASH_RV1_START_ADDR                (0x030000)
#define FLASH_RV1_END_ADDR                  (0x037FFF)
// 9. RV 2
#define FLASH_RV2_START_ADDR                (0x038000)
#define FLASH_RV2_END_ADDR                  (0x03FFFF)
// 10. RV 3
#define FLASH_RV3_START_ADDR                (0x040000)
#define FLASH_RV3_END_ADDR                  (0x047FFF)
// 11. RV 4
#define FLASH_RV4_START_ADDR                (0x048000)
#define FLASH_RV4_END_ADDR                  (0x04FFFF)
// 12. RV 5
#define FLASH_RV5_START_ADDR                (0x050000)
#define FLASH_RV5_END_ADDR                  (0x057FFF)
// 13. RV 6
#define FLASH_RV6_START_ADDR                (0x058000)
#define FLASH_RV6_END_ADDR                  (0x05FFFF)
#define FLASH_RV_BLOCK_SIZE                 FALSH_SIZE_32KB
/* RA sectot 6 */
#define FLASH_RA_BLOCK32_NUM                2
// 14. RA 1
#define FLASH_RA1_START_ADDR                (0x060000)
#define FLASH_RA1_END_ADDR                  (0x067FFF)
// 15. RA 2
#define FLASH_RA2_START_ADDR                (0x068000)
#define FLASH_RA2_END_ADDR                  (0x06FFFF)
#define FLASH_RA_BLOCK_SIZE                 FALSH_SIZE_32KB
/*
  sector 7  (32KB)
  sector 8  (8KB)
  sector 10 (16KB)
  reserved
*/

// Rx ecg flag
#define FLASH_RA_ECG_FLAG                   1
#define FLASH_RS_ECG_FLAG                   2
#define FLASH_RV_ECG_FLAG                   3

// block info 1024 bytes + ecg data
#define FLASH_R_DATA_OFFSET                 (0x400)


/* spi flash Opcode */

// Read Manufacturer and Device ID
#define FLASH_OP_READ_DEV_ID                0x9F
#define FLASH_DEV_ID_DEFAULT_VALUE          0x0144
#define FLASH_DEV_ID_INDEX                  1

// enter Deep Power-down mode
#define FLASH_OP_DPM                        0xB9
// flash is ready, delay sometime, enter dpm
#define FLASH_DPM_PERIOD                    TIMEOUT_1S
// Resume from Deep Power-down
#define FLASH_OP_RESUME                     0xAB

//  Read Status Register
#define FLASH_OP_READ_STATUS                0x05
#define FLASH_SOFT_PROTECTED_SOME           0x04
#define FLASH_SOFT_PROTECTED_ALL            0x0C
#define FLASH_STATUS_BIT_BUSY               0x01

// Write Enable
#define FLASH_OP_WRITE_ENABLE               0x06

// Write Status Register
#define FLASH_OP_WRITE_STATUS               0x01

// Read Array
#define FLASH_OP_READ_ARRAY                 0x03

// Block Erase (32 Kbytes)
#define FLASH_OP_ERASE_32KB                 0x52

// Byte/Page Program (1 to 256 Bytes)
#define FLASH_OP_PROGRAM                    0x02



/* type define ******************************************************/

typedef enum{
  /*
    1. read and check or update devID and SR;
    2. enter ready status;
  */
  flash_initing_status,
  /*
    1. Any operation is over, can enter this status;
    2. check SR is NOT busy;
    3. check DPM is not;
    4. enter DPM;
    5. go into ready status;
  */
  flash_readyWaiting_status,
  /*
    1. set period timeout(10ms??);
    2. enter ready with DPM;
  */
  flash_ready_status,
  /*
    1. resume flash from DPM for reading data;
  */
  flash_resumeForRead_status,
  /*
    1. loop check and lock spi for making read action;
    2. go into reading status, waiting result;
  */
  flash_readData_status,
  /*
  */
  flash_reading_status,
  /*
    1. loop check and lock spi for get Rsv sectors info;
    2. if full, erase the oldest block;
    3. return addr, and record sector info;
  */
  flash_startupStore_status,
  /*
    1. loop check SR status for waiting erase;
    2. check flash_RvIegmAdcChNum, do same work with flash_RsIegmAdcChNum;
  */
  flash_erasingForStore,
  /*
    1. loop check SR status for waiting Rs block info write OK;
    2. program Rv block info;
    3. update status, start program ecg data;
  */
  flash_programmingRs_status,
  /*
  */
  flash_programming_status,
  /*
    1. loop check SR status for programming;
    2. write data len;
    3. update status, we can dpm;
  */
  flash_programmed_status,



  /*
  */
  flash_error_status,

  flash_max_status
} flash_status_typeDef;

/*
  sys info struct define
    erased flag     1B
    all items struct list

    every item struct:
      programmed flag 1B + ...
*/
typedef struct{
  u8 eraseFlag;

  // first item
  

} flash_sysInfo_typeDef;

/*
  block(32KB) struct define
  1. the first block(4KB) struct
    erased flag       1B
    programmed flag   1B
    Rwave struct info
      kind flag of R    1B
      date and time     6B
        year + month + day + hour + minute + second
      other info...
    other bytes reserved in this blocks(4KB);
  2. others bolck padding ecg data 2 minutes;
*/
typedef struct{
  u8 eraseFlag;
//  u8 programmedFlag;
  u8 startYear, startMonth, startDay;
  u8 startHour, startMinute, startSecond;
  // only compare: ((((year * 12 + month) * 31 + day) * 24 + hours) * 60 + minute) + second
  u32 startSecondCount;
  // ecg data addr of the first byte
  u32 dataStartAddr;

  /* datalen MUST BE the last segment of this struct */
  // ecg data len
  u32 datalen;
} flash_RecgInfo_typeDef;

/*
*/
typedef struct{
  // sys info
  flash_sysInfo_typeDef flash_sysInfo;
  // Ra blocks info
  flash_RecgInfo_typeDef flash_RaIegmInfo[FLASH_RA_BLOCK32_NUM];
  // Rs blocks info
  flash_RecgInfo_typeDef flash_RsIegmInfo[FLASH_RS_BLOCK32_NUM];
  // Rv blocks info
  flash_RecgInfo_typeDef flash_RvIegmInfo[FLASH_RV_BLOCK32_NUM];
} flash_sectorInfo_typeDef;


/* extern public function **************************************************/


extern void flash_stateMachine(void);
extern void flash_init(void);
extern bool flash_isReady(void);
extern bool flash_isAdcStoreCh(u8 _adcCurChNum);
extern void flash_adcConvCplCB(u8 _adcCurChNum);
extern void flash_stopFlashStore(void);
extern void flash_startFlashStore(void);
extern void flash_startFlashStoreTwins(void);
extern bool flash_readData(u32 _startAddr, u8 *_prx, u16 _len);
extern bool flash_isEnterLpm(void);




#endif /* INC_FLASH_H_ */
