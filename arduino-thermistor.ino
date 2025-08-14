//#include <LiquidCrystal.h>
#include <LiquidCrystal_I2C.h>
#include  <Wire.h>

// Thermistor setup
int ThermistorPin = 0;
int Vo;
float R1 = 10000;
float logR2, R2, T;
float c1 = 1.009249522e-03, c2 = 2.378405444e-04, c3 = 2.019202697e-07;

// LCD setup
const int screenW = 16;
const int screenH = 2;
// LiquidCrystal lcd(7, 8, 9, 10, 11, 12);
LiquidCrystal_I2C lcd(0x27,  screenW, screenH);

// LED pins
const int redPin = 5;
const int greenPin = 6;
const int yellowPin = 3;

bool warming = false;

// Encoder pins
const int clkPin = 2;
const int dtPin = 4;
const int swPin = 13;

// Temperature and time settings
float targetTempC = 20.0;
float lastTargetTemp = -1;
int targetMinutes = 240;
bool fahrenheit = false;

// State management
const int MODE_OFF = 0;
const int MODE_ON = 1;
const int MODE_DONE = -1;
int mode = MODE_OFF;
const int samplesMaxI = 200;
const int sampleDelay = 5;

double samplesSum = 0.0;
const double samplesMax = (double)samplesMaxI;
// ^ "On the Uno and other ATMEGA based boards, Double precision floating-point number occupies four bytes.
//   That is, the double implementation is exactly the same as the float,
//   with no gain in precision. On the Arduino Due, doubles have 8-byte (64 bit) precision."
int sampleCount = 0;

// const float variance = (float)(0.43 * 10.0 / samplesMax);
// ^ varies by around 0.42 if taking an average of 10 samples, so check difference rather than truncated/rounded.
const float variance = (float)(0.23 * 100.0 / samplesMax);
// ^ varies by around 0.23 if taking an average of 100 samples, so check difference rather than truncated/rounded.


// Time tracking
long tempReachedTick = -1;
long stateChangeT = -1000;
long tempLostTick = -1;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 250; // 250ms debounce
int lastShownTempI = -1;
float lastShownTemp = -1;
int lastUnitIsF = fahrenheit;
long lastTempReachedSecond = -1;
long lastTempReached = -1;  // temp used when marked reached
long lastTempLostSecond = -1;

// Cursor management
int cursorIdx = 0;
int prevCursorIdx = -1;  // -1 to force first draw
const char cursorChar = '\x7E'; // Right-pointing arrow (fallback)
String heatingStr = "+";

const int optionX = 12; // where to start right side options
const int optionCursorX = optionX - 1; // don't clear this when clearing left column (managed by cursor code)

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


// Convert milliseconds to human-readable format
//[<h>h][<m>m]<s>s (last part only shown if showSeconds is true)
String hr_milliseconds(long ms, bool showSeconds) {
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
      if ((seconds > 0) && showSeconds) {
        result += String(seconds) + "s";
      }
    }
  } else if (minutes > 0) {
    result += String(minutes) + "m";
    if ((seconds > 0) && showSeconds) {
      result += String(seconds) + "s";
    }
  } else {
    if (showSeconds) {
      result += String(seconds) + "s";
    }
    else {
      result += "0m";
    }
  }
  return result;
}


String millisecondsToHMS(long ms) {
    return hr_milliseconds(ms, true);
}

String millisecondsToHM(long ms) {
    return hr_milliseconds(ms, false);
}



void setup() {
  // Initialize LCD
  // lcd.begin(screenW, screenH);

  // Initialize I2C LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Initialize LED pins
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(yellowPin, OUTPUT);

  // Set initial LED states
  digitalWrite(redPin, LOW);
  setWarming(false);

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
  tempLostTick = -1;
  tempReachedTick = -1;
  showMode();
}

void acquireTemp() {
  // Read thermistor
  Vo = analogRead(ThermistorPin);
  R2 = R1 * (1023.0 / (float)Vo - 1.0);
  logR2 = log(R2);
  T = (1.0 / (c1 + c2*logR2 + c3*logR2*logR2*logR2));
  T = T - 273.15; // Temperature in Celsius
  samplesSum += T;
  sampleCount += 1;
}

