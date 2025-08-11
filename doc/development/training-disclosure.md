# Training Disclosure for arduino-thermistor
This Training Disclosure, which may be more specifically titled above here (and in this document possibly referred to as "this disclosure"), is based on **Training Disclosure version 1.1.4** at https://github.com/Hierosoft/training-disclosure by Jake Gustafson. Jake Gustafson is probably *not* an author of the project unless listed as a project author, nor necessarily the disclosure editor(s) of this copy of the disclosure unless this copy is the original which among other places I, Jake Gustafson, state IANAL. The original disclosure is released under the [CC0](https://creativecommons.org/public-domain/cc0/) license, but regarding any text that differs from the original:

This disclosure also functions as a claim of copyright to the scope described in the paragraph below since potentially in some jurisdictions output not of direct human origin, by certain means of generation at least, may not be copyrightable (again, IANAL):

Various author(s) may make claims of authorship to content in the project not mentioned in this disclosure, which this disclosure by way of omission unless stated elsewhere implies is of direct human origin unless stated elsewhere. Such statements elsewhere are present and complete if applicable to the best of the disclosure editor(s) ability. Additionally, the project author(s) hereby claim copyright and claim direct human origin to any and all content in the subsections of this disclosure itself, where scope is defined to the best of the ability of the disclosure editor(s), including the subsection names themselves, unless where stated, and unless implied such as by context, being copyrighted or trademarked elsewhere, or other means of statement or implication according to law in applicable jurisdiction(s).

Disclosure editor(s): Hierosoft LLC

Project author: Hierosoft LLC

This disclosure is a voluntary of how and where content in or used by this project was produced by LLM(s) or any tools that are "trained" in any way.

The main section of this disclosure lists such tools. For each, the version, install location, and a scope of their training sources in a way that is specific as possible.

Subsections of this disclosure contain prompts used to generate content, in a way that is complete to the best ability of the disclosure editor(s).

tool(s) used:
- Grok 3

Scope of use: code described in subsections--typically modified by hand to improve logic, variable naming, integration, etc.


## usb-thermistor.ino
- 2025-08-10

Explain why there is no delay in this program and if one is necessary or helpful. Set a target temperature of 80C. Make a boolean for Farenheit, false by default. Show the temperature in Celcius if false. Show it on the first line instead of the second. Set a tempLostTick at the start of the program or any iteration when temperature is below target, and set tempReachedTick to -1 in that case. Set a tempReachedTick variable to the millis when targetTempC is reached and set tempLostTick to -1 in that case. On the second line, show the following. If tempReachedTick is > -1, show "Sustained>=" + targetTempC + "C: " + current tick minus that + "s", else show "Below " + targetTempC + "C: " + current tick minus tempLostTick + "s". Do not show any decimal places. If >= 60*60, show number of hours then "h", then if remainder is >= 60 show number of minutes + "m" then remainder (seconds) + "s". Make a tidy millisecondsToHMS function to do that. Lets make 3 lights, red in pin 5, green in pin 6, and yellow in pin 3. Whenever the temperature is below target + .5 degrees C, turn on the yellow light until target is at least target + 1 degrees C. Make debounce code for 250ms to avoid bounce. Whenever the temperature is at target or above, turn on the green light.

- pasted LCD example from Arduino UNO Super Starter Kit, and "Make an Arduino Temperature Sensor" by circuitbasics.com globals and loop code.

Since we default tempReachedTick to -1, we should start redPin HIGH

Make a state variable and 3 state constants: STATE_OFF = 0, STATE_ON = 1, STATE_DONE = -1
Start with state = STATE_OFF
Due to that, lets go back to initializing redPin to LOW.
Make a targetMinutes variable as well, starting at 240.
if tempReachedTick/1000/60 >= targetMinutes, set state = STATE_DONE
Save HIGH or LOW as a warming variable and move the ternary operator so it sets that variable, so that it can be used for both red and yellow.
When state != STATE_ON, force red & yellow off.
Make a cursor: cursorIdx can be either 0, 1, or 2. The default should be 0. Also make a global prevCursorIdx = 0. 0 places it on first column of 1st row, 1 is 1st column of 2nd row. 2 is 1st row, 11th column and place the right arrow there, otherwise space. Use the ASCII right-pointing triangle character (or arrow if there is one) and put it at the beginning of the selected line. If the line is not selected, put a space there instead. To reduce writes, only write to those 3 places when cursorIdx changes (if cursorIdx != prevCursorIdx), writing a space to the other 2 that are not selected when it changes to make sure they are cleared. Also when cursorIdx != prevCursorIdx: if prevCursorIdx == 2 set state to STATE_OFF else if cursorIdx == 2 set state to STATE_ON. Make sure to follow those instructions exactly: Only changing to or from 2 should force a state change. In any case, after that set prevCursorIdx = cursorIdx.
Assume there is a 5-pin arduino encoder wheel with pins labeled CLK, DT, SW, +, GND. Explain where to connect each that is necessary for our project on the Arduino UNO R3. When the encoder wheel is rotated right, increase the target temperature if selected line is 0, or decrease if left (counter-clockwise) by 5. If cursorIdx is 1, increase or decrease targetMinutes by 30. When the encoder wheel is clicked inward, increase the cursor, but if it becomes > 2, set it to 0.
- Change the first line format to ((cursorIdx==0)?cursor:" ") + + T + "/" + targetTempC, then at column index 12 do a separate write of state <represented as "ON  " "OFF " or "DONE">. To reduce writes, only write the temperature string if a global integer lastShownTempI != int(T), and only ever write the state string during the (cursorIdx != prevCursorIdx) scope discussed earlier.
Change the second line format as follows.
if tempLostTick > -1: "0/" + millisecondsToHMS(targetMinutes*60*1000) + " Pre. " + millisecondsToHMS(currentMillis - tempLostTick) + "/"
else: millisecondsToHMS(currentMillis - tempReachedTick) + "/" + millisecondsToHMS(targetMinutes*60*1000)
Each time the line is written, create a string of the whole thing first, and clear the number of columns after that with " " times the number of remaining columns after the written part to efficiently ensure they are cleared in case the line was longer previously.
Only write the second line if a global lastTempReachedSecond != tempReachedTick / 1000 / 60 or lastTempLostTick != tempLostTick / 1000 / 60
Then set a global lastTempReachedSecond = tempReachedTick / 1000 / 60
Then set a global lastTempLostTick = tempLostTick / 1000 / 60

