/*********************************************************************************
 * Hostseitiges Register-/Zeitmodell für Tests von My_Spark_1_2026.c
 *
 * Dieser Header ersetzt <htc.h>/<pic16f1827.h>, wenn HOST_TEST definiert ist,
 * und stellt die vom Produktionscode tatsächlich benutzten SFR-Namen als
 * einfache globale Variablen/Bitfelder bereit. Es handelt sich NICHT um ein
 * unabhängiges Verhaltensmodell: Die eigentliche Timing-/Zustandslogik
 * (SensorEreignis, NormalenFunkenPlanen, CompareSetzen, isr, ...) bleibt der
 * unveränderte Produktionscode aus My_Spark_1_2026.c und wird von den Tests
 * per #include eingebunden. Die Testhilfsfunktionen (sim_*), die Timer1
 * hochzählen und CCP-/Overflow-Matches wie die Hardware auslösen, befinden
 * sich in tests/sim_harness.h.
 *
 * WICHTIG: Dies ersetzt keine Zielübersetzung mit HI-TECH C/XC8 für den
 * PIC16F1827 und keine Hardwaremessung. Siehe README.md, Abschnitt Tests.
 *********************************************************************************/
#ifndef HOST_PIC_SHIM_H
#define HOST_PIC_SHIM_H

#include <stdint.h>

#define interrupt /* HI-TECH-spezifisches Schlüsselwort, im Hosttest ohne Wirkung. */

/* Einfache 8-Bit-„Register“, exakt wie im Produktionscode referenziert. */
extern unsigned char TMR1H, TMR1L, CCPR1H, CCPR1L;
extern unsigned char LATA, LATB, ANSELA, ANSELB, TRISA, TRISB, WPUB;
extern unsigned char OPTION_REG, OSCCON, APFCON;
extern unsigned char RCSTA, SPBRGH, SPBRG, TXSTA, BAUDCON;
extern unsigned char EEADRL, EEDATA, EECON2;
extern unsigned char RCREG, TXREG;
extern unsigned char T1CON, CCP1CON;

/* Bitadressierbare Ausgangs-Latches (LATAx/LATBx), einzeln wie im Original. */
extern unsigned char LATA0, LATA1, LATA4;
extern unsigned char LATB3, LATB5, LATB6, LATB7;

typedef struct { unsigned char TMR1ON; } T1CONbits_t;
typedef struct { unsigned char TMR1IF, CCP1IF, RCIF, TXIF; } PIR1bits_t;
typedef struct { unsigned char CCP1IE, TMR1IE, RCIE; } PIE1bits_t;
typedef struct { unsigned char IOCIF, TMR0IF, TMR0IE, IOCIE, PEIE, GIE; } INTCONbits_t;
typedef struct { unsigned char IOCBF4; } IOCBFbits_t;
typedef struct { unsigned char IOCBN4; } IOCBNbits_t;
typedef struct { unsigned char OERR, CREN, FERR; } RCSTAbits_t;
typedef struct { unsigned char EEPGD, WREN, WR, RD; } EECON1bits_t;

extern T1CONbits_t T1CONbits;
extern PIR1bits_t PIR1bits;
extern PIE1bits_t PIE1bits;
extern INTCONbits_t INTCONbits;
extern IOCBFbits_t IOCBFbits;
extern IOCBNbits_t IOCBNbits;
extern RCSTAbits_t RCSTAbits;
extern EECON1bits_t EECON1bits;

#endif /* HOST_PIC_SHIM_H */
