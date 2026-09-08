/*********************************************************************************
*        File: host_pic_shim.h
*        Zweck: PIC16F1827 Register- und Zeitmodell fuer Host-Tests (gcc)
*
*        Das Modell bildet gezielt die Eigenschaften nach, an denen die
*        Zuendsteuerung scheitern kann:
*          - Zieltypbreiten (u8/u16/u32/s32 wie HI-TECH C / XC8),
*          - Timer1-Takt aus T1CON (TMR1CS, T1CKPS): ein falscher Prescaler
*            laesst die simulierte Zeitbasis entsprechend falsch laufen,
*          - Compare-Treffer nur bei Gleichheit des 16-Bit-Standes,
*          - CCPxIF wird unabhaengig von CCPxIE gesetzt (stehengebliebene
*            Vergleiche muessen von der Firmware abgefangen werden),
*          - IOCIF ist nur lesbar und folgt IOCBF,
*          - RCREG-Lesen holt das naechste Byte nach, FERR gehoert zum Byte
*            VOR dem Lesen, OERR bei vollem Puffer,
*          - EEPROM-Schreiben dauert Zeit (EECON1.WR bleibt gesetzt),
*          - TMR1H/TMR1L koennen zwischen zwei Lesezugriffen umlaufen.
*
*        Achtung: Das Modell validiert NICHT die Ziel-Firmware. Kein Ersatz
*        fuer HI-TECH C / XC8 und Oszilloskop-Messung!
************************************************************************************/
#ifndef HOST_PIC_SHIM_H
#define HOST_PIC_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zieltypbreiten: auf dem Host ist int 32 Bit und long 64 Bit, auf dem
   PIC16F1827 (HI-TECH C / XC8) int 16 Bit und long 32 Bit. Die Firmware
   benutzt ausschliesslich diese Namen, damit Ueberlaeufe und Umlaeufe im
   Test dieselben sind wie auf dem Ziel. */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t  s32;

typedef union {
    uint8_t byte;
    struct { unsigned b0:1, b1:1, b2:1, b3:1, b4:1, b5:1, b6:1, b7:1; } bit;
} sim_reg8;

extern sim_reg8 sim_LATA, sim_LATB;

extern uint8_t  TRISA_REG, TRISB_REG;
extern uint8_t  ANSELA_REG, ANSELB_REG, WPUB_REG, OPTION_REG_REG;
extern uint8_t  APFCON0_REG, APFCON1_REG, OSCCON_REG;
extern uint8_t  TXSTA_REG, RCSTA_REG, BAUDCON_REG, SPBRGH_REG, SPBRG_REG;
extern uint8_t  T1GCON_REG, T1CON_REG, CCP1CON_REG, CCP2CON_REG;
extern uint8_t  IOCBN_REG, IOCBF_REG, PIR1_REG, PIR2_REG, PIE1_REG, PIE2_REG;
extern uint8_t  TMR1L_REG, TMR1H_REG, CCPR1L_REG, CCPR1H_REG, CCPR2L_REG, CCPR2H_REG;
extern uint8_t  EEDATA_REG, EEADRL_REG, EECON2_REG;
extern uint8_t  TXREG_REG, TXIF_REG;

#define LATA  sim_LATA.byte
#define LATB  sim_LATB.byte
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
#define TMR1L TMR1L_REG
#define TMR1H TMR1H_REG
#define CCPR1L CCPR1L_REG
#define CCPR1H CCPR1H_REG
#define CCPR2L CCPR2L_REG
#define CCPR2H CCPR2H_REG
#define EEDATA EEDATA_REG
#define EEADRL EEADRL_REG
#define EECON2 EECON2_REG
#define TXREG TXREG_REG
#define TXIF  TXIF_REG

