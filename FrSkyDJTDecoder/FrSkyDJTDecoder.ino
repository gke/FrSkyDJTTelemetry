
// FrSky DJT "D8" Tx Telemetry Module
// by ceptimus on 2015-09-10

// cleaned up by gke for UAVX and OLED display 2020

// CANNOT KEEP UP #define NO_INVERTER // uncomment for direct connection of DJT using ~100K resistor

#include "SSD1X06.h"

#define USE_UAVX_DJT

SSD1X06 oled;

#define MIN_RSSI 45
#define USE_COMPUTED_MAH

#define ScrollPin 2
#define BeeperPin 3
#define ZeroPin 4

typedef union {
  int16_t i16;
  uint8_t u8[2];
} i16u8u;

enum FlightStates {
  Starting,
  Warmup,
  Landing,
  Landed,
  Shutdown,
  InFlight,
  IREmulateUNUSED,
  Preflight,
  Ready,
  ThrottleOpenCheck,
  ErectingGyros,
  MonitorInstruments,
  InitialisingGPS,
  UnknownFlightState
};

char  modeNames[UnknownFlightState + 1][5] = //
{ "STRT", "WARM", "LNDG", "DOWN", "SHUT", "FLY ", "IR  ",//
  "PRE ", "RDY ", "THR ", "GYRO", "MON ", "GPS ", "    "
};

enum NavStates {
  HoldingStation,
  ReturningHome,
  AtHome,
  Descending,
  Touchdown,
  Transiting,
  Loitering,
  OrbitingPOI,
  Perching,
  Takeoff,
  PIC,
  AcquiringAltitude,
  UsingThermal,
  UsingRidge,
  UsingWave,
  BoostClimb,
  AltitudeLimiting,
  JustGliding,
  RateControl, // not actually a nav state
  PassThruControl, // not actually a nav state
  HorizonControl,
  WPAltFail,
  WPProximityFail,
  NavStateUndefined
};

char navNames[NavStateUndefined + 1][5] = //
{ "HOLD", "RTH ", "HOME", "DESC", "TCHD", "TRAN", "LOIT", //
  "ORBT", " PRCH", "TOFF", "PIC ", "ACQA", "THRM", "RDG ", //
  "WAVE", "BST ", "LIMA", "GLID", "ALTF", "PRXF", "    "
};

enum {
  // Data IDs  (BP = before decimal point; AP = after decimal point)

  ID_D4R = 0xfe,
  ID_USER_DATA = 0xfd,

  ID_GPS_ALT_BP = 0x01,
  ID_GPS_ALT_AP = 0x09,
  ID_TEMP1 = 0x02,
  ID_RPM = 0x03,
  ID_FUEL = 0x04,
  ID_TEMP2 = 0x05,
  ID_VOLTS = 0x06,

  ID_WHERE_DIST = 0x07, // used by LUA - metres the aircraft is way

  ID_PITCH = 0x08,

  ID_ALT_BP = 0x10,
  ID_ALT_AP = 0x21,
  ID_GPS_SPEED_BP = 0x11,
  ID_GPS_SPEED_AP = 0x19,
  ID_GPS_LONG_BP = 0x12,
  ID_GPS_LONG_AP = 0x1A,
  ID_E_W = 0x22,
  ID_GPS_LAT_BP = 0x13,
  ID_GPS_LAT_AP = 0x1B,

  ID_COURSE_BP = 0x14,
  ID_COURSE_AP = 0x1C,
  ID_DATE_MONTH = 0x15,
  ID_YEAR = 0x16,
  ID_HOUR_MINUTE = 0x17,
  ID_SECOND = 0x18,

  ID_ROLL = 0x20,

  ID_N_S = 0x23,
  ID_ACC_X = 0x24,
  ID_ACC_Y = 0x25,
  ID_ACC_Z = 0x26,
  ID_CURRENT = 0x28,

