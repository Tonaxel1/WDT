/*********************************************************************************
*        File: My_Spark_1_2026.c
*        Controller: PIC16F1827
*        Compiler: Hitech C / XC8 (C89-Stil, keine C99-Konstrukte)
*        Date: 02.04.2026.
*        Author: Axel
*        Description: Sensorsignal rein, Funken raus, serielle Parametereingabe
*
*        Zeitbasis
*        ---------
*        FOSC = 8 MHz intern -> Befehlstakt FOSC/4 = 2 MHz.
*        Timer1: TMR1CS<1:0> = 00 (FOSC/4), T1CKPS<1:0> = 11 (1:8), TMR1ON = 1
*        -> T1CON = 0b00110001 = 0x31, ein Tick = 8 / 2 MHz = 4 us.
*        (PIC16(L)F1826/27 Datenblatt DS41391D, Register T1CON.)
*        Ein 16-Bit-Umlauf = 65536 Ticks = 262,144 ms.
*        Die Ueberlaeufe zaehlen die oberen 16 Bit (timer1_hoch), daraus
*        entsteht ein 32-Bit-Zeitstempel mit 4 us Aufloesung, der erst nach
*        2^32 * 4 us = ca. 4,77 h umlaeuft. Der Umlauf ist mit vorzeichen-
*        behafteten Differenzen (s32) ueberall beruecksichtigt.
*
*        Abgeleitete Konstanten
*        ----------------------
*        60 s / 4 us            = 15 000 000 Ticks je Minute (Drehzahlformel)
*        Ladezeit 1..5          = 1,0 / 1,5 / 2,0 / 2,5 / 3,0 ms
*                               = 250 / 375 / 500 / 625 / 750 Ticks
*        UART-Rahmenabbruch     = 2000 Ticks = 8 ms
*        Sensorausfall          = 500 000 Ticks = 2,000 s (echte Zeitmessung,
*                                 nicht ueber gezaehlte Timer-Ueberlaeufe, weil
*                                 die Ueberlaufphase vom Timerstand abhaengt)
*
*        Signale
*        -------
*        Fallende Flanke an RB4 (Interrupt-on-Change) = OT, ein Impuls pro
*        Umdrehung. COIL = LATA4; die fallende Flanke von COIL erzeugt den
*        Funken. CCP2-Compare = Ladebeginn (COIL ein), CCP1-Compare = Funke
*        (COIL aus). CCP1 wird IMMER armiert, bevor die Spule Strom bekommt,
*        damit die Ladezeit auch bei blockiertem Hauptprogramm begrenzt ist.
*
*        Ablauf
*        ------
*        1. Messphase: fuenf OT-Flanken. Die erste Flanke ist nur Referenz,
*           ab der zweiten entsteht eine Periode.
*        2. Startphase: genau fuenf Startfunken. START_WINKEL ist der Winkel
*           des FUNKENS nach OT.
*        3. Normalbetrieb: Funke vor OT (Vorzuendung), linearer Verlauf bis
*           DREHZ_W, darueber konstant VOR_W, Begrenzer bei DREHZ_MAX.
*
*        Termine werden praediktiv geplant. Grundlage ist die Vorhersage der
*        naechsten Periode: gemessene Periode plus begrenzter Trend
*        (+/- 25 %). Ohne diese Fortschreibung liegen bei Beschleunigung alle
*        Termine systematisch hinter der tatsaechlichen Kante. Drehzahl und
*        Vorwinkel werden dagegen aus der ECHTEN Messung gebildet.
*
*        Im Normalbetrieb gilt der Funke der naechsten Umdrehung; die Ladung
*        beginnt entsprechend vor deren OT. In der Startphase wird der Funke
*        derselben Umdrehung geplant, sofern START_WINKEL genug Zeit fuer die
*        volle Ladezeit laesst (ziel >= ZEIT + PLAN_LATENZ). Andernfalls -
*        z. B. bei START_WINKEL 0 - ist er nachtraeglich nicht mehr mit voller
*        Ladezeit erzeugbar; dann wird die naechste Umdrehung vorhergesagt
*        (PLAN_VORAUS = 1) und die Spule laedt vor deren OT.
*        An der neuen OT-Flanke wird ein bereits laufender Ladevorgang nur
*        innerhalb enger Grenzen korrigiert (KORREKTUR_MAX) und niemals
*        verlaengert. Ein vorausgeplanter, noch nicht begonnener Termin bleibt
*        erhalten; ein veralteter Termin der beendeten Umdrehung wird ersetzt.
*
*        Physik-Grenze: COIL aus erzeugt immer einen Funken. Eine geladene
*        Spule wird deshalb nie "funkenfrei" abgeschaltet - auch nicht bei
*        Begrenzer oder Sensorausfall.
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

/* Zieltypen: HI-TECH C / XC8 -> char 8, int 16, long 32 Bit.
   Der Host-Shim definiert dieselben Namen mit fester Breite, damit die
   Tests dieselbe Arithmetik sehen wie das Ziel. */
