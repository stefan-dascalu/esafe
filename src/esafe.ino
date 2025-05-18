#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <Keypad.h>

// define hardware pins
constexpr uint8_t pin_buzzer     = 1;
constexpr uint8_t pin_led_green  = 12;
constexpr uint8_t pin_led_red    = 13;
constexpr uint8_t pin_servo      = 10;

// define servo positions
constexpr uint8_t servo_locked   = 110;
constexpr uint8_t servo_unlocked =  50;

// security settings
const char    master_pin[]       = "0123";
const uint8_t max_pin_length     = sizeof(master_pin) - 1;
const uint8_t max_wrong         = 3;
const uint32_t alarm_duration   = 5000UL;

// operating hours (inclusive)
constexpr uint8_t window_start  = 9;
constexpr uint8_t window_end    = 17;

// rtc and display objects
RTC_DS1307        rtc;
LiquidCrystal_I2C lcd(0x27,16,2);
Servo latch_servo;

// keypad layout
const byte rows = 4, cols = 4;
char keys[rows][cols] = {
  {'D','C','B','A'},
  {'#','9','6','3'},
  {'0','8','5','2'},
  {'*','7','4','1'}
};
byte row_pins[rows] = {2,3,4,5};
byte col_pins[cols] = {6,7,8,9};
Keypad keypad(makeKeymap(keys), row_pins, col_pins, rows, cols);

// runtime variables
String   entered;
uint8_t  wrong_count   = 0;
DateTime last_unlock(0);
uint32_t last_update_ms = 0;

// check if current hour is within allowed window
bool in_window(const DateTime& t) {
  uint8_t h = t.hour();
  return h >= window_start && h <= window_end;
}

// clear entered pin on display
void reset_entry() {
  entered = "";
  lcd.setCursor(0,1);
  lcd.print("PIN:            ");
  lcd.setCursor(5,1);
}

// sound alarm for wrong attempts
void trigger_alarm() {
  lcd.setCursor(0,1);
  lcd.print("ALARM!         ");
  uint32_t start = millis();
  while (millis() - start < alarm_duration) {
    digitalWrite(pin_led_red, HIGH);
    tone(pin_buzzer, 4000, 200);
    delay(200);
    digitalWrite(pin_led_red, LOW);
    delay(200);
  }
  wrong_count = 0;
  reset_entry();
}

// move servo to locked position
void lock_door() {
  latch_servo.write(servo_locked);
  digitalWrite(pin_led_green, LOW);
  digitalWrite(pin_led_red, LOW);
  lcd.setCursor(0,1);
  lcd.print("LOCKED         ");
  tone(pin_buzzer, 1500, 100);
  delay(1000);
  reset_entry();
}

// move servo to unlocked position and log time
void unlock_door(const DateTime& now) {
  latch_servo.write(servo_unlocked);
  digitalWrite(pin_led_green, HIGH);
  digitalWrite(pin_led_red, LOW);
  tone(pin_buzzer, 3000, 200);
  lcd.setCursor(0,1);
  lcd.print("UNLOCKED       ");
  last_unlock = now;
  delay(2000);
  digitalWrite(pin_led_green, LOW);
  reset_entry();
}

// handle each key press from keypad
void handle_key(char k) {
  if (k == 'C') {
    reset_entry();
    return;
  }
  if (k == 'D') {
    if (entered == master_pin) {
      DateTime now = rtc.now();
      if (in_window(now)) unlock_door(now);
      else {
        lcd.setCursor(0,1);
        lcd.print("OUTSIDE HOURS  ");
      }
    } else {
      trigger_alarm();
    }
    return;
  }
  if (!isDigit(k)) return;
  if (entered.length() < max_pin_length) {
    entered += k;
    lcd.setCursor(5 + entered.length() - 1, 1);
    lcd.print('*');
  }
  if (entered.length() == max_pin_length) {
    if (entered == master_pin) {
      DateTime now = rtc.now();
      if (in_window(now)) unlock_door(now);
      else {
        lcd.setCursor(0,1);
        lcd.print("OUTSIDE HOURS  ");
      }
      wrong_count = 0;
    } else {
      wrong_count++;
      if (wrong_count >= max_wrong) trigger_alarm();
      else {
        lcd.setCursor(0,1);
        lcd.print("WRONG PIN      ");
        tone(pin_buzzer, 2000, 250);
        delay(500);
        reset_entry();
      }
    }
  }
}

// update time and last unlock info on display
void update_display() {
  DateTime now = rtc.now();
  char buf[17];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u %02u/%02u/%02u",
           now.hour(), now.minute(), now.second(),
           now.day(), now.month(), now.year() % 100);
  lcd.setCursor(0,0);
  lcd.print(buf);
  if (entered.length() == 0) {
    lcd.setCursor(0,1);
    if (last_unlock.unixtime() != 0) {
      snprintf(buf, sizeof(buf), "Last %02u:%02u %02u/%02u",
               last_unlock.hour(), last_unlock.minute(),
               last_unlock.day(), last_unlock.month());
      lcd.print(buf);
    } else {
      reset_entry();
    }
  }
}

void setup() {
  pinMode(pin_buzzer, OUTPUT);
  pinMode(pin_led_green, OUTPUT);
  pinMode(pin_led_red, OUTPUT);
  latch_servo.attach(pin_servo);
  latch_servo.write(servo_locked);

  Wire.begin();
  lcd.init();
  lcd.backlight();

  if (!rtc.begin()) {
    lcd.clear();
    lcd.print("rtc error");
    while (true);
  }
  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(__DATE__, __TIME__));
  }

  last_update_ms = millis();
  update_display();
}

void loop() {
  if (millis() - last_update_ms >= 1000) {
    last_update_ms = millis();
    update_display();
  }
  char k = keypad.getKey();
  if (k) handle_key(k);
}
