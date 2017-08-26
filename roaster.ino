#include <PID_v1.h>
#include <SendOnlySoftwareSerial.h>
#include <max6675.h>
#include <LiquidCrystal_SR2W.h>
#include <EEPROM.h>

/* Wiring
                  attiny85
                    +----+
(a0/rst/pot/pgm-cs))-|1  8|-(vcc)
  (heater relay/d3)-|2  7|-(d2/therm cs+lcd data/en/pgm-clk)
       (fan pwm/d4)-|3  6|-(d1/serial tx/therm+sr clocks/pgm-miso)
              (gnd)-|4  5|-(d0/therm do/pgm-mosi)
                    +----+

      max6675K
       +----+
 (gnd)-|1  8|-(nc)
  (t-)-|2  7|-(so)
  (t+)-|3  6|-(/cs)   pull /cs to vcc
 (vcc)-|4  5|-(sck)
       +----+


TODO:
d bug - random attiny reset during roast
- feature - integrate PID control
- feature - option to save profile when roast complete
*/

#define RXPIN 0
#define TXPIN 1
#define PWMPIN 4
#define HEATERPIN 3
#define THERMCS 2
#define SRDATAEN 2
#define THERMCLK 1
#define SRCLK 1
#define THERMDO 0
#define POTPIN A0
#define POTMIN 528
#define POTMAX 956
#define MINFANHEAT 4
#define POTSENSITIVITY 5
#define BANNERMS 2000
#define LCD_ROWS 2
#define LCD_COLS 20
#define MAXTEMP 280
#define ROASTTEMPFANSPEED 0.8
#define USEPID 0

#define ROASTMANUAL 0
#define ROASTPROFILE 1
#define ROASTTEMP 2

bool started = false;
unsigned long curr_time = 0;
unsigned long menu_time = 0;
short stage = 0;
double pidout = 0.0; // pid output
double target_temp = 0.0; // pid setpoint
double tempreading = 0.0; // pid input
int WindowSize = 5000;
unsigned long windowStartTime;
double lastpot;
byte roastmode = ROASTMANUAL;
byte roastprofile = 0;
short *stages_secs;
short *stages_temp;

PID pid(&tempreading, &pidout, &target_temp,2,5,1, DIRECT);
LiquidCrystal_SR2W lcd(SRDATAEN,SRCLK, NEGATIVE);
MAX6675 thermocouple(THERMCLK, THERMCS, THERMDO);
//SendOnlySoftwareSerial Serial(TXPIN); //tx


void mainMenu();
void profileMenu();
void doRoast();
bool loadProfile();
bool saveProfile();
int getRelativeInput(const __FlashStringHelper* prompt, double timeoutms, bool wrap, bool allownone, int numchoices, const char* choices);

struct profile {
    char name[20];
    int seconds[20];
    int temps[20];
};

#define NUM_STAGES 9
short city_stages_secs[] = {0,   160, 220, 280, 340, 400, 460, 520, 640};
short city_stages_temp[] = {200, 208, 212, 216, 220, 225, 230, 235, 0  };

short citypluss_stages_secs[] = {0,   160, 220, 280, 340, 400, 460, 520, 640};
short citypluss_stages_temp[] = {200, 210, 215, 220, 225, 230, 240, 245, 0  };

short vienna_stages_secs[] = {0,   160, 220, 280, 340, 400, 460, 520, 640};
short vienna_stages_temp[] = {200, 210, 215, 225, 230, 245, 255, 260, 0  };

/* Light roast not full first crack
short stages_secs[] = {0,   160, 220, 280, 340, 400, 460, 520, 640};
short stages_temp[] = {200, 204, 208, 212, 216, 220, 224, 230, 0  };
*/

void setup()
{
  pinMode(HEATERPIN, OUTPUT);
  digitalWrite(HEATERPIN, LOW);
  
  lcd.begin(LCD_COLS,LCD_ROWS);
  lcd.clear();
  lcd.setCursor(4,0);
  lcd.print(F("*Donnypants*"));
  lcd.setCursor(4,1);
  lcd.print(F("* Roaster  *"));
  delay(BANNERMS);

  lastpot = analogRead(POTPIN);
  while(lastpot <= POTMIN + 10 || lastpot >= POTMAX - 10)
  {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(F("Dial at "));
    lcd.print(lastpot);
    lcd.setCursor(0,1);
    lcd.print(F("Centre to begin"));
    delay(250);
    lastpot = analogRead(POTPIN);
  }
  delay(2000);

  pid.SetOutputLimits(0, WindowSize);
  pid.SetMode(AUTOMATIC);
}

