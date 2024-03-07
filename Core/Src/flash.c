/*
 * flash.c
 *
 *  Created on: 2023年11月18日
 *      Author: Johnny
 */

#include "main.h"

static flash_status_typeDef flash_status;
// enter deep power down mode flah
static bool flash_isDpm;
// record spi data buf
static u8 flash_spiTxBuf[FLASH_SPI_BUF_SIZE];
static u8 flash_spiRxBuf[FLASH_SPI_BUF_SIZE];
// DPM period
static u32 flash_dpmTick;
// record read or write flash param
static u32 flash_userStartAddr;
static u8 *flash_pUserData;
static u16 flash_userDataLen;
// adc twins channel
static u8 flash_RsIegmAdcChNum;
static u8 flash_RvIegmAdcChNum;
// adc startup flash store flag
// adc max/min value pair
static int32_t flash_adcMaxValue, flash_adcMinValue;
// adc peak value in 2.5s
static int32 flash_adcPeakMaxValue, flash_adcPeakMinValue;
// trim adc peak timeout tick
static u32 flash_adcTrimPeakTick;
// all of flash sector info
static flash_sectorInfo_typeDef flash_sectorInfo;
// record current Rs index
static u8 flash_RsInfoIndex;
// record current Rv index
static u8 flash_RvInfoIndex;

// buffer to adc sample datas transfered to flash through SPI.
#ifdef LiuJH_ECG
static u8 flash_RsAdcDataBuf[100];
#else
static u8 flash_RsAdcDataBuf[BUFFER_SIZE_512B];
#endif
//static u32 flash_RsBufSize = sizeof(flash_RsAdcDataBuf);
static u8 *flash_pRsBufStart = flash_RsAdcDataBuf;
static u8 *flash_pRsBufEnd = flash_RsAdcDataBuf;
static u32 flash_RsBufNum = 0;
static bool flash_RsWriteLocking;
// record current time write data num into flash
static u16 flash_RsWrittenNum;

#ifdef LiuJH_ECG
static u8 flash_RvAdcDataBuf[100];
#else
static u8 flash_RvAdcDataBuf[BUFFER_SIZE_512B];
#endif
//static u32 flash_RvBufSize = sizeof(flash_RvAdcDataBuf);
static u8 *flash_pRvBufStart = flash_RvAdcDataBuf;
static u8 *flash_pRvBufEnd = flash_RvAdcDataBuf;
static u32 flash_RvBufNum = 0;
static bool flash_RvWriteLocking;
// record current time write data num into flash
static u16 flash_RvWrittenNum;



/* private function define ******************************************/


/*
  brief:
    1. send op only to flash through in blocked mode;
    2. dont need receive any data;
*/
static bool flash_writeOp(u8 _op)
{
  bool ret = false;

  FLASH_CS_ASSERTED;
  if(HAL_SPI_Transmit(&hspi2, &_op, sizeof(_op), FLASH_SPI_TIMEOUT) == HAL_OK)
    ret = true;
  FLASH_CS_DEASSERTED;

  return ret;
}

/*
  brief:
    1. send op and address to flash through in blocked mode;
    2. dont need receive any data;
*/
static bool flash_writeOpAddr(u8 _op, u32 _addr)
{
  u8 txbuf[FLASH_SPI_BUF_SIZE];
  u8 *ptx = txbuf;
  u8 *p = (u8 *)(&_addr);
  bool ret = false;

  // check param
  if(_addr > FLASH_MAX_ADDRESS) return ret;

  // padding command
  *ptx++ = _op;
  *ptx++ = p[2];
  *ptx++ = p[1];
  *ptx++ = p[0];

  FLASH_CS_ASSERTED;
  if(HAL_SPI_Transmit(&hspi2, txbuf, sizeof(txbuf), FLASH_SPI_TIMEOUT) == HAL_OK)
    ret = true;
  FLASH_CS_DEASSERTED;

  return ret;
}

/*
  brief:
    1. send op and address and data to flash through in blocked mode;
    2. dont need receive any data;
*/
static bool flash_writeOpAddrData(u8 _op, u32 _addr, u8 *_psrc, u16 _len)
{
  u8 txbuf[FLASH_SPI_BUF_SIZE];
  u8 *ptx = txbuf;
  u8 *p = (u8 *)(&_addr);
  bool ret = false;

  // check param
  if(!_psrc || _len == 0 || _addr > FLASH_MAX_ADDRESS) return ret;

  // padding command
  *ptx++ = _op;
  *ptx++ = p[2];
  *ptx++ = p[1];
  *ptx++ = p[0];

  FLASH_CS_ASSERTED;
  if(HAL_SPI_Transmit(&hspi2, txbuf, sizeof(txbuf), FLASH_SPI_TIMEOUT) == HAL_OK
    && HAL_SPI_Transmit(&hspi2, _psrc, _len, (_len << 1)) == HAL_OK)
    ret = true;
  FLASH_CS_DEASSERTED;

  return ret;
}