  //ID_WHERE_DIST = 0x29,
  ID_WHERE_BEAR = 0x2a, // bearing (deg) to aircraft
  ID_WHERE_ELEV = 0x2b, // elevation (deg) of the aircraft above the horizon
  ID_WHERE_HINT = 0x2c, // which to turn to come home intended for voice guidance

  ID_COMPASS = 0x2d, // deg

  ID_BEEPER = 0x2E,

  ID_VERT_SPEED = 0x30,

  ID_MAH = 0x36, // mAH battery consumption

  ID_VFAS = 0x39,
  ID_VOLTS_BP = 0x3A,
  ID_VOLTS_AP = 0x3B,

  ID_FRSKY_LAST = 0x3C
                  //opentx vario
};

#define MAX_SCROLL 2
enum {WaitRxSentinel, WaitRxID, WaitRxBody};

bool BeeperOn = false;
uint32_t BeeperTimeout = 0;

uint8_t Scroll = 0;
uint8_t rssi;
bool rssiseen = false;
uint8_t a1;
uint8_t a2;

int16_t IntervalmS;
int32_t LastFuelUpdatemS = 0;
int16_t mAHUsed = 0;
int16_t Current;

uint8_t NoOfSats, HDOP;

int16_t OriginAltitude = 0;
int16_t Altitude = 0;

uint8_t FrSkyPacketID;
uint8_t FrSkyUserchLow, ch;
uint8_t chPacket[4];

bool GPSValid, OriginValid, Armed;

uint32_t  TelemetryTimoutmS;

int16_t computemAHUsed(int16_t Current) {
  if (digitalRead(ZeroPin) == LOW)
    mAHUsed = 0;

  if (LastFuelUpdatemS == 0)
    LastFuelUpdatemS = millis();
  IntervalmS = millis() - LastFuelUpdatemS;
  LastFuelUpdatemS = millis();
  mAHUsed += (float)Current * 0.1 * IntervalmS * (1.0 / 3600.0);

  return mAHUsed;
}

inline int16_t make16(uint8_t h, uint8_t l) {
  i16u8u u;

  u.u8[0] = l;
  u.u8[1] = h;

  return u.i16;
}

void updateDisplay(uint8_t Scroll, uint8_t ch) {
  uint8_t high, low;
  int16_t i, Temp, Temp1, Temp2;
  int32_t ddd, mmm;

  switch (Scroll) {
    case 0:

      switch ( FrSkyPacketID ) {
        case ID_COURSE_BP:
        case ID_ALT_BP:
        case ID_GPS_ALT_BP:
        case ID_GPS_SPEED_BP:
        case ID_VOLTS_BP:
        case ID_GPS_LAT_BP:
        case ID_GPS_LONG_BP:
          chPacket[0] = FrSkyUserchLow;
          chPacket[1] = ch;
          break;

        case ID_GPS_LAT_AP:
        case ID_GPS_LONG_AP:
          chPacket[2] = FrSkyUserchLow;
          chPacket[3] = ch;
          break;

        //________________________________


        case ID_GPS_ALT_AP:
          oled.displayString6x8(6, 0, "    ", false);
          oled.displayReal32(6, 0, ((int16_t)chPacket[1] << 8) | chPacket[0], 0, 'm');
          break;
        case ID_COURSE_AP:
          oled.displayString6x8(6, 8, "    ", false);
          oled.displayReal32(6, 8, ((int16_t)chPacket[1] << 8) | chPacket[0], 0, 'd');
          break;
        case ID_ALT_AP:
          oled.displayString6x8(0, 0, "    ", false);
          //writeOled(0, 0, chPacket[0], chPacket[1], FrSkyUserchLow, ch, 10, 1);
          Altitude = ((int16_t)chPacket[1] << 8) | chPacket[0];
          if (digitalRead(ZeroPin) == LOW)
            OriginAltitude = Altitude;
          oled.displayReal32(0, 0, Altitude - OriginAltitude, 0, 'm');
          break;
        case ID_GPS_SPEED_AP:
          oled.displayString6x8(6, 15, "    ", false);
          writeOled(6, 15, chPacket[0], chPacket[1], FrSkyUserchLow, ch, 10, 1);
          break;
        case ID_N_S:
          i = make16(chPacket[1], chPacket[0]); // degrees * 100 + minutes
          ddd = i / 100;
          mmm = (((int32_t)i % 100) * 10000L + make16(chPacket[3], chPacket[3])) / 60;
          mmm += ddd * 10000;
          oled.displayReal32(7, 0, mmm, 4, ' ');
          oled.displayChar6x8(7, 7, FrSkyUserchLow);
          break;
        case ID_E_W:
          i = make16(chPacket[1], chPacket[0]); // degrees * 100 + minutes
          ddd = i / 100;
          mmm = (((int32_t)i % 100) * 10000L + make16(chPacket[3], chPacket[3])) / 60;
          mmm += ddd * 10000;
          oled.displayReal32(7, 11, mmm, 4, ' ');
          oled.displayChar6x8(7, 19, FrSkyUserchLow);
          break;

        case ID_VOLTS_AP:
          break;

        // single word data
        case ID_BEEPER:
          BeeperOn = (make16(ch, FrSkyUserchLow) & 1) != 0;
          if (BeeperOn) {
            oled.displayChar6x8(1, 14, '*');
            BeeperTimeout = millis() + 5000;
          } else
            oled.displayChar6x8(1, 14, ' ');
          break;
        case ID_TEMP1: // flight mode
#if defined(USE_UAVX_DJT)

          // if (F.HoldingAlt)         r |= 0b000010000000000;
          // if (F.RapidDescentHazard)   r |= 0b000100000000000;
          // if (F.LowBatt)        r |= 0b001000000000000;
          // if (Armed())        r |= 0b0100000000000000;

          Temp = (uint16_t)ch << 8 | FrSkyUserchLow;
          oled.displayString6x8(1, 15, "      ", false);
          if ((Temp & 0b001000000000000) != 0)
            oled.displayString6x8(1, 15, "VOLTS", false);
          else if ((Temp & 0b000100000000000) != 0)
            oled.displayString6x8(1, 15, "VRS", false);
          else
            if ( (Temp & 0x001f) == InFlight)
              oled.displayString6x8(1, 15, navNames[constrain((Temp >> 5) & 0x001f, 0, UnknownFlightState)], false);
            else
              oled.displayString6x8(1, 15,  modeNames[constrain(Temp & 0x001f, 0, NavStateUndefined)], false);

#else
          oled.displayString6x8(1, 15, "    ", false);
          Temp = (uint16_t)ch << 8 | FrSkyUserchLow;
          if (((Temp / 10) % 10) >= 4)
            oled.displayString6x8(1, 15, "MAN ", false);

          Temp2 = (Temp / 100) % 10;
          if (Temp2 == 2)
            oled.displayString6x8(1, 15, "AH  ", false);
          else if ((Temp2 == 4) || (Temp2 == 6))
            oled.displayString6x8(1, 15, "HOLD", false);

          Temp2 = (Temp / 1000) % 10;
          if (Temp2 == 1)
            oled.displayString6x8(1, 15, "RTH ", false);
          else if (Temp2 == 2)
            oled.displayString6x8(1, 15, "NAV ", false);
#endif
          break;
        case ID_TEMP2: // gps flags
          Temp = (uint16_t)ch << 8 | FrSkyUserchLow;
          GPSValid = ((Temp / 1000) % 10) >= 1;
          OriginValid = ((Temp / 1000) % 10) >= 3;
          Armed =  ((Temp / 1000) % 10) >= 7;
          if (GPSValid)// && OriginValid)
            oled.displayString6x8(5, 0, "gpsOK", false);
          else
            oled.displayString6x8(5, 0, "     ", false);
          oled.displayString6x8(5, 6, "hdop  ", false);
          oled.displayInt32(5, 11, (Temp / 1000) % 10);
          NoOfSats = Temp % 100;
          oled.displayString6x8(5, 13, "sats   ", false);
          oled.displayInt32(5, 18, NoOfSats);
          break;

        case ID_COMPASS :
          oled.displayString6x8(3, 15, "     ", false);
          oled.displayReal32(3, 15, make16(ch, FrSkyUserchLow), 0, 'd');
          break;
        case ID_VERT_SPEED:
          oled.displayString6x8(0, 8, "      ", false);
          oled.displayReal32(0, 8, make16(ch, FrSkyUserchLow), 1, 0);
          break;
        case ID_RPM:
          //Temp = make16(ch, FrSkyUserchLow);
          //oled.displayString6x8(0, 15, "     ", false);
          // oled.displayInt32(0, 15, Temp * 5);
          break;
        case ID_FUEL:
          // oled.displayString6x8(1, 15, "    ", false);
          // oled.displayReal32(1, 15, make16(ch, FrSkyUserchLow), 0, '%');
          break;

        case ID_VOLTS:
          break;
        case ID_VFAS:
          oled.displayString6x8(1, 0, "    ", false);
          oled.displayReal32(1, 0, make16(ch, FrSkyUserchLow), 1, 'v');
          // ? estimate #cells and do beeper alarm
          break;
        case ID_CURRENT:
          oled.displayString6x8(1, 8, "     ", false);
          oled.displayReal32(1, 8, make16(ch, FrSkyUserchLow), 1, 'a');
          break;
        case ID_MAH:
          //  ??
          break;
        case ID_PITCH:
          oled.displayString6x8(3, 0, "p    ", false);
          oled.displayInt32(3, 1, make16(ch, FrSkyUserchLow));
          break;
        case ID_ROLL:
          oled.displayString6x8(3, 8, "r    ", false);
          oled.displayInt32(3, 9, make16(ch, FrSkyUserchLow));
          break;
        case ID_DATE_MONTH:
          break;
        case ID_YEAR:
          break;
        case ID_HOUR_MINUTE:
          break;
        case ID_SECOND:
          break;
        case ID_WHERE_BEAR: // bearing (deg) to aircraft
          oled.displayString6x8(4, 0, "     ", false);
          oled.displayReal32(4, 0, make16(ch, FrSkyUserchLow), 0, 'd');
          break;
        case ID_WHERE_DIST:
          oled.displayString6x8(4, 6, "     ", false);
          oled.displayReal32(4, 6, make16(ch, FrSkyUserchLow), 0, 'm');
          break;
        case ID_WHERE_HINT: // which to turn to come home intended for voice guidance
          oled.displayString6x8(4, 11, "hint      ", false);
          oled.displayReal32(4, 16, make16(ch, FrSkyUserchLow), 0, 'd');
          break;
        case ID_WHERE_ELEV: // elevation (deg) of the aircraft above the horizon
          //oled.displayString6x8(4, 15, "     ", false);
          //oled.displayReal32(4, 15, make16(ch, FrSkyUserchLow), 0, 'd');
          break;

        default:
          break;
      }
      break;
    case 1: // bare bones Fixed Wing
      switch ( FrSkyPacketID ) {
        case ID_ALT_BP:
        case ID_VOLTS_BP:
          chPacket[0] = FrSkyUserchLow;
          chPacket[1] = ch;
          break;

        //________________________________


        case ID_ALT_AP:
          oled.displayString6x8(0, 0, "    ", false);
          //writeOled(0, 0, chPacket[0], chPacket[1], FrSkyUserchLow, ch, 10, 1);
          Altitude = ((int16_t)chPacket[1] << 8) | chPacket[0];
          if (digitalRead(ZeroPin) == LOW)
            OriginAltitude = Altitude;
          oled.displayReal32(0, 0, Altitude - OriginAltitude, 0, 'm');
          break;
        case ID_VOLTS_AP:
          break;

        case ID_VERT_SPEED:
          oled.displayString6x8(0, 8, "      ", false);
          oled.displayReal32(0, 8, make16(ch, FrSkyUserchLow), 1, 0);
          break;
        case ID_VFAS:
          oled.displayString6x8(1, 0, "    ", false);
          oled.displayReal32(1, 0, make16(ch, FrSkyUserchLow), 1, 'v');
          break;
        case ID_CURRENT:
          oled.displayString6x8(1, 8, "     ", false);
          Current = make16(ch, FrSkyUserchLow);
          oled.displayReal32(1, 8, Current, 1, 'a');
#if defined(USE_COMPUTED_MAH)
          oled.displayString6x8(1, 13, "mAH     ", false);
          oled.displayReal32(1, 17,  computemAHUsed(Current), 0, ' ');
#endif
          break;
        case ID_FUEL:
          // oled.displayString6x8(1, 15, "    ", false);
          // oled.displayReal32(1, 15, make16(ch, FrSkyUserchLow), 0, ' ');
          break;
        case ID_MAH:
#if !defined(USE_COMPUTED_MAH)
          oled.displayString6x8(1, 12, "mAH     ", false);
          oled.displayReal32(1, 16, make16(ch, FrSkyUserchLow), 0, '%');
#endif
          break;
        case ID_TEMP1: // flight mode
          oled.displayString6x8(7, 0, "T1     ", false);
          oled.displayReal32(7, 3, make16(ch, FrSkyUserchLow), 1, 'C');
          break;
        case ID_TEMP2: // gps flags
          oled.displayString6x8(7, 12, "T2     ", false);
          oled.displayReal32(7, 15, make16(ch, FrSkyUserchLow), 1, 'C');
          break;
        default:
          break;
      }
      break;
    default:
      oled.displayString6x8(0, 0, F("UNUSED 2"), 0);
      break;
  }
}