void loop()
{
  if(!started)
  {
    mainMenu();
    lcd.clear();
  
    if(roastmode == ROASTPROFILE)
    {
      profileMenu();
      lcd.clear();
    }

    started = true;
    menu_time = millis();
  }
  
  windowStartTime = millis();
  doRoast();
}

void mainMenu()
{
  int pos = getRelativeInput(F("Man, Temp, Profile: "),
    10000,
    false,
    false,
    3,
    " m  t  p ");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("You chose "));
  switch(pos)
  {
    case 1:
      lcd.print(F("manual"));
      roastmode = ROASTMANUAL;
      break;
    case 2:
      lcd.print(F("temp"));
      roastmode = ROASTTEMP;
      break;
    case 3:
      lcd.print(F("profile"));
      roastmode = ROASTPROFILE;
      break;
  }
  delay(1000);
}

void profileMenu()
{
  int pos = getRelativeInput(F("City, City+, Vienna: "),
    10000,
    false,
    false,
    3,
    " c  +  v ");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("You chose "));
  switch(pos)
  {
    case 1:
      lcd.print(F("city"));
      stages_secs = city_stages_secs;
      stages_temp = city_stages_temp;
      break;
    case 2:
      lcd.print(F("city+"));
      stages_secs = citypluss_stages_secs;
      stages_temp = citypluss_stages_temp;
      break;
    case 3:
      lcd.print(F("vienna"));
      stages_secs = vienna_stages_secs;
      stages_temp = vienna_stages_temp;
      break;
  }
  roastprofile = pos-1;
  delay(1000);
}

void doRoast()
{
  curr_time = (millis()-menu_time)/1000;
  
  // Clear the SR to fix LCD issue due to sharing of data/en + cs pins
  shiftOut(SRDATAEN, SRCLK, 0, MSBFIRST);

  analogReference(DEFAULT);
  double potreading = 0.0;
  tempreading = 0.0;
  for(int i=0; i<5; i++)
  {
    // Thermo reading
    digitalWrite(THERMCS,HIGH);
    delay(5);
    tempreading += thermocouple.readCelsius();
    digitalWrite(THERMCS, LOW);
    delay(5);

    // Pot reading
    potreading += analogRead(POTPIN);
    delay(10);
  }
  potreading /= 5;
  tempreading /= 5;

  // PID calculation based on current temperature
  pid.Compute();

  float val = (float)(potreading-POTMIN)/(float)(POTMAX-POTMIN);
  if(val < 0 || curr_time < 1.0) val = 0;
  if(val > 1.0) val = 1.0;

  if(roastmode == ROASTPROFILE)
  {
    // Which stage are we at?
    for(int i=0; i<NUM_STAGES-1; i++)
    {
      unsigned int mint = stages_secs[i];
      unsigned int maxt = stages_secs[i+1];
      if (curr_time > mint && curr_time < maxt)
        stage = i;
      else if(curr_time >= maxt)
        stage = i+1;
      target_temp = stages_temp[stage];
    }
  
    if(stage >= NUM_STAGES-1)
    {
      // Turn off the heater
      digitalWrite(HEATERPIN, LOW);
      
      // Coool off beans?
//      analogWrite(PWMPIN, 255);
    }
    else
    {
      // Heater control
#if USEPID
      unsigned long now = millis();
      if(now - windowStartTime>WindowSize)
      {
        windowStartTime += WindowSize;
      }
      if(pidout > now - windowStartTime)
#else
      if(tempreading < target_temp)
#endif
      {
        digitalWrite(HEATERPIN, HIGH);
        lcd.setCursor(0,0);
        lcd.print(F("H"));
      }
      else
      {
        digitalWrite(HEATERPIN, LOW);
        lcd.setCursor(0,0);
        lcd.print(F("h"));
      }
    }
  }
  else if (roastmode == ROASTMANUAL)
  {
    if(val*9 < MINFANHEAT)
    {
      digitalWrite(HEATERPIN, LOW);
  //    lcd.setCursor(0,0);
  //    lcd.print("h");
  //    info = "Low fan, heat off";
    }
    else
    {
      digitalWrite(HEATERPIN, HIGH);
    }
  }
  else if (roastmode == ROASTTEMP)
  {
    analogWrite(PWMPIN, ROASTTEMPFANSPEED*255);
    target_temp = val*MAXTEMP;
    // Heater control
#if USEPID
      unsigned long now = millis();
      if(now - windowStartTime>WindowSize)
      {
        windowStartTime += WindowSize;
      }
      if(pidout > now - windowStartTime)
#else
      if(tempreading < target_temp)
#endif
    {
      digitalWrite(HEATERPIN, HIGH);
      lcd.setCursor(0,0);
      lcd.print(F("H"));
    }
    else
    {
      digitalWrite(HEATERPIN, LOW);
      lcd.setCursor(0,0);
      lcd.print(F("h"));
    }
  }

  // Status
  lcd.begin(LCD_COLS,LCD_ROWS); // display bug possibly due to RFI
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(F("         "));
  lcd.setCursor(3,0);
  lcd.print(tempreading,1);
  if(roastmode != ROASTMANUAL)
  {
    lcd.print(F("->"));
    lcd.print(target_temp);
  }
  lcd.print(F("C"));
  lcd.setCursor(3,1);
  lcd.print(F("         "));
  lcd.setCursor(3,1);
  lcd.print(curr_time,1);
  lcd.print(F(" s "));
  if(roastmode == ROASTPROFILE)
  {
    lcd.print(stage);
  }

  if(roastmode == ROASTTEMP)
  {
    lcd.setCursor(0,1);
    lcd.print((int)(ROASTTEMPFANSPEED*9));
    lcd.print(F("  "));
  }
  else
  {
    // Fan control
    analogWrite(PWMPIN, val*255);
    // Show fan speed
    lcd.setCursor(0,1);
    lcd.print((int)(val*9));
    lcd.print(F("  "));
  }

  // Set thermocouple CS high after LCD activity just in case
  digitalWrite(THERMCS, HIGH);

  delay(250);
}

/* getRelativeInput presents a prompt and lets user scroll through menu using
 * only the pot with a timer counting down to choose the selected item.
 * 
 *  choices a string of format " a  b  c "
 *  numchoices in this case would be 3
 *  prompt should not be longer than 20 characters, null terminated
 */
int getRelativeInput(const __FlashStringHelper* prompt, double timeoutms, bool wrap, bool allownone, int numchoices, const char* choices)
{
  int res = 0;
  double remainingms = timeoutms;
  double selectedms = 0;
  lastpot = analogRead(A0);
  char buff[21];

  while(remainingms > 0)
  {
    strcpy(buff, choices);
    if(res >= 0)
    {
      buff[(res-1)*(numchoices)] = '(';
      buff[(res-1)*(numchoices)+2] = ')';
    }
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(prompt);
    lcd.setCursor(0,1);
    lcd.print(buff);
    lcd.print(" ");
    lcd.print((int)(remainingms/1000));
    
    double pot = analogRead(A0);

    // Move selector to the left
    if(pot < lastpot - POTSENSITIVITY)
    {
      if(res > (allownone?0:1))
        res--;
      else if(wrap)
        // Wrap back to right!
        res = numchoices;

      if(res != 0)
      {
        selectedms = millis();
        remainingms = timeoutms;
      }
    }
    // Move selector to the right
    else if (pot > lastpot + POTSENSITIVITY)
    {
      if(res < numchoices)
        res++;
      else if(wrap)
        // Wrap back to left!
        res = 1;

      if(res != 0)
      {
        selectedms = millis();
        remainingms = timeoutms;
      }
    }
    
    remainingms = remainingms - (millis() - selectedms);
    
    // In non-choice position (nothing selected), disable timer
    if(res == 0)
    {
      selectedms = 0;
      remainingms = timeoutms;
    }

    lastpot = pot;
    delay(100);
  }

  return res;
}
