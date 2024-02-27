/*
 * protocol.h
 *
 *  Created on: 2023年11月16日
 *      Author: Johnny
 */

#ifndef INC_PROTOCOL_H_
#define INC_PROTOCOL_H_
/*
  1. All SPI comm protocol between MCU and RSL10 in here;
  2. "CS" is Check XOR sum;
  3. 
*/

/* mcu Query command of sending protocol ****************************/
// mcu->rsl10: query what to do
//#define BLE_P_QUERY_REQ_BUF     0xA5, 0x5A, 0, 0, 0, 0xFF
// mcu->rsl10: ask rsl10 go into LPM
#define BLE_P_ASK_RSL10_LPM_BUF 0xA5, ble_p_rsl10AgreeEnterLpm, 0, 0, 0, 0xC6

/* mcu received COMMAND protocol ************************************/
// fomat is: 0xA5 CMD Data Data Data CS
// the first byte MUST be 0xA5
#define BLE_P_HEAD              0xA5
// command code index in buffer
#define BLE_P_COMMAND_INDEX     1
#define BLE_P_DATA_INDEX        2

#define BLE_P_OK_FLAG           1
#define BLE_P_ERROR_FLAG        0

#define BLE_P_RESP_BUF_MAX_SIZE 16
#define BLE_P_RESP_BUF_SIZE     6



/* command code value */

typedef enum{
  // app told mcu we can LPM
  ble_p_sleep = 0x11,
  // app told mcu: startup charging(0x12)
  ble_p_charge_on,
  // app told mcu: stop charging(0x13)
  ble_p_charge_off,
  // read status of charging or not(0x14)
  ble_p_read_charge_state,
  // startup pulsing(0x15)
  ble_p_pulse_on,
  // stop pulsing(0x16)
  ble_p_pulse_off,
  /*
  	config1 only config base param:
		Data：0F 02 19 0A
		Note：Pulse_Delay + Pulse_Number + Pulse_Width + Pulse_Rsvi.
			Pulse_Delay:	0F indicate 15ms.
			Pulse_Number:	02 indicate 2 pulse.
			Pulse_Wideth:	19 indicate 2.5ms(25/10).
			Pulse_Rsvi:		0A indecate 10ms.
  */
  // read pulse config1(0x17)
  ble_p_read_pulseConfig1,
  // write pulse config1(0x18)
  ble_p_write_pulseConfig1,
  // request FOTA(0x19)
  ble_p_req_fota,
  // read bpm(0x1A)
  ble_p_read_bpm,
  // read batt level(0x1B)
  ble_p_read_batt_level,
  // read motion state(0x1C)
  ble_p_read_motion_state,
  // read mcu date and time(0x1D)
  ble_p_read_date_time,
  // write date and time(0x1E)
  ble_p_write_date_time,


  // request RT_ECG_RA_RDET(0x23)
  ble_p_read_RtEcg_RaRdet = 0x23,
  // request RT_ECG_RA_IEGM(0x24)
  ble_p_read_RtEcg_RaIegm,
  // request RT_ECG_RS_RDET(0x25)
  ble_p_read_RtEcg_RsRdet,
  // request RT_ECG_RS_IEGM(0x26)
  ble_p_read_RtEcg_RsIegm,
  // request RT_ECG_RV_RDET(0x27)
  ble_p_read_RtEcg_RvRdet,
  // request RT_ECG_RV_IEGM(0x28)
  ble_p_read_RtEcg_RvIegm,
  // request ECG of storing in spi flash(0x29)
  ble_p_read_StEcg_flash,
  // stop ecg(one of six ecg) reading(0x2A)
  ble_p_stop_readingEcg,


  // RSL10 read pulse working status(0x30)
  ble_p_read_pulseWorkingStatus = 0x30,
  // RSL10 read Rs Rv interval(unit: ms)(0x31)
  ble_p_read_RsRvInterval,
  // RSL10 read pulse Vout_set status(0x32)
  ble_p_read_pulseVoutSetStatus,
  // RSL10 write: pulse Vout_set status(0x33)
  ble_p_write_pulseVoutSetStatus,
  // RSL10 read pulse holiday date&time(0x34)
  ble_p_read_pulseHolidayDt,
  // RSL10 write pulse holiday date&time(0x35)
  ble_p_write_pulseHolidayDt,
  // RSL10 read base data(0x36)
  ble_p_read_baseData,

  // RSL10 read twins channel ecg data(0x37)
  ble_p_read_RtEcg_twins = 0x37,


  // RSL10 read accel config value(0x39)
  ble_p_read_AccelCfg = 0x39,
  // RSL10 write accel config value(0x3A)
  ble_p_write_AccelCfg,

  // RSL10 told mcu: force pulsing ignore R wave and others condition(0x3C)
  ble_p_forcePulsing = 0x3C,
	/*
		set and read pulse time(up to 6 groups);
		First three sets use config2;
		other three sets use config3;
		Data：
			00 00 01 00 04 00 05 00 08 00 09 00
			12 00 13 00 16 00 17 00 20 00 21 00
		Note:
			Pulse_time1 + Pulse_time2 + Pulse_time3.
			Pulse_time4 + Pulse_time5 + Pulse_time6.
			Pulse_time: 12 00 13 00 indicate 12:00～13:00.
	*/
  // read pulse config2(0x3D)
  ble_p_read_pulseConfig2,
  // write pulse config2(0x3E)
  ble_p_write_pulseConfig2,
	// read pulse config3(0x3F)
	ble_p_read_pulseConfig3,
	// write pulse config3(0x40)
	ble_p_write_pulseConfig3,

  // read bpm calm max value(0x41)
  ble_p_read_bpmCalmMax,
  // write bpm calm max value(0x42)
  ble_p_write_bpmCalmMax,

  // rsl10 told mcu: its status is IDLE, no other command(0x60)
  ble_p_rsl10IsIdle = 0x60,
  // rsl10 told mcu: its status is FOTA with APP(0x61)
  ble_p_rsl10IsFota,
  // rsl10 told mcu: its status is connected with APP(0x62)
  ble_p_rsl10IsConnected,
  // rsl10 response mcu's sleep notify(0x63)
  ble_p_rsl10AgreeEnterLpm,


  /* ERROR value ******************************************************/
  ble_p_error_vlaue = 0xFF
} ble_p_cmd_code_typeDef;


#endif /* INC_PROTOCOL_H_ */
