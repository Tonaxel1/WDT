/*********************************************************************************
*        File: My_Spark_1_2026.c
*        Controller: PIC16F1827
*        Compiler: Hitech C
*        Date: 02.04.2026.
*        Author: Axel
*        Description: Sensorsignal rein, Funken raus, serielle Parametereingabe
*
*        Zeitschema: Timer1 laeuft frei mit 4 us je Tick (8 MHz / Prescaler 1:2).
*        Die Ueberlaeufe zaehlen die oberen 16 Bit, daraus entsteht ein
*        32-Bit Zeitstempel, der erst nach ca. 4,7 h ueberlaeuft.
*        CCP1-Compare = COIL aus (Funke, Flanke 1 -> 0 an LATA4).
*        CCP2-Compare = Ladebeginn (COIL ein, LATA4 = 1).
*        Fallende Flanke an RB4 (Interrupt-on-Change) = OT, ein Impuls
*        pro Umdrehung. START_WINKEL = Grad NACH dieser OT-Flanke.
*        Ziel ist der Funke VOR dem naechsten OT (Vorzuendung ueber VOR_W).
*        Wenn die Ladezeit zwischen Funke und naechstem OT nicht mehr Platz
*        hat, wird die Ladung bereits vor OT begonnen (Vorlauf aus der
*        letzten gueltigen Umdrehung) und der Funkenzeitpunkt am neuen OT
*        nachkorrigiert. Physik-Grenze: COIL aus erzeugt immer einen Funken,
*        deshalb wird eine geladene Spule nie "funkenfrei" abgeschaltet.
************************************************************************************
         Header files
************************************************************************************/
#ifdef HOST_TEST
#include "host_pic_shim.h"       // Host-Simulation (gcc), siehe tests/
#define interrupt
#define __CONFIG(...)
#else
#include <htc.h>
#include <pic16f1827.h>          //Pic Werte

/***********************************************************************************
         Configuration Bits
************************************************************************************/
__CONFIG(FOSC_INTOSC & PWRTE_ON & BOREN_ON & MCLRE_OFF & WDTE_OFF & CP_OFF);
__CONFIG(LVP_OFF & PLLEN_OFF); // Config 1 + 2
#endif

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

#define  TIMER_UEBERLAEUFE_MAX  120          // ohne Impuls nach ca. 2 s: Motor aus
#define  RX_TIMEOUT_TICKS       2000         // Rahmenabbruch nach ca. 8 ms

/**********************************************************************************
         Variablen, Programmdeklarationen
***********************************************************************************/
unsigned char OT_1, LADEZEIT, WERT_1, WERT_2, WERT_3, TEMP, TXWERT;
unsigned char STARTFUNKE, START_WINKEL;
unsigned int  DREHZ_W, VOR_W, DREHZ_MAX, ZEIT;
unsigned long TIMER_WERT, DREHZAHL, SOLLWINKEL;
unsigned long NEUE_PERIODE, LADUNGSBEGINN, FUNKENZEITPUNKT, NEUES_OT;
unsigned char LADUNG_AKTIV, EREIGNIS, VORLAUF, OT_KORRIGIEREN;
unsigned char EMPFANGSZAEHLER, RAHMEN_AKTIV;
unsigned long RAHMEN_ZEIT;
unsigned char EE_INDEX, EE_PENDING;
unsigned char EE_SPEICHER[10];
unsigned char GIE_GEMERKT;
unsigned char ueberlaeufe;
unsigned int  timer1_hoch;
unsigned char PLAN_Sperre;

unsigned char BYTE1, BYTE2, BYTE3, BYTE4, BYTE5, BYTE6, BYTE7, BYTE8, BYTE9, BYTE10, BYTE11;

void Init (void);
void EESchreibe (void);
void EELese (void);
void EEDienst (void);
void Funke (void);
void Blink (void);
void Empfang (void);
void Aufwachen (void);
void PruefeParameter (void);
void LadezeitSetzen (void);
void PlaneEreignisse (void);
void EmpfangsDienst (void);

/***********************************************************************************
         Interrupt Routine - kurz gehalten, Planung laeuft im Hauptprogramm
***********************************************************************************/

