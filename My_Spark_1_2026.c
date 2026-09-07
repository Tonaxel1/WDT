/*********************************************************************************
*        File: My Spark_1_2026.c
*        Controller: PIC16f1827
*        Compiler: Hitech C
*        Date: 02.04.2026.
*        Author: Axel
*        Description: Sensorsignal rein, Funken raus, serielle Parametereingabe
************************************************************************************
         Header files
************************************************************************************/
#include <htc.h>
#include <pic16f1827.h>          //Pic Werte

/***********************************************************************************
         Configuration Bits
************************************************************************************/
__CONFIG(FOSC_INTOSC & PWRTE_ON & BOREN_ON & MCLRE_OFF & WDTE_OFF & CP_OFF & PLLEN_OFF);
__CONFIG (LVP_OFF); // Config 1 + 2

/***********************************************************************************
         Konstanten
************************************************************************************/
#define  _XTAL_FREQ 8000000
#define  SENSOR  RB4
#define  LED_1   LATA0
#define  LED_2   LATA1
#define  COIL    LATA4
#define  DREHZ   LATB3
#define  ROT     LATB5
#define  GRUEN   LATB6
#define  BLAU    LATB7

/**********************************************************************************
         Variablen, Programmdeklarationen
***********************************************************************************/
unsigned char OT_1, LADEZEIT, WERT_1, WERT_2, WERT_3, TEMP, TXWERT;
unsigned char STARTFUNKE, START_WINKEL;
unsigned int DREHZ_W, VOR_W, DREHZ_MAX, ZEIT;
unsigned long TIMER_WERT, DREHZAHL, SOLLWINKEL, SOLLW_WERT;
unsigned char BYTE1, BYTE2, BYTE3, BYTE4, BYTE5, BYTE6, BYTE7, BYTE8, BYTE9, BYTE10, BYTE11;

void Init (void);
void EESchreibe (void);
void EELese(void);
void Funke (void);
void Blink (void);
void Empfang (void);
void Aufwachen (void);
/***********************************************************************************
         Interrupt Routine - robust, ohne blockierende Schleifen
***********************************************************************************/

void interrupt isr(void)
{
    if (INTCONbits.IOCIF && IOCBFbits.IOCBF4)
    {
        if (OT_1 == 5)
        {
          OT_1 = 6;
          Funke();
        }
        T1CONbits.TMR1ON = 0;
        TIMER_WERT = (TMR1H << 8) + TMR1L;
        TMR1L = 0;
        TMR1H = 0;
        T1CONbits.TMR1ON = 1;
        PIR1bits.TMR1IF = 0;
        if (TIMER_WERT == 0) TIMER_WERT = 1;

        DREHZAHL = 15000000UL / TIMER_WERT;
        if (DREHZAHL > 12000) DREHZAHL = 12000;

        if (OT_1 < 5)
        {
            OT_1++;
            SOLLW_WERT = ((unsigned long)TIMER_WERT * START_WINKEL) / 360UL;
            if (SOLLW_WERT < 1) SOLLW_WERT = 1;
            STARTFUNKE = 1;           
        }
        else
        {
            unsigned long temp = (unsigned long)DREHZAHL * VOR_W;
            if (DREHZAHL < DREHZ_W)
                SOLLWINKEL = (temp + (DREHZ_W >> 1)) / DREHZ_W;
            else
                SOLLWINKEL = VOR_W;

            temp = (unsigned long)TIMER_WERT * (360 - SOLLWINKEL);
            SOLLW_WERT = ((temp + 180) / 360) - ZEIT;
            if (SOLLW_WERT < 1) SOLLW_WERT = 1;
        }

        CCPR1L = SOLLW_WERT & 0xFF;
        CCPR1H = SOLLW_WERT >> 8;

        IOCBFbits.IOCBF4 = 0;
        INTCONbits.IOCIF = 0;
    }
}
/**********************************************************************************
         Main
***********************************************************************************/
void main (void)
{
  ROT = 1;
  GRUEN = 1;
  BLAU = 1;
  Init();
  LED_1 = 0;
  LED_2 = 0;
  COIL = 0;
  DREHZ = 1;
  Blink();
  ROT = 0;
  __delay_ms(100);
  ROT = 1;
  GRUEN = 0;
  __delay_ms(100);
  GRUEN = 1;
  BLAU = 0;
  __delay_ms(100);
  BLAU = 1;               //Test LED´s
  
  PIE1bits.CCP1IE = 1; // CCP1 Compare Interrupt aktivieren
  TMR1L = 0;
  TMR1H = 0;
  OT_1 = 0;
  TEMP = 0;
  Aufwachen();
  IOCBNbits.IOCBN4 = 1;
  PIE1bits.TMR1IE = 1;
  PIR1bits.TMR1IF = 0;
  T1CON = 0b00110001;
  CCP1CON = 0b00001010;
  INTCON = 0b10001000;
  
  TXWERT = 0;
  while (TXWERT < 250)
  {
    TXREG =  0b11011101;
    __delay_ms(1);
    while (TXIF == 0);
    TXWERT++;
  } 
  

  while (1)
  { 
    if (PIR1bits.RCIF)
    Empfang();

    if (PIR1bits.TMR1IF == 1)
    {
      PIR1bits.TMR1IF = 0;
      GRUEN = 1;
      BLAU = 1;
      OT_1 = 0;
    }


    if (PIR1bits.CCP1IF)
    {
        if (STARTFUNKE)
        {
            Funke();                 // Zündung nach OT für die ersten 5 Impulse
            STARTFUNKE = 0;
        }
        else if ((DREHZAHL < DREHZ_MAX) && (OT_1 >= 5))
        {
            Funke();
        }
        PIR1bits.CCP1IF = 0;
    }

    if ((DREHZAHL <= 500*WERT_1)&&(PIR1bits.TMR1IF == 0))
    { 
      ROT = 1;
      GRUEN = 1; 
      BLAU = 0; 
    }

    if ((DREHZAHL >= WERT_1*500)&&(DREHZAHL <= WERT_2*500)&&(PIR1bits.TMR1IF == 0))
    { 
      ROT = 1; 
      GRUEN = 0; 
      BLAU = 1; 
    }

    if ((DREHZAHL >= WERT_2*500)&&(DREHZAHL <= WERT_3*500)&&(PIR1bits.TMR1IF == 0))
    { 
      ROT = 0; 
      GRUEN = 0; 
      BLAU = 1;
    }

    if ((DREHZAHL >= WERT_3*500)&&(PIR1bits.TMR1IF == 0))
    { 
      ROT = 0; 
      GRUEN = 1; 
      BLAU = 1; 
    }
  }
}
/**********************************************************************************
         Funktionen, Unterprogramme
***********************************************************************************/
void Aufwachen (void)
{
  while (TEMP < 100)
  {
    TEMP++;
    if (TEMP < 100)
    TXREG = 0b10101010;
    __delay_us(500);
  } 
}


