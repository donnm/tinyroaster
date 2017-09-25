#include <avr/pgmspace.h>
#include <SendOnlySoftwareSerial.h>
#include <max6675.h>
#include <LiquidCrystal_SR2W.h>

/* Wiring
                  attiny85
                    +----+
(a0/rst/pot/pgm-cs)-|1  8|-(vcc)
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
- bug - getRelativeInput countdown strangeness
- bug - linearise pot readings
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
#define BANNERMS 1000
#define LCD_ROWS 2
#define LCD_COLS 20
#define MAXTEMP 280

#define ROASTMANUAL 0
#define ROASTTEMP 1
#define ROASTPROFILE 2

bool started = false;
bool emergency = false;
unsigned long curr_time = 0;
unsigned long stopped_time = 0;
unsigned long startup_time = 0;
short numzeros = 0; // Error tracking for temp sensor
short stage = 0;
short num_stages = 0;
double target_temp = 0.0;
double lastpot;
byte roastmode = ROASTMANUAL;
byte roastprofile = 0;
double roast_temp_fan_speed = 0.8;
//short *stages_secs;
short *stages_temp;

/* String table
 *  Use PROGMEM to store in flash
 */
const char str_you_chose[] PROGMEM = "You chose ";

const char profile_light[]  PROGMEM = "Light";
const char profile_city[]   PROGMEM = "City";
const char profile_cityp[]  PROGMEM = "City+";
const char profile_vienna[] PROGMEM = "Vienna";
const char profile_french[] PROGMEM = "French";

const char mode_manual[]  PROGMEM = "Manual";
const char mode_temp[]    PROGMEM = "Temperature";
const char mode_profile[] PROGMEM = "Profile";

const char fanspeed_70[]  PROGMEM = "70%";
const char fanspeed_80[]  PROGMEM = "80%";
const char fanspeed_90[]  PROGMEM = "90%";
const char fanspeed_100[] PROGMEM = "100%";

const char* const profile_choices[]  PROGMEM = {profile_light, profile_city, profile_cityp, profile_vienna, profile_french};
const char* const mode_choices[]     PROGMEM = {mode_manual, mode_temp, mode_profile};
const char* const fanspeed_choices[] PROGMEM = {fanspeed_70,fanspeed_80,fanspeed_90,fanspeed_100};

LiquidCrystal_SR2W lcd(SRDATAEN,SRCLK, NEGATIVE);
MAX6675 thermocouple(THERMCLK, THERMCS, THERMDO);
//SendOnlySoftwareSerial Serial(TXPIN); //tx

void mainMenu();
void profileMenu();
void tempMenu();
void doRoast();
bool loadProfile();
bool saveProfile();
int getRelativeInput(const char* prompt, double timeoutms, bool wrap, bool allownone, int numchoices, const char** choices);

/* Roast profiles
 *  Since these are all 10 minute profiles, and the timings are the same, only
 *  use one array for timings (stage_secs). May adjust later as needed. Saves RAM.
 *  
 *  Number of stages calculated based on the number of non-zero entries in
 *  *_stages_temp. 0-terminated integer array.
 */
short stages_secs[]           = {0,   160, 220, 280, 340, 400, 460, 520, 640};
//short stages_secs[]           = {0,   4, 8, 10, 15, 17, 19, 21, 25};
//short light_stages_secs[]     = {0,   160, 220, 280, 340, 400, 460, 520, 640};
short light_stages_temp[]     = {200, 204, 205, 206, 208, 0,   0,   0,   0};

//short city_stages_secs[]      = {0,   160, 220, 280, 340, 400, 460, 520, 640};
short city_stages_temp[]      = {200, 208, 212, 216, 220, 0,   0,   0,   0};

//short citypluss_stages_secs[] = {0,   160, 220, 280, 340, 400, 460, 520, 640};
short citypluss_stages_temp[] = {200, 210, 215, 220, 225, 230,   0,   0,   0};

//short vienna_stages_secs[]    = {0,   160, 220, 280, 340, 400, 460, 520, 640};
short vienna_stages_temp[]      = {200, 210, 215, 220, 227, 235, 240, 0,   0};

//short french_stages_secs[]    = {0,   160, 220, 280, 340, 400, 460, 520, 640};
short french_stages_temp[]      = {200, 210, 215, 220, 225, 230, 235, 240,   0};

void setup()
{
  pinMode(HEATERPIN, OUTPUT);
  digitalWrite(HEATERPIN, LOW);

  // Emergency fan mode - heat off, cool down beans ASAP
  lastpot = analogRead(POTPIN);
  if(lastpot >= POTMAX - 10)
  {
    roastmode = ROASTMANUAL;
    started = true;
    emergency = true;
    doRoast();
  }

  lcd.begin(LCD_COLS,LCD_ROWS);
  lcd.clear();
  lcd.setCursor(20,0);
  if(emergency)
    lcd.print(F("*Emergency *"));
  else
    lcd.print(F("*Donnypants*"));
  lcd.setCursor(20,1);
  if(emergency)
    lcd.print(F("*   Mode   *"));
  else
    lcd.print(F("* Roaster  *"));
  for (int i=0; i<16; i++)
  {
    lcd.scrollDisplayLeft();
    delay(75);
  }
  delay(BANNERMS);

  delay(500);
}