void writeOled(uint8_t row, uint8_t col, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t frac, uint8_t dp) {
  oled.displayString6x8(row, col, "         ", false);
  oled.displayReal32(row, col, (((int32_t)b << 8) + a) * frac + (((int16_t)d << 8) + c), dp, ' ');
}

void handlechByte(uint8_t ch) {

  static uint8_t FrSkyPacketRxState = WaitRxSentinel;
  static bool FrSkyUserchStuffing;
  static bool FrSkyUserchLowFlag; // flag for low byte of ch, which is the first of the following two

  switch (FrSkyPacketRxState) {
    case WaitRxSentinel: // idle
      if (ch == 0x5E) // telemetry hub frames begin with ^ (0x5E)
        FrSkyPacketRxState = 1; // expect telemetry hub chID next
      break;
    case WaitRxID: // expecting telemetry hub chID
      if (ch < ID_FRSKY_LAST) { // store chID (address)
        FrSkyUserchStuffing = false;
        FrSkyPacketID = ch;
        FrSkyPacketRxState = WaitRxBody; // expect two bytes of ch next
        FrSkyUserchLowFlag = true; // flag for low byte of ch, which is the first of the following two
      }
      else if (ch != 0x5E) // the header byte 0x5E may occur twice running as it is also used as an 'end of frame' so remain in mode 1. Otherwise chID was > 0x3B, so invalid
        FrSkyPacketRxState = WaitRxSentinel;

      break;
    case WaitRxBody: // expecting two bytes of ch
      if (FrSkyUserchStuffing) {
        FrSkyUserchStuffing = false;
        if ((ch != 0x3D) && (ch != 0x3E)) { // byte stuffing is only valid for (unstuffed) bytes 0x5D or or 0x5E
          FrSkyPacketRxState = WaitRxSentinel; // back to idle mode
          break;
        }
        else
          ch ^= 0x20; // unstuff byte
      }
      else if (ch == 0x5D) { // following byte is stuffed
        FrSkyUserchStuffing = true;
        break;
      } // switch

      if (FrSkyUserchLowFlag) { // expecting low byte of ch
        FrSkyUserchLow = ch; // remember low byte
        FrSkyUserchLowFlag = false; // expect high byte next
      }
      else {
        updateDisplay(Scroll, ch);
        FrSkyPacketRxState = WaitRxSentinel;
      } // else
      break;
    default: // should never happen
      FrSkyPacketRxState = WaitRxSentinel;
      break;
  }
}

