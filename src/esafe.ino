#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <Keypad.h>

// define pins for buzzer, green LED, red LED, and servo
constexpr uint8_t PIN_BUZZER    = 11;
constexpr uint8_t PIN_LED_GREEN = 12;
constexpr uint8_t PIN_LED_RED   = 13;
constexpr uint8_t PIN_SERVO     = 10;

// define servo positions for locked and unlocked (110° and 20°)
constexpr uint8_t SERVO_LOCKED   = 110;
constexpr uint8_t SERVO_UNLOCKED =  20;

// security settings: master PIN, maximum PIN length, allowed wrong attempts
const char    MASTER_PIN[]    = "0123";
constexpr uint8_t MAX_PIN_LEN = sizeof(MASTER_PIN) - 1;
constexpr uint8_t MAX_WRONG   = 3;
const uint32_t ALARM_DUR      = 5000UL;  // duration of alarm in ms

// allowed time window for unlocking (9 to 23)
constexpr uint8_t WINDOW_START =  9;
constexpr uint8_t WINDOW_END   = 23;

// initialize RTC and LCD display objects
RTC_DS1307        rtc;
LiquidCrystal_I2C lcd(0x27,16,2);

// configure servo and 4x4 keypad matrix
Servo latchServo;
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'D','C','B','A'},
  {'#','9','6','3'},
  {'0','8','5','2'},
  {'*','7','4','1'}
};
byte rowPins[ROWS] = {2,3,4,5};
byte colPins[COLS] = {6,7,8,9};
Keypad keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// runtime variables: entered PIN, wrong attempt counter, last unlock time, last display update timestamp, unlock state
String   entered;
uint8_t  wrongCount     = 0;
DateTime lastUnlock((uint32_t)0);
uint32_t lastDisplayMs   = 0;
bool     hasUnlocked     = false;

// timestamp for automatic servo relock after unlock
uint32_t lockRestoreMs   = 0;

// helper functions

// clear an entire row on the LCD
void clearLine(uint8_t row) {
  lcd.setCursor(0,row);
  for (uint8_t i = 0; i < 16; ++i) lcd.print(' ');
}

// check if current time is within allowed window
bool inWindow(const DateTime& t) {
  uint8_t h = t.hour();
  return h >= WINDOW_START && h <= WINDOW_END;
}

// update the LCD display with live time and context messages
void updateDisplay() {
  DateTime now = rtc.now();
  char buf[17];

  // display current time in HH:MM:SS format on row 0
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", now.hour(), now.minute(), now.second());
  clearLine(0);
  lcd.setCursor(0,0);
  lcd.print(buf);

  clearLine(1);

  // if user is typing PIN digits
  if (entered.length() > 0) {
    if (!hasUnlocked) {
      // before first unlock: show PIN:**** on row 1
      lcd.setCursor(0,1);
      lcd.print("PIN:");
      lcd.setCursor(4,1);
      for (uint8_t i = 0; i < entered.length(); ++i) lcd.print('*');
    } else {
      // after unlocking: show "Last HH:MM" on row 1
      snprintf(buf, sizeof(buf), "Last %02u:%02u", lastUnlock.hour(), lastUnlock.minute());
      lcd.setCursor(0,1);
      lcd.print(buf);
      // and show asterisks for new PIN entry on row 0 after time
      uint8_t offset = 8;
      lcd.setCursor(offset,0);
      lcd.print(' ');
      for (uint8_t i = 0; i < entered.length(); ++i) lcd.print('*');
    }
    return;
  }

  // if no digits are entered
  if (hasUnlocked) {
    // after unlocking: show last unlock time on row 1
    snprintf(buf, sizeof(buf), "Last %02u:%02u", lastUnlock.hour(), lastUnlock.minute());
    lcd.setCursor(0,1);
    lcd.print(buf);
  } else {
    // before unlocking: prompt for PIN on row 1
    lcd.setCursor(0,1);
    lcd.print("PIN:");
  }
}

// reset PIN entry buffer and refresh display
void resetEntry() {
  entered = "";
  updateDisplay();
}

// trigger an alarm after too many wrong PIN attempts
void triggerAlarm() {
  clearLine(1);
  lcd.setCursor(0,1);
  lcd.print("ALARM!");
  uint32_t start = millis();
  while (millis() - start < ALARM_DUR) {
    digitalWrite(PIN_LED_RED, HIGH);
    tone(PIN_BUZZER, 4000);
    delay(200);
    digitalWrite(PIN_LED_RED, LOW);
    noTone(PIN_BUZZER);
    delay(200);
  }
  wrongCount = 0;
  resetEntry();
}

// perform unlock sequence: move servo, beep, display message, schedule relock
void unlockDoor(const DateTime& now) {
  clearLine(1);
  latchServo.write(SERVO_UNLOCKED);
  digitalWrite(PIN_LED_GREEN, HIGH);
  tone(PIN_BUZZER, 3000);
  lcd.setCursor(0,1);
  lcd.print("UNLOCKED");
  lastUnlock = now;
  hasUnlocked = true;
  delay(1000);
  noTone(PIN_BUZZER);
  digitalWrite(PIN_LED_GREEN, LOW);

  // schedule servo to relock after 5 seconds
  lockRestoreMs = millis() + 5000;
  resetEntry();
}

// show outside hours message when unlocking is attempted out of window
void outsideHours() {
  clearLine(1);
  lcd.setCursor(0,1);
  lcd.print("OUTSIDE HOURS");
  digitalWrite(PIN_LED_RED, HIGH);
  tone(PIN_BUZZER, 2500);
  delay(1000);
  noTone(PIN_BUZZER);
  digitalWrite(PIN_LED_RED, LOW);
  resetEntry();
}

// beep briefly for wrong PIN
void wrongPin() {
  tone(PIN_BUZZER, 2000);
  delay(300);
  noTone(PIN_BUZZER);
  resetEntry();
}

// handle keypad input: C to clear, D to submit, digits to append
void handleKey(char k) {
  if (k == 'C') {
    resetEntry();
    return;
  }
  if (k == 'D') {
    if (entered == MASTER_PIN) {
      DateTime now = rtc.now();
      if (inWindow(now)) 
          unlockDoor(now);
      else
          outsideHours();
    } else {
      if (++wrongCount >= MAX_WRONG) 
          triggerAlarm();
      else
          wrongPin();
    }
    return;
  }
  if (!isDigit(k))
    return;
  if (entered.length() < MAX_PIN_LEN) {
    entered += k;
    updateDisplay();
  }
}

// initial setup: configure pins, servo, I2C, LCD, and RTC
void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  latchServo.attach(PIN_SERVO);
  latchServo.write(SERVO_LOCKED);

  Wire.begin();
  lcd.init();
  lcd.backlight();

  // show startup message
  lcd.clear();
  lcd.print("eSafe starting");
  delay(2000);
  lcd.clear();

  if (!rtc.begin()) {
    lcd.clear();
    lcd.print("RTC ERROR");
    while (1);
  }
  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  entered = "";
  lastDisplayMs = millis();
  updateDisplay();
}

// main loop: check for relock time, update display, and read keypad
void loop() {
  uint32_t nowMs = millis();

  // relock servo if scheduled time has passed
  if (lockRestoreMs && nowMs >= lockRestoreMs) {
    latchServo.write(SERVO_LOCKED);
    lockRestoreMs = 0;
  }

  // update display every second without blocking
  if (nowMs - lastDisplayMs >= 1000) {
    lastDisplayMs = nowMs;
    updateDisplay();
  }

  // read keypad input
  char k = keypad.getKey();
  if (k)
    handleKey(k);
}