/*
*/
static void flash_WriteEnable(void)
{
  flash_writeOp(FLASH_OP_WRITE_ENABLE);
}

/*
  brief:
    1. modify the SPRL bit of SR to Global Unprotect operation;
    2. 
*/
static bool flash_writeStatus(u8 _value)
{
  u8 txbuf[2] = {FLASH_OP_WRITE_STATUS, 0};
  bool ret = false;

  flash_WriteEnable();

  // padding command
  txbuf[1] = _value;

  FLASH_CS_ASSERTED;
  if(HAL_SPI_Transmit(&hspi2, txbuf, sizeof(txbuf), FLASH_SPI_TIMEOUT) == HAL_OK)
    ret = true;
  FLASH_CS_DEASSERTED;

  return ret;
}

/*
*/
static void flash_eraseBlock32K(u32 _addr)
{
  flash_WriteEnable();
  flash_writeOpAddr(FLASH_OP_ERASE_32KB, _addr);
}

/*
  brief:
    1. send op and read status byte of flash in blocked mode;
    2. 
*/
static u8 flash_readStatus(void)
{
  u8 op = FLASH_OP_READ_STATUS;
  u8 st = 0xFF;

  // spi tx rx in blocked mode
  FLASH_CS_ASSERTED;
  // 1. send command;
  HAL_SPI_Transmit(&hspi2, &op,sizeof(op), FLASH_SPI_TIMEOUT);
  // 2. recv data;
  HAL_SPI_Receive(&hspi2, &st, sizeof(st), FLASH_SPI_TIMEOUT);
  FLASH_CS_DEASSERTED;

  return st;
}

/*
  brief:
    1. send op and read device ID(four bytes) of flash in blocked mode;
    2. recv data place in flash_spiRxBuf;
*/
static void flash_readDevID(void)
{
  u8 op = FLASH_OP_READ_DEV_ID;
  u8 *prx = flash_spiRxBuf;
  u16 len = sizeof(flash_spiRxBuf);

  // clear buf
  memset(prx, 0, len);

  FLASH_CS_ASSERTED;
  // 1. send command;
  HAL_SPI_Transmit(&hspi2, &op,sizeof(op), FLASH_SPI_TIMEOUT);
  // 2. recv data;
  HAL_SPI_Receive(&hspi2, prx, len, FLASH_SPI_TIMEOUT);
  FLASH_CS_DEASSERTED;
}

/*
  brief:
    1. get dev id and check communication OK or not;
*/
static bool flash_commOk(void)
{
  u8 *p = flash_spiRxBuf;
  u16 id = 0;
  bool ret = false;

  // read dev id in blocked mode
  flash_readDevID();
  // format: MANUFACTURER ID + DEVICE ID1 + DEVICE ID2 + EXTENDED byte
//  id = *((u16 *)(p + 1));
  id = (p[2] << 8) + p[1];

  if(id == FLASH_DEV_ID_DEFAULT_VALUE)
    ret = true;

  return ret;  
}

/*
  brief:
    1. WP pin default is 1; SPRL bit default is 0;
    2. if we will set SPRL 1, and unprotect all sectors,the SR value is: 1x0000xx;
    3. if we will set SPRL 0, and unprotect all sectors,the SR value is: 0x0000xx;
*/
static void flash_unprotectedAll(void)
{
  // we will set SPRL 0, and unprotect all sectors,the SR value is: 0000 0000
  flash_writeStatus(0);
}

/*
  read data in spi blocked mode
*/
static void flash_halSpiRead(u32 _addr, u8 *_prx, u16 _len)
{
  u8 *ptx = flash_spiTxBuf;
  u8 *p = (u8 *)(&_addr);

  // padding command
  *ptx++ = FLASH_OP_READ_ARRAY;
  *ptx++ = p[2];
  *ptx++ = p[1];
  *ptx++ = p[0];

  FLASH_CS_ASSERTED;
  // 1. send command in blocked mode
  HAL_SPI_Transmit(&hspi2, flash_spiTxBuf, sizeof(flash_spiTxBuf), FLASH_SPI_TIMEOUT);
  // 2. recv data
  HAL_SPI_Receive(&hspi2, _prx, _len, FLASH_SPI_TIMEOUT);
  FLASH_CS_DEASSERTED;
}

