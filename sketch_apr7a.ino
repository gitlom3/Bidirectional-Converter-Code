#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================= PIN DEFINITIONS =================
#define BUCK_PWM_PIN      25
#define BOOST_PWM_PIN     27      // change if needed
#define BUCK_OFF_PIN      33      // turns buck switch OFF
#define BOOST_OFF_PIN     14      // change if needed
#define BATTERY_VOLT_PIN  34
#define CURRENT_PIN       32
#define STATUS_LED_PIN    26

// ================= PWM SETTINGS =================
#define PWM_FREQ    30000
#define PWM_RES     10
#define DUTY_MAX    1023

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= CALIBRATION =================
float voltageScale  = 11.0;   // voltage divider scale
float currentOffset = 2.5;
float currentScale  = 0.185;

// ================= BUCK PARAMETERS =================
#define BUCK_FIXED_DUTY     511   // about 50%
#define BUCK_CURRENT_LIMIT  1.3
#define RAMP_STEP_DELAY     15

// ================= BOOST PARAMETERS =================
#define BOOST_FIXED_DUTY    511   // about 50%
#define BOOST_CURRENT_LIMIT 1.3

// ================= SOC THRESHOLDS =================
// Approximate lead-acid thresholds using battery voltage
#define SOC70_HIGH_V   12.5   // switch to boost above this
#define SOC70_LOW_V    12.3   // return to buck below this

// ================= MODE STATE =================
enum Mode {
  MODE_BUCK,
  MODE_BOOST
};

Mode currentMode = MODE_BUCK;

// ================= HELPER: READ ADC AVERAGE =================
float readADC(int pin) {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(pin);
    delayMicroseconds(50);
  }
  return sum / 20.0;
}

// ================= HELPER: READ BATTERY VOLTAGE =================
float readBatteryVoltage() {
  float rawV = readADC(BATTERY_VOLT_PIN);
  float adcV = (rawV / 4095.0) * 3.3;
  return adcV * voltageScale;
}

// ================= HELPER: READ CURRENT =================
float readCurrent() {
  float rawI    = readADC(CURRENT_PIN);
  float adcI    = (rawI / 4095.0) * 3.3;
  float current = (adcI - currentOffset) / currentScale;
  return (current < 0) ? 0 : current;
}

// ================= HELPER: ESTIMATE SOC =================
float estimateSOC(float battV) {
  // Very simple lead-acid voltage to SOC estimate
  if (battV >= 12.7) return 100.0;
  if (battV >= 12.5) return 80.0;
  if (battV >= 12.4) return 70.0;
  if (battV >= 12.2) return 50.0;
  if (battV >= 12.0) return 25.0;
  return 10.0;
}

// ================= ALL PWM OFF =================
void allPWMOff() {
  ledcWrite(BUCK_PWM_PIN, 0);
  ledcWrite(BOOST_PWM_PIN, 0);
}

// ================= CONFIGURE BUCK MODE =================
void setBuckMode() {
  currentMode = MODE_BUCK;

  allPWMOff();

  // Buck active, boost disabled
  digitalWrite(BUCK_OFF_PIN, LOW);    // buck path enabled
  digitalWrite(BOOST_OFF_PIN, HIGH);  // boost path disabled

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode: BUCK      ");
  lcd.setCursor(0, 1);
  lcd.print("Charging...     ");

  Serial.println(">> Switched to BUCK mode");
  delay(1000);
}

// ================= CONFIGURE BOOST MODE =================
void setBoostMode() {
  currentMode = MODE_BOOST;

  allPWMOff();

  // Boost active, buck disabled
  digitalWrite(BUCK_OFF_PIN, HIGH);   // buck path disabled
  digitalWrite(BOOST_OFF_PIN, LOW);   // boost path enabled

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode: BOOST     ");
  lcd.setCursor(0, 1);
  lcd.print("Discharging...  ");

  Serial.println(">> Switched to BOOST mode");
  delay(1000);
}

// ================= BUCK SOFT START =================
void softStartBuck() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Buck Soft Start ");
  lcd.setCursor(0, 1);
  lcd.print("Please Wait...  ");
  Serial.println(">> Buck soft-start...");

  for (int d = 0; d <= BUCK_FIXED_DUTY; d++) {
    ledcWrite(BUCK_PWM_PIN, d);
    delay(RAMP_STEP_DELAY);

    float I = readCurrent();
    if (I > BUCK_CURRENT_LIMIT) {
      Serial.print(">> Buck ramp stopped at duty ");
      Serial.print(d);
      Serial.print(" | I = ");
      Serial.print(I, 2);
      Serial.println(" A");

      lcd.setCursor(0, 1);
      lcd.print("I Limit Reached ");
      delay(500);
      break;
    }
  }

  lcd.clear();
}