typedef unsigned char u8;
typedef unsigned int  u16;
typedef unsigned long u32;
typedef signed long   s32;

/* Registerzugriffe, die der Host abfangen muss (Lesenebenwirkungen). */
#define TMR1L_LESEN()   TMR1L
#define TMR1H_LESEN()   TMR1H
#define RCREG_LESEN()   RCREG
#define FERR_LESEN()    RCSTAbits.FERR
#define UART_NEUSTART() do { RCSTAbits.CREN = 0; RCSTAbits.CREN = 1; } while (0)
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

#define  TICKS_PRO_MINUTE       15000000UL   // 60 s / 4 us
#define  SENSOR_TIMEOUT_TICKS   500000UL     // 2,000 s ohne OT-Flanke
#define  RX_TIMEOUT_TICKS       2000UL       // Rahmenabbruch nach 8 ms
#define  PERIODE_MIN            375UL        // < 1,5 ms = > 40000 1/min: Stoerung
#define  PERIODE_MAX            SENSOR_TIMEOUT_TICKS
#define  MIN_VORLAUF            8UL          // 32 us: darunter nicht mehr armieren
#define  LADUNG_MAX             1000UL       // 4 ms harte Obergrenze der Ladezeit
#define  LADUNG_RESERVE         250UL        // 1 ms Reserve fuer die Notabschaltung
#define  KORREKTUR_MAX          125UL        // 500 us Grenze der OT-Nachkorrektur
#define  PLAN_LATENZ            25UL         // 100 us Reserve fuer die Planung
#define  QUITTUNG_TICKS         50000UL      // 200 ms LED-Quittung (nichtblockierend)

/**********************************************************************************
         Variablen, Programmdeklarationen
***********************************************************************************/
/* Parameter (RAM-Abbild des EEPROM) */
u8  LADEZEIT, WERT_1, WERT_2, WERT_3, TEMP, TXWERT;
u8  START_WINKEL;
u16 DREHZ_W, VOR_W, DREHZ_MAX, ZEIT;

/* Messung / Betriebszustand des Planers (nur Hauptprogramm) */
u8  OT_1;                       // gezaehlte Messkanten 0..5
u8  STARTFUNKE;                 // geplante Startfunken 0..5
u8  KANTE_GUELTIG;              // 1 = LETZTE_KANTE ist eine gueltige Referenz
u32 LETZTE_KANTE;               // Zeitstempel der letzten verwerteten Flanke
u32 TIMER_WERT;                 // zuletzt gemessene Periode in Ticks
u32 PERIODE_VOR;                // vorletzte Periode, Grundlage der Vorhersage
u32 DREHZAHL;                   // echte Messdrehzahl, ungekappt
u32 SOLLWINKEL;                 // aktueller Vorzuendwinkel in Grad
u8  FEHLTERMIN;                 // ausgelassene Zyklen (Diagnose)

/* Erfassung der OT-Flanke: nur diese beiden Zellen schreibt die ISR fuer
   das Hauptprogramm. Sie sind vom Spulen-/Planerzustand getrennt, damit
   gleichzeitige Compare- oder Ueberlauf-Interrupts kein OT-Ereignis
   loeschen koennen. */
volatile u32 OT_ZEIT;
volatile u8  OT_NEU;
volatile u8  OT_VERLOREN;       // Flanke kam vor der Verarbeitung der alten

/* Zeitbasis */
volatile u16 timer1_hoch;       // obere 16 Bit des freien 32-Bit-Zeitstempels

/* Spulen- und Terminzustand (ISR und Hauptprogramm) */
volatile u8  LADUNG_AKTIV;      // 1 = COIL fuehrt Strom
volatile u8  FUNKE_ARMIERT;     // 1 = CCP1 traegt einen gueltigen Abschalttermin
volatile u8  LADUNG_ARMIERT;    // 1 = CCP2 traegt einen gueltigen Ladetermin
volatile u32 FUNKENZEITPUNKT;   // 32-Bit-Termin des armierten Abschaltens
volatile u32 LADUNGSBEGINN;     // 32-Bit-Termin des armierten Einschaltens
volatile u32 PLAN_FUNKE;        // Abschalttermin, den CCP2 beim Einschalten armiert
u8 PLAN_VORAUS;                 // 1 = geplanter Funke gehoert zur Umdrehung NACH der naechsten Kante
volatile u32 LADUNG_START_IST;  // tatsaechlicher Beginn der laufenden Ladung

