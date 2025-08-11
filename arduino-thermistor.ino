#include <LiquidCrystal.h>

// Thermistor setup
int ThermistorPin = 0;
int Vo;
float R1 = 10000;
float logR2, R2, T;
float c1 = 1.009249522e-03, c2 = 2.378405444e-04, c3 = 2.019202697e-07;

// LCD setup
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

// LED pins
const int redPin = 5;
const int greenPin = 6;
const int yellowPin = 3;

// Encoder pins
const int clkPin = 2;
const int dtPin = 4;
const int swPin = 13;

// Temperature and time settings
float targetTempC = 20.0;
int targetMinutes = 240;
bool fahrenheit = false;

// State management
const int STATE_OFF = 0;
const int STATE_ON = 1;
const int STATE_DONE = -1;
int state = STATE_OFF;

// Time tracking
long tempReachedTick = -1;
long tempLostTick = 0;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 250; // 250ms debounce
int lastShownTempI = -1;
long lastTempReachedSecond = -1;
long lastTempReached = -1;  // temp used when marked reached
long lastTempLostSecond = -1;

// Cursor management
int cursorIdx = 0;
int prevCursorIdx = 0;
const char cursor = '\x7E'; // Right-pointing arrow (fallback)

// Encoder state
// int lastClkState;
int lastCount = 0;

// Interrupt 0 on clkPin
const int interrupt0 = 0;
int count = 0;
//CLK initial value
int lastCLK = 0;
unsigned long lastInterruptTime = 0;
const unsigned long interruptDebounceDelay = 10; // 10ms debounce for encoder


// Convert milliseconds to HMS format
String millisecondsToHMS(long ms) {
  long seconds = ms / 1000;
  long hours = seconds / 3600;
  seconds %= 3600;
  long minutes = seconds / 60;
  seconds %= 60;

  String result = "";
  if (hours > 0) {
    result += String(hours) + "h";
    if (minutes > 0 || seconds > 0) {
      result += String(minutes) + "m";
      if (seconds > 0) {
        result += String(seconds) + "s";
      }
    }
  } else if (minutes > 0) {
    result += String(minutes) + "m";
    if (seconds > 0) {
      result += String(seconds) + "s";
    }
  } else {
    result += String(seconds) + "s";
  }
  return result;
}

void setup() {
  // Initialize LCD
  lcd.begin(16, 2);

  // Initialize LED pins
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(yellowPin, OUTPUT);

  // Set initial LED states
  digitalWrite(redPin, LOW);
  digitalWrite(greenPin, LOW);
  digitalWrite(yellowPin, LOW);

  // Initialize encoder pins
  pinMode(clkPin, INPUT);
  pinMode(dtPin, INPUT);
  pinMode(swPin, INPUT);
  digitalWrite(swPin, HIGH);
  // Grok had made these into one line: INPUT_PULLUP, but 37 Sensor Kit V2.0 documentation says set INPUT and HIGH
  // lastClkState = digitalRead(clkPin);

  attachInterrupt(interrupt0, ClockChanged, CHANGE);
  // attachInterrupt(interrupt0, ClockRising, RISING);
  // ^ RISING, since CHANGE gets called twice per notch (LOW to HIGH *and* HIGH to LOW).
  Serial.begin(9600);
  // Set initial time tracking
  tempLostTick = millis();
  tempReachedTick = -1;
}

