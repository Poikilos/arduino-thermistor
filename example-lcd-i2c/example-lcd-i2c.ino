// Authors: Jake Gustafson, Arduino_uno_guy
// based on "I2C Liquid Crystal Displays"
// |———————————————————————————————————————————————————————| 
// |  made by Arduino_uno_guy 11/13/2019                   |
// |   https://create.arduino.cc/projecthub/arduino_uno_guy|
// |———————————————————————————————————————————————————————|


#include <LiquidCrystal_I2C.h>

#include  <Wire.h>

//initialize the liquid crystal library
//the first parameter is  the I2C address
//the second parameter is how many rows are on your screen
//the  third parameter is how many columns are on your screen
LiquidCrystal_I2C lcd(0x27,  16, 2);

int counter = 0;
const char cursorChar = '\x7E'; // Right-pointing arrow (fallback)
String cursorStr = "\x7E";

void setup() {
  
  //initialize lcd screen
  lcd.init();
  // turn on the backlight
  lcd.backlight();
}
void loop() {
  //wait  for a second
  delay(1000);
  // tell the screen to write on the top row
  int x = counter % 16;
  int y = (int)(counter / 16) % 2;
  String thisCursor = (((int)(counter / 32) % 2) == 0) ? cursorStr : " ";
  char thisCursorChar = (((int)(counter / 32) % 2) == 0) ? cursorChar : ' ';
  lcd.setCursor(x,y);
  // tell the screen to write “hello, from” on the top  row
  // lcd.print("Hello, From");
  // tell the screen to write on the bottom  row
  //lcd.setCursor(0,1);
  // tell the screen to write “Arduino_uno_guy”  on the bottom row
  // you can change whats in the quotes to be what you want  it to be!
  //lcd.print("Arduino_uno_guy");
  lcd.write(' '); // write accepts (and *assumes*) char not int/String
  lcd.write(thisCursorChar); // write accepts (and *assumes*) char not int/String
  counter++;
  lcd.setCursor(0,0);
  lcd.print(counter); // String (formats non-String automatically)
}