/*
  brief:
    1. NOW: spi is locked, and flash is ready;
    2. read all info of flash;
*/
static void flash_readSectorInfo(void)
{
  u8 *prx = (u8 *)(&flash_sectorInfo);
  u32 addr[FLASH_RS_BLOCK32_NUM];
  u16 len = sizeof(flash_sysInfo_typeDef);
  int i;

  addr[0] = FLASH_SYS_INFO_START_ADDR;
  // read sys info
  flash_halSpiRead(addr[0], prx, len);


  // read Ra info
  addr[0] = FLASH_RA1_START_ADDR;
  addr[1] = FLASH_RA2_START_ADDR;
  len = sizeof(flash_RecgInfo_typeDef);
  for(i = 0; i < FLASH_RA_BLOCK32_NUM; i++){
    prx += len;
    flash_halSpiRead(addr[i], prx, len);
  }
  
  // read Rs info
  addr[0] = FLASH_RS1_START_ADDR;
  addr[1] = FLASH_RS2_START_ADDR;
  addr[2] = FLASH_RS3_START_ADDR;
  addr[3] = FLASH_RS4_START_ADDR;
  addr[4] = FLASH_RS5_START_ADDR;
  addr[5] = FLASH_RS6_START_ADDR;
  for(i = 0; i < FLASH_RS_BLOCK32_NUM; i++){
    prx += len;
    flash_halSpiRead(addr[i], prx, len);
  }

  // read Rv info
  addr[0] = FLASH_RV1_START_ADDR;
  addr[1] = FLASH_RV2_START_ADDR;
  addr[2] = FLASH_RV3_START_ADDR;
  addr[3] = FLASH_RV4_START_ADDR;
  addr[4] = FLASH_RV5_START_ADDR;
  addr[5] = FLASH_RV6_START_ADDR;
  for(i = 0; i < FLASH_RV_BLOCK32_NUM; i++){
    prx += len;
    flash_halSpiRead(addr[i], prx, len);
  }
}

/*
  brief:
    1. read and check SWP flag of SR;
    2. 
*/
static void  flash_updateSwp(void)
{
  u8 status = 0xFF;

  // read SR
  status = flash_readStatus();
  // check swp, all or some sectors protected?
  if(status & FLASH_SOFT_PROTECTED_ALL){
    // unprotected all
    flash_unprotectedAll();
  }
}

/*
  brief:
    1. flash is ready interval;
    2. NOTICE: must lock spi before call this function;
*/
static bool flash_flashIsReady(void)
{
  bool ret = true;

  // is busy
  if(flash_readStatus() & FLASH_STATUS_BIT_BUSY)
    ret = false;

  return ret;
}

/*
*/
static void flash_resumeFlash(void)
{
  // resume falsh from DPM
  flash_writeOp(FLASH_OP_RESUME);
  flash_isDpm = false;
}

/*
  brief:
    1. if ble and pulse NOT working, enter dpm NOW!
*/
static bool flash_isDpmTime(void)
{
  bool ret = false;

  if((!ble_isWorking() && !pulse_isWorking())
    || (HAL_GetTick() > flash_dpmTick))
    ret = true;

  return ret;
}

/*
  brief:
    1. try to entering dpm mode;
*/
static bool flash_enterDpm(void)
{
  bool ret = false;

  if(mcu_tryLockSpi(MCU_SPI_FLASH)){
    /*
      NOW:
        1. spi locked and flash_isDpm is false;
        2. set flash into dpm mode;
    */

    // check SR is NOT busy
    if(flash_flashIsReady()){
      // enter into dpm mode
      flash_writeOp(FLASH_OP_DPM);
      ret = true;
    }

    // release spi
    mcu_unlockSpi();
  }

  return ret;
}

/*
  brief:
    1. real-time update max and min value;
    2. real-time record peak value pair per 2.5s;
    3. 
*/
static void flash_recordPeakValue(i32 _adcValue)
{
  // update max and min data
  if(_adcValue > flash_adcMaxValue)
    flash_adcMaxValue = _adcValue;
  if(_adcValue < flash_adcMinValue)
    flash_adcMinValue = _adcValue;

  // record peak value interval 2.5s
  if(_adcValue > flash_adcPeakMaxValue)
    flash_adcPeakMaxValue = _adcValue;
  if(_adcValue < flash_adcPeakMinValue)
    flash_adcPeakMinValue = _adcValue;
}

/*
  brief:
    1. adjust adc data from 12 bits to byte;[0, 255]
*/
static u8 flash_adjustAdcValueToByte(i32 _adcValue)
{
//  return (uint8_t)((_adcValue - ecg_adcMinValue) * 255 / (ecg_adcMaxValue - ecg_adcMinValue));

  // for efficiency
  i32 sub1 = _adcValue - flash_adcMinValue;
  i32 sub2 = flash_adcMaxValue - flash_adcMinValue;
  return (u8)(((sub1 << 8) - sub1) / sub2);
}

/*
*/
static void flash_clearAdcBuf(void)
{
  // clear vars about adc data buf
  flash_pRsBufStart = flash_pRsBufEnd = flash_RsAdcDataBuf;
  flash_pRvBufStart = flash_pRvBufEnd = flash_RvAdcDataBuf;
  flash_RsBufNum = flash_RvBufNum = 0;
  flash_RsWriteLocking = flash_RvWriteLocking = false;
  flash_RsWrittenNum = flash_RvWrittenNum = 0;
}