// ================= BOOST SOFT START =================
void softStartBoost() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Boost SoftStart ");
  lcd.setCursor(0, 1);
  lcd.print("Please Wait...  ");
  Serial.println(">> Boost soft-start...");

  for (int d = 0; d <= BOOST_FIXED_DUTY; d++) {
    ledcWrite(BOOST_PWM_PIN, d);
    delay(RAMP_STEP_DELAY);

    float I = readCurrent();
    if (I > BOOST_CURRENT_LIMIT) {
      Serial.print(">> Boost ramp stopped at duty ");
      Serial.print(d);
      Serial.print(" | I = ");
      Serial.print(I, 2);
      Serial.println(" A");

      lcd.setCursor(0, 1);
      lcd.print("I Limit Reached ");
      delay(500);
      break;
    }
  }

  lcd.clear();
}

// ================= CHECK AND UPDATE MODE =================
void updateModeFromSOC() {
  float battV = readBatteryVoltage();

  // Hysteresis
  if (currentMode == MODE_BUCK && battV > SOC70_HIGH_V) {
    setBoostMode();
    softStartBoost();
  }
  else if (currentMode == MODE_BOOST && battV < SOC70_LOW_V) {
    setBuckMode();
    softStartBuck();
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(500);

  // PWM attach
  ledcAttach(BUCK_PWM_PIN, PWM_FREQ, PWM_RES);
  ledcAttach(BOOST_PWM_PIN, PWM_FREQ, PWM_RES);

  allPWMOff();

  pinMode(BUCK_OFF_PIN, OUTPUT);
  pinMode(BOOST_OFF_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);

  digitalWrite(BUCK_OFF_PIN, LOW);
  digitalWrite(BOOST_OFF_PIN, HIGH);
  digitalWrite(STATUS_LED_PIN, HIGH);

  analogReadResolution(12);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  float battV = readBatteryVoltage();

  lcd.setCursor(0, 0);
  lcd.print("Battery Check   ");
  lcd.setCursor(0, 1);
  lcd.print("V:");
  lcd.print(battV, 2);
  lcd.print("V");
  delay(2000);

  Serial.println("================================");
  Serial.println(" Bidirectional Converter System ");
  Serial.println("================================");

  if (battV > SOC70_HIGH_V) {
    setBoostMode();
    softStartBoost();
  } else {
    setBuckMode();
    softStartBuck();
  }
}

// ================= MAIN LOOP =================
void loop() {
  updateModeFromSOC();

  float battV   = readBatteryVoltage();
  float current = readCurrent();
  float power   = battV * current;
  float soc     = estimateSOC(battV);

  // -------- OVERCURRENT GUARD --------
  if ((currentMode == MODE_BUCK && current > BUCK_CURRENT_LIMIT) ||
      (currentMode == MODE_BOOST && current > BOOST_CURRENT_LIMIT)) {

    allPWMOff();
    digitalWrite(STATUS_LED_PIN, LOW);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("!! OVERCURRENT  ");
    lcd.setCursor(0, 1);
    lcd.print("I:");
    lcd.print(current, 2);
    lcd.print("A");

    Serial.print("!! OVERCURRENT | I = ");
    Serial.print(current, 2);
    Serial.println(" A");

    delay(2000);
    digitalWrite(STATUS_LED_PIN, HIGH);

    if (currentMode == MODE_BUCK) {
      softStartBuck();
    } else {
      softStartBoost();
    }

    return;
  }

  // -------- APPLY FIXED DUTY --------
  if (currentMode == MODE_BUCK) {
    ledcWrite(BUCK_PWM_PIN, BUCK_FIXED_DUTY);
    ledcWrite(BOOST_PWM_PIN, 0);
  } else {
    ledcWrite(BOOST_PWM_PIN, BOOST_FIXED_DUTY);
    ledcWrite(BUCK_PWM_PIN, 0);
  }

  // -------- LCD DISPLAY --------
  lcd.setCursor(0, 0);
  if (currentMode == MODE_BUCK) {
    lcd.print("BCK ");
  } else {
    lcd.print("BST ");
  }
  lcd.print("V:");
  lcd.print(battV, 2);
  lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print("I:");
  lcd.print(current, 2);
  lcd.print(" S:");
  lcd.print((int)soc);
  lcd.print("%   ");

  // -------- SERIAL MONITOR --------
  Serial.print("Mode: ");
  Serial.print((currentMode == MODE_BUCK) ? "BUCK" : "BOOST");
  Serial.print(" | Vbat: ");
  Serial.print(battV, 2);
  Serial.print(" V | I: ");
  Serial.print(current, 2);
  Serial.print(" A | P: ");
  Serial.print(power, 2);
  Serial.print(" W | SOC: ");
  Serial.print(soc, 0);
  Serial.println(" %");

  delay(300);
}
