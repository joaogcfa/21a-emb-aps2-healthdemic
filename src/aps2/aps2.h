/*
 * config.h
 *
 * Created: 07/05/2021 22:51:45
 *  Author: RafaelCF4
 */ 


#ifndef APS2_H_
#define APS2_H_

#include "asf.h"
#include "ecg.h"

//! DAC channel used for test
#define DACC_CHANNEL        1 // (PB13)
//! DAC register base for test
#define DACC_BASE           DACC
//! DAC ID for test
#define DACC_ID             ID_DACC

#define DACC_ANALOG_CONTROL (DACC_ACR_IBCTLCH0(0x02) | DACC_ACR_IBCTLCH1(0x02))


static void config_DAC(void) ;
void TC_init_modificado(Tc * TC, int ID_TC, int TC_CHANNEL, int freq);

#endif /* APS2_H_ */