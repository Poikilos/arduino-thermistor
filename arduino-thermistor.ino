#include <LiquidCrystal_I2C.h>
#include <Wire.h>

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
LiquidCrystal_I2C lcd(0x27, screenW, screenH);

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

const int samplesMaxI = 100;
float samples[100];

const int sampleDelay = 5;
const int resampleDelay = 500;

double samplesSum = 0.0;
const double samplesMax = (double)samplesMaxI;
// ^ "On the Uno and other ATMEGA based boards, Double precision floating-point number occupies four bytes.
//   That is, the double implementation is exactly the same as the float,
//   with no gain in precision. On the Arduino Due, doubles have 8-byte (64 bit) precision."
int sampleCount = 0;
// Allow rotating the buffer without rewriting each value:
int oldestIndex = -1;
int emptyIndex = -1;

// const float variance = (float)(0.43 * 10.0 / (float)samplesMax);
// ^ varies by around 0.42 if taking an average of 10 samples, so check difference rather than truncated/rounded.
const float variance = (float)(0.23 * 100.0 / (float)samplesMax);
// ^ varies by around 0.23 if taking an average of 100 samples, so check difference rather than truncated/rounded.


// Time tracking
long tempReachedTick = -1;
long stateChangeT = -1000;
long tempLostTick = -1;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 250; // 250ms debounce for LEDs
int lastShownTempI = -1;
float lastShownTemp = -1;
int lastUnitIsF = fahrenheit;
long lastTempReachedSecond = -1;
// long lastTempReached = -1;  // temp used when marked reached
long lastTempLostSecond = -1;

// Cursor management
int cursorIdx = 0;
int prevCursorIdx = -1;  // -1 to force first draw
const char cursorChar = '\x7E'; // Right-pointing arrow (fallback)
String heatingStr = "+";

const int optionX = 12; // where to start right side options
const int optionCursorX = optionX - 1; // don't clear this when clearing left column (managed by cursor code)

// Encoder state
const int interrupt0 = 0; // clkPin (pin 2)
const int interrupt1 = 1; // dtPin (pin 4)
volatile int count = 0;
volatile byte encoderState = 0;
volatile int stateCount = 0; // Track state transitions per notch
volatile int lastUsedCount = count;
volatile int lastCLK = 9999;
volatile int presses = 0;
volatile int lastDT = HIGH; // Only count press more than once if user let go!

double sumSamples() {
  double sum = 0.0;
  for (int i = 0; i < sampleCount; i++) {
    sum += (double)samples[i];
  }
  return sum;
}

float averageSamples() {
  float average = 0.0f;
  for (int i=0; i < sampleCount; i++) {
    if (i == emptyIndex) {
      continue;
    }
    average += samples[i] / (float)sampleCount;
  }
  return average;
}

float popSample() {
  if (sampleCount < 1) {
    Serial.println("error=\"call acquireSample before popSample after init/clear\"");
    return 0.0f;
  }
  if (sampleCount < samplesMaxI) {
    Serial.println("error=\"call popSample only when buffer is full\"");
    return 0.0f;
  }

  // too slow:
  //float value = samples[0];
  //for (int i=1; i < sampleCount; i++) {
  //  samples[i-1] = samples[i];
  //}
  // so rotate buffer virtually instead:
  if (oldestIndex == -1) {
    oldestIndex = 0;
  }
  float value = samples[oldestIndex];
  emptyIndex = oldestIndex;
  oldestIndex++;
  if (oldestIndex == samplesMaxI) {
    oldestIndex = 0;
  }

  // sampleCount -= 1;
  return value;
}

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
    result += showSeconds ? String(seconds) + "s" : "0m";
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
  // Grok had made these into one line: `pinMode(swPin, INPUT_PULLUP);`, but 37 Sensor Kit V2.0 documentation says set INPUT and HIGH
  // lastClkState = digitalRead(clkPin);
  // lastClk = digitalRead(clkPin);  // TODO: why not declared?

  // attachInterrupt(interrupt0, ClockChanged, CHANGE);
  // attachInterrupt(interrupt0, ClockRising, RISING);
  // ^ RISING, since CHANGE gets called twice per notch (LOW to HIGH *and* HIGH to LOW).

  // Attach interrupts on CLK and DT
  attachInterrupt(interrupt0, ClockChanged, CHANGE);
  attachInterrupt(interrupt1, EncoderChanged, CHANGE);

  // Initialize serial for debugging
  Serial.begin(9600);

  // Set initial time tracking
  tempLostTick = -1;
  tempReachedTick = -1;
  showMode();

  // Initialize encoder state
  encoderState = (digitalRead(clkPin) << 1) | digitalRead(dtPin);
}