void Empfang (void)
{
    unsigned int timeout;
    unsigned char i;
    unsigned char tmp;

    if (!PIR1bits.RCIF) return;

    if (RCSTAbits.OERR) {
        RCSTAbits.CREN = 0;
        RCSTAbits.CREN = 1;
    }

    for (i = 1; i <= 11; ++i)
    {
        timeout = 8000u; // Bei 8 MHz.  20 ms

        // Warte auf neues empfangenes Byte
        while (!PIR1bits.RCIF)
        {
            if (--timeout == 0u) {
                // Timeout: Abbruch, unvollständige Übertragung -> raus
                return;
            }
        }

        // Lese RCREG (dies setzt RCIF zurück, falls keine weiteren Daten)
        tmp = RCREG;

        // Bei Framing-Fehlern könnte man hier optional reagieren:
        // if (RCSTAbits.FERR) { /* Fehlerbehandlung falls gewünscht */ }

        // In die globalen Byte-Variablen schreiben
        switch (i)
        {
            case 1: BYTE1 = tmp; break;
            case 2: BYTE2 = tmp; break;    // Beachte: Variable heißt BYTE2 (Typos im Original)
            case 3: BYTE3 = tmp; break;
            case 4: BYTE4 = tmp; break;
            case 5: BYTE5 = tmp; break;
            case 6: BYTE6 = tmp; break;
            case 7: BYTE7 = tmp; break;
            case 8: BYTE8 = tmp; break;
            case 9: BYTE9 = tmp; break;
            case 10: BYTE10 = tmp; break;
            case 11: BYTE11 = tmp; break;
        }

        // Kurze Pause nicht erforderlich; nächstes Byte wird erwartet
    }

    if (BYTE1 == (unsigned char)'S')
    {
        EEDATA = BYTE2;          // Startwinkel
        EEADRL = 1;
        EESchreibe();            
        EEDATA = BYTE3;          // Vorwinkel max.
        EEADRL = 2;
        EESchreibe();
        EEDATA = BYTE4;          // Ladezeit
        EEADRL = 3;
        EESchreibe();
        EEDATA = BYTE5;          // DREHZ_W High
        EEADRL = 4;
        EESchreibe();
        EEDATA = BYTE6;          // DREHZ_W Low
        EEADRL = 5;
        EESchreibe();
        EEDATA = BYTE7;          // DREHZ_MAX High
        EEADRL = 6;
        EESchreibe();
        EEDATA = BYTE8;          // Drehz_Max Low
        WERT_1 = BYTE8;
        EEADRL = 7;
        EESchreibe();
        EEDATA = BYTE9;          // Unterer Drehzahlwert für LED (Blau)
        WERT_2 = BYTE9;
        EEADRL = 8;
        EESchreibe();
        EEDATA = BYTE10;         // Mittlerer Drehzahlwert für LED (Grün)
        WERT_3 = BYTE10;
        EEADRL = 9;
        EESchreibe();            // Oberer Drehzahlwert für LED (Gelb)
        EEDATA = BYTE11;
        EEADRL = 10;
        EESchreibe();


        if (PIR1bits.TMR1IF == 1) // Wenn Moped aus
        Blink();                  // Blinken bei gültigem Empfang

        START_WINKEL = BYTE2;
        // RAM-Variablen sofort aktualisieren (sonst bleiben alte Werte aus EEPROM bestehen, was zu unplausiblen Werten führt)
        VOR_W = (unsigned int)BYTE3;
        if (VOR_W == 0) VOR_W = 1;

        DREHZ_W = ((unsigned int)BYTE6 << 8) | (unsigned int)BYTE5; 

        DREHZ_MAX = ((unsigned int)BYTE8 << 8) | (unsigned int)BYTE7; 

        LADEZEIT = BYTE4;
        if (LADEZEIT == 1)
        ZEIT = 250;
        if (LADEZEIT == 2)
        ZEIT = 375;
        if (LADEZEIT == 3)
        ZEIT = 500;
        if (LADEZEIT == 4)
        ZEIT = 625;
        if (LADEZEIT == 5)
        ZEIT = 750;
        if (VOR_W > 40) VOR_W = 40;
        if (VOR_W == 0) VOR_W = 1;          // Bei 0 vergrößert sich der Puls auf ca. 6 ms.
        if (DREHZ_W < 2000 || DREHZ_W > 6000) DREHZ_W = 4000;
        if (DREHZ_MAX < 4000 || DREHZ_MAX > 11000) DREHZ_MAX = 11000;
    }
}