void loop() {
  delay(sampleDelay);
  acquireTemp();

  // Update time tracking
  unsigned long currentMillis = millis();

  if ((sampleCount < samplesMaxI) && (!checkInput(currentMillis))) {
    return;
  }

  calculateTemp(currentMillis, false);
}

void calculateTemp(unsigned long currentMillis, bool forceOutputCheck) {
  if (sampleCount < 1) {
    acquireTemp();
  }
  T = samplesSum / (double)sampleCount;  // Get average temperature
  if (sampleCount >= samplesMaxI) {
    // reset the sample pool
    // TODO: ideally this should use a rotating buffer (instead of a sum)
    //   so we get the most recent average and a steady curve.
    samplesSum = 0;
    sampleCount = 0;
  }

  if (T >= targetTempC) {
    if (tempReachedTick == -1) {
      Serial.print(T);
      Serial.print("C reached or above target temperature ");
      Serial.print(targetTempC);
      Serial.println("C");
      tempReachedTick = currentMillis;
      tempLostTick = -1;
      stateChangeT = (int)T;
    }
  } else {
    if (tempLostTick == -1) {
      Serial.print(T);
      Serial.print("C dropped below target temperature ");
      Serial.print(targetTempC);
      Serial.println("C");
      tempLostTick = currentMillis;
      tempReachedTick = -1;
      stateChangeT = (int)T;
    }
  }

  // Check if target minutes reached
  if (tempReachedTick > -1 && (tempReachedTick / 1000 / 60) >= targetMinutes) {
    setMode(currentMillis, MODE_DONE);
  }

  if ((currentMillis - lastDebounceTime >= debounceDelay) || forceOutputCheck) {
    updateLEDs(currentMillis, T);
  }

  updateLCD(currentMillis, T);
}

bool checkInput(unsigned long currentMillis) {
  bool changed = false;
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
    changed = true;
    calculateTemp(currentMillis, true);
  }
}

void setWarming(bool enable) {
  if (warming != enable) {
    warming = enable;
    showMode();
  }
  warming = enable;
}

void updateLEDs(unsigned long currentMillis, float T) {
    String unit = fahrenheit ? "F" : "C";

    // Update LEDs with debounce
    bool heatPinState = (mode == MODE_ON && T < targetTempC) ? HIGH : (T >= targetTempC + 1.0 ? LOW : digitalRead(yellowPin));
    setWarming(heatPinState);
    if (mode == MODE_ON) {
      digitalWrite(redPin, heatPinState);
      digitalWrite(greenPin, T >= targetTempC ? HIGH : LOW);
      digitalWrite(yellowPin, heatPinState);
      // Serial.print("ON");
    } else {
      digitalWrite(redPin, LOW);
      digitalWrite(greenPin, T >= targetTempC ? HIGH : LOW);
      digitalWrite(yellowPin, LOW);
      // Serial.print("OFF");
    }
    Serial.print((mode==MODE_OFF)?"OFF":((mode==MODE_ON)?"ON":("DONE")));
    Serial.print(" warming=");
    Serial.print(warming);
    Serial.print(" temperature=");
    Serial.print(T);
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
      Serial.print("mode=");
      Serial.print((mode==MODE_OFF)?"OFF":((mode==MODE_ON)?"ON":("DONE")));
    }
    Serial.println();

    lastDebounceTime = currentMillis;
}