/*
  brief:
    1. start up store adc data in flash;
    2. 
*/
static void flash_startStoreTwins(bool _isTwins)
{
  flash_RsIegmAdcChNum = ADC_CH_NUM_RS_IEGM;
  if(_isTwins)
    flash_RvIegmAdcChNum = ADC_CH_NUM_RV_IEGM;
  else
    flash_RvIegmAdcChNum = 0;

  flash_clearAdcBuf();

  // trim adc tick startup
  flash_adcTrimPeakTick = HAL_GetTick() + ADC_TRIM_PEAK_TIMEOUT;

  // start check flash memory need erase or not
  flash_status = flash_startupStore_status;
}

/*
*/
static void flash_fillRecgInfo(u8 _Rflag, u8 _index)
{
  flash_RecgInfo_typeDef *pinfo;
  u32 addr;

  switch(_Rflag){
    case FLASH_RA_ECG_FLAG:
      pinfo = &(flash_sectorInfo.flash_RaIegmInfo[_index]);
      addr = FLASH_RA1_START_ADDR + FLASH_RA_BLOCK_SIZE * _index;
      break;
    case FLASH_RS_ECG_FLAG:
      addr = FLASH_RS1_START_ADDR + FLASH_RS_BLOCK_SIZE * _index;
      pinfo = &(flash_sectorInfo.flash_RsIegmInfo[_index]);
      break;
    case FLASH_RV_ECG_FLAG:
      addr = FLASH_RV1_START_ADDR + FLASH_RV_BLOCK_SIZE * _index;
      pinfo = &(flash_sectorInfo.flash_RvIegmInfo[_index]);
      break;
  }

  // get current date and time
  HAL_RTC_GetTime(&hrtc, &mcu_time, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &mcu_date, RTC_FORMAT_BIN);

  // fill info
  pinfo->eraseFlag = MCU_DATA_STRUCT_VALID_VALUE;
  pinfo->startYear = mcu_date.Year;
  pinfo->startMonth = mcu_date.Month;
  pinfo->startDay = mcu_date.Date;
  pinfo->startHour = mcu_time.Hours;
  pinfo->startMinute = mcu_time.Minutes;
  pinfo->startSecond = mcu_time.Seconds;
  pinfo->startSecondCount = ((((mcu_date.Year * 12 + mcu_date.Month) * 31
    + mcu_date.Date) * 24 + mcu_time.Hours) * 60 + mcu_time.Minutes) * 60
    + mcu_time.Seconds;
  // infor(1KB) + ecg data
  pinfo->dataStartAddr = addr + FLASH_R_DATA_OFFSET;
  // at last padding the datalen segment
  pinfo->datalen = 0;
}

/*
*/
static void flash_catchOneRxBlock(u8 _RxFlag)
{
  flash_RecgInfo_typeDef *pinfo = flash_sectorInfo.flash_RsIegmInfo;
  bool found = false;
  u8 i, index = 0;
  u32 oldestCount;
  u8 num;

  if(_RxFlag == FLASH_RS_ECG_FLAG){
    pinfo = flash_sectorInfo.flash_RsIegmInfo;
    num = FLASH_RS_BLOCK32_NUM;
  }else{
    pinfo = flash_sectorInfo.flash_RvIegmInfo;
    num = FLASH_RV_BLOCK32_NUM;
  }

  // loop check every block
  for(i = 0; i < num; i++){
    // is not erased block?
    if(pinfo[i].eraseFlag != MCU_DATA_STRUCT_VALID_VALUE){
      found = true;
      break;
    }
  }

  // found it?
  if(!found){
    // Rs is full, so erase the oldest block data
    oldestCount = pinfo[0].startSecondCount;
    for(i = 1; i < num; i++){
      if(pinfo[i].startSecondCount < oldestCount){
        oldestCount = pinfo[i].startSecondCount;
        index = i;
      }
    }
  }else{
    // found it, record this index
    index = i;
  }

  if(_RxFlag == FLASH_RS_ECG_FLAG){
    // store Rs index
    flash_RsInfoIndex = index;
  }else{
    // store Rs index
    flash_RvInfoIndex = index;
  }

  // fill block struct with current info
  flash_fillRecgInfo(_RxFlag, i);

  // need erase this block
  flash_eraseBlock32K(pinfo[i].dataStartAddr);
}

/*
  brief:
    1. NOW: we blocked spi, can oprate flash;
    2. check Rv six blocks, if have one unused, is OK;
    3. if all used, erase the oldest and told caller;
*/
static void flash_catchOneRvBlock(void)
{
  flash_catchOneRxBlock(FLASH_RV_ECG_FLAG);
}

/*
  brief:
    1. NOW: we blocked spi, can oprate flash;
    2. check Rs six blocks, if have one unused, is OK;
    3. if all used, erase the oldest and told caller;
*/
static void flash_catchOneRsBlock(void)
{
  flash_catchOneRxBlock(FLASH_RS_ECG_FLAG);
}

