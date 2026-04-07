#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================= PIN DEFINITIONS =================
#define PWM_PIN        25
#define MOSFET_OFF_PIN 33
#define VOLTAGE_PIN    34
#define CURRENT_PIN    32
#define BUCK_LED_PIN   26

// ================= PWM SETTINGS =================
#define PWM_FREQ    30000
#define PWM_RES     10

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= CALIBRATION =================
float voltageScale  = 11.0;
float currentOffset = 2.5;
float currentScale  = 0.185;

// ================= BUCK PARAMETERS =================
#define FIXED_DUTY      511
#define DUTY_MAX        1023
#define CURRENT_LIMIT   1.3
#define RAMP_STEP_DELAY 15

// ================= HELPER: READ ADC AVERAGE =================
float readADC(int pin) {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(pin);
    delayMicroseconds(50);
  }
  return sum / 20.0;
}

// ================= HELPER: READ VOLTAGE =================
float readVoltage() {
  float rawV = readADC(VOLTAGE_PIN);
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

// ================= SOFT START RAMP =================
void softStart() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Soft Starting ");
  lcd.setCursor(0, 1);
  lcd.print("  Please Wait.. ");
  Serial.println(">> Soft-start ramp beginning...");

  for (int d = 0; d <= FIXED_DUTY; d++) {
    ledcWrite(PWM_PIN, d);
    delay(RAMP_STEP_DELAY);

    float I = readCurrent();
    if (I > CURRENT_LIMIT) {
      Serial.print(">> Ramp stopped at duty ");
      Serial.print(d);
      Serial.print(" — I: ");
      Serial.print(I, 2);
      Serial.println("A exceeds limit");
      lcd.setCursor(0, 1);
      lcd.print("I Limit Reached ");
      delay(500);
      break;
    }
  }

  Serial.println(">> Soft-start complete.");
  lcd.clear();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(500);

  ledcAttach(PWM_PIN, PWM_FREQ, PWM_RES);
  ledcWrite(PWM_PIN, 0);

  pinMode(MOSFET_OFF_PIN, OUTPUT);
  digitalWrite(MOSFET_OFF_PIN, LOW);

  pinMode(BUCK_LED_PIN, OUTPUT);
  digitalWrite(BUCK_LED_PIN, HIGH);

  analogReadResolution(12);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("  Buck  Mode    ");
  lcd.setCursor(0, 1);
  lcd.print(" Resistor  Test ");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Duty:");
  lcd.print((FIXED_DUTY * 100) / DUTY_MAX);
  lcd.print("% ~12V  ");
  lcd.setCursor(0, 1);
  lcd.print("I Limit:");
  lcd.print(CURRENT_LIMIT, 1);
  lcd.print("A    ");
  delay(2000);

  Serial.println("==============================");
  Serial.println("   Buck Mode — Resistor Test  ");
  Serial.println("==============================");
  Serial.print("Fixed Duty    : "); Serial.print((FIXED_DUTY * 100) / DUTY_MAX); Serial.println("%");
  Serial.print("Current Limit : "); Serial.print(CURRENT_LIMIT); Serial.println(" A");
  Serial.print("Ramp Speed    : "); Serial.print(RAMP_STEP_DELAY); Serial.println(" ms/step");
  Serial.println("------------------------------");

  softStart();
}

// ================= MAIN LOOP =================
void loop() {

  float voltage = readVoltage();
  float current = readCurrent();
  float power   = voltage * current;

  // ---- OVERCURRENT GUARD ----
  if (current > CURRENT_LIMIT) {
    ledcWrite(PWM_PIN, 0);
    digitalWrite(BUCK_LED_PIN, LOW);

    lcd.setCursor(0, 0);
    lcd.print("!! OVERCURRENT  ");
    lcd.setCursor(0, 1);
    lcd.print("PWM OFF  I:");
    lcd.print(current, 2);
    lcd.print("A");

    Serial.print("!! OVERCURRENT — PWM cut | I: ");
    Serial.print(current, 2);
    Serial.println("A");

    delay(2000);
    digitalWrite(BUCK_LED_PIN, HIGH);
    softStart();
    return;
  }

  // ---- LCD Display ----
  lcd.setCursor(0, 0);
  lcd.print("V:");
  lcd.print(voltage, 2);
  lcd.print("V       ");

  lcd.setCursor(0, 1);
  lcd.print("I:");
  lcd.print(current, 3);
  lcd.print("A P:");
  lcd.print(power, 1);
  lcd.print("W  ");

  // ---- Serial Monitor ----
  Serial.print("V: ");        Serial.print(voltage, 3);
  Serial.print("V | I: ");    Serial.print(current, 3);
  Serial.print("A | P: ");    Serial.print(power, 3);
  Serial.print("W | Duty: "); Serial.print((FIXED_DUTY * 100) / DUTY_MAX);
  Serial.println("%");

  delay(200);
}