void interrupt isr(void)
{
  if (INTCONbits.IOCIF && IOCBFbits.IOCBF4)          // fallende Flanke RB4 = OT
  {
    unsigned int t;
    t = (unsigned int)TMR1L;
    t |= (unsigned int)TMR1H << 8;
    NEUE_PERIODE = ((unsigned long)timer1_hoch << 16) | (unsigned long)t;
    if (PIR1bits.TMR1IF && t < 0x8000)               // Ueberlauf kurz vor dem Lesen
      NEUE_PERIODE += 65536UL;                       // Kante lag nach dem Ueberlauf
    EREIGNIS = 1;                                    // Planung im Hauptprogramm
    ueberlaeufe = 0;
    IOCBFbits.IOCBF4 = 0;
    INTCONbits.IOCIF = 0;
  }

  if (PIR1bits.TMR1IF)
  {
    PIR1bits.TMR1IF = 0;
    timer1_hoch++;
    if (ueberlaeufe < 255)
      ueberlaeufe++;
  }

  if (PIR1bits.CCP1IF)                               // Funkenzeitpunkt erreicht
  {
    PIR1bits.CCP1IF = 0;
    if (EREIGNIS == 3 || EREIGNIS == 5 || LADUNG_AKTIV)
    {
      Funke();                                       // COIL aus -> Funke
      EREIGNIS = 0;
    }
  }

  if (PIR2bits.CCP2IF)                               // Ladebeginn erreicht
  {
    PIR2bits.CCP2IF = 0;
    if (EREIGNIS == 2 || EREIGNIS == 4)
    {
      COIL = 1;
      DREHZ = 1;
      LED_2 = 1;
      LADUNG_AKTIV = 1;
      EREIGNIS = 3;
    }
  }
}