/*
  brief:
    1. program block info struct into flash;
    2. now: spi is locked by caller;
    3. NOTICE: DONT program the datalen of block info!!!
    4. program Rs block info at first;
    5. if exist Rv block info, program it in next status;
*/
static void flash_programBlockInfo(u8 _Rindex)
{
  flash_RecgInfo_typeDef *pinfo = &flash_sectorInfo.flash_RsIegmInfo[_Rindex];
  u32 addr = pinfo->dataStartAddr - FLASH_R_DATA_OFFSET;
  u8 op = FLASH_OP_PROGRAM;
  u8 *psrc = (u8 *)pinfo;
  u16 len = sizeof(flash_RecgInfo_typeDef) - sizeof(pinfo->datalen);

  flash_WriteEnable();
  flash_writeOpAddrData(op, addr, psrc, len);
}

/*
  brief:
    1. notice: pad point to the next byte of last data.
    2. this is loop buffer;
    3. there are two buffer: Rs, Rv;
    4. 
*/
static void flash_storeAdcDateToBuf(u8 _adcCurChNum, u8 _data)
{
  u8 *pbuf = flash_RsAdcDataBuf;
  u32 bufsize = sizeof(flash_RsAdcDataBuf);
  u8 **ppstart = &flash_pRsBufStart;
  u8 **ppend = &flash_pRsBufEnd;
  u32 *pbufnum = &flash_RsBufNum;
  bool *plock = &flash_RsWriteLocking;
  u16 *pwnum = &flash_RsWrittenNum;

  // judge current data is Rv IEGM channel?
  if(_adcCurChNum ^ ADC_CH_NUM_RS_IEGM){
    pbuf = flash_RvAdcDataBuf;
    bufsize = sizeof(flash_RvAdcDataBuf);
    ppstart = &flash_pRvBufStart;
    ppend = &flash_pRvBufEnd;
    pbufnum = &flash_RvBufNum;
    plock = &flash_RvWriteLocking;
    pwnum = &flash_RvWrittenNum;
  }

  /* store data to buffer */

  // add data the end of buf
  *(*ppend)++ = _data;
  if(*pbufnum < bufsize)
    // update num
    (*pbufnum)++;

  // arrive top of buffer?
  if((*ppend) >= pbuf + bufsize)
    *ppend = pbuf;

  // over flow?? discard the oldest byte that pstart pointed to.
  if(*pbufnum >= bufsize)
    *ppstart = *ppend;

  /* process written data into flash */
  if(!(*plock) && (*pwnum)){
    // trim sent data
    *ppstart += *pwnum;
    if((*ppstart) >= pbuf + bufsize)
      *ppstart = pbuf;
    *pbufnum -= *pwnum;
    *pwnum = 0;
  }
}


/*
  brief:
    1. update adc max and min value for downward;
    2. 
*/
static void flash_trimAdcPeak(void)
{
  // adc channel of flash is working or not
  if(flash_RsIegmAdcChNum + flash_RvIegmAdcChNum == 0)
    return;

  // trim timeout?
  if(HAL_GetTick() >= flash_adcTrimPeakTick){
    // if not equal, MUST: max > peak max
    if(!(flash_adcMaxValue ^ flash_adcPeakMaxValue)){
      flash_adcMaxValue += flash_adcPeakMaxValue;
      flash_adcMaxValue >>= 1;
    }

    // if not equal, MUST: min < peak min
    if(!(flash_adcMinValue ^ flash_adcPeakMinValue)){
      flash_adcMinValue += flash_adcPeakMinValue;
      flash_adcMinValue >>= 1;
    }

    // for next update
    flash_adcTrimPeakTick = HAL_GetTick() + ADC_TRIM_PEAK_TIMEOUT;
    flash_adcPeakMaxValue = ADC_MIN_VALUE;
    flash_adcPeakMinValue = ADC_MAX_VALUE;
  }
}

/*
*/
static void flash_otherWork(void)
{
/*
  if(!ble_isWorking() && !pulse_isWorking()){
    flash_status = flash_ready_status;
  }else
*/
    // trim adc peak value for R wave
    flash_trimAdcPeak();
}