void loop() {
  // Read thermistor
  Vo = analogRead(ThermistorPin);
  R2 = R1 * (1023.0 / (float)Vo - 1.0);
  logR2 = log(R2);
  T = (1.0 / (c1 + c2*logR2 + c3*logR2*logR2*logR2));
  T = T - 273.15; // Temperature in Celsius

  // Convert to Fahrenheit if needed
  float displayTemp = fahrenheit ? (T * 9.0 / 5.0 + 32.0) : T;
  String unit = fahrenheit ? "F" : "C";

  // Update time tracking
  unsigned long currentMillis = millis();
  if (T >= targetTempC) {
    if (tempReachedTick == -1) {
      Serial.print(T);
      Serial.print("C reached or above target temperature ");
      Serial.print(targetTempC);
      Serial.println("C");
      tempReachedTick = currentMillis;
      tempLostTick = -1;
    }
  } else {
    if (tempLostTick == -1) {
      Serial.print(T);
      Serial.print("C dropped below target temperature ");
      Serial.print(targetTempC);
      Serial.println("C");
      tempLostTick = currentMillis;
      tempReachedTick = -1;
    }
  }

  // Check if target minutes reached
  if (tempReachedTick > -1 && (tempReachedTick / 1000 / 60) >= targetMinutes) {
    state = STATE_DONE;
  }

  // Handle encoder rotation
  /*
  int clkState = digitalRead(clkPin);
  if (clkState != lastClkState) {
    changeValue(digitalRead(dtPin) != clkState);
  }
  lastClkState = clkState;
  */
  // Handle encoder switch
  static unsigned long lastSwitchTime = 0;
  if (digitalRead(swPin) == LOW && (currentMillis - lastSwitchTime) > 250) {
    cursorIdx = (cursorIdx + 1) % 3; // Cycle 0,1,2
    lastSwitchTime = currentMillis;
  }

  // Update LEDs with debounce
  bool warming = (state == STATE_ON && T < targetTempC) ? HIGH : (T >= targetTempC + 1.0 ? LOW : digitalRead(yellowPin));
  if (currentMillis - lastDebounceTime >= debounceDelay) {
    if (state == STATE_ON) {
      digitalWrite(redPin, warming);
      digitalWrite(greenPin, T >= targetTempC ? HIGH : LOW);
      digitalWrite(yellowPin, warming);
      // Serial.print("ON");
    } else {
      digitalWrite(redPin, LOW);
      digitalWrite(greenPin, T >= targetTempC ? HIGH : LOW);
      digitalWrite(yellowPin, LOW);
      // Serial.print("OFF");
    }
    Serial.print((state==STATE_OFF)?"OFF":((state==STATE_ON)?"ON":("DONE")));
    Serial.print(" warming=");
    Serial.print(warming);
    Serial.print(" temperature=");
    Serial.print(displayTemp);
    Serial.print(unit);
    Serial.print("/");
    Serial.print(targetTempC);
    Serial.print("C");
    if (tempReachedTick > -1) {
      Serial.print(" sustained=");
      Serial.print(millisecondsToHMS((currentMillis-tempReachedTick)));
    }
    Serial.print(" set ");
    if (cursorIdx==0) {
      Serial.print("target=");
      Serial.print(targetTempC);
    }
    else if (cursorIdx==1) {
      Serial.print("time=");
      Serial.print(millisecondsToHMS((long)targetMinutes*60*1000));
    }
    else {//cursorIdx==2
      Serial.print("state=");
      Serial.print((state==STATE_OFF)?"OFF":((state==STATE_ON)?"ON":("DONE")));
    }
    Serial.println();

    lastDebounceTime = currentMillis;
  }

  // Update LCD
  lcd.clear();

  // Handle cursor and state changes
  if (cursorIdx != prevCursorIdx) {
    // Update cursor positions
    lcd.setCursor(0, 0);
    lcd.write(cursorIdx == 0 ? cursor : ' ');
    lcd.setCursor(0, 1);
    lcd.write(cursorIdx == 1 ? cursor : ' ');
    lcd.setCursor(11, 0);
    lcd.write(cursorIdx == 2 ? cursor : ' ');

    // Update state based on cursor change
    if (prevCursorIdx == 2) {
      state = STATE_OFF;
    } else if (cursorIdx == 2) {
      state = STATE_ON;
    }

    // Write state string at (12,0)
    lcd.setCursor(12, 0);
    if (state == STATE_ON) {
      lcd.print("ON  ");
    } else if (state == STATE_OFF) {
      lcd.print("OFF ");
    } else {
      lcd.print("DONE");
    }

    prevCursorIdx = cursorIdx;
  }

  // First line: Temperature
  if (lastShownTempI != int(T)) {
    lcd.setCursor(1, 0);
    String tempStr = String((int)displayTemp) + "/" + String((int)targetTempC) + unit;
    lcd.print(tempStr);
    lastShownTempI = int(T);
  }

  // Second line: Time status
  if (lastTempReachedSecond != tempReachedTick / 1000 / 60 || lastTempLostSecond != tempLostTick / 1000 / 60) {
    lcd.setCursor(0, 1);
    String line2;
    if (tempLostTick > -1) {
      line2 = "0/" + millisecondsToHMS((long)targetMinutes * 60 * 1000) + " Pre. " + millisecondsToHMS(currentMillis - tempLostTick) + "/";
    } else {
      line2 = millisecondsToHMS(currentMillis - tempReachedTick) + "/" + millisecondsToHMS((long)targetMinutes * 60 * 1000);
    }
    lcd.print(line2);
    // Clear remaining columns
    for (int i = line2.length(); i < 16; i++) {
      lcd.write(' ');
    }
    lastTempReachedSecond = tempReachedTick / 1000 / 60;
    lastTempLostSecond = tempLostTick / 1000 / 60;
  }
}

void changeValue(bool right)
{
    if (right) { // Right rotation
      if (cursorIdx == 0) {
        targetTempC += 5;
      } else if (cursorIdx == 1) {
        targetMinutes += 30;
      }
    } else { // Left rotation
      if (cursorIdx == 0) {
        targetTempC -= 5;
      } else if (cursorIdx == 1) {
        targetMinutes = max(0, targetMinutes - 30); // Prevent negative
      }
    }
}

void ClockChanged()
{
  // NOTE: CHANGE gets called twice per notch, on LOW to HIGH *and* HIGH to LOW
  //   So attach to interrupt using the RISING constant.
  // based on 37 Sensor Kit V2.0 encoder example
  //Read the CLK pin level
  int clkValue = digitalRead(clkPin);
  //Read the DT pin level
  int dtValue = digitalRead(dtPin);
  if (lastCLK != clkValue)
  {
    lastCLK = clkValue;
    //CLK and inconsistent DT + 1, otherwise - 1
    bool right = clkValue == dtValue;
    count += (right ? 1 : -1);
    if (count % 2 == 0) {
        changeValue(right);
    } // else skip FALLING
    Serial.print("count:");
    Serial.println(count);
  }
}


void ClockRising()
{
  //unsigned long currentInterruptTime = millis();
  //if (currentInterruptTime - lastInterruptTime < interruptDebounceDelay) {
  //  return; // Ignore if within debounce period
  //}
  //lastInterruptTime = currentInterruptTime;

  // NOTE: CHANGE gets called twice per notch, on LOW to HIGH *and* HIGH to LOW
  //   So attach to interrupt using the RISING constant.
  // based on 37 Sensor Kit V2.0 encoder example
  //Read the CLK pin level
  int clkValue = digitalRead(clkPin);
  //Read the DT pin level
  int dtValue = digitalRead(dtPin);
  // NOTE: Don't check lastCLK != clkValue, only occurs in change in direction in case of RISING
  //CLK and inconsistent DT + 1, otherwise - 1
  bool right = clkValue == dtValue;
  changeValue(right);
  count += (right ? 1 : -1);
  Serial.print("count:");
  Serial.println(count);
}
