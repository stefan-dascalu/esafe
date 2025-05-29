#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <Keypad.h>
#include <avr/io.h>

constexpr uint8_t PIN_BUZZER = 11;
constexpr uint8_t PIN_LED_GREEN = 12;
constexpr uint8_t PIN_LED_RED = 13;
constexpr uint8_t PIN_SERVO = 10;

constexpr uint8_t SERVO_LOCKED = 110;
constexpr uint8_t SERVO_UNLOCKED = 20;

const char MASTER_PIN[] = "0123";
constexpr uint8_t MAX_PIN_LEN = sizeof(MASTER_PIN) - 1;
constexpr uint8_t MAX_WRONG = 3;

// store alarm duration in ms
const uint32_t ALARM_DUR = 5000UL;

constexpr uint8_t WINDOW_START = 9;
constexpr uint8_t WINDOW_END = 23;

// declare RTC object (Lab 6)
RTC_DS1307 rtc;

// declare LCD object at I²C addr 0×27 (Lab 6)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// declare servo object (Lab 3)
Servo latchServo;

// declare keypad rows and columns
const byte ROWS = 4;
const byte COLS = 4;

// create key map
char keys[ROWS][COLS] = { {'D','C','B','A'},
                          {'#','9','6','3'},
                          {'0','8','5','2'},
                          {'*','7','4','1'} };

// assign MCU pins to rows
byte rowPins[ROWS] = { 2, 3, 4, 5 };
// assign MCU pins to columns
byte colPins[COLS] = { 6, 7, 8, 9 };

// create keypad helper
Keypad keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
String entered;
uint8_t wrongCount = 0;
DateTime lastUnlock((uint32_t)0);

// store last LCD refresh moment
uint32_t lastDisplayMs = 0;

// flag that becomes true after first unlock
bool hasUnlocked = false;

// store moment when servo must relock
uint32_t lockRestoreMs = 0;

/* ADC support (Lab 4) */

// store last supply-voltage measurement moment
uint32_t lastVccMs = 0;

// store most recent Vcc reading in mV
uint16_t vcc_mV = 0;

/* ADC initialisation (Lab 4) */
void adcInit()
{
  // select AVcc as reference and internal band-gap (channel 14)
  ADMUX = _BV(REFS0) | 0b11110;

  // enable ADC and set prescaler to ÷16
  ADCSRA = _BV(ADEN) | _BV(ADPS2);
}

/* convert band-gap reading to millivolts (Lab 4) */
uint16_t readVccmV()
{
  // start a single conversion
  ADCSRA |= _BV(ADSC);

  // wait until ADSC clears
  while (ADCSRA & _BV(ADSC));

  // read 10-bit result
  uint16_t adc = ADC;

  // compute Vcc using Vbg ≈ 1.126 V
  return (uint32_t)1126 * 1024 / adc;
}

/* helper: overwrite one LCD row with spaces */
void clearLine(uint8_t row)
{
  // move cursor to beginning of row
  lcd.setCursor(0, row);

  // print 16 spaces
  for (uint8_t i = 0; i < 16; ++i) 
    lcd.print(' ');
}

/* helper: check if time is inside allowed window */
bool inWindow(const DateTime& t)
{
  // extract hour from timestamp
  uint8_t h = t.hour();

  // return true if inside bounds
  return h >= WINDOW_START && h <= WINDOW_END;
}


/* redraw complete user interface */
void updateDisplay()
{
  // request current time from RTC
  DateTime now = rtc.now();

  // allocate buffer for formatted strings
  char buf[17];

  // build "HH:MM:SS"
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
           now.hour(), now.minute(), now.second());

  // clear first row
  clearLine(0);

  // place cursor at row 0 col 0
  lcd.setCursor(0, 0);

  // print time string
  lcd.print(buf);

  // clear second row
  clearLine(1);

  // check if user is typing
  if (entered.length() > 0)
  {
    // check if safe hasn’t been unlocked yet
    if (!hasUnlocked)
    {
      // print “PIN:” label
      lcd.setCursor(0, 1);
      lcd.print("PIN:");

      // move after label
      lcd.setCursor(4, 1);

      // print one asterisk per digit typed
      for (uint8_t i = 0; i < entered.length(); ++i) 
        lcd.print('*');
    }
    else
    {
      // build “Last HH:MM” string
      snprintf(buf, sizeof(buf), "Last %02u:%02u",
               lastUnlock.hour(), lastUnlock.minute());

      // print last-unlock time
      lcd.setCursor(0, 1);
      lcd.print(buf);

      // show asterisks after clock on first row
      lcd.setCursor(8, 0);
      lcd.print(' ');
      for (uint8_t i = 0; i < entered.length(); ++i)
        lcd.print('*');
    }

    // stop further processing
    return;
  }

  // if not typing and safe has been unlocked before
  if (hasUnlocked)
  {
    // build message again
    snprintf(buf, sizeof(buf), "Last %02u:%02u",
             lastUnlock.hour(), lastUnlock.minute());

    // print on second row
    lcd.setCursor(0, 1);
    lcd.print(buf);
  }
  else
  {
    // prompt for PIN the very first time
    lcd.setCursor(0, 1);
    lcd.print("PIN:");
  }
}