/*
  brief:
    1. loop check SR status for programming;
    2. write data len of two channel(if Rv is exist);
    3. update status, we can dpm;
    4. NOTE: we must do this work after ecg data programmed;
*/
static void flash_smProgrammDataLen(void)
{
  flash_RecgInfo_typeDef *pinfo;
  u8 *pindex;
  u8 op = FLASH_OP_PROGRAM;
  u32 addr;
  u8 *psrc;
  u16 len;

  /* check flash SR is ready, internal written is OK */

  // check spi
  if(!mcu_tryLockSpi(MCU_SPI_FLASH)) return;

  // check flash is ready?
  if(!flash_flashIsReady()){
    // release spi
    mcu_unlockSpi();
    return;
  }

  /* NOW: internal written is finished and spi is locked */

  // write datalen into block info, at first write Rs length
  if(flash_RsInfoIndex != FLASH_INVALID_VALUE){
    pindex = &flash_RsInfoIndex;
    pinfo = &(flash_sectorInfo.flash_RsIegmInfo[flash_RsInfoIndex]);
  }else if(flash_RvInfoIndex != FLASH_INVALID_VALUE){
    pindex = &flash_RvInfoIndex;
    pinfo = &(flash_sectorInfo.flash_RvIegmInfo[flash_RvInfoIndex]);
  }else{
    // both of Rs, Rv index is zero, It means: both datalen written into flash is finished
    flash_status = flash_readyWaiting_status;
  }

  // padding param
  addr = pinfo->dataStartAddr - FLASH_R_DATA_OFFSET;
  addr += sizeof(flash_RecgInfo_typeDef) - sizeof(pinfo->datalen);
  psrc = (u8 *)(&(pinfo->datalen));
  len = sizeof(pinfo->datalen);

  flash_writeOpAddrData(op, addr, psrc, len);

  // clear current block info
  *pindex = 0;

  // release spi
  mcu_unlockSpi();
}

/*
  brief:
    1. loop check buf(two buf if exist), program into flash;
    2. we only memory 2 minute(30KB);
    3. 
*/
static void flash_smProgramming(void)
{
  flash_RecgInfo_typeDef *pinfo;
  u8 op = FLASH_OP_PROGRAM;
  u32 addr;
  u8 *psrc;
  u16 len = FLASH_ONE_PAGE_SIZE;
  u16 *pwritten;
  u8 *pchnum;
  bool *plocking;

  /* check two channel(if exist) is sending time or neither */

  // at first: process Rs channel
  if(flash_RsBufNum >= len){
    // NOW: Rs channel need process
    plocking = &flash_RsWriteLocking;
    *plocking = true;
    pinfo = &(flash_sectorInfo.flash_RsIegmInfo[flash_RsInfoIndex]);
    psrc = flash_pRsBufStart;
    pwritten = &flash_RsWrittenNum;
    pchnum = &flash_RsIegmAdcChNum;
  }else if(flash_RvBufNum >= len){
    // NOW: Rv channel need process
    plocking = &flash_RvWriteLocking;
    *plocking = true;
    pinfo = &(flash_sectorInfo.flash_RvIegmInfo[flash_RvInfoIndex]);
    psrc = flash_pRvBufStart;
    pwritten = &flash_RvWrittenNum;
    pchnum = &flash_RvIegmAdcChNum;
  }else{
    // both of Rs, Rv is less than len
    return;
  }
  addr = pinfo->dataStartAddr + pinfo->datalen;


  /* check flash SR is ready, internal written is OK */

  // check spi
  if(!mcu_tryLockSpi(MCU_SPI_FLASH)){
    // unlocking write notify adcCB
    *plocking = false;
    return;
  }

  // check flash is ready?
  if(!flash_flashIsReady()){
    // release spi
    mcu_unlockSpi();
    // unlocking write notify adcCB
    *plocking = false;
    return;
  }

  /* NOW: internal written is finished and spi is locked */

  // locking write notify adcCB
  *plocking = true;
  // program current channel data into flash
  flash_writeOpAddrData(op, addr, psrc, len);

  // record written bytes num
  (*pwritten) += len;
  // unlocking write notify adcCB
  *plocking = false;

  // update ecg data datalen
  pinfo->datalen += len;

  // current channel is full?
  if((addr & 0xFF8000) != ((addr + len) & 0xFF8000)){
    // close this channel, donnt accept data in flash adc CB
    *pchnum = 0;

    // both of channel closed?
    if(flash_RsIegmAdcChNum == 0 && flash_RvIegmAdcChNum == 0){
      // flash store is finished, update status for writting data len
      flash_status = flash_programmed_status;
    }
  }

  // release spi
  mcu_unlockSpi();
}

/*
  brief:
    1. loop check SR status for waiting Rs block info write OK;
    2. program Rv block info;
    3. update status, start program ecg data;
*/
static void flash_smProgramRsBlockInfo(void)
{
  // check spi
  if(!mcu_tryLockSpi(MCU_SPI_FLASH)) return;

  // check flash is ready?
  if(!flash_flashIsReady()){
    // release spi
    mcu_unlockSpi();
    return;
  }

  /* NOW: flash SR is ready, we can send command */

  // check need Rv?
  if(flash_RvInfoIndex != FLASH_INVALID_VALUE)
    // program Rv block info struct into flash
    flash_programBlockInfo(flash_RvInfoIndex);

  // start loop programming data into flash at next status
  flash_status = flash_programming_status;
//  flash_RsWrittenNum = flash_RvWrittenNum = 0;

  // release spi
  mcu_unlockSpi();
}