/* UART / EEPROM */
u8  EMPFANGSZAEHLER, RAHMEN_AKTIV;
u32 RAHMEN_ZEIT;
u8  EE_INDEX, EE_PENDING;
u8  EE_SPEICHER[10];            // laufender Schreibauftrag
u8  EE_WARTE[10];               // juengster Rahmen, wartet auf den Bus
u8  EE_WARTE_GUELTIG;
u8  GIE_GEMERKT;
u8  QUITTUNG_AKTIV;
u32 QUITTUNG_ENDE;

u8 BYTE1, BYTE2, BYTE3, BYTE4, BYTE5, BYTE6, BYTE7, BYTE8, BYTE9, BYTE10, BYTE11;

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
void SystemStart (void);
void HauptSchleife (void);
u32  Zeit32 (void);
u32  ZeitJetzt (void);

/**********************************************************************************
         Zeit- und Terminwerkzeuge
***********************************************************************************/
/* Kohaerenter 32-Bit-Zeitstempel. Nur mit gesperrten Interrupts aufrufen
   (in der ISR ist GIE bereits durch die Hardware geloescht).
   TMR1H/TMR1L werden nacheinander gelesen; laeuft das Low-Byte dazwischen
   ueber, wiederholt die Schleife den Zugriff. Ein noch nicht bearbeiteter
   Ueberlauf (TMR1IF) wird zum High-Teil addiert, wenn der gelesene Stand
   bereits in der neuen Runde liegt. */
u32 Zeit32 (void)
{
  u8  h1, h2, l;
  u16 t, hoch;

  h1 = TMR1H_LESEN();
  for (;;)
  {
    l  = TMR1L_LESEN();
    h2 = TMR1H_LESEN();
    if (h2 == h1)
      break;
    h1 = h2;
  }
  t = (u16)(((u16)h1 << 8) | (u16)l);
  hoch = timer1_hoch;
  if (PIR1bits.TMR1IF && t < 0x8000u)
    hoch++;                            // Ueberlauf lag vor dem Lesen
  return (((u32)hoch << 16) | (u32)t);
}

/* Zeitstempel aus dem Hauptprogramm: kurze kritische Strecke mit
   Sicherung und Wiederherstellung von GIE. */
u32 ZeitJetzt (void)
{
  u8  gie;
  u32 t;

  gie = INTCONbits.GIE;
  do { INTCONbits.GIE = 0; } while (INTCONbits.GIE);
  t = Zeit32();
  if (gie)
    INTCONbits.GIE = 1;
  return t;
}

/* CCP1 = Abschalttermin scharf machen. Immer VOR dem Einschalten der Spule. */
static void ArmiereFunke (u32 ziel)
{
  FUNKENZEITPUNKT = ziel;
  FUNKE_ARMIERT = 1;
  CCPR1L = (u8)(ziel & 0xFFUL);
  CCPR1H = (u8)((ziel >> 8) & 0xFFUL);
  PIR1bits.CCP1IF = 0;                 // moeglichen Alt-Treffer verwerfen
  PIE1bits.CCP1IE = 1;
}

static void EntwaffneFunke (void)
{
  FUNKE_ARMIERT = 0;
  PIE1bits.CCP1IE = 0;
  PIR1bits.CCP1IF = 0;
}

static void ArmiereLadung (u32 ziel)
{
  LADUNGSBEGINN = ziel;
  LADUNG_ARMIERT = 1;
  CCPR2L = (u8)(ziel & 0xFFUL);
  CCPR2H = (u8)((ziel >> 8) & 0xFFUL);
  PIR2bits.CCP2IF = 0;
  PIE2bits.CCP2IE = 1;
}

static void EntwaffneLadung (void)
{
  LADUNG_ARMIERT = 0;
  PIE2bits.CCP2IE = 0;
  PIR2bits.CCP2IF = 0;
}