// Get the temperature and place it in the smoothing queue.
void acquireSample() {
  // Read thermistor
  Vo = analogRead(ThermistorPin);
  R2 = R1 * (1023.0 / (float)Vo - 1.0);
  logR2 = log(R2);
  T = (1.0 / (c1 + c2*logR2 + c3*logR2*logR2*logR2));
  T = T - 273.15;  // Temperature in Celsius
  samplesSum += T;  // maintain a pre-smoothed value
  if (sampleCount >= samplesMax) {
    samplesSum -= popSample();
    samples[emptyIndex] = T;
    emptyIndex = -1;
    // Now there is no empty index, the old empty index is the newest,
    //   so oldestIndex is one after that.
  } else {
    samples[sampleCount] = T;
    sampleCount += 1;
  }
}

void loopOld() {
  if (sampleCount < samplesMaxI) {
    delay(sampleDelay);
  }
  acquireSample();

  // Update time tracking
  unsigned long currentMillis = millis();

  checkInput(currentMillis);

  if ((!handleInput(currentMillis)) && sampleCount < samplesMaxI) {
    // ^ do handleInput before short-circuit so interactions aren't skipped
    return;
  }


  if (sampleCount == samplesMaxI) {
    // We are rotating the buffer, so after buffer fills this code is going to repeat quickly
    //   (so slow it down):
    delay(resampleDelay);
  }

  calculateTemp(currentMillis, false);
}

void loop() {
  static unsigned long lastSampleTime = 0;
  static unsigned long lastResampleTime = 0;
  unsigned long currentMillis = millis();

  // Non-blocking sample acquisition
  if (sampleCount < samplesMaxI) {
    if (currentMillis - lastSampleTime >= sampleDelay) {
      acquireSample();
      lastSampleTime = currentMillis;
    }
  } else if (currentMillis - lastResampleTime >= resampleDelay) {
    acquireSample();
    lastResampleTime = currentMillis;
  }

  // Always check inputs to avoid missing presses
  checkInput(currentMillis);
  bool changed = handleInput(currentMillis);

  // Process temperature if buffer is full or input changed
  if (sampleCount >= samplesMaxI || changed) {
    calculateTemp(currentMillis, changed);
  }
}