/*
  brief:
    1. loop check SR for erasing is finished or not;
    2. 
*/
static void flash_smErasingForStore(void)
{
  // check spi
  if(!mcu_tryLockSpi(MCU_SPI_FLASH)) return;

  // check flash is ready?
  if(!flash_flashIsReady()){
    // release spi
    mcu_unlockSpi();
    return;
  }

  // erase is finished; so we can check next channel
  if(flash_RvInfoIndex == FLASH_INVALID_VALUE && flash_RvIegmAdcChNum){
    // check all Rv blocks, catch one
    flash_catchOneRvBlock();

    flash_status = flash_erasingForStore;
  }else{
    /* NOW: flash SR is ready, we can send command */

    // at first, program block info struct into flash
    flash_programBlockInfo(flash_RsInfoIndex);

    // programming Rs block info into flash at next status
    flash_status = flash_programmingRs_status;
  }

  // release spi
  mcu_unlockSpi();
}

/*
  brief:
    1. check block, if full, erase the oldest block;
    2. return addr, and record sector info;
*/
static void flash_smStartupStore(void)
{
  // check spi
  if(!mcu_tryLockSpi(MCU_SPI_FLASH)) return;

  // check flash is ready?
  if(!flash_flashIsReady()){
    // release spi
    mcu_unlockSpi();
    return;
  }

  // read sector info in blocked mode
//  if(flash_RsIegmAdcChNum){
    // check all Rs blocks, catch one
    flash_catchOneRsBlock();

    // update status(because of erase time, we will check RaIegmChNum(if selected twins) in next status)
    flash_status = flash_erasingForStore;
//  }else{
    // update status
//    flash_status = flash_storingEcg_status;
//  }

  // release spi
  mcu_unlockSpi();
}

/*
  brief:
    1. spi is reading data through spi in interrupt mode;
    2. if spi is ready, the recv buf will filled data we want;
*/
static void flash_smReading(void)
{
  // Only loop check spi reading is finished or not
  if(mcu_spiIsReady()){
    // enter next status for DPM and at last go into ready status;
    flash_status = flash_readyWaiting_status;
  }
}

/*
  brief:
    1. loop check and lock spi for making read action;
    2. go into reading status, waiting result;
*/
static void flash_smReadData(void)
{
  u8 *ptx = flash_spiTxBuf;
  u8 *prx = flash_pUserData;
  u8 *p = (u8 *)(&flash_userStartAddr);
  u16 len = flash_userDataLen;

  // check spi
  if(!mcu_tryLockSpi(MCU_SPI_FLASH)) return;

  // check flash is ready?
  if(!flash_flashIsReady()){
    // release spi
    mcu_unlockSpi();
    return;
  }

  // padding command
  *ptx++ = FLASH_OP_READ_ARRAY;
  *ptx++ = p[2];
  *ptx++ = p[1];
  *ptx++ = p[0];

  FLASH_CS_ASSERTED;
  // 1. send command in blocked mode
  HAL_SPI_Transmit(&hspi2, flash_spiTxBuf, sizeof(flash_spiTxBuf), FLASH_SPI_TIMEOUT);

  // release spi
  mcu_unlockSpi();

  // 2. recv data in unblocked mode
  mcu_spiRxUnblocked(MCU_SPI_FLASH, prx, len);

  // update status
  flash_status = flash_reading_status;
}

/*
  brief:
    1. make resume action for wake up flash from DPM;
*/
static void flash_smResumeForRead(void)
{
  // check spi
  if(!mcu_tryLockSpi(MCU_SPI_FLASH)) return;

  // resume flash
  flash_resumeFlash();

  // release spi
  mcu_unlockSpi();

  flash_status = flash_readData_status;
}

/*
  brief:
    1. NOW: flash is in DPM;
    2. nothing to do;
*/
static void flash_smReady(void)
{
  // do noting in dpm mode
  if(flash_isDpm) return;

  // delay sometimes, and then make dpm action
  if(flash_isDpmTime() && flash_enterDpm()){
      flash_isDpm = true;
  }
}

/*
  brief:
    1. Any operation is over, can enter this status;
    2. check SR is NOT busy;
    3. check DPM is not;
    4. enter DPM;
    5. go into ready status;
*/
static void flash_smReadyWaiting(void)
{
  // waiting for user operation before DPM
  flash_dpmTick = HAL_GetTick() + FLASH_DPM_PERIOD;

  flash_status = flash_ready_status;
}

/*
  brief:
    1. loop waiting spi ready;
    2. read devID and check it;
    3. read SR and check SWP;
*/
static void flash_smIniting(void)
{
  if(mcu_tryLockSpi(MCU_SPI_FLASH)){
    // get dev id and check communication OK or not
    if(!flash_commOk()){
      flash_status = flash_error_status;
    }else{
      // read and check SWP flag of SR
      flash_updateSwp();
      // read sector info
      flash_readSectorInfo();
      flash_status = flash_readyWaiting_status;
    }

    // release spi using
    mcu_unlockSpi();
  }
}


/* public function define *******************************************/