void handlePacket(uint8_t *packet) {
  switch (packet[0]) {
    case 0xFD:
      if (packet[1] > 0 && packet[1] <= 6)
        for (int i = 0; i < packet[1]; i++)
          handlechByte(packet[3 + i]);

      break;
    case 0xFE:
      a1 = packet[1]; // A1:
      a2 = packet[2]; // A2:
      rssiseen = true;
      rssi = packet[3]; // main (Rx) link quality 100+ is full signal  40 is no signal
      // packet[4] secondary (Tx) link quality.

      oled.displayString6x8(0, 13, "rssi    ", false);
      oled.displayInt32(0, 18, rssi);
      break;
  }

}

void handleRxChar(uint16_t b) { // decode FrSky basic telemetry ch
  static uint8_t packetPosition = 0;
  static uint8_t packet[9];
  static bool byteStuffing = false;

  if (b == 0x7E) { // framing character
    if (packetPosition > 8)
      handlePacket(packet);
    packetPosition = 0;
  } else {
    if (b == 0x7D)
      byteStuffing = true;
    else {
      if (byteStuffing) {
        byteStuffing = false;
        if (b != 0x5E && b != 0x5D) {
          packetPosition = 0;
          return;
        }
        else
          b ^= 0x20;
      }
      if (packetPosition > 8)
        packetPosition = 0;
      else
        packet[packetPosition++] = b;
    }
  }
}