void calculateTemp(unsigned long currentMillis, bool forceOutputCheck) {
  if (sampleCount < 1) {
    acquireSample();
  }

  T = samplesSum / (double)sampleCount;  // smoothed average temperature (quick)
  // T = averageSamples();  // smoothed average temperature

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

// check encoder button (*not* rotation)
bool checkInput(unsigned long currentMillis) {
  // encoder rotation
  bool changed = false;
  /*
  int clkState = digitalRead(clkPin);
  if (clkState != lastClkState) {
    changeValue(digitalRead(dtPin) != clkState);
  }
  lastClkState = clkState;
  */
  int dtValue = digitalRead(swPin);
  if (dtValue == LOW && lastDT != LOW) {
    presses++;
    changed = true;
  }
  lastDT = dtValue;
  return changed; // presses > 0;
}

bool handleInput(unsigned long currentMillis) {
  // Handle encoder switch
  bool handled = false;
  static unsigned long lastSwitchTime = 0;
  if (presses > 0 && (currentMillis - lastSwitchTime) > 500) {
    cursorIdx = (cursorIdx + 1) % 3; // Cycle 0,1,2
    lastSwitchTime = currentMillis;
    // changed = true;
    calculateTemp(currentMillis, true);
    presses = 0;
    handled = true;
  }
  return handled;
}

void setWarming(bool enable) {
  if (warming != enable) {
    warming = enable;
    showMode();
  }
}

void updateLEDs(unsigned long currentMillis, float T) {
  String unit = fahrenheit ? "F" : "C";
  bool heatPinState = (mode == MODE_ON && T < targetTempC) ? HIGH : (T >= targetTempC + 1.0 ? LOW : digitalRead(yellowPin));
  setWarming(heatPinState);
  if (mode == MODE_ON) {
    digitalWrite(redPin, heatPinState);
    digitalWrite(greenPin, T >= targetTempC ? HIGH : LOW);
    digitalWrite(yellowPin, heatPinState);
  } else {
    digitalWrite(redPin, LOW);
    digitalWrite(greenPin, T >= targetTempC ? HIGH : LOW);
    digitalWrite(yellowPin, LOW);
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
  } else if (cursorIdx==1) {
    Serial.print("time=");
    Serial.print(millisecondsToHMS((long)targetMinutes*60*1000));
  } else {  //cursorIdx==2
    Serial.print("mode=");
    Serial.print((mode==MODE_OFF)?"OFF":((mode==MODE_ON)?"ON":("DONE")));
  }
  Serial.println();
  lastDebounceTime = currentMillis;
}

void updateLCD(unsigned long currentMillis, float T) {
  float displayTemp = fahrenheit ? (T * 9.0 / 5.0 + 32.0) : T;
  String unit = fahrenheit ? "F" : "C";

  if (cursorIdx != prevCursorIdx) {
    lcd.setCursor(0, 0);
    Serial.print("lcd.cursorIdx=");
    Serial.println(cursorIdx);
    lcd.write(cursorIdx == 0 ? cursorChar : ' ');
    lcd.setCursor(0, 1);
    lcd.write(cursorIdx == 1 ? cursorChar : ' ');
    lcd.setCursor(11, 0);
    lcd.write(cursorIdx == 2 ? cursorChar : ' ');
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
    Serial.println("error=\"Must call calculateTemp at least once before updateLCD\"");
  } else if ((lastTempReachedSecond != (long)((currentMillis - tempReachedTick) / 1000)) || (lastTempLostSecond != (long)((currentMillis - tempLostTick) / 1000))) {
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
}

void showMode() {
  // Write mode string at (12,0)
  lcd.setCursor(12, 0);  // near right edge of 1st row
  if (mode == MODE_ON) {
    lcd.print(warming ? ("ON " + heatingStr) : "ON  ");
  } else if (mode == MODE_OFF) {
    lcd.print("OFF ");
  } else {
    lcd.print("DONE");
  }
}

void changeValue(bool right) {
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

void EncoderChanged() {
  // checkInput();
  if (digitalRead(swPin) == LOW) {
    presses++;
  }
}


//The interrupt handlers
void ClockChanged()
{
  // based on 37 Sensor Kit V2.0 example
  int clkValue = digitalRead(clkPin);//Read the CLK pin level
  int dtValue = digitalRead(dtPin);//Read the DT pin level
  if (lastCLK != clkValue)
  {
    lastCLK = clkValue;
    count += (clkValue != dtValue ? 1 : -1);//CLK and inconsistent DT + 1, otherwise - 1

    //Serial.print("count:");
    //Serial.println(count);
    if (abs(lastUsedCount-count) > 1) {
      if (count < lastUsedCount) {
        changeValue(true); // Right rotation
      }
      else {
        changeValue(false); // Left rotation
      }
      lastUsedCount = count;
    }
  }
}