/*
  brief:
    1. working in main while loop;
    2. All process will loop check flash SR busy or not;
*/
void flash_stateMachine(void)
{
  switch(flash_status){
    case flash_initing_status:
      flash_smIniting();
      break;
    case flash_readyWaiting_status:
      flash_smReadyWaiting();
      break;
    case flash_ready_status:
      flash_smReady();
      break;
    case flash_resumeForRead_status:
      flash_smResumeForRead();
      break;
    case flash_readData_status:
      flash_smReadData();
      break;
    case flash_reading_status:
      flash_smReading();
      break;
    case flash_startupStore_status:
      flash_smStartupStore();
      break;
    case flash_erasingForStore:
      flash_smErasingForStore();
      break;
    case flash_programmingRs_status:
      flash_smProgramRsBlockInfo();
      break;
    case flash_programming_status:
      flash_smProgramming();
      break;
    case flash_programmed_status:
      flash_smProgrammDataLen();
      break;
/*
    case :
      break;
*/

    default:
      break;
  }

  flash_otherWork();
}


/*
  brief:
    1. running in adcCB, so quickly!
    2. if flash_needStore disable, this function will NOT called;
    3. 
*/
void flash_adcConvCplCB(u8 _adcCurChNum)
{
  i32 value;
  u8 byte;

  // get adc sample value
  value = (int32_t)(HAL_ADC_GetValue(&hadc) & 0x0FFF);

  /*
    update adc max and min value;
    record adc peak value pair interval 2.5s;
  */
  flash_recordPeakValue(value);

  // adjust adc value to byte: [0, 255]
  byte = flash_adjustAdcValueToByte(value);

  /*
    start flash store real-time process
  */

  // Only store byte into buf(flash state machine will send them through SPI)
  flash_storeAdcDateToBuf(_adcCurChNum, byte);
}

/*
*/
bool flash_isEnterLpm(void)
{
  return flash_isDpm;
}

/*
  brief:
    1. stop flash store;
    2. 
*/
void flash_stopFlashStore(void)
{
  if(flash_status != flash_readyWaiting_status
    && flash_status != flash_ready_status)
    flash_status = flash_readyWaiting_status;
}

/*
  brief:
    1. store data of Rs_IEGM only into flash(pulse move it);
    2. 
*/
void flash_startFlashStore(void)
{
  if(!flash_isReady()) return;

  flash_startStoreTwins(false);
}

/*
  brief:
    1. store data of Rs_IEGM only into flash(pulse move it);
    2. 
*/
void flash_startFlashStoreTwins(void)
{
  if(!flash_isReady()) return;

  flash_startStoreTwins(true);
}


/*
  brief:
    1. this run ADC CB, so quickly as soon;
    2. 
*/
bool flash_isAdcStoreCh(u8 _adcCurChNum)
{
  // flash ready? need store, clock channel
  return ((!(_adcCurChNum ^ flash_RsIegmAdcChNum))
    || (!(_adcCurChNum ^ flash_RvIegmAdcChNum)));
}

/*
  brief:
    1. read some bytes in unblocked mode;
    2. return value is not mean we already get data;
    3. user need loop check flash_dataIsReady;
*/
bool flash_readData(u32 _startAddr, u8 *_prx, u16 _len)
{
  bool ret = false;

  // flash is busy?
  if(flash_status != flash_ready_status) return ret;

  // check param
  if(!_prx || _len == 0 || _startAddr > FLASH_MAX_ADDRESS)
    return ret;

  // temp store read params
  flash_userStartAddr = _startAddr;
  flash_pUserData = _prx;
  flash_userDataLen = _len;

  // being in DPM mode? resume it
  if(flash_isDpm){
    flash_status = flash_resumeForRead_status;
  }else{
    flash_status = flash_readData_status;
  }

  return true;
}

/*
  brief:
    1. flash is ready, device can use it;
    2. flash maybe in DPM(deep power down mode);
*/
bool flash_isReady(void)
{
  return !(flash_status ^ flash_ready_status);
}

/*
  brief:
    1. init all about flash vars;
    2. 
*/
void flash_init(void)
{
  // init vars
  flash_isDpm = false;
  memset(flash_spiTxBuf, 0, sizeof(flash_spiTxBuf));
  memset(flash_spiRxBuf, 0, sizeof(flash_spiRxBuf));
  flash_dpmTick = 0;
  flash_userStartAddr = FLASH_INVALID_VALUE;
  flash_pUserData = NULL;
  flash_userDataLen = 0;
  flash_RsIegmAdcChNum = 0;
  flash_RvIegmAdcChNum = 0;
  // init peak value
  flash_adcMaxValue = flash_adcPeakMaxValue = ADC_MIN_VALUE;
  flash_adcMinValue = flash_adcPeakMinValue = ADC_MAX_VALUE;
  flash_adcTrimPeakTick = 0;
  flash_RsInfoIndex = flash_RvInfoIndex = FLASH_INVALID_VALUE;
  flash_RsWrittenNum = flash_RvWrittenNum = 0;


  // update status
  flash_status = flash_initing_status;
}