/* Bit-Aliase auf dieselbe Speicherstelle wie LATA/LATB */
#define RB4   sim_RB4
#define LED_1_BIT sim_LATA.bit.b0
#define LATA0 sim_LATA.bit.b0
#define LATA1 sim_LATA.bit.b1
#define LATA4 sim_LATA.bit.b4
#define LATB3 sim_LATB.bit.b3
#define LATB5 sim_LATB.bit.b5
#define LATB6 sim_LATB.bit.b6
#define LATB7 sim_LATB.bit.b7

/* Testsicht auf die Ausgaenge */
#define sim_LATA0 (sim_LATA.bit.b0)
#define sim_LATA1 (sim_LATA.bit.b1)
#define sim_LATA4 (sim_LATA.bit.b4)
#define sim_LATB3 (sim_LATB.bit.b3)
#define sim_LATB5 (sim_LATB.bit.b5)
#define sim_LATB6 (sim_LATB.bit.b6)
#define sim_LATB7 (sim_LATB.bit.b7)

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
typedef struct { unsigned res0:1, res1:1, res2:1, RX9D:1, OERR:1, FERR:1, res6:1, CREN:1, SPEN:1; } RCSTAbits_t;
extern RCSTAbits_t RCSTAbits;
typedef struct { unsigned RD:1, WR:1, WREN:1, res3:1, res4:1, res5:1, CFGS:1, EEPGD:1; } EECON1bits_t;
extern EECON1bits_t EECON1bits;

extern uint8_t sim_RB4;

/* Registerzugriffe mit Lesenebenwirkung (siehe Firmware) */
uint8_t sim_tmr1l(void);
uint8_t sim_tmr1h(void);
uint8_t sim_rcreg_lesen(void);
uint8_t sim_ferr_lesen(void);
void    sim_cren_neustart(void);

#define TMR1L_LESEN()   sim_tmr1l()
#define TMR1H_LESEN()   sim_tmr1h()
#define RCREG_LESEN()   sim_rcreg_lesen()
#define FERR_LESEN()    sim_ferr_lesen()
#define UART_NEUSTART() do { RCSTAbits.CREN = 0; sim_cren_neustart(); RCSTAbits.CREN = 1; } while (0)

void __delay_us(unsigned long us);
void __delay_ms(unsigned long ms);

extern uint32_t sim_now_ticks;          // Simulationszeit in 4-us-Ticks
extern uint32_t sim_timer1_basis;       // Startpunkt des freien Timer1
extern uint32_t sim_debug_step;
extern uint32_t sim_guard;              // Endlos-Schutz
extern uint8_t  sim_eeprom[256];
extern uint32_t sim_eeprom_wr_rest;     // Restlaufzeit des EEPROM-Schreibens
extern uint32_t sim_delay_ticks;        // von __delay verbrauchte Zeit
extern uint32_t sim_tmr1_lese_versatz;  // kuenstliche Zeit zwischen zwei Timerlesungen
extern uint32_t sim_isr_zaehler;
extern int      sim_takt_fehler;        // T1CON nicht FOSC/4 (Modell ungueltig)
extern uint32_t sim_faktor;             // Timerschritte je Simulationstick

#define SIM_EE_DAUER_TICKS 1250u        // EEPROM-Schreibdauer: ca. 5 ms

uint32_t sim_zeitstempel(void);         // 32-Bit Simulationszeit seit Timerstart
uint64_t sim_timer_roh(void);           // roher Timer1-Stand (mit Prescalerfaktor)
void     sim_reset(void);               // gesamten Shim-Zustand loeschen
void     host_register_sync(void);      // T1CON/TMR1-Schreibzugriffe uebernehmen
void     sim_schritt(uint32_t ticks);   // Zeit ablaufen lassen, Interrupts bedienen
void     sim_hauptschleife(uint32_t durchlaeufe);  // Firmware-Rumpf + Zeit
void     sim_kante(void);               // fallende RB4-Flanke (OT) ausloesen
void     sim_sende(uint8_t b);          // serielles Byte in den Empfang legen
void     sim_sende_ferr(uint8_t b);     // Byte mit Framing-Fehler

#ifdef __cplusplus
}
#endif

#endif /* HOST_PIC_SHIM_H */