void Funke (void)
{
  COIL = 1;
  DREHZ = 1;
  LED_2 = 1;
  if (LADEZEIT == 1)
  __delay_us(1000);
  if (LADEZEIT == 2)
  __delay_us(1500); 
   if (LADEZEIT == 3)
  __delay_us(2000);
  if (LADEZEIT == 4)
  __delay_us(2500);
  if (LADEZEIT == 5)
  __delay_us(3000);
  COIL = 0;
  DREHZ = 0;
  LED_2 = 0;
}


void Blink (void)
{
  LED_1 = 1;
  __delay_ms(200);
  LED_1 = 0;
  __delay_ms(200);

  LED_1 = 1;
  __delay_ms(200);
  LED_1 = 0;
  __delay_ms(200);

  LED_1 = 1;
  __delay_ms(200);
  LED_1 = 0;
  __delay_ms(200);
}


void EESchreibe (void)
{
  EECON1bits.EEPGD = 0;
  EECON1bits.WREN = 1;
  INTCONbits.GIE = 0;
  EECON2 = 0x55;
  EECON2 = 0xAA;
  EECON1bits.WR = 1;
  while (EECON1bits.WR == 1); /* Warte auf Schreibende */
  INTCONbits.GIE = 1;
  EECON1bits.WREN = 0;
}


void EELese (void)
{
  EECON1bits.EEPGD = 0;
  EECON1bits.RD = 1; /* EEDATA enthält nun den gelesenen Wert */
}


void Init (void)
{
  ANSELA = 0;
  ANSELB = 0;
  TRISA = 0b00000000;
  TRISB = 0b10010111;
  WPUB = 0b00010111;  // Pull-ups für RB0, RB2, RB3, RB4 aktivieren
  OPTION_REG = 0b00111111;
  OSCCON = 0b01110010;
  RCSTA = 0b10010000;
  SPBRGH =0;
  SPBRG = 207;                    // 9600 Baud
  TXSTA= 0b10100100;
  BAUDCON = 0b00001000;

  EEADRL = 1;
  EELese();
  START_WINKEL = EEDATA;
  EEADRL = 2;
  EELese();
  VOR_W = EEDATA;
  EEADRL = 3;
  EELese();
  LADEZEIT = EEDATA;
  EEADRL = 4;
  EELese();
  DREHZ_W = EEDATA;
  DREHZ_W = DREHZ_W << 8;
  EEADRL = 5;
  EELese();
  DREHZ_W = DREHZ_W | EEDATA;
  EEADRL = 6;
  EELese();
  DREHZ_MAX = EEDATA;
  DREHZ_MAX = DREHZ_MAX << 8;
  EEADRL = 7;
  EELese();
  DREHZ_MAX = DREHZ_MAX | EEDATA;
  EEADRL = 8;
  EELese();
  WERT_1 = EEDATA;
  EEADRL = 9;
  EELese();
  WERT_2 = EEDATA;
  EEADRL = 10;
  EELese();
  WERT_3 = EEDATA;

  if (START_WINKEL > 20) START_WINKEL = 10;
  if (VOR_W > 40) VOR_W = 40;
  if (DREHZ_W < 2000 || DREHZ_W > 6000) DREHZ_W = 4000;
  if (DREHZ_MAX < 4000 || DREHZ_MAX > 12000) DREHZ_MAX = 10000;
  if (LADEZEIT > 5)
  LADEZEIT = 3;
  if (LADEZEIT == 1)
  ZEIT = 250;
  if (LADEZEIT == 2)
  ZEIT = 375;
  if (LADEZEIT == 3)
  ZEIT = 500;
  if (LADEZEIT == 4)
  ZEIT = 625;
  if (LADEZEIT == 5)
  ZEIT = 750;
  if (WERT_1 > 30)
  WERT_1 = 4;
  if (WERT_2 > 30)
  WERT_2 = 8;
  if (WERT_3 > 30)
  WERT_3 = 12;
}