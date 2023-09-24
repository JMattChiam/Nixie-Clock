/* This is version 6, updated from version 5 and version minimal

    updates from version 5
    - reset time mode working
    - alarms working
    - pushbutton working, *longpress changed to auto activate?
    - added new showTime() with input digits already parsed

    updates from version minimal
    - Added anti-poison routine and digit cyling.

    27/4/21
*/

#include <Sleep_n0m1.h>
#include <Wire.h>
#include <DS3231_Simple.h>
#include <ShiftRegister74HC595.h>
#include <Bounce2.h>

#define wakeUpPin 2 //DS3231 alarm
#define encoderButton 3 //Rotary encoder button
#define encoderPinA 5
#define encoderPinB 4
#define resetDuration 1000 //How long to press button to activate reset time mode
#define blinkInterval 500 //Interval for blinking nixies during reset time mode
#define poisonInterval 100 //Interval for anti-poison routine
#define cycleInterval 75 //Interval for cycling digits when time changes

DS3231_Simple Clock;
ShiftRegister74HC595<1> sr_h(9, 8, 7); //Shift register for hours (srData, clock, latch)
ShiftRegister74HC595<1> sr_m(12, 10, 11); //Shift register for minutes

Sleep sleep;
Bounce button = Bounce(); //Rotary encoder pushbutton
Bounce encoder = Bounce(); //rotary encoder

volatile bool resetTime = false; //Flag for activating reset time mode
unsigned long previousMillis = 0; //Used for blinking nixies
int nixieState = HIGH; //Used for blinking nixies

//Truth table for BCD decoder inputs. in order of {D, C, B, A}
int digits[11][4]
{
  {0, 0, 0, 1}, // 0
  {0, 0, 0, 0}, // 1
  {1, 0, 0, 1}, // 2
  {1, 0, 0, 0}, // 3
  {0, 1, 1, 1}, // 4
  {0, 1, 1, 0}, // 5
  {0, 1, 0, 1}, // 6
  {0, 1, 0, 0}, // 7
  {0, 0, 1, 1}, // 8
  {0, 0, 1, 0}, // 9
  {1, 0, 1, 0}, // Blank
};

void setup()
{
  Clock.begin();
  Wire.begin();

  //Set up for rotary encoder
  pinMode(encoderButton, INPUT_PULLUP);
  pinMode(encoderPinA, INPUT_PULLUP);
  pinMode(encoderPinB, INPUT_PULLUP);
  button.attach(encoderButton);
  button.interval(20);
  encoder.attach(encoderPinA);
  encoder.interval(2);

  resetTime = false;
  attachInterrupt(digitalPinToInterrupt(encoderButton), reset, LOW);

  //Set up for clock
  Clock.disableAlarms();
  Clock.setAlarm(DS3231_Simple::ALARM_EVERY_MINUTE);
  pinMode(wakeUpPin, INPUT_PULLUP);
  digitalWrite(wakeUpPin, HIGH);
  attachInterrupt(digitalPinToInterrupt(wakeUpPin), minuteChange, FALLING);
}

void loop()
{
  update_time();

  if (resetTime == true)
  {
    if (longPress() == true)
    {
      resetTimeMode();
    }
  }

  //Reattach interrupts that were detached in the reset() ISR. Reset flag back to false
  resetTime = false;
  attachInterrupt(digitalPinToInterrupt(encoderButton), reset, LOW);
  attachInterrupt(digitalPinToInterrupt(wakeUpPin), minuteChange, FALLING);

  // Reset alarm
  Clock.disableAlarms();
  Clock.setAlarm(DS3231_Simple::ALARM_EVERY_MINUTE);
  digitalWrite(wakeUpPin, HIGH);

  // Go to sleep
  delay(100);
  sleep.pwrDownMode();
  sleep.sleepPinInterrupt(digitalPinToInterrupt(wakeUpPin), FALLING);
}

/* ISR for DS3231 alarm
   Alarm should fire whenever the minute changes
*/
void minuteChange()
{
}

/* ISR for encoder push button
   Disable all interrupts and set reset time flag to true
*/
void reset()
{
  detachInterrupt(digitalPinToInterrupt(encoderButton));
  detachInterrupt(digitalPinToInterrupt(wakeUpPin));
  resetTime = true;
}

/* Update the time displayed on the nixie tubes
   if the time is 4am, run the anti-poison routine
   if the time are new 1st, 2nd or 3rd digits, cycle to them.
*/
void update_time()
{
  DateTime now;
  now = Clock.read();
  int hours = now.Hour;
  int minutes = now.Minute;

  if ((hours == 4) && (minutes == 0)) //4am
  {
    antiPoison();
  }

  if ((hours == 0) && (minutes == 0)) //Time is 00:00
  {
    cycleNewDay();
  }
  else if ((hours % 10 == 0) && (minutes == 0)) //Time is X0:00
  {
    cycleHoursTens(hours / 10);
  }
  else if (minutes % 10 == 0)
  {
    if (minutes == 0) //Time is XX:00
    {
      cycleNewHour(hours / 10, hours % 10);
    }
    else //time is  XX:X0
    {
      cycleMinutesOnes(hours / 10, hours % 10, minutes / 10);
    }
  }
  showTime(hours, minutes);
}

void showTime(int hours, int minutes)
{
  int hour_tens = hours / 10;
  int hour_ones = hours % 10;
  int minute_tens = minutes / 10;
  int minute_ones = minutes % 10;
  set_hours(hour_tens, hour_ones);
  set_minutes(minute_tens, minute_ones);
  sr_h.updateRegisters();
  sr_m.updateRegisters();
}

/* Display a time, digits already parsed
*/
void showTime(int h1, int h2, int m1, int m2)
{
  set_hours(h1, h2);
  set_minutes(m1, m2);
  sr_h.updateRegisters();
  sr_m.updateRegisters();
}

/* Set bits for hours
*/
void set_hours(int tens, int ones)
{
  for (int i = 0; i < 4; i++)
  {
    sr_h.setNoUpdate(i + 4, digits[ones][i]);
  }
  for (int i = 0; i < 4; i++)
  {
    sr_h.setNoUpdate(i, digits[tens][i]);
  }
}

/* Set bits for minutes
*/
void set_minutes(int tens, int ones)
{
  for (int i = 0; i < 4; i++)
  {
    sr_m.setNoUpdate(i + 4, digits[ones][i]);
  }
  for (int i = 0; i < 4; i++)
  {
    sr_m.setNoUpdate(i, digits[tens][i]);
  }
}

/* Checks how long the pushbutton was pressed.
   returns true if button is pressed longer than set duration
   returns false otherwise
   button state is low when entering the function.
*/
bool longPress()
{
  unsigned long pressTime = millis(); // Time stamp for initial button press
  unsigned long pressDuration;
  bool long_press = false;
  bool pressed = true;
  while (pressed == true)
  {
    pressDuration = millis() - pressTime;
    button.update();
    if (button.rose()) // button released
    {
      if (pressDuration > resetDuration)
      {
        long_press = true;
      }
      pressed = false;
    }
    if (pressDuration > resetDuration)
    {
      long_press = true;
      return long_press;
    }
  }
  return long_press;
}

/* This function tells whether the nixie tubes need to be blinked
   returns 1 if the tubes need to be ON
   returns 0 if the tubes need to be OFF
*/
int blinkCheck(int current)
{
  if (current - previousMillis > blinkInterval)
  {
    previousMillis = current;
    if (nixieState == LOW)
    {
      nixieState = HIGH;
      return 1; //Tubes need to light
    }
    else
    {
      nixieState = LOW;
      return 0; //Tubes need to off
    }
  }
}

/* This function sets the array elements to 10, excluding array[digit]
   For use in blinking nixies that are not being reset
*/
void blinkSet(int blinkTime[4], int digit)
{
  for (int i = 0; i < 4; i++)
  {
    if (i != digit)
    {
      blinkTime[i] = 10;
    }
  }
}

