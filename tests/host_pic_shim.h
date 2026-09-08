/*********************************************************************************
*        File: host_pic_shim.h
*        Zweck: PIC16F1827 Register-Shim fuer Host-Tests (gcc)
*        Achtung: Simuliert die Hardware nur grob, validiert NICHT die
*        Ziel-Firmware. Kein Ersatz fuer HI-TECH C / Oszi-Messung!
************************************************************************************/
#ifndef HOST_PIC_SHIM_H
#define HOST_PIC_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t  LATA_REG, LATB_REG, TRISA_REG, TRISB_REG;
extern uint8_t  ANSELA_REG, ANSELB_REG, WPUB_REG, OPTION_REG_REG;
extern uint8_t  APFCON0_REG, APFCON1_REG, OSCCON_REG;
extern uint8_t  TXSTA_REG, RCSTA_REG, BAUDCON_REG, SPBRGH_REG, SPBRG_REG;
extern uint8_t  T1GCON_REG, T1CON_REG, CCP1CON_REG, CCP2CON_REG;
extern uint8_t  IOCBN_REG, IOCBF_REG, PIR1_REG, PIR2_REG, PIE1_REG, PIE2_REG, INTCON_REG;
extern uint8_t  TMR1L_REG, TMR1H_REG, CCPR1L_REG, CCPR1H_REG, CCPR2L_REG, CCPR2H_REG;
extern uint8_t  EEDATA_REG, EEADRL_REG, EECON1_REG, EECON2_REG;
extern uint8_t  RCREG_REG, TXREG_REG, TXIF_REG;

#define LATA  LATA_REG
#define LATB  LATB_REG
#define TRISA TRISA_REG
#define TRISB TRISB_REG
#define ANSELA ANSELA_REG
#define ANSELB ANSELB_REG
#define WPUB  WPUB_REG
#define OPTION_REG OPTION_REG_REG
#define APFCON0 APFCON0_REG
#define APFCON1 APFCON1_REG
#define OSCCON OSCCON_REG
#define TXSTA TXSTA_REG
#define RCSTA RCSTA_REG
#define BAUDCON BAUDCON_REG
#define SPBRGH SPBRGH_REG
#define SPBRG SPBRG_REG
#define T1GCON T1GCON_REG
#define T1CON T1CON_REG
#define CCP1CON CCP1CON_REG
#define CCP2CON CCP2CON_REG
#define IOCBN IOCBN_REG
#define IOCBF IOCBF_REG
#define PIR1  PIR1_REG
#define PIR2  PIR2_REG
#define PIE1  PIE1_REG
#define PIE2  PIE2_REG
#define INTCON INTCON_REG
#define TMR1L TMR1L_REG
#define TMR1H TMR1H_REG
#define CCPR1L CCPR1L_REG
#define CCPR1H CCPR1H_REG
#define CCPR2L CCPR2L_REG
#define CCPR2H CCPR2H_REG
#define EEDATA EEDATA_REG
#define EEADRL EEADRL_REG
#define EECON1 EECON1_REG
#define EECON2 EECON2_REG
#define RCREG RCREG_REG
#define TXREG TXREG_REG
#define TXIF  TXIF_REG

#define RB4   sim_RB4
#define LATA0 sim_LATA0
#define LATA1 sim_LATA1
#define LATA4 sim_LATA4
#define LATB3 sim_LATB3
#define LATB5 sim_LATB5
#define LATB6 sim_LATB6
#define LATB7 sim_LATB7

typedef struct { unsigned IOCIF:1, res1:1, res2:1, IOCIE:1, res4:1, res5:1, PEIE:1, GIE:1; } INTCONbits_t;
extern INTCONbits_t INTCONbits;
typedef struct { unsigned IOCBF0:1, IOCBF1:1, IOCBF2:1, IOCBF3:1, IOCBF4:1, IOCBF5:1, IOCBF6:1, IOCBF7:1; } IOCBFbits_t;
extern IOCBFbits_t IOCBFbits;
typedef struct { unsigned IOCBN0:1, IOCBN1:1, IOCBN2:1, IOCBN3:1, IOCBN4:1, IOCBN5:1, IOCBN6:1, IOCBN7:1; } IOCBNbits_t;
extern IOCBNbits_t IOCBNbits;
typedef struct { unsigned TMR1IF:1, res1:1, RCIF:1, TXIF:1, res4:1, CCP1IF:1, res6:1, res7:1; } PIR1bits_t;
extern PIR1bits_t PIR1bits;
typedef struct { unsigned CCP2IF:1, res1:1, res2:1, res3:1, res4:1, res5:1, res6:1, res7:1; } PIR2bits_t;
extern PIR2bits_t PIR2bits;
typedef struct { unsigned TMR1IE:1, res1:1, res2:1, RCIE:1, res4:1, CCP1IE:1, res6:1, res7:1; } PIE1bits_t;
extern PIE1bits_t PIE1bits;
typedef struct { unsigned CCP2IE:1, res1:1, res2:1, res3:1, res4:1, res5:1, res6:1, res7:1; } PIE2bits_t;
extern PIE2bits_t PIE2bits;
typedef struct { unsigned res0:1, res1:1, res2:1, res3:1, res4:1, res5:1, res6:1, TMR1ON:1; } T1CONbits_t;
extern T1CONbits_t T1CONbits;
typedef struct { unsigned res0:1, res1:1, res2:1, RX9D:1, OERR:1, FERR:1, res5:1, CREN:1, SPEN:1; } RCSTAbits_t;
extern RCSTAbits_t RCSTAbits;
typedef struct { unsigned RD:1, WR:1, WREN:1, res3:1, res4:1, res5:1, CFGS:1, EEPGD:1; } EECON1bits_t;
extern EECON1bits_t EECON1bits;

extern uint8_t sim_RB4, sim_LATA0, sim_LATA1, sim_LATA4, sim_LATB3, sim_LATB5, sim_LATB6, sim_LATB7;

void __delay_us(unsigned long us);
void __delay_ms(unsigned long ms);

extern uint32_t sim_now_ticks;          // Simulationszeit in 4-us-Ticks
extern uint32_t sim_timer1_basis;       // Startpunkt des freien Timer1
extern uint32_t sim_debug_step;
extern uint32_t sim_guard;              // Endlos-Schutz
extern uint8_t  sim_eeprom[256];
extern uint32_t sim_eeprom_wr_rest;     // Restlaufzeit des EEPROM-Schreibens
extern uint32_t sim_delay_ticks;        // Zeit, die __delay verbrauchen soll

#define SIM_EE_DAUER_TICKS 1250u        // EEPROM-Schreibdauer: ca. 5 ms

uint32_t sim_zeitstempel(void);         // 32-Bit Zeit seit Timer1-Start
void sim_timer_aktualisieren(void);
void sim_ausgaenge_aktualisieren(void);
void host_register_sync(void);          // Byte-Register in Bitfelder spiegeln
void sim_schritt(uint32_t ticks);       // Zeit vorlauf lassen, Interrupts bedienen
void sim_hauptschleife(uint32_t durchlaeufe);  // Firmware-Rumpf + Zeit
void sim_kante(void);                   // fallende RB4-Flanke (OT) ausloesen
void sim_sende(uint8_t b);              // serielles Byte in den Empfang legen
void sim_eeprom_schreiben_starten(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_PIC_SHIM_H */