/***********************************************************************************
         Interrupt Routine
         Reihenfolge und damit Prioritaet bei gleichzeitigen Ereignissen:
         1. Timer1-Ueberlauf  (Zeitbasis zuerst, sonst sind alle Stempel falsch)
         2. OT-Flanke         (nur erfassen und veroeffentlichen, keine Rechnung)
         3. CCP1 = Funke      (Abschalten hat Vorrang vor neuem Einschalten)
         4. CCP2 = Ladebeginn
         Jede Quelle prueft ihr eigenes Flag zusammen mit ihrer Freigabe; keine
         Quelle loescht das Ereignis einer anderen. Division und Winkelrechnung
         bleiben im Hauptprogramm.
************************************************************************************/
void interrupt isr(void)
{
  u32 jetzt;

  if (PIR1bits.TMR1IF)
  {
    PIR1bits.TMR1IF = 0;
    timer1_hoch++;
  }

  if (INTCONbits.IOCIE && IOCBFbits.IOCBF4)          // fallende Flanke RB4 = OT
  {
    IOCBFbits.IOCBF4 = 0;             // IOCIF ist nur lesbar und folgt IOCBF
    jetzt = Zeit32();
    if (OT_NEU)
      OT_VERLOREN = 1;                // Hauptprogramm war zu langsam
    OT_ZEIT = jetzt;
    OT_NEU = 1;
  }

  if (PIE1bits.CCP1IE && PIR1bits.CCP1IF)            // Abschalten / Funke
  {
    PIR1bits.CCP1IF = 0;
    if (FUNKE_ARMIERT)
    {
      jetzt = Zeit32();
      if ((s32)(jetzt - FUNKENZEITPUNKT) >= 0)
      {
        Funke();                                     // COIL aus -> Funke
        FUNKE_ARMIERT = 0;
        PIE1bits.CCP1IE = 0;
      }
      /* sonst: Treffer eines frueheren 16-Bit-Umlaufs, armiert lassen */
    }
    else
      PIE1bits.CCP1IE = 0;
  }

  if (PIE2bits.CCP2IE && PIR2bits.CCP2IF)            // Ladebeginn
  {
    PIR2bits.CCP2IF = 0;
    if (LADUNG_ARMIERT)
    {
      jetzt = Zeit32();
      if ((s32)(jetzt - LADUNGSBEGINN) >= 0)
      {
        LADUNG_ARMIERT = 0;
        PIE2bits.CCP2IE = 0;
        /* Abschalttermin zuerst armieren, danach erst Strom geben. */
        if (!LADUNG_AKTIV &&
            (s32)(PLAN_FUNKE - jetzt) > 0 &&
            (u32)(PLAN_FUNKE - jetzt) <= LADUNG_MAX)
        {
          ArmiereFunke(PLAN_FUNKE);
          COIL = 1;
          DREHZ = 1;
          LED_2 = 1;
          LADUNG_AKTIV = 1;
          LADUNG_START_IST = jetzt;
        }
      }
      /* sonst: zu frueher Treffer, armiert lassen */
    }
    else
      PIE2bits.CCP2IE = 0;
  }
}

/**********************************************************************************
         Main
***********************************************************************************/
#ifndef HOST_TEST
void main (void)
{
  SystemStart();
  while (1)
    HauptSchleife();
}
#endif

/* Initialisierung bis zur Zuendfreigabe. Im Host-Test ruft die Simulation
   dieselbe Funktion auf, damit keine zweite Startsequenz gepflegt werden muss. */
void SystemStart (void)
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
  KANTE_GUELTIG = 0;
  LETZTE_KANTE = 0;
  PERIODE_VOR = 0;
  TIMER_WERT = 0;
  DREHZAHL = 0;
  SOLLWINKEL = 0;
  FEHLTERMIN = 0;
  OT_NEU = 0;
  OT_VERLOREN = 0;
  OT_ZEIT = 0;
  LADUNG_AKTIV = 0;
  FUNKE_ARMIERT = 0;
  LADUNG_ARMIERT = 0;
  FUNKENZEITPUNKT = 0;
  LADUNGSBEGINN = 0;
  PLAN_FUNKE = 0;
  PLAN_VORAUS = 0;
  LADUNG_START_IST = 0;
  timer1_hoch = 0;
  EMPFANGSZAEHLER = 0;
  RAHMEN_AKTIV = 0;
  RAHMEN_ZEIT = 0;
  EE_PENDING = 0;
  EE_WARTE_GUELTIG = 0;
  QUITTUNG_AKTIV = 0;
  COIL = 0;
  DREHZ = 0;
  LED_2 = 0;

  T1CON = 0;                        // Timer waehrend der Initialisierung aus
  TMR1H = 0;                        // definierter Startwert
  TMR1L = 0;
  IOCBF = 0;
  PIR1 = 0;
  PIR2 = 0;
  T1GCON = 0;                       // Timer1 Gate aus
  T1CON = 0b00110001;               // FOSC/4, Prescaler 1:8, ein -> 4 us/Tick
  CCP1CON = 0b00001010;             // Compare mit Software-Interrupt
  CCP2CON = 0b00001010;