void loop()
{
  if(!started)
  {
    mainMenu();
    lcd.clear();
  
    switch(roastmode)
    {
      case ROASTPROFILE:
        profileMenu();
        break;
      case ROASTTEMP:
        tempMenu();
        break;
      case ROASTMANUAL:
        break;
    }

    lcd.clear();

    started = true;
    startup_time = millis();
  }

  while(true) doRoast();
}

void mainMenu()
{
  int pos = getRelativeInput(PSTR("Select mode: "),
    10000,
    false,
    false,
    3,
    mode_choices);
  lcd.clear();
  lcd.setCursor(0,0);
  char buff[LCD_COLS+1];
  strcpy_P(buff, str_you_chose);
  lcd.print(buff);
  strcpy_P(buff, (char*)pgm_read_word(&(mode_choices[pos-1])));
  lcd.print(buff);
  switch(pos)
  {
    case 1:
      roastmode = ROASTMANUAL;
      break;
    case 2:
      roastmode = ROASTTEMP;
      break;
    case 3:
      roastmode = ROASTPROFILE;
      break;
  }
  delay(1000);
}

void profileMenu()
{
  int pos = getRelativeInput(PSTR("Select profile: "),
    10000,
    false,
    false,
    5,
    profile_choices);
  lcd.clear();
  lcd.setCursor(0,0);
  char buff[LCD_COLS+1];
  strcpy_P(buff, str_you_chose);
  lcd.print(buff);
  strcpy_P(buff, (char*)pgm_read_word(&(profile_choices[pos-1])));
  roastprofile = pos-1;
  lcd.print(buff);
  switch(pos)
  {
    case 1:
      stages_temp = light_stages_temp;
      break;
    case 2:
      stages_temp = city_stages_temp;
      break;
    case 3:
      stages_temp = citypluss_stages_temp;
      break;
    case 4:
      stages_temp = vienna_stages_temp;
      break;
    case 5:
      stages_temp = french_stages_temp;
      break;
  }
  roastprofile = pos-1;
  delay(1000);
}

void tempMenu()
{
  int pos = getRelativeInput(PSTR("Select fan speed: "),
    10000,
    false,
    false,
    4,
    fanspeed_choices);
  lcd.clear();
  lcd.setCursor(0,0);
  char buff[LCD_COLS+1];
  strcpy_P(buff, str_you_chose);
  lcd.print(buff);
  strcpy_P(buff, (char*)pgm_read_word(&(fanspeed_choices[pos-1])));
  lcd.print(buff);
  switch(pos)
  {
    case 1:
      roast_temp_fan_speed = 0.7;
      break;
    case 2:
      roast_temp_fan_speed = 0.8;
      break;
    case 3:
      roast_temp_fan_speed = 0.9;
      break;
    case 4:
      roast_temp_fan_speed = 1;
      break;
  }
  delay(1000);
}

