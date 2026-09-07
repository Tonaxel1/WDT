/*********************************************************************************
 * Speicher für das hostseitige Register-/Zeitmodell, siehe host_pic_shim.h.
 *********************************************************************************/
#include "host_pic_shim.h"

unsigned char TMR1H, TMR1L, CCPR1H, CCPR1L;
unsigned char LATA, LATB, ANSELA, ANSELB, TRISA, TRISB, WPUB;
unsigned char OPTION_REG, OSCCON, APFCON;
unsigned char RCSTA, SPBRGH, SPBRG, TXSTA, BAUDCON;
unsigned char EEADRL, EEDATA, EECON2;
unsigned char RCREG, TXREG;
unsigned char T1CON, CCP1CON;

unsigned char LATA0, LATA1, LATA4;
unsigned char LATB3, LATB5, LATB6, LATB7;

T1CONbits_t T1CONbits;
PIR1bits_t PIR1bits;
PIE1bits_t PIE1bits;
INTCONbits_t INTCONbits;
IOCBFbits_t IOCBFbits;
IOCBNbits_t IOCBNbits;
RCSTAbits_t RCSTAbits;
EECON1bits_t EECON1bits;