#ifdef HOST_TEST
  host_register_sync();
#endif
  IOCBNbits.IOCBN4 = 1;
  PIE1bits.TMR1IE = 1;
  PIE1bits.CCP1IE = 0;              // Compares erst mit gueltigem Termin frei
  PIE2bits.CCP2IE = 0;
  PIE1bits.RCIE = 0;
  INTCONbits.PEIE = 1;              // Compare-Interrupts brauchen PEIE
  INTCONbits.IOCIE = 1;
  INTCONbits.GIE = 1;               // ab hier laeuft die Zuendung
}

/* Ein Durchlauf des Hauptprogramms. */
void HauptSchleife (void)
{
  u32 jetzt;

  jetzt = ZeitJetzt();

  /* Notabschaltung: die Spule darf auch bei ausgefallenem Compare oder
     haengendem Hauptprogramm nicht unbegrenzt Strom fuehren. */
  if (LADUNG_AKTIV &&
      (s32)(jetzt - LADUNG_START_IST) > (s32)(LADUNG_MAX + LADUNG_RESERVE))
  {
    u8 gie;
    gie = INTCONbits.GIE;
    do { INTCONbits.GIE = 0; } while (INTCONbits.GIE);
    EntwaffneFunke();
    Funke();                         // Abschalten erzeugt zwangslaeufig Funken
    if (gie) INTCONbits.GIE = 1;
    FEHLTERMIN++;
  }

  if (OT_NEU)
    PlaneEreignisse();

  /* Sensorausfall ueber echte Zeitmessung, nicht ueber Ueberlaufzaehlung. */
  if (KANTE_GUELTIG && (u32)(jetzt - LETZTE_KANTE) > SENSOR_TIMEOUT_TICKS)
  {
    u8 gie;
    gie = INTCONbits.GIE;
    do { INTCONbits.GIE = 0; } while (INTCONbits.GIE);
    EntwaffneLadung();               // keine neue Ladung mehr beginnen
    if (gie) INTCONbits.GIE = 1;
    KANTE_GUELTIG = 0;
    DREHZAHL = 0;
    TIMER_WERT = 0;
    PERIODE_VOR = 0;
    OT_1 = 0;
    STARTFUNKE = 0;
    /* Eine bereits laufende Ladung behaelt ihren armierten Abschalttermin
       und erzeugt dabei einen Funken. Die Notabschaltung oben begrenzt sie
       zusaetzlich. */
  }

  EmpfangsDienst();
  EEDienst();

  if (QUITTUNG_AKTIV && (s32)(jetzt - QUITTUNG_ENDE) >= 0)
  {
    LED_1 = 0;                       // nichtblockierende Quittung beenden
    QUITTUNG_AKTIV = 0;
  }

  if (DREHZAHL == 0)               // RGB aus bei Stillstand
  {
    ROT = 1;
    GRUEN = 1;
    BLAU = 1;
  }
  else if (DREHZAHL <= 500UL * (u32)WERT_1)
  {
    ROT = 1;
    GRUEN = 1;
    BLAU = 0;
  }
  else if (DREHZAHL <= 500UL * (u32)WERT_2)
  {
    ROT = 1;
    GRUEN = 0;
    BLAU = 1;
  }
  else if (DREHZAHL <= 500UL * (u32)WERT_3)
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

/**********************************************************************************
         Funktionen, Unterprogramme
***********************************************************************************/
/* Termine der naechsten Umdrehung eintragen.
   funke_abs = geplanter Funke, lade_abs = funke_abs - ZEIT.
   Rueckgabe 0 = Zyklus ausgelassen (Termin nicht mehr einhaltbar). */
static u8 PlaneTermine (u32 funke_abs, u32 lade_abs)
{
  u32 jetzt;
  s32 d_funke, d_lade;
  u8  gie;

  jetzt = ZeitJetzt();
  d_funke = (s32)(funke_abs - jetzt);
  d_lade  = (s32)(lade_abs  - jetzt);

  if (d_funke <= (s32)MIN_VORLAUF)
    return 0;                          // Funkentermin verstrichen: auslassen
  if ((u32)d_funke > PERIODE_MAX)
    return 0;                          // unplausibel weit weg

  if (d_lade >= (s32)MIN_VORLAUF)
  {
    /* Regelfall: Ladebeginn liegt in der Zukunft. Der Abschalttermin wird
       von der CCP2-Routine unmittelbar vor dem Einschalten armiert. */
    gie = INTCONbits.GIE;
    do { INTCONbits.GIE = 0; } while (INTCONbits.GIE);
    PLAN_FUNKE = funke_abs;            // gemeinsamer Schnappschuss
    ArmiereLadung(lade_abs);
    if (gie) INTCONbits.GIE = 1;
    return 1;
  }

  /* Ladebeginn liegt bereits zurueck. Sofort laden ist nur zulaessig, wenn
     mindestens 75 % der Ladezeit uebrig bleiben, sonst wird der Zyklus
     bewusst ausgelassen (definierte Fehlerreaktion statt Schwachfunke). */
  if (LADUNG_AKTIV)
    return 0;                          // laufende Ladung nicht stoeren
  if ((u32)d_funke + (u32)(ZEIT >> 2) < (u32)ZEIT)
    return 0;
  if ((u32)d_funke > LADUNG_MAX)
    return 0;

  gie = INTCONbits.GIE;
  do { INTCONbits.GIE = 0; } while (INTCONbits.GIE);
  PLAN_FUNKE = funke_abs;
  EntwaffneLadung();
  ArmiereFunke(funke_abs);             // erst Abschalttermin, dann Strom
  COIL = 1;
  DREHZ = 1;
  LED_2 = 1;
  LADUNG_AKTIV = 1;
  LADUNG_START_IST = jetzt;
  if (gie) INTCONbits.GIE = 1;
  return 1;
}

/* Begrenzte Nachkorrektur eines bereits laufenden Ladevorgangs, wenn die
   tatsaechliche OT-Flanke von der Vorhersage abweicht. Nur fuer Funken
   NACH OT (Startphase). Die Ladezeit darf dabei nicht ueber LADUNG_MAX
   wachsen, der Termin muss in der Zukunft bleiben und die Verschiebung
   ist auf KORREKTUR_MAX begrenzt. */
static void KorrigiereFunke (u32 neuer_termin, u32 jetzt)
{
  s32 abweichung;

  if (!LADUNG_AKTIV || !FUNKE_ARMIERT)
    return;
  abweichung = (s32)(neuer_termin - FUNKENZEITPUNKT);
  if (abweichung > (s32)KORREKTUR_MAX || abweichung < -(s32)KORREKTUR_MAX)
    return;
  if ((s32)(neuer_termin - jetzt) <= (s32)MIN_VORLAUF)
    return;
  if ((u32)(neuer_termin - LADUNG_START_IST) > LADUNG_MAX)
    return;
  ArmiereFunke(neuer_termin);
}

void PlaneEreignisse (void)
{
  u32 kante;
  u32 jetzt;
  u32 periode;
  u32 vorhersage;
  s32 trend;
  u32 grenze;
  u32 ziel;
  u32 funke_abs;
  u8  gie;

  /* Schnappschuss der veroeffentlichten Flanke: 32-Bit-Wert und Flag
     zusammen, mit kurzer Interruptsperre. IOCIE allein wuerde nicht
     genuegen, weil die ISR auch aus anderer Quelle laufen kann. */
  gie = INTCONbits.GIE;
  do { INTCONbits.GIE = 0; } while (INTCONbits.GIE);
  kante = OT_ZEIT;
  OT_NEU = 0;
  if (gie) INTCONbits.GIE = 1;

  if (KANTE_GUELTIG)
  {
    periode = kante - LETZTE_KANTE;
    if (periode < PERIODE_MIN || periode > PERIODE_MAX)
      periode = 0;                     // Stoerimpuls: Referenz behalten
  }
  else
    periode = 0;                       // erste Kante legt nur die Referenz fest

  LETZTE_KANTE = kante;
  KANTE_GUELTIG = 1;

  if (periode != 0)
  {
    TIMER_WERT = periode;
    DREHZAHL = TICKS_PRO_MINUTE / periode;    // echte Messung, nicht gekappt
  }

  if (OT_1 < 5)
    OT_1++;                            // fuenf Messkanten zaehlen

  if (periode == 0)
    return;                            // ohne gueltige Periode nichts planen

  /* Vorhersage der naechsten Umdrehung: lineare Fortschreibung des Trends,
     begrenzt auf +/- 25 %. Ohne sie liegen bei Beschleunigung alle Termine
     systematisch hinter der tatsaechlichen Kante. Die Drehzahlanzeige und
     der Vorwinkel bleiben von der ECHTEN Messung abhaengig. */
  vorhersage = periode;
  if (PERIODE_VOR != 0)
  {
    trend  = (s32)periode - (s32)PERIODE_VOR;
    grenze = periode >> 2;
    if (trend >  (s32)grenze) trend =  (s32)grenze;
    if (trend < -(s32)grenze) trend = -(s32)grenze;
    vorhersage = (u32)((s32)periode + trend);
  }
  PERIODE_VOR = periode;

  if (OT_1 < 5)
    return;                            // noch in der Messphase

  jetzt = ZeitJetzt();

  /* Ein vorausgeplanter Termin (Funke der jetzt beginnenden Umdrehung, der
     bereits vor dieser Kante geladen werden sollte) darf nicht ueberschrieben
     werden - sonst entfaellt genau ein Funke. Ein Termin der gerade beendeten
     Umdrehung ist dagegen veraltet und wird ersetzt; das ist der Fall bei
     Beschleunigung, wenn die Kante frueher kommt als vorhergesagt. */
  if (PLAN_VORAUS && LADUNG_ARMIERT && !LADUNG_AKTIV &&
      (s32)(PLAN_FUNKE - jetzt) > 0 &&
      (s32)(PLAN_FUNKE - (kante + vorhersage)) < 0)
    return;

  if (STARTFUNKE < 5)
  {
    /* Startphase: genau fuenf Funken, START_WINKEL Grad NACH OT.
       Reicht der Winkel fuer die volle Ladezeit, wird der Funke dieser
       Umdrehung geplant. Andernfalls ist er nachtraeglich nicht mehr mit
       voller Ladezeit erzeugbar; dann wird die naechste Umdrehung
       vorhergesagt und die Spule beginnt vor deren OT zu laden. */
    ziel = (((u32)periode * (u32)START_WINKEL) + 180UL) / 360UL;
    KorrigiereFunke(kante + ziel, jetzt);     // laufende Ladung nachfuehren
    STARTFUNKE++;
    if (ziel >= (u32)ZEIT + PLAN_LATENZ)
    {
      funke_abs = kante + ziel;               // dieselbe Umdrehung
      PLAN_VORAUS = 0;
    }
    else
    {
      funke_abs = kante + vorhersage + ziel;  // vorhergesagte naechste Umdrehung
      PLAN_VORAUS = 1;
    }
  }
  else
  {
    if (DREHZAHL >= (u32)DREHZ_MAX)
    {
      /* Begrenzer: keine neue Ladung. Eine laufende Ladung behaelt ihren
         Abschalttermin und erzeugt dabei einen Funken. */
      gie = INTCONbits.GIE;
      do { INTCONbits.GIE = 0; } while (INTCONbits.GIE);
      EntwaffneLadung();
      if (gie) INTCONbits.GIE = 1;
      return;
    }

    if (DREHZAHL < (u32)DREHZ_W)
      SOLLWINKEL = ((DREHZAHL * (u32)VOR_W) + ((u32)DREHZ_W >> 1)) / (u32)DREHZ_W;
    else
      SOLLWINKEL = (u32)VOR_W;

    ziel = (((u32)vorhersage * SOLLWINKEL) + 180UL) / 360UL;   // Vorzuendung
    funke_abs = kante + vorhersage - ziel;
    PLAN_VORAUS = 0;
  }

  if ((u32)ZEIT + MIN_VORLAUF >= vorhersage)
    return;                            // Ladezeit passt nicht in eine Umdrehung

  if (!PlaneTermine(funke_abs, funke_abs - (u32)ZEIT))
    FEHLTERMIN++;
}


void EmpfangsDienst (void)
{
  u8  zeichen;
  u8  rahmenfehler;
  u32 jetzt;

  if (RCSTAbits.OERR)
  {
    UART_NEUSTART();                     // Ueberlauf: Empfang neu starten
    RAHMEN_AKTIV = 0;
    EMPFANGSZAEHLER = 0;
  }

  if (PIR1bits.RCIF)
  {
    rahmenfehler = FERR_LESEN();         // FERR gehoert zum Byte VOR dem Lesen
    zeichen = RCREG_LESEN();             // Lesen holt das naechste Byte nach
    jetzt = ZeitJetzt();
    if (rahmenfehler)
    {
      RAHMEN_AKTIV = 0;                  // Framing-Fehler: Byte verwerfen
      EMPFANGSZAEHLER = 0;
    }
    else if (!RAHMEN_AKTIV)
    {
      if (zeichen == (u8)'S')
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

  if (RAHMEN_AKTIV)
  {
    jetzt = ZeitJetzt();
    if ((u32)(jetzt - RAHMEN_ZEIT) > RX_TIMEOUT_TICKS)   // umlaufsicher
    {
      RAHMEN_AKTIV = 0;                  // unvollstaendiger Rahmen
      EMPFANGSZAEHLER = 0;
    }
  }
}


void Empfang (void)
{
  u8 i;

  /* Erst die EMPFANGENEN Rohwerte uebernehmen, dann pruefen und erst
     danach ablegen: sonst wuerde die alte RAM-Belegung geprueft und
     ungeprueft ins EEPROM geschrieben. Eine laufende Ladung wird nicht
     angetastet; ZEIT wirkt erst auf die naechste Planung. */
  START_WINKEL = BYTE2;
  VOR_W = BYTE3;
  LADEZEIT = BYTE4;
  DREHZ_W = (u16)(((u16)BYTE5 << 8) | (u16)BYTE6);
  DREHZ_MAX = (u16)(((u16)BYTE7 << 8) | (u16)BYTE8);
  WERT_1 = BYTE9;
  WERT_2 = BYTE10;
  WERT_3 = BYTE11;
  PruefeParameter();
  LadezeitSetzen();

  /* Geprueftes Ergebnis als geschlossener Satz ablegen (Layout unveraendert,
     DREHZ_W/DREHZ_MAX big-endian). */
  EE_WARTE[0] = START_WINKEL;
  EE_WARTE[1] = (u8)VOR_W;
  EE_WARTE[2] = LADEZEIT;
  EE_WARTE[3] = (u8)(DREHZ_W >> 8);
  EE_WARTE[4] = (u8)(DREHZ_W & 0x00FFu);
  EE_WARTE[5] = (u8)(DREHZ_MAX >> 8);
  EE_WARTE[6] = (u8)(DREHZ_MAX & 0x00FFu);
  EE_WARTE[7] = WERT_1;
  EE_WARTE[8] = WERT_2;
  EE_WARTE[9] = WERT_3;

  if (EE_PENDING)
    EE_WARTE_GUELTIG = 1;                // juengster Rahmen kommt nach dem Bus
  else
  {
    for (i = 0; i < 10; i++)
      EE_SPEICHER[i] = EE_WARTE[i];
    EE_WARTE_GUELTIG = 0;
    EE_PENDING = 1;
    EE_INDEX = 0;
    EEDATA = EE_SPEICHER[0];
    EEADRL = 1;
    EESchreibe();
  }

  LED_1 = 1;                             // Quittung ohne Blockade
  QUITTUNG_AKTIV = 1;
  QUITTUNG_ENDE = ZeitJetzt() + QUITTUNG_TICKS;
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
  if (LADEZEIT == 1) ZEIT = 250;         // 1,0 ms
  if (LADEZEIT == 2) ZEIT = 375;         // 1,5 ms
  if (LADEZEIT == 3) ZEIT = 500;         // 2,0 ms
  if (LADEZEIT == 4) ZEIT = 625;         // 2,5 ms
  if (LADEZEIT == 5) ZEIT = 750;         // 3,0 ms
}


void EEDienst (void)
{
  u8 i;

  if (EE_PENDING && !EECON1bits.WR)
  {
    if (EE_INDEX < 9)
    {
      EE_INDEX++;
      EEDATA = EE_SPEICHER[EE_INDEX];
      EEADRL = (u8)(EE_INDEX + 1);
      EESchreibe();
    }
    else if (EE_WARTE_GUELTIG)
    {
      /* Juengsten vollstaendigen Rahmen anschliessen; niemals einen
         gemischten Parametersatz schreiben. */
      for (i = 0; i < 10; i++)
        EE_SPEICHER[i] = EE_WARTE[i];
      EE_WARTE_GUELTIG = 0;
      EE_INDEX = 0;
      EEDATA = EE_SPEICHER[0];
      EEADRL = 1;
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
  do { INTCONbits.GIE = 0; } while (INTCONbits.GIE);   // nur die Freigabesequenz
  EECON2 = 0x55;
  EECON2 = 0xAA;
  EECON1bits.WR = 1;
  if (GIE_GEMERKT)
    INTCONbits.GIE = 1;
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
  DREHZ_W = (u16)EEDATA << 8;
  EEADRL = 5;
  EELese();
  DREHZ_W = (u16)(DREHZ_W | (u16)EEDATA);
  EEADRL = 6;
  EELese();
  DREHZ_MAX = (u16)EEDATA << 8;
  EEADRL = 7;
  EELese();
  DREHZ_MAX = (u16)(DREHZ_MAX | (u16)EEDATA);
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