/**********************************************************************************
         Main
***********************************************************************************/
#ifdef HOST_TEST
void Firmware_main (void)              // Host-Test: Hauptprogramm ohne Endlosschleife
#else
void main (void)
#endif
{
  Init();
  Blink();                          // LED-Starttest vor der Zuendfreigabe

  Aufwachen();                      // 99 x 0xAA zum Aufwecken
  TXWERT = 0;
  while (TXWERT < 250)
  {
    while (TXIF == 0);
    TXREG = 0b11011101;             // 250 x 0xDD als Startmeldung
    TXWERT++;
  }

  OT_1 = 0;
  STARTFUNKE = 0;
  EREIGNIS = 0;
  LADUNG_AKTIV = 0;
  ueberlaeufe = 0;
  timer1_hoch = 0;
  TIMER_WERT = 0;
  DREHZAHL = 0;
  EMPFANGSZAEHLER = 0;
  RAHMEN_AKTIV = 0;
  EE_PENDING = 0;
  IOCBF = 0;
  PIR1 = 0;
  PIR2 = 0;
  T1GCON = 0;                       // Timer1 Gate aus
  T1CON = 0b00010001;               // Timer1 ein, Prescaler 1:2, interner Takt
  CCP1CON = 0b00001010;             // Compare mit Software-Interrupt
  CCP2CON = 0b00001010;
#ifdef HOST_TEST
  host_register_sync();
#endif
  IOCBNbits.IOCBN4 = 1;
  PIE1bits.TMR1IE = 1;
  PIE1bits.CCP1IE = 1;
  PIE2bits.CCP2IE = 1;
  PIE1bits.RCIE = 0;
  INTCONbits.PEIE = 1;              // Compare-Interrupts brauchen PEIE
  INTCONbits.IOCIE = 1;
  INTCONbits.GIE = 1;               // ab hier laeuft die Zuendung

  while (1)
  {
    if (ueberlaeufe >= TIMER_UEBERLAEUFE_MAX && EREIGNIS != 1)
    {
      // Kein Sensorsignal mehr: Drehzahl 0, Betrieb neu starten
      ueberlaeufe = 0;
      DREHZAHL = 0;
      OT_1 = 0;
      STARTFUNKE = 0;
      GRUEN = 1;
      BLAU = 1;
      if (LADUNG_AKTIV && (EREIGNIS == 0 || EREIGNIS == 3))
        Funke();                   // begrenztes Abschalten, erzeugt Funken
      EREIGNIS = 0;
    }

    if (EREIGNIS == 1)
      PlaneEreignisse();

    EmpfangsDienst();
    EEDienst();

    if (DREHZAHL == 0)               // RGB aus bei Stillstand
    {
      ROT = 1;
      GRUEN = 1;
      BLAU = 1;
    }
    else if (DREHZAHL <= 500UL * WERT_1)
    {
      ROT = 1;
      GRUEN = 1;
      BLAU = 0;
    }
    else if (DREHZAHL <= 500UL * WERT_2)
    {
      ROT = 1;
      GRUEN = 0;
      BLAU = 1;
    }
    else if (DREHZAHL <= 500UL * WERT_3)
    {
      ROT = 0;
      GRUEN = 0;
      BLAU = 1;
    }
    else
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
void PlaneEreignisse (void)
{
  unsigned long kante;
  unsigned long periode;
  unsigned long ziel;
  unsigned long start;
  long          differenz;

  INTCONbits.IOCIE = 0;              // nur fuer den Schnappschuss sperren
  kante = NEUE_PERIODE;
  EREIGNIS = 0;
  INTCONbits.IOCIE = 1;

  if (TIMER_WERT != 0)
    periode = kante - NEUES_OT;      // Abstand zur letzten gueltigen Kante
  else
    periode = 0;
  NEUES_OT = kante;

  if (periode != 0)
  {
    TIMER_WERT = periode;
    DREHZAHL = 15000000UL / TIMER_WERT;   // Messung nicht kappen
  }
  else
    periode = TIMER_WERT;

  if (OT_1 < 5)
  {
    // Messphase: fuenf Kanten zaehlen, erst danach Startfunken
    OT_1++;
    if (OT_1 == 5 && periode != 0)
    {
      STARTFUNKE = 1;
      ziel = ((periode * START_WINKEL) + 180UL) / 360UL;
      EREIGNIS = 2;
      VORLAUF = 0;
      OT_KORRIGIEREN = 0;
      LADUNGSBEGINN = kante + ziel;
      FUNKENZEITPUNKT = LADUNGSBEGINN + ZEIT;
    }
  }
  else if (STARTFUNKE < 6 && periode != 0)
  {
    // fuenf Startfunken mit festem Winkel hinter OT
    STARTFUNKE++;
    ziel = ((periode * START_WINKEL) + 180UL) / 360UL;
    EREIGNIS = 2;
    VORLAUF = 0;
    OT_KORRIGIEREN = 0;
    LADUNGSBEGINN = kante + ziel;
    FUNKENZEITPUNKT = LADUNGSBEGINN + ZEIT;
  }
  else if (periode != 0)
  {
    if (DREHZAHL < DREHZ_MAX)
    {
      if (DREHZAHL < DREHZ_W)
        SOLLWINKEL = ((DREHZAHL * VOR_W) + (DREHZ_W >> 1)) / DREHZ_W;
      else
        SOLLWINKEL = VOR_W;

      ziel = ((periode * SOLLWINKEL) + 180UL) / 360UL;   // Funkenzeit vor OT
      if (ziel < ZEIT)
        ziel = ZEIT;                     // Funke nie naeher am OT als Ladezeit

      start = kante + periode - ziel;    // Soll-Funkenzeitpunkt
      EREIGNIS = 2;
      LADUNGSBEGINN = start - ZEIT;
      FUNKENZEITPUNKT = start;
      if (LADUNGSBEGINN <= kante)
      {
        // Ladezeit passt nicht mehr hinter OT: Ladung vor OT beginnen,
        // Zielzeitpunkt an der neuen Kante nachkorrigieren
        VORLAUF = 1;
        OT_KORRIGIEREN = 1;
      }
      else
      {
        VORLAUF = 0;
        OT_KORRIGIEREN = 0;
      }
    }
    else
      EREIGNIS = 0;                      // Begrenzer: weder laden noch zuenden
  }

  if (EREIGNIS == 2)
  {
    if (VORLAUF)
    {
      differenz = (long)(LADUNGSBEGINN - kante);
      if (differenz <= 0)
      {
        // Ladebeginn liegt schon zurueck: sofort laden
        COIL = 1;
        DREHZ = 1;
        LED_2 = 1;
        LADUNG_AKTIV = 1;
        EREIGNIS = 3;
      }
    }

    if (EREIGNIS == 2)
    {
      CCPR2L = (unsigned char)(LADUNGSBEGINN & 0xFF);
      CCPR2H = (unsigned char)(LADUNGSBEGINN >> 8);
      PIR2bits.CCP2IF = 0;
    }
    else
    {
      if (OT_KORRIGIEREN)
        FUNKENZEITPUNKT = kante + ziel;  // Funkenzeit relativ zum neuen OT
      CCPR1L = (unsigned char)(FUNKENZEITPUNKT & 0xFF);
      CCPR1H = (unsigned char)(FUNKENZEITPUNKT >> 8);
      PIR1bits.CCP1IF = 0;
    }
  }
}


void EmpfangsDienst (void)
{
  unsigned char zeichen;
  unsigned long jetzt;

  if (PIR1bits.RCIF)
  {
    zeichen = RCREG;
    jetzt = ((unsigned long)timer1_hoch << 16) + (unsigned int)TMR1L;
    if (RCSTAbits.FERR)
    {
      RAHMEN_AKTIV = 0;                  // Framing-Fehler: Byte verwerfen
      EMPFANGSZAEHLER = 0;
    }
    else if (!RAHMEN_AKTIV)
    {
      if (zeichen == (unsigned char)'S')
      {
        RAHMEN_AKTIV = 1;
        EMPFANGSZAEHLER = 1;
        BYTE1 = zeichen;
        RAHMEN_ZEIT = jetzt;
      }
    }
    else
    {
      RAHMEN_ZEIT = jetzt;
      EMPFANGSZAEHLER++;
      switch (EMPFANGSZAEHLER)
      {
        case 2:  BYTE2  = zeichen; break;
        case 3:  BYTE3  = zeichen; break;
        case 4:  BYTE4  = zeichen; break;
        case 5:  BYTE5  = zeichen; break;
        case 6:  BYTE6  = zeichen; break;
        case 7:  BYTE7  = zeichen; break;
        case 8:  BYTE8  = zeichen; break;
        case 9:  BYTE9  = zeichen; break;
        case 10: BYTE10 = zeichen; break;
        case 11:
          BYTE11 = zeichen;
          RAHMEN_AKTIV = 0;
          EMPFANGSZAEHLER = 0;
          Empfang();
          break;
      }
    }
  }

  if (RCSTAbits.OERR)
  {
    RCSTAbits.CREN = 0;                  // Ueberlauf: Empfang neu starten
    RCSTAbits.CREN = 1;
    RAHMEN_AKTIV = 0;
    EMPFANGSZAEHLER = 0;
  }

  if (RAHMEN_AKTIV)
  {
    jetzt = ((unsigned long)timer1_hoch << 16) + (unsigned int)TMR1L;
    if (jetzt - RAHMEN_ZEIT > RX_TIMEOUT_TICKS)
    {
      RAHMEN_AKTIV = 0;                  // unvollstaendiger Rahmen
      EMPFANGSZAEHLER = 0;
    }
  }
}


void Empfang (void)
{
  PruefeParameter();                     // empfangene Rohwerte begrenzen

  EE_SPEICHER[0] = BYTE2;                // Startwinkel
  EE_SPEICHER[1] = BYTE3;                // Vorwinkel max.
  EE_SPEICHER[2] = BYTE4;                // Ladezeit
  EE_SPEICHER[3] = BYTE5;                // DREHZ_W High
  EE_SPEICHER[4] = BYTE6;                // DREHZ_W Low
  EE_SPEICHER[5] = BYTE7;                // DREHZ_MAX High
  EE_SPEICHER[6] = BYTE8;                // DREHZ_MAX Low
  EE_SPEICHER[7] = BYTE9;                // unterer LED-Wert
  EE_SPEICHER[8] = BYTE10;               // mittlerer LED-Wert
  EE_SPEICHER[9] = BYTE11;               // oberer LED-Wert
  if (!EE_PENDING)
  {
    EE_PENDING = 1;
    EE_INDEX = 0;
    EECON1bits.EEPGD = 0;
    EECON1bits.CFGS = 0;
    EEDATA = EE_SPEICHER[0];
    EEADRL = 1;
    EESchreibe();
  }

  // RAM uebernehmen; ein laufender Ladezyklus wird nicht veraendert
  START_WINKEL = BYTE2;
  VOR_W = BYTE3;
  LADEZEIT = BYTE4;
  DREHZ_W = ((unsigned int)BYTE5 << 8) | BYTE6;
  DREHZ_MAX = ((unsigned int)BYTE7 << 8) | BYTE8;
  WERT_1 = BYTE9;
  WERT_2 = BYTE10;
  WERT_3 = BYTE11;
  PruefeParameter();
  LadezeitSetzen();

  if (!LADUNG_AKTIV && DREHZAHL == 0)
    Blink();                             // Quittung nur im Stillstand
}


void PruefeParameter (void)
{
  if (START_WINKEL > 20) START_WINKEL = 10;
  if (VOR_W > 40) VOR_W = 40;                        // 0 ist erlaubt
  if (DREHZ_W < 2000 || DREHZ_W > 6000) DREHZ_W = 4000;
  if (DREHZ_MAX < 4000 || DREHZ_MAX > 12000) DREHZ_MAX = 10000;
  if (LADEZEIT < 1 || LADEZEIT > 5) LADEZEIT = 3;
  if (WERT_1 > 30) WERT_1 = 4;
  if (WERT_2 > 30) WERT_2 = 8;
  if (WERT_3 > 30) WERT_3 = 12;
}


void LadezeitSetzen (void)
{
  if (LADEZEIT == 1) ZEIT = 250;
  if (LADEZEIT == 2) ZEIT = 375;
  if (LADEZEIT == 3) ZEIT = 500;
  if (LADEZEIT == 4) ZEIT = 625;
  if (LADEZEIT == 5) ZEIT = 750;
}


void EEDienst (void)
{
  if (EE_PENDING && !EECON1bits.WR)
  {
    if (EE_INDEX < 9)
    {
      EE_INDEX++;
      EEDATA = EE_SPEICHER[EE_INDEX];
      EEADRL = EE_INDEX + 1;
      EESchreibe();
    }
    else
      EE_PENDING = 0;
  }
}


void Funke (void)
{
  COIL = 0;                              // Flanke 1 -> 0 erzeugt den Funken
  DREHZ = 0;
  LED_2 = 0;
  LADUNG_AKTIV = 0;
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
  EECON1bits.CFGS = 0;
  EECON1bits.WREN = 1;
  GIE_GEMERKT = INTCONbits.GIE;
  INTCONbits.GIE = 0;                    // nur fuer die Freigabesequenz sperren
  EECON2 = 0x55;
  EECON2 = 0xAA;
  EECON1bits.WR = 1;
  INTCONbits.GIE = GIE_GEMERKT;
  EECON1bits.WREN = 0;                   // Warten uebernimmt EEDienst
}


void EELese (void)
{
  EECON1bits.EEPGD = 0;
  EECON1bits.CFGS = 0;
  EECON1bits.RD = 1;                     // EEDATA enthaelt den gelesenen Wert
#ifdef HOST_TEST
  EEDATA = sim_eeprom[EEADRL];
#endif
}


void Aufwachen (void)
{
  TEMP = 0;
  while (TEMP < 99)
  {
    while (TXIF == 0);
    TXREG = 0b10101010;
    TEMP++;
  }
}


void Init (void)
{
  LATA = 0;                              // Latches vor der Richtung setzen
  LATB = 0b11101000;
  ANSELA = 0;
  ANSELB = 0;
  TRISA = 0;
  TRISB = 0b00010111;                    // RB7 als Ausgang (Blau)
  WPUB = 0b00010111;                     // Pull-ups fuer RB0, RB1, RB2, RB4
  OPTION_REG = 0b00111111;
  APFCON0 = 0;                           // RX auf RB1, TX auf RB2
  APFCON1 = 0;
  OSCCON = 0b01110010;                   // 8 MHz intern
  TXSTA = 0b00100100;                    // Senden ein, BRGH = 1
  RCSTA = 0b10010000;
  BAUDCON = 0b00001000;                  // 16-Bit Baudratenteiler
  SPBRGH = 0;
  SPBRG = 207;                           // 9600 Baud

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
  DREHZ_W = (unsigned int)EEDATA << 8;
  EEADRL = 5;
  EELese();
  DREHZ_W = DREHZ_W | EEDATA;
  EEADRL = 6;
  EELese();
  DREHZ_MAX = (unsigned int)EEDATA << 8;
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

  PruefeParameter();
  LadezeitSetzen();
}