/*  Reset time mode. Use rotary encoder to change the time stored on the DS3231. Time is changed digit by digit.
    Nixies that are not being reset will blink at a specified interval.
    Stop reset time mode when pushbutton is long pressed again.
    All interrupts are disabled while in reset time mode.
*/
void resetTimeMode()
{
  int encoderLastState;
  int encoderState;
  int encoderCount = 0; //To change digit every other encoder pulse
  encoderLastState = digitalRead(encoderPinA);
  bool resetting = true;

  //Take current time, store individual digits in array
  DateTime now;
  now = Clock.read();
  int tempTime[4] = {now.Hour / 10, now.Hour % 10, now.Minute / 10, now.Minute % 10};
  int blinkTime[4] = {tempTime[0], tempTime[1], tempTime[2], tempTime[3]}; //Temporary array to allow nixies to blink without changing the time
  int maxDigit[4] = {2, 9, 5, 9}; //The maximum for each digit

  showTime(tempTime[0], tempTime[1], tempTime[2], tempTime[3]);

  unsigned long currentMillis;
  int blinkOrNot;

  int i = 0;
  while (resetting == true)
  {
    currentMillis = millis();
    blinkOrNot = blinkCheck(currentMillis);

    button.update();
    encoder.update();
    encoderState = digitalRead(encoderPinA);

    if (encoder.changed()) //Encoder moved
    {
      encoderCount += 1;
      encoderLastState = encoderState;
      if (encoderCount == 2)//one notch
      {
        encoderCount = 0; //Reset encoder count
        if (digitalRead(encoderPinB) != encoderState) //Clockwise turn, increase digit by 1. If any of the digits exceed their maximum value, overflow back to 0
        {
          tempTime[i] += 1;
          if (tempTime[i] > maxDigit[i]) //If the digit is greater than the max, roll over back to 0
          {
            tempTime[i] = 0;
          }
        }
        else //Anti-clockwise turn, decrease digit by 1. If any goes negative, overflow back to maximum value.
        {
          tempTime[i] -= 1;
          if (tempTime[i] < 0)
          {
            tempTime[i] = maxDigit[i]; //If the digit is less than 0, roll over back to the max
          }
        }

        if (blinkOrNot == 0) //Turn OFF nixies that are not reset
        {
          for (int i = 0; i < 4; i++)
          {
            blinkTime[i] = tempTime[i];
          }
          blinkSet(blinkTime, i);
          showTime(blinkTime[0], blinkTime[1], blinkTime[2], blinkTime[3]);
        }
        else
        {
          showTime(tempTime[0], tempTime[1], tempTime[2], tempTime[3]);
        }
      }
    }

    if (blinkOrNot == 0)
    {
      for (int i = 0; i < 4; i++)
      {
        blinkTime[i] = tempTime[i];
      }
      blinkSet(blinkTime, i);
      showTime(blinkTime[0], blinkTime[1], blinkTime[2], blinkTime[3]);
    }
    else
    {
      showTime(tempTime[0], tempTime[1], tempTime[2], tempTime[3]);
    }

    if (button.fell()) //Pushbutton pressed
    {
      if (longPress() == true)
      {
        //Create new datetime and set to the new values
        DateTime now;
        now.Hour = (tempTime[0] * 10) + tempTime[1];
        now.Minute = (tempTime[2] * 10) + tempTime[3];
        now.Second = 0;
        Clock.write(now);
        resetting = false;

        //blink nixies twice to show that new time has been set
        showTime(tempTime[0], tempTime[1], tempTime[2], tempTime[3]); //Display the new set time
        delay(150);
        showTime(10, 10, 10, 10);
        delay(150);
        showTime(tempTime[0], tempTime[1], tempTime[2], tempTime[3]);
        delay(150);
        showTime(10, 10, 10, 10);
        delay(150);
        showTime(tempTime[0], tempTime[1], tempTime[2], tempTime[3]);
        return;
      }
      else
      {
        i++; //Move to next digit
        if (i > 3)
        {
          i = 0;
        }
      }
    }
  }
}

/* To cycle digits to a new first digit of hours (i.e. X0:00)
   Time was X9:59 before this
*/
void cycleHoursTens(int h1)
{
  for (int i = 0; i < 6; i++) //cycle minutes right digit from 0 to 5
  {
    showTime(h1-1, 9, 5, i);
    delay(cycleInterval);
  }
  for (int i = 6; i < 10; i++) //cycle minutes both digits from 6 to 9
  {
    showTime(h1-1, 9, i, i);
    delay(cycleInterval);
  }
  for (int i = 0; i < 10; i++)
  {
    showTime(h1-1, i, i, i);
    delay(cycleInterval);
  }
}

/* To cycle digits to new hour
   Time was XX:59 before this
*/
void cycleNewHour(int h1, int h2)
{
  for (int i = 0; i < 6; i++) //cycle minutes right digit from 0 to 5
  {
    showTime(h1, h2-1, 5, i);
    delay(cycleInterval);
  }
  for (int i = 6; i < 10; i++) //cycle minutes both digits from 6 to 9
  {
    showTime(h1, h2-1, i, i);
    delay(cycleInterval);
  }
  for (int i = 0; i < 10; i++)  //cycle minutes both digits from 0 to 9
  {
    showTime(h1, h2-1, i, i);
    delay(cycleInterval);
  }
}


/* To cycle right digit of minutes from 0 to 9, keeping left digit the same
   Time was XX:X9 before this
*/
void cycleMinutesOnes(int h1, int h2, int m1)
{
  for (int i = 0; i < 10; i++)
  {
    showTime(h1, h2, m1 - 1, i);
    delay(cycleInterval);
  }
}

/* To cycle digits to 00:00
 * Time is 23:59 before this
 */
void cycleNewDay()
{
  for (int i = 0; i < 6; i++) //cycle minutes right digit from 0 to 5
  {
    showTime(2, 3, 5, i);
    delay(cycleInterval);
  }
  for (int i = 6; i < 10; i++) //cycle minutes both digits from 6 to 9
  {
    showTime(2, 3, i, i);
    delay(cycleInterval);
  }
  for (int i = 0; i < 10; i++)
  {
    showTime(2, i, i, i);
    delay(cycleInterval);
  }
}
/* To cycle through all cathodes for 20 minutes at 4am every morning to prevent cathode poisoning
   Each cathode will fire for 100ms for a total of 2 minutes for each cathode.
*/
void antiPoison()
{
  for (int i = 0; i < 900; i++)
  {
    // One cycle from 0 to 9 takes 1 second, with 100ms for each digit
    for (int i = 9; i >= 0; i--)
    {
      showTime(i, i, i, i);
      if (resetTime == true)
      {
        return;
      }
      delay(poisonInterval);
    }
  }
}
