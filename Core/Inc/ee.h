/*
 * eeprom.h
 *
 *  Created on: 2023年11月8日
 *      Author: Johnny
 */

#ifndef INC_EE_H_
#define INC_EE_H_

/* define ----------------------------------------------------------------- */


#ifndef LiuJH_DEBUG
#define LiuJH_EE
#endif


#define ee_word_type                  FLASH_TYPEPROGRAMDATA_WORD
#define ee_half_word_type             FLASH_TYPEPROGRAMDATA_HALFWORD
#define ee_byte_type                  FLASH_TYPEPROGRAMDATA_BYTE

//                 [DATA_EEPROM_BASE, DATA_EEPROM_END]
// EEPROM address: [0x0808 0000     , 0x0808 07FF    ]

// pulse_config address in EEPROM
#define ee_addr_pulseConfig           DATA_EEPROM_BASE
#define ee_addr_pulseConfig_size      (sizeof(pulse_config_typeDef))
#define ee_addr_pulseConfig_end       (ee_addr_pulseConfig + ee_addr_pulseConfig_size - 1)

// unpulsing period address in EEPROM
#define ee_addr_unpulsingPeriod       (ee_addr_pulseConfig_end + 1)
#define ee_addr_unpulsingPeriod_size  (sizeof(pulse_unpulsing_period_typeDef))
#define ee_addr_unpulsingPerod_end    (ee_addr_unpulsingPeriod + ee_addr_unpulsingPeriod_size - 1)

// pulse Vout_set config in EEPROM
#define ee_addr_VoutSet               (ee_addr_unpulsingPerod_end + 1)
#define ee_addr_VoutSet_size          (sizeof(mcu_Voutset_typeDef))
#define ee_addr_VoutSet_end           (ee_addr_VoutSet + ee_addr_VoutSet_size - 1)

// motion period config value in EEPROM
#define ee_addr_motionPeriod          (ee_addr_VoutSet_end + 1)
#define ee_addr_motionPeriod_size     (sizeof(mcu_MotionConfig_typeDef))
#define ee_addr_motionPeriod_end      (ee_addr_motionPeriod + ee_addr_motionPeriod_size -1)

// base data in EERPOM
#define ee_addr_baseData              (ee_addr_motionPeriod_end + 1)
#define ee_addr_baseData_size         (sizeof(mcu_baseData_typeDef))
#define ee_addr_baseData_end          (ee_addr_baseData + ee_addr_baseData_size - 1)

// bpm calm max in EERPOM
#define ee_addr_bpmCalmMax            (ee_addr_baseData_end + 1)
#define ee_addr_bpmCalmMax_size       (sizeof(mcu_bpmCalmMax_typeDef))
#define ee_addr_bpmCalmMax_end        (ee_addr_bpmCalmMax + ee_addr_bpmCalmMax_size - 1)

// ADC no signal weight value and max value gain in EEPROM
#define ee_addr_AdcWeightGain         (ee_addr_bpmCalmMax_end + 1)
#define ee_addr_AdcWeightGain_size    (sizeof(ecg_AdcWeightGain_typeDef))
#define ee_addr_AdcWeightGain_end     (ee_addr_AdcWeightGain + ee_addr_AdcWeightGain_size - 1)


/* type define ----------------------------------------------------------- */

typedef enum{
  ee_kv_pulseConfig = 0,
  ee_kv_unpulsingPeriod,
  ee_kv_VoutSet,
  ee_kv_motionPeriod,
  ee_kv_baseData = 4,
  ee_kv_bpmCalmMax,
  ee_kv_AdcWeightGain,


  ee_kv_max
} ee_keyvalue_typeDef;


/* extern public function ------------------------------------------------- */
extern bool ee_storeKeyValue(void);
extern bool ee_restoreKeyValue(void);
extern bool ee_readOrWriteKeyValue(ee_keyvalue_typeDef _key, bool _isRead);

#endif /* INC_EE_H_ */