void updateLCD(unsigned long currentMillis, float T) {
  // Convert to Fahrenheit if needed
  float displayTemp = fahrenheit ? (T * 9.0 / 5.0 + 32.0) : T;

  String unit = fahrenheit ? "F" : "C";
  // Update LCD
  // lcd.clear();  // commented for partial updates!

  // Handle cursor and mode changes
  if (cursorIdx != prevCursorIdx) {
    // Update cursor positions
    lcd.setCursor(0, 0);
    Serial.print("lcd.cursorIdx=");
    Serial.println(cursorIdx);
    lcd.write(cursorIdx == 0 ? cursorChar : ' ');
    lcd.setCursor(0, 1);
    lcd.write(cursorIdx == 1 ? cursorChar : ' ');
    lcd.setCursor(11, 0);
    lcd.write(cursorIdx == 2 ? cursorChar : ' ');

    // Update mode based on cursor change
    if (prevCursorIdx == 2) {
      setMode(currentMillis, MODE_OFF);
    } else if (cursorIdx == 2) {
      setMode(currentMillis, MODE_ON);
    }
    showMode();
    prevCursorIdx = cursorIdx;
  }

  // const int maxLength = 16;
  const int maxLength = optionCursorX;

  // First line: Temperature
  //if (lastShownTempI != int(T)) {
  int roundDisplayTemp = (int)(displayTemp);
  // if ((lastShownTempI != roundDisplayTemp) || (lastUnitIsF != fahrenheit) || (lastTargetTemp != targetTempC)) {
  if ((abs(lastShownTemp-displayTemp) > variance) || (lastUnitIsF != fahrenheit) || (lastTargetTemp != targetTempC)) {
    // || (lastShownTempI != stateChangeT)
    //int roundT = (int)(T);  // NOTE: commented +.5 to round since display should follow *sustained minimum*
    Serial.print("lcd.T=");
    Serial.println(displayTemp);
    lcd.setCursor(1, 0);
    String tempStr = String(roundDisplayTemp) + "/" + String((int)targetTempC) + unit;
    lcd.print(tempStr);
    lastShownTempI = roundDisplayTemp;
    lastShownTemp = displayTemp;
    lastUnitIsF = fahrenheit;
    lastTargetTemp = targetTempC;

    // Clear remaining columns
    for (int i = tempStr.length() + 1; i < maxLength; i++) {  // +1 since one cursor/blank is always to left of the message.
      lcd.write(' ');
    }

  }

  // Second line: Time status
  if ((tempReachedTick==-1) && (tempLostTick==-1)) {
    Serial.print("error=\"Must call calculateTemp at least once before updateLCD\"");
  }
  else if ((lastTempReachedSecond != (long)((currentMillis - tempReachedTick) / 1000)) || (lastTempLostSecond != (long)((currentMillis - tempLostTick) / 1000))) {
    //Serial.print("lcd.tempReachedTick=");
    //Serial.println(tempReachedTick);
    //Serial.print("lcd.tempLostTick=");
    //Serial.println(tempLostTick);
    lcd.setCursor(1, 1);
    String line2;
    if (tempLostTick > -1) {
      line2 = "0/" + millisecondsToHMS((long)targetMinutes * 60 * 1000) + (warming?" pre":" off") + millisecondsToHMS(currentMillis - tempLostTick);
    } else {
      line2 = millisecondsToHMS(currentMillis - tempReachedTick) + "/" + millisecondsToHM((long)targetMinutes * 60 * 1000);
    }
    lcd.print(line2);
    // Clear remaining columns
    for (int i = line2.length() + 1; i < screenW; i++) {  // +1 since one cursor/blank is always to left of the message.
      // Don't use maxLength, since there is no room for anything on the 2nd line anyway (may be long such as "0/3h30m off1m18s" or "1h30m18s/3h30m")
      lcd.write(' ');
    }
    // Even if -1, do division same as the "if" condition:
    lastTempReachedSecond = (long)((currentMillis - tempReachedTick) / 1000);
    lastTempLostSecond = (long)((currentMillis - tempLostTick) / 1000);
    //Serial.print("lcd.lastTempReachedSecond=");
    //Serial.println(lastTempReachedSecond);
    //Serial.print("lcd.lastTempLostSecond=");
    //Serial.println(lastTempLostSecond);
  }
}

void setMode(long currentMillis, int newMode) {
  if (newMode != mode) {
    mode = newMode;
    showMode();
    // Don't say preheating 1hr if was off 1hr, and vise versa:
    if ((tempLostTick != -1) && (currentMillis != -1)) {
      tempLostTick = (currentMillis == -1) ? millis() : currentMillis;
    }
  }
  mode = newMode;
}

void showMode() {
    // Write mode string at (12,0)
    lcd.setCursor(12, 0);  // near right edge of 1st row
    if (mode == MODE_ON) {
      lcd.print(warming ? ("ON " + heatingStr): "ON  ");
    } else if (mode == MODE_OFF) {
      lcd.print("OFF ");
    } else {
      lcd.print("DONE");
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