void doRoast()
{
  // heat (true/false), fanpwm (0-255)
  // These are set in the loop body and only acted on
  // at the end of the routine.
  bool heat = false;
  double fanpwm = 0;
  char lcdbuff[LCD_COLS];
  double potreading = 0.0;
  double tempreading = 0.0;

  curr_time = (millis()-startup_time)/1000.0;
  /* stopped_time: save the time we finished so we can display it 
   * statically while keeping time timer going so we can flash between
   * roastmode and roastprofile for nicer UI
   */
  if(started)
    stopped_time = curr_time;

  // Multiple temperature readings to reduce noise
  int temps[10];
  for(int i=0; i<10; i++)
  {
    // Thermo reading
    digitalWrite(THERMCS,HIGH);
    delay(5);
    temps[i] = thermocouple.readCelsius();
    digitalWrite(THERMCS, LOW);
    delay(5);
  }
  // Insertion sort on the array of readings
  for(int i=0; i<10; i++)
  {
    int j = i, t;
    while (j > 0 && temps[j] < temps[j-1])
    {
        t = temps[j];
        temps[j] = temps[j-1];
        temps[j-1] = t;
        j--;
    }
  }
  // Take median temperature
  tempreading = temps[7];

  // Record failed readings
  if(tempreading == 0.0)
    numzeros++;
  
  // Pot reading
  potreading += analogRead(POTPIN);
  float val = (float)(potreading-POTMIN)/(float)(POTMAX-POTMIN);
  if(val < 0) val = 0; // || curr_time < 1.0
  if(val > 1.0) val = 1.0;

  // Mode-specific heat and fan control
  if(started)
  switch(roastmode)
  {
    case ROASTPROFILE:
      // Calculate number of stages if profile
      num_stages = 0;
      while(stages_temp[num_stages] != 0)
        num_stages++;
      // Include trailing zero
      num_stages++;
    
      // Which stage are we at?
      stage = 0;
      while(stage < num_stages && curr_time >= stages_secs[stage])
        stage++;
      target_temp = stages_temp[stage-1];

      // Heater control
      if(tempreading < target_temp + 2 && tempreading != 0) // zero temp reading can mean short in probe
        heat = true;
      // Fan control
      fanpwm = val*255;

      // Turn off fan if heat is off AND we're at last stage AND temp less than 50
      if(stage == num_stages && heat == false && tempreading < 50 && tempreading != 0.0)
      {
        fanpwm = 0;
        started = false;
      }
      break;
    case ROASTMANUAL:
      // Heater control
      if(val*9 >= MINFANHEAT && !emergency)
        heat = true;
      // Fan control
      fanpwm = val*255;
      break;
    case ROASTTEMP:
      target_temp = val*MAXTEMP;
      // Heater control
      if(tempreading < target_temp + 2 && tempreading != 0) // zero temp reading can mean short in probe
        heat = true;
      // Fan control
      fanpwm = roast_temp_fan_speed*255;
      break;
  }

  // Re-init LCD (avoids garbage chars due to shared pins with thermocouple)
  lcd.begin(LCD_COLS,LCD_ROWS);
  lcd.clear();

  // Display heat/fan status
  lcd.setCursor(0,0);
  lcd.print(heat?"H":"h");
  lcd.setCursor(0,1);
  lcd.print((int)((fanpwm/255)*9));

  // Display roast mode (and profile)
  lcd.setCursor(2,0);
  strcpy_P(lcdbuff, (char*)pgm_read_word(&(mode_choices[roastmode])));
  if(roastmode == ROASTPROFILE)
    if(curr_time % 4 == 0) // Alternate Mode + profile
      strcpy_P(lcdbuff, (char*)pgm_read_word(&(profile_choices[roastprofile])));
  // Truncate the string
  if(strlen(lcdbuff) > 8)
  {
    lcdbuff[7] = '.';
    lcdbuff[8] = '\0';
  }
  lcd.print(lcdbuff);

  // Display temperature and target
  lcd.setCursor(10,0);
  lcd.print((int)tempreading,1);
  lcd.print("C");
  if(roastmode != ROASTMANUAL)
  {
    lcd.setCursor(14,0);
    lcd.print(" (");
    lcd.print((int)target_temp,1);
    lcd.print(")");
  }

  // Display profile stage
  if(roastmode == ROASTPROFILE)
  {
    lcd.setCursor(2,1);
    if(!started)
    {
      // Show status of zero temperature reads
      if(curr_time % 4 == 0)
      {
        lcd.print(F("# 0C: "));
        lcd.print(numzeros);
      }
      else
        lcd.print(F("Done!"));
    }
    else
    {
      lcd.print(F("Stage "));
      lcd.print(stage);
      lcd.print(F("/"));
      lcd.print(num_stages);
    }
  }

  // If necessary, show we're in emergency cool-down mode
  if(roastmode == ROASTMANUAL && emergency)
  {
    lcd.setCursor(2,1);
    if(curr_time % 4 == 0)
      lcd.print(F("Heater off"));
    else
      lcd.print(F("Emerg. mode"));
  }

  // Display current time
  lcd.setCursor(15,1);
  lcd.print(stopped_time);
  lcd.print(" s");
  
  // Heat and fan control
  digitalWrite(HEATERPIN, heat?HIGH:LOW);
  analogWrite(PWMPIN, fanpwm);

  // Set thermocouple CS high after LCD activity (required!)
  digitalWrite(THERMCS,HIGH);

  // Long delay to make LCD more readable (less blinking)
  delay(250);

  // This required for stable LCD
  lcd.clear();
}

int getRelativeInput(const char* prompt, double timeoutms, bool wrap, bool allownone, int numchoices, const char** choices)
{
  int res = 0;
  double remainingms = timeoutms;
  double selectedms = 0;
  lastpot = analogRead(A0);
  char buff[LCD_COLS+1];

  while(remainingms > 0)
  {
    // Display menu prompt
    lcd.clear();
    lcd.setCursor(0,0);
    strncpy_P(buff, prompt, LCD_COLS);
    lcd.print(buff);

    char *ptr = buff;
    int cursorloc = 0;
    int remaining = LCD_COLS - 3;
    int startpos = res==0?0:res-1;
    for(int i=startpos; i<numchoices && remaining > 0; i++)
    {
      int len = strlen_P((char*)pgm_read_word(&(choices[i])));
      strncpy_P(ptr, (char*)pgm_read_word(&(choices[i])), remaining);
      if(res - 1 == i)
        cursorloc = ptr - buff;
      if(i != numchoices -1)
      {
        strcat(ptr++, " ");
        remaining--;
      }
      ptr += len;
      remaining -= len;
    }

    lcd.setCursor(0,1);
    lcd.print(buff);

    lcd.setCursor(18,1);
    lcd.print((int)(remainingms/1000.0));

    if(res > 0)
    {
      // Highlight selected menu item
      lcd.cursor();
      lcd.setCursor(cursorloc,1);
    }
    
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

  lcd.noCursor();
  return res;
}