void initDisplay(void) {
  uint8_t c;

  delay(500);
  oled.start();
  delay(300);
  oled.fillDisplay(' ');
  delay(2000);

  oled.displayString6x8(0, 0, F("FRSKY DJT TELEMETRY"), 0);

  delay(2000); // start up message

  oled.displayString6x8(0, 0, F("                   "), 0);

}

void checkScroll() {
  enum {IsHigh, SeenLow, WaitHigh};
  static int32_t nextUpdatemS = 0;
  static uint8_t ScrollState = IsHigh;

  if (millis() > nextUpdatemS) {
    nextUpdatemS = millis() + 100;

    switch (ScrollState) {
      case IsHigh:
        if (digitalRead(ScrollPin) == LOW)
          ScrollState = SeenLow;
        break;
      case SeenLow:
        if (digitalRead(ScrollPin) == LOW) {
          Scroll++;
          if (Scroll >= MAX_SCROLL)
            Scroll = 0;
          ScrollState = WaitHigh;
        } else
          ScrollState = IsHigh;
        break;
      case WaitHigh:
        if (digitalRead(ScrollPin) == HIGH)
          ScrollState = IsHigh;
    }
  }

}

void setup(void) {

  pinMode(BeeperPin, OUTPUT);
  rssiseen = false;
  digitalWrite(BeeperPin, LOW);

  pinMode(ScrollPin, INPUT_PULLUP);
  pinMode(ZeroPin, INPUT_PULLUP);

  initDisplay();

  rssi = a1 = a2 = 0;

  Serial.begin(9600);

  TelemetryTimoutmS = millis() + 10000;

}

void loop(void) {

  uint8_t ch;

  if (Serial.available())
    handleRxChar(Serial.read());

  if (millis() > BeeperTimeout)
    BeeperOn = false;

  if (BeeperOn || ((rssi < MIN_RSSI) && rssiseen))
    digitalWrite(BeeperPin, HIGH);
  else
    digitalWrite(BeeperPin, LOW);

  if (digitalRead(ScrollPin) == LOW) {
    oled.fillDisplay(' ');
    if (++Scroll > MAX_SCROLL)
      Scroll = 0;
    BeeperOn = false;
    delay(1000);
  }

}