/* empty buffer and refresh display */
void resetEntry()
{
  // clear string object
  entered = "";

  // redraw UI
  updateDisplay();
}

/* play alarm (LED + buzzer) */
void triggerAlarm()
{
  clearLine(1);
  lcd.setCursor(0, 1);
  lcd.print("ALARM!");

  // record start time
  uint32_t start = millis();

  // loop for ALARM_DUR ms
  while (millis() - start < ALARM_DUR)
  {
    digitalWrite(PIN_LED_RED, HIGH);

    tone(PIN_BUZZER, 4000);

    delay(200);

    digitalWrite(PIN_LED_RED, LOW);

    noTone(PIN_BUZZER);

    delay(200);
  }

  // reset wrong-attempt counter
  wrongCount = 0;

  resetEntry();
}

/* unlock the door and schedule auto-relock */
void unlockDoor(const DateTime& now)
{
  clearLine(1);

  lcd.setCursor(0, 1);
  lcd.print("UNLOCKED");

  latchServo.write(SERVO_UNLOCKED);

  digitalWrite(PIN_LED_GREEN, HIGH);

  tone(PIN_BUZZER, 3000);

  delay(1000);

  noTone(PIN_BUZZER);

  digitalWrite(PIN_LED_GREEN, LOW);

  lastUnlock = now;
  hasUnlocked = true;

  lockRestoreMs = millis() + 5000;

  resetEntry();
}

/* feedback when unlocking outside hours */
void outsideHours()
{
  clearLine(1);
  lcd.setCursor(0, 1);
  lcd.print("OUTSIDE HOURS");

  digitalWrite(PIN_LED_RED, HIGH);

  tone(PIN_BUZZER, 2500);

  delay(1000);

  noTone(PIN_BUZZER);

  digitalWrite(PIN_LED_RED, LOW);

  resetEntry();
}

/* short beep for wrong PIN */
void wrongPin()
{
  tone(PIN_BUZZER, 2000);

  delay(300);

  noTone(PIN_BUZZER);

  resetEntry();
}

/* process one keypad character */
void handleKey(char k)
{
  // if C was pressed, clear buffer
  if (k == 'C')
  {
    resetEntry();
    return;
  }

  // if D was pressed, evaluate PIN
  if (k == 'D')
  {
    // if buffer matches master pin
    if (entered == MASTER_PIN)
    {
      // fetch current time
      DateTime now = rtc.now();

      // check time window
      if (inWindow(now))
        unlockDoor(now);
      else
        outsideHours();
    }
    else
    {
      ++wrongCount;

      // trigger alarm on third strike
      if (wrongCount >= MAX_WRONG)
        triggerAlarm();
      else
        wrongPin();
    }

    // stop processing for D
    return;
  }

  // ignore non-digit keys
  if (!isDigit(k)) 
    return;

  // append digit if buffer not full
  if (entered.length() < MAX_PIN_LEN)
  {
    entered += k;
    updateDisplay();
  }
}

void setup()
{
  // set pins as output
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  // attach servo to its pin
  latchServo.attach(PIN_SERVO);
  // start with latch locked
  latchServo.write(SERVO_LOCKED);

  // open serial port at 9600 baud
  Serial.begin(9600);

  // start I²C bus
  Wire.begin();

  lcd.init();

  // switch LCD backlight on
  lcd.backlight();

  lcd.print("eSafe starting");

  delay(2000);

  lcd.clear();

  // start RTC; halt if not found
  if (!rtc.begin())
  {
    lcd.print("RTC ERROR");
    while (true);
  }

  // set compile-time if RTC battery flat
  if (!rtc.isrunning())
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // enable ADC subsystem
  adcInit();

  updateDisplay();
}

void loop()
{
  // snapshot current milliseconds
  uint32_t nowMs = millis();

  // auto-relock check
  if (lockRestoreMs && nowMs >= lockRestoreMs)
  {
    latchServo.write(SERVO_LOCKED);
    lockRestoreMs = 0;
  }

  // periodic LCD refresh
  if (nowMs - lastDisplayMs >= 1000)
  {
    lastDisplayMs = nowMs;
    updateDisplay();
  }

  // periodic Vcc measurement
  if (nowMs - lastVccMs >= 5000)
  {
    lastVccMs = nowMs;
    vcc_mV = readVccmV();

    Serial.print("VCC = ");
    Serial.print(vcc_mV);
    Serial.println(" mV");

    if (vcc_mV < 4600)
      digitalWrite(PIN_LED_RED, !digitalRead(PIN_LED_RED));
    else
      digitalWrite(PIN_LED_RED, LOW);
  }

  // read keypad once per pass
  char k = keypad.getKey();

  // handle key if present
  if (k) 
    handleKey(k);
}
