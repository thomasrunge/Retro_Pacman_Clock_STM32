/*  Retro Pac-Man Clock
  Author: @TechKiwiGadgets Date 08/04/2017
  V5
  - introduction of optional Backlight Dimmer Code
  - fix issues with Scoreboard display
  V6
  - Introduce TEST button on Alarm screen to enable playing alarm sound
  V7
  - Fix AM/PM error at midday
  V8
  - Add Ms Pac-Man feature in Setup menu

  V.coto
  - ported to esp8266/TFT_eSPI/XPT2046
  - replaced RTC with NTP
  - using original pac-man font
  - using original images for pacman, ghosts and fruit
  - ghost and pacman can move through side doors
  - more accurate time display (no dependencies to delay()s)
  - play several wakeup tunes, including rttl pacman song (via tone())
  - read alarm and time settings on every fresh start
  - don't switch off alarm flag after alarm rings and user
    switched off
  - real "cancel" functionality on setup dialog
  - display (and toggling) current time and date on setup
    dialog when tapped in top area
  - ghost has a running animation
  - choose ghost (Blinky, Pinky, Inky, Clyde) on setup dialog
  - showing a bell icon if alarm is set which flashes on alarm
  - using defines for rows and columns, easier to port to
    bigger displays
  - improved pac-man control via touch panel: direction taps
    will be used on next section and cleared
  - control background light of display via ambient light sensor
  - some code cleanups
    coto tom@truwo.de
    2017-05-02
  - ported to STM32 2018-05-14
*/

/*
 * STM32 - ILI9341 - DS3231 - SPKR - LDR
 * PA0   -  LED
 * PA1   -----------------------------X - VCC+10kOhm to GND
 * PA2   -  RESET
 * PA3   -  D/C
 * PA4   -  CS
 * PA5   -  SCK
 * PA6   -  SDO
 * PA7   -  SDI
 * PB5   -----------------------X
 * PB6   -------------SCL - 5kOhm to VCC
 * PB7   -------------SDA - 5kOhm to VCC
 * PB12  -  T_CS
 * PB13  -  T_CLK
 * PB14  -  T_DO
 * PB15  -  T_DIN
 */

#include <SPI.h>
#include <Adafruit_ILI9341_STM.h>
#include <XPT2046_touch.h>
#include <DS3232RTC.h>
#include <EEPROM.h>
#include <itoa.h>

#include "PlayRTTTL.h"
#include "pixmaps.h"


#define TFT_CS     PA4
#define TFT_DC     PA3
#define TFT_RST    PA2
#define TOUCH_CS   PB12

#define BG_LED_PIN PA0
#define LDR_PIN    PA1
#define TONE_PIN   PA8

#define RECTROUND 4
#define BELL_BLINK 500
#define LDR_CHECK 1000
#define EEPROM_HOUR 0
#define EEPROM_MIN 1
#define EEPROM_ALARM_SET 2
#define EEPROM_MSPACMAN 3
#define EEPROM_GHOST 4
#define EEPROM_SIZE 5

typedef enum { DIR_RIGHT=0, DIR_DOWN, DIR_LEFT, DIR_UP, DIR_BEAM, DIR_NONE } t_dir;

#define ROW_1 4
#define ROW_2 46
#define ROW_DOOR 108
#define ROW_3 168
#define ROW_4 208

#define COL_1 4
#define COL_2 30
#define COL_3 62
#define COL_4 120
#define COL_FRUIT 146
#define COL_5 170
#define COL_6 228
#define COL_7 260
#define COL_8 288

#define CHAR_SIZE 28 // width and height of pacman, ghost and fruit
#define TFT_WIDTH 320
#define TFT_HEIGHT 240

#define G_BLINKY 0
#define G_PINKY  1
#define G_INKY   2
#define G_CLYDE  3
#define G_DARK   4

// Alarm Variables
boolean alarmstatus; // flag where false is off and true is on
boolean soundalarm; // Flag to indicate the alarm needs to be initiated
int alarmhour;  // hour of alarm setting
int alarmminute; // Minute of alarm setting
boolean alarm_show_bell;
unsigned long alarm_bell_blink;
unsigned long next_alarm_start; // When to play next alarm sound

typedef struct {
  const char *song; // which song
  uint16_t repeats; // how often to play this song
  uint16_t pause_r; // pause this long before next repeat (in ms)
  uint16_t pause_n; // pause this long before next song (in ms)
} alarm_songs_t;

const alarm_songs_t alarm_songs[] = {
  { s_knock,      4, 3000, 3000 },
  { s_wakawaka,   4, 0,    3000 },
  { s_wakawaka,   4, 0,    3000 },
  { s_pacman,     1, 0,    3000 },
  { s_pacinter,   1, 0,    3000 },
  { s_beep1,      3, 3000, 0    },
  { s_beep2,      3, 3000, 0    },
  { s_beep3,      3, 3000, 3000 },
  { s_urgent,     1, 1,    0    } // we always stay on last song, no repeats or pause_n required
};
uint8_t alarmsong_cnt;
int8_t alarmsong_idx;
uint8_t alarmsong_rpt_lft;


// Time Variables
// Declare global variables for previous time,  to enable refesh of only digits that have changed
// There are four digits that bneed to be drawn independently to ensure consisitent positioning of time
int c1 = -1;  // Tens hour digit
int c2 = -1;  // Ones hour digit
int c3 = -1;  // Tens minute digit
int c4 = -1;  // Ones minute digit

// Creating Objects
Adafruit_ILI9341_STM tft = Adafruit_ILI9341_STM(TFT_CS, TFT_DC, TFT_RST);

SPIClass mySPI(2); // Create an SPI instance on SPI2 port.
XPT2046_touch touch(TOUCH_CS, mySPI);

PlayRTTTL buzzer(TONE_PIN);

DS3232RTC myRTC(false);

// Touch screen coordinates
boolean screenPressed = false;
uint16_t xT, yT;

// Fruit flags
boolean fruitgone = false;
boolean fruitdrawn = false;
boolean fruiteatenpacman = false;

// Pacman & Ghost kill flags
boolean pacmanlost = false;
boolean ghostlost = false;

// Scorecard
int pacmanscore = 0;
int ghostscore = 0;
boolean score_changed = true;

int last_minute; // check if new minute, update display
unsigned long last_millis; // check for overflow after ~49 days
unsigned long next_step; // when is our next step
unsigned long next_ldr_check; // when to check ambient light
unsigned long next_ldr_change; // when to change ambient light
int current_ldr, target_ldr;

// Pacman start coordinates
int xP = COL_1;
int yP = ROW_DOOR;
int P = 0; // Pacman Graphic Flag 0 = Closed, 1 = Medium Open, 2 = Wide Open, 3 = Medium Open
t_dir D = DIR_RIGHT; // Pacman direction
t_dir WD = DIR_NONE; // requested direction (change on next chance)
t_dir prevD = DIR_NONE; // Capture legacy direction to enable adequate blanking of trail
boolean mspacman = false;  //  if this is is set to true then play the game as Ms Pac-man

// Ghost start coordinates
int xG = COL_8;
int yG = ROW_DOOR;
t_dir GD = DIR_LEFT; // Ghost direction
t_dir GWD = DIR_NONE; // always DIR_NONE!
t_dir prevGD = DIR_NONE; // Capture legacy direction to enable adequate blanking of trail
int ghost_color = G_BLINKY;


const unsigned short *pix_pacman[][4] = {
  { c_pacman, r_m_pacman, r_o_pacman, r_m_pacman },
  { c_pacman, d_m_pacman, d_o_pacman, d_m_pacman },
  { c_pacman, l_m_pacman, l_o_pacman, l_m_pacman },
  { c_pacman, u_m_pacman, u_o_pacman, u_m_pacman }
};
const unsigned short *pix_mspacman[][4] = {
  { ms_c_pacman_r, ms_r_m_pacman, ms_r_o_pacman, ms_r_m_pacman },
  { ms_c_pacman_d, ms_d_m_pacman, ms_d_o_pacman, ms_d_m_pacman },
  { ms_c_pacman_l, ms_l_m_pacman, ms_l_o_pacman, ms_l_m_pacman },
  { ms_c_pacman_u, ms_u_m_pacman, ms_u_o_pacman, ms_u_m_pacman }
};
const unsigned short *pix_ghost[][2] = {
  { ghost_r1, ghost_r2 },
  { ghost_d1, ghost_d2 },
  { ghost_l1, ghost_l2 },
  { ghost_u1, ghost_u2 }
};
const unsigned short *pix_blueghost[] = {
  blue_ghost_1, blue_ghost_2
};


void setup() {
  // while (!Serial);
  // Serial.begin(115200);
  // Serial.println("Hello Pacman!");

  last_minute = -1;
  last_millis = 0;
  next_step = 0;
  next_ldr_check = 0;
  next_ldr_change = 0;
  next_alarm_start = 0;
  alarm_bell_blink = 0;
  alarmsong_cnt = sizeof(alarm_songs) / sizeof(alarm_songs[0]);
  alarmsong_idx = -1;
  alarmsong_rpt_lft = 0;

  pinMode(LDR_PIN, INPUT_ANALOG);
  pinMode(BG_LED_PIN, PWM);
  digitalWrite(BG_LED_PIN, LOW);
  current_ldr = target_ldr = 0;

  tft.begin();
  tft.fillScreen(ILI9341_BLACK);
  tft.setRotation(3);

  touch.begin(ILI9341_TFTWIDTH, ILI9341_TFTHEIGHT);
  touch.setCalibrationPoints(109, 150, 3780, 3714);
  touch.setRotation(ROT270);

  // Read Alarm Set Time from EEPROM
  alarmhour = EEPROM.read(EEPROM_HOUR);
  if (alarmhour > 24) {
    alarmhour = 0;
  }

  alarmminute = EEPROM.read(EEPROM_MIN);
  if (alarmminute > 60) {
    alarmminute = 0;
  }

  if (EEPROM.read(EEPROM_ALARM_SET) == 0) {
    alarmstatus = false;
  } else {
    alarmstatus = true;
  }

  if (EEPROM.read(EEPROM_MSPACMAN) == 0) {
    mspacman = false;
  } else {
    mspacman = true;
  }

  ghost_color = EEPROM.read(EEPROM_GHOST);
  if (ghost_color < G_BLINKY || ghost_color > G_CLYDE) {
    ghost_color = G_BLINKY;
  }

  soundalarm = false;

  myRTC.begin();
  setSyncProvider(RTC.get);

  drawScreen(); // Initiate the game
  updateDisp(); // update value to clock

  randomSeed(analogRead(PB0));
  // Serial.println(" setup done!");
}

void loop() {
  uint16_t xy[2];

  buzzer.update();

  if (millis() < last_millis) {
    // woohoo! 49 days gone like nothing!
    next_ldr_check = 0;
    next_ldr_change = 0;
  }
  last_millis = millis();

  if (millis() > next_ldr_check) {
    handle_ldr();
    next_ldr_check = millis() + LDR_CHECK;
  }

  if (millis() > next_ldr_change) {
    if (target_ldr < current_ldr) {
      current_ldr--;
      analogWrite(BG_LED_PIN, current_ldr);
    }
    if (target_ldr > current_ldr) {
      current_ldr++;
      analogWrite(BG_LED_PIN, current_ldr);
    }
    next_ldr_change = millis() + 10;
  }

  // Read the current date and time and reset board
  if (last_minute != minute()) {
    // print time
    updateDisp(); // update value to clock then ...
    fruiteatenpacman = false; // Turn Ghost red again
    fruitdrawn = false; // If Pacman eats fruit then fruit disappears
    fruitgone = false;
    // Reset every minute both characters
    pacmanlost = false;
    ghostlost = false;
    last_minute = minute();
  }

  buzzer.update();

  // Check if Alarm needs to be sounded
  if (alarmstatus == true) {
    if (alarmhour == hour() && alarmminute == minute() && second() == 0) {
      // Sound the alarm
      soundalarm = true;
      // take care of millis()'s uint32 overflow after ~49 days
      alarm_bell_blink = 0;
      next_alarm_start = 0;
    }
  }

  // Start Alarm Sound - Sound plays once then will restart at 20 second mark
  if (soundalarm == true) {
    if (millis() > next_alarm_start && !buzzer.isPlaying()) {
      // Set off alarm
      if (alarmsong_rpt_lft == 0) {
        // no repeats left
        uint16_t pause = 0;
        if (alarmsong_idx >= 0) {
          // not the first alarm after startup
          pause = alarm_songs[alarmsong_idx].pause_n;
        }
        alarmsong_idx++; // increment song
        if (alarmsong_idx >= alarmsong_cnt) {
          alarmsong_idx = alarmsong_cnt-1; // stay on last alarm song
        }
        alarmsong_rpt_lft = alarm_songs[alarmsong_idx].repeats;
        if (alarmsong_rpt_lft == 0) {
          alarmsong_rpt_lft = 1; // be on the safe side, just in case
        }
        next_alarm_start = millis() + pause;
      } else {
        // there are repeats left, decrement, start playing (incl. pause)
        alarmsong_rpt_lft--;
        buzzer.play(alarm_songs[alarmsong_idx].song, alarm_songs[alarmsong_idx].pause_r);
      }
    } // next_alarm_start
    if (millis() > alarm_bell_blink) {
      alarm_show_bell = !alarm_show_bell;
      drawBell(alarm_show_bell);
      alarm_bell_blink = millis() + BELL_BLINK;
    } // alarm_bell_blink
  }

  buzzer.update();

  // Reset scoreboard if over 95
  if ((ghostscore >= 95) || (pacmanscore >= 95)) {
    // cleanup and repaint area
    tft.fillRect(0,   84, 32, 16, ILI9341_BLACK);
    tft.fillRect(287, 84, 32, 16, ILI9341_BLACK);
    drawScreen();
    ghostscore = 0;
    pacmanscore = 0;
    score_changed = true;
  }

  // Print scoreboard
  if (score_changed == true) {
    drawNumber(ghostscore,  (ghostscore  < 10 ? 295 : 287), 84, ILI9341_RED);
    drawNumber(pacmanscore, (pacmanscore < 10 ? 8 : 0),     84, ILI9341_YELLOW);
    score_changed = false;
  }

  buzzer.update();

  // Draw fruit
  if ((fruitdrawn == false) && (fruitgone == false)) {
    // draw fruit and set flag that fruit present so its not drawn again
    drawBitmap(COL_FRUIT, ROW_3, CHAR_SIZE, CHAR_SIZE, fruit);
    fruitdrawn = true;
  }

  buzzer.update();

  if ((fruitdrawn == true) && (fruitgone == false)) {
    // Redraw fruit if Ghost eats fruit
    if ((yG == ROW_3) && ((xG == COL_4) || (xG == COL_5))) {
      drawBitmap(COL_FRUIT, ROW_3, CHAR_SIZE, CHAR_SIZE, fruit);
    }
    // Turn Ghost Blue if Pacman eats fruit
    if ((yP == ROW_3) && (xP == COL_FRUIT)) {
      fruiteatenpacman = true; // If Pacman eats fruit then Pacman turns blue
      fruitgone = true; // If Pacman eats fruit then fruit disappears
      pacmanscore++; // Increment pacman score
      score_changed = true;
    }
  }

  buzzer.update();

  // Check if user input to touch screen
  if (touch.read_XY(xy)) {
    if (!screenPressed) {
      xT = xy[0];
      yT = xy[1];

      if ((xT >= 120) && (xT <= 200) && (yT >= 105) && (yT <= 140)) {
        if (soundalarm == false) {
          // Call Setup Routine if alarm is not sounding
          clocksetup(); // Call Clock Setup Routine
        } else {
          // If centre of screen touched while alarm sounding then turn off the sound
          soundalarm = false;
          alarmsong_idx = -1;
          drawBell(alarmstatus);
          buzzer.stop();
        }
      }
      // Capture direction request from user
      if ((xT >= 1) && (xT <= 80) && (yT >= 80) && (yT <= 160)) {
        // Request to go left
        if (D == DIR_RIGHT) {
          D = DIR_LEFT;
        } else {
          WD = DIR_LEFT;
        }
      }
      if ((xT >= 240) && (xT <= 318) && (yT >= 80) && (yT <= 160)) {
        // Request to go right
        if (D == DIR_LEFT) {
          D = DIR_RIGHT;
        } else {
          WD = DIR_RIGHT;
        }
      }
      if ((xT >= 110) && (xT <= 210) && (yT >= 1) && (yT <= 80)) {
        // Request to go Up
        if (D == DIR_DOWN) {
          D = DIR_UP;
        } else {
          WD = DIR_UP;
        }
      }
      if ((xT >= 110) && (xT <= 210) && (yT >= 160) && (yT <= 238)) {
        // Request to go Down
        if (D == DIR_UP) {
          D = DIR_DOWN;
        } else {
          WD = DIR_DOWN;
        }
      }

      screenPressed = true;
    }
  } else if (screenPressed) {
    // Doesn't allow holding the screen / you must tap it
    screenPressed = false;
  }

  if (millis() > next_step) {
    next_step = millis() + (pacmanlost ? 90 : 60);

    // increment Pacman/Ghost Graphic Flag 0 = Closed, 1 = Medium Open, 2 = Wide Open, 3 = Medium Open
    P = P + 1;
    if (P == 4) {
      P = 0; // Reset counter to closed
    }

    if (abs(xG - xP) <= 5 && abs(yG - yP) <= 5 ) {
      if (!fruiteatenpacman && !pacmanlost) {
        // Pacman Captured
        // If pacman captured then pacman disappears until reset
        // blank out Pacman
        tft.fillRect(xP, yP, CHAR_SIZE, CHAR_SIZE, ILI9341_BLACK);
        ghostscore += 2;
        pacmanlost = true;
        score_changed = true;
      }
      if (fruiteatenpacman && !ghostlost) {
        pacmanscore += 2;
        ghostlost = true;
        score_changed = true;
      }
    }

    buzzer.update();

    // draw pacman (only if he is still alive)
    if (!pacmanlost) {
      stepCharacter(xP, yP, prevD, D, WD, true);
      // Draws Pacman at these coordinates
      drawCharacter(xP, yP, prevD, false, mspacman ? pix_mspacman[D][P] : pix_pacman[D][P]);
    }

    buzzer.update();

    // draw ghost (always visible, at least as eyes-only)
    stepCharacter(xG, yG, prevGD, GD, GWD, false);
    // Draws Ghost at these coordinates
    if (fruiteatenpacman && !ghostlost) {
      drawCharacter(xG, yG, prevGD, false, pix_blueghost[P/2]);
    } else {
      drawCharacter(xG, yG, prevGD, true, pix_ghost[GD][P/2]);
    }

    buzzer.update();
  }
} // loop

void handle_ldr() {
  // Set Screen Brightness
  // Check the ambient light and adjust LED brightness to suit ambient
  int ldr = analogRead(LDR_PIN);
  // tft.fillRect(0, 0, 64, 36, ILI9341_BLACK);
  // drawNumber(ldr, 0, 0, ILI9341_GREEN);

  ldr = map(ldr, 0, 4095, 2, 255);
  // drawNumber(ldr, 0, 20, ILI9341_GREEN);

  target_ldr = ldr;
}

// Update Digital Clock
void updateDisp() {
  int h; // Hour value in 24 hour format
  int e; // Minute value in minute format

  // There are four digits that need to be drawn independently to ensure consisitent positioning of time
  int d1;  // Tens hour digit
  int d2;  // Ones hour digit
  int d3;  // Tens minute digit
  int d4;  // Ones minute digit

  h = hour(); // 24 hour RT clock value
  e = minute();

  // there is no space to a leading "2", thus we can't display
  // 20-23 o'clock, so we switch to 12-hour clock after 19:59
  if (h > 19) {
    h -= 12;
  }

  d1 = h / 10;
  d2 = h % 10;
  d3 = e / 10;
  d4 = e % 10;

  // Print each digit if it has changed to reduce screen impact/flicker

  // First Digit
  if (d1 != c1) {
    if (d1 != 0) {
      // Do not print zero in first digit position
      drawXXXLNum(d1, 12, 70, false);
    } else {
      // Clear the previous First Digit if gone from 19 to 8
      tft.fillRect(56, 74, 20, 90, ILI9341_BLACK);
    }
  }

  // Second Digit
  if (d2 != c2) {
    drawXXXLNum(d2, 70, 70, true);
  }

  // Round dots
  uint16_t dotcolor = ILI9341_WHITE;
  if(timeStatus() == timeNotSet) {
    dotcolor = ILI9341_RED;
  }
  if(timeStatus() == timeNeedsSync) {
    dotcolor = ILI9341_ORANGE;
  }
  tft.fillCircle(137, 105, 5, dotcolor);
  tft.fillCircle(137, 135, 5, dotcolor);

  // Third Digit
  if (d3 != c3) {
    drawXXXLNum(d3, 142, 70, true);
  }

  // Fourth Digit
  if (d4 != c4) {
    drawXXXLNum(d4, 202, 70, true);
  }

  // Print PM or AM
  drawString("M", 300, 148, true, ILI9341_DARKGREY);
  if (isAM()) {
    drawString("A", 292, 140, false, ILI9341_WHITE);
  } else {
    drawString("P", 292, 140, false, ILI9341_WHITE);
  }

  // Alarm Set on LHS lower pillar
  if (alarmstatus == true) {
    // show a bell on screenleft hand side
    drawBell(true);
  }

  // redraw scores
  score_changed = true;

  // copy existing time digits to global variables so that these can be used to test which digits change in future
  c1 = d1;
  c2 = d2;
  c3 = d3;
  c4 = d4;
}

// Enter Setup Mode
// Use up down arrows to change time and alrm settings
void clocksetup() {
  int8_t t_hour = hour();
  int t_minute = minute();
  int t_alarmhour = alarmhour;
  int t_alarmminute = alarmminute;
  boolean t_alarmstatus = alarmstatus;
  boolean t_mspacman = mspacman;
  int t_ghost_color = ghost_color;
  boolean savetimealarm = false;
  boolean timechanged = false;

  uint16_t color = ILI9341_YELLOW;

  // Setup Screen
  tft.fillScreen(ILI9341_BLACK);

  // Outside wall
  tft.drawRoundRect(0, 0, 319, 239, RECTROUND, color);
  tft.drawRoundRect(2, 2, 315, 235, RECTROUND, color);

  // Reset screenpressed flag
  screenPressed = false;

  // Read in current clock time and Alarm time
  // Setup buttons

  // Time- and Alarm-labels
  drawString(" TIME", 40,  60, false, color);
  drawString("ALARM", 40, 160, false, color);

  // Time Set buttons
  tft.drawRoundRect(140, 35, 36, 20, RECTROUND, color); // hour +
  drawString("+", 150, 37, false, color);
  tft.drawRoundRect(188, 35, 36, 20, RECTROUND, color); // minute +
  drawString("+", 198, 37, false, color);
  tft.drawRoundRect(140, 80, 36, 20, RECTROUND, color); // hour -
  drawString("-", 150, 81, false, color);
  tft.drawRoundRect(188, 80, 36, 20, RECTROUND, color); // minute -
  drawString("-", 198, 81, false, color);
  drawString(":", 174, 60, false, color); // separator

  // Alarm Set buttons
  tft.drawRoundRect(140, 135, 36, 20, RECTROUND, color); // alarm hour +
  drawString("+", 150, 137, false, color);
  tft.drawRoundRect(188, 135, 36, 20, RECTROUND, color); // alarm minute +
  drawString("+", 198, 137, false, color);
  tft.drawRoundRect(140, 180, 36, 20, RECTROUND, color); // alarm hour -
  drawString("-", 150, 181, false, color);
  tft.drawRoundRect(188, 180, 36, 20, RECTROUND, color); // alarm minute -
  drawString("-", 198, 181, false, color);
  drawString(":", 174, 160, false, color); // separator
  tft.drawRoundRect(235, 158, 70, 20, RECTROUND, color); // alarmstate

  drawString("SAVE",   15,  212, false, color);
  drawString("CANCEL", 209, 212, false, color);
  tft.drawRoundRect(10,  210, 75,  20, RECTROUND, color); // SAVE
  tft.drawRoundRect(206, 210, 103, 20, RECTROUND, color); // CANCEL

  // Draw Sound Button
  drawString("TEST", 52, 110, false, color);  // Triggers alarm sound
  tft.drawRoundRect(48, 108, 75, 20, 4, color); // TEST

  // get your ghosts on
  drawBitmap(280, 100, CHAR_SIZE, CHAR_SIZE, blue_ghost_1);

  // begin loop here
  while (true) {
    uint16_t xy[2];
    int last_sec = -1;

    if (t_alarmstatus == true) {
      // flag where false is off and true is on
      drawString("SET", 245, 160, true, color);
    } else {
      drawString("OFF", 245, 160, true, color);
    }

    // Display Current Alarm Setting
    drawMinutes(t_alarmhour,   142, 160, color);
    drawMinutes(t_alarmminute, 190, 160, color);

    // Display Current Time
    drawMinutes(t_hour,   142, 60, color);
    drawMinutes(t_minute, 190, 60, color);

    // Display MS Pacman or Pacman in menu - push to change
    drawBitmap(154, 208, CHAR_SIZE, CHAR_SIZE, t_mspacman ? ms_r_m_pacman : r_m_pacman);

    // Display colored ghost - push to change
    drawGhost(10, 100, CHAR_SIZE, CHAR_SIZE, ghost_r1);

    // Read input to determine if buttons pressed
    while (false == touch.read_XY(xy)) {
      char timeStr[16];

      buzzer.update();
      if  (last_sec != second()) {
        uint8_t temp = RTC.temperature() / 4;
        sprintf (timeStr, "%02d:%02d:%02d  %dC", hour(), minute(), second(), temp);
        // Display Current Time and Temperature
        drawString(timeStr, 72, 10, color);
        last_sec = second();
      }
    }
    xT = xy[0];
    yT = xy[1];
    //drawNumber(xT, 0,  0, ILI9341_GREEN);
    //drawNumber(yT, 0, 20, ILI9341_GREEN);

    // Capture input command from user
    if ((xT >= 206) && (xT <= 319) && (yT >= 210) && (yT <= 239)) {
      buzzer.tok();
      // Cancel Button
      break; // Exit setupmode
    } else if ((xT >= 0) && (xT <= 95) && (yT >= 200) && (yT <= 239)) {
      buzzer.tok();
      // Save Alarm and Time Button
      savetimealarm = true; // Exit and save time and alarm
      break; // Exit setupmode
    } else if ((xT >= 135) && (xT <= 181) && (yT >= 30) && (yT <= 60)) {
      buzzer.tok();
      // hour +
      t_hour++;
      if (t_hour >= 24) {
        // reset hour to 0 hours if 24
        t_hour = 0;
      }
      timechanged = true;
    } else if ((xT >= 135) && (xT <= 181) && (yT >= 75) && (yT <= 105)) {
      buzzer.tok();
      // hour -
      t_hour--;
      if (t_hour < 0) {
        // reset hour to 23 hours if < 0
        t_hour = 23;
      }
      timechanged = true;
    } else if ((xT >= 183) && (xT <= 229) && (yT >= 30) && (yT <= 60)) {
      buzzer.tok();
      // minute +
      t_minute++;
      if (t_minute >= 60) {
        // reset minute to 0 minutes if 60
        t_minute = 0;
      }
      timechanged = true;
    } else if ((xT >= 183) && (xT <= 229) && (yT >= 75) && (yT <= 105)) {
      buzzer.tok();
      // minute -
      t_minute--;
      if (t_minute < 0) {
        // reset minute to 59 minutes if < 0
        t_minute = 59;
      }
      timechanged = true;
    } else if ((xT >= 135) && (xT <= 181) && (yT >= 130) && (yT <= 160)) {
      buzzer.tok();
      // alarm hour +
      t_alarmhour++;
      if (t_alarmhour >= 24) {
        // reset hour to 0 hours if 24
        t_alarmhour = 0;
      }
    } else if ((xT >= 135) && (xT <= 181) && (yT >= 175) && (yT <= 205)) {
      buzzer.tok();
      // alarm hour -
      t_alarmhour--;
      if (t_alarmhour < 0) {
        // reset hour to 23 hours if < 0
        t_alarmhour = 23;
      }
    } else if ((xT >= 183) && (xT <= 229) && (yT >= 130) && (yT <= 160)) {
      buzzer.tok();
      // alarm minute +
      t_alarmminute++;
      if (t_alarmminute >= 60) {
        // reset minute to 0 minutes if 60
        t_alarmminute = 0;
      }
    } else if ((xT >= 183) && (xT <= 229) && (yT >= 175) && (yT <= 205)) {
      buzzer.tok();
      // alarm minute -
      t_alarmminute--;
      if (t_alarmminute < 0) {
        // reset minute to 59 minutes if < 0
        t_alarmminute = 59;
      }
    } else if ((xT >= 154) && (xT <= 182) && (yT >= 208) && (yT <= 236)) {
      buzzer.tok();
      // toggle Pacman code
      t_mspacman = !t_mspacman; // toggle the value
    } else if ((xT >= 10) && (xT <= 38) && (yT >= 100) && (yT <= 128)) {
      buzzer.tok();
      // toggle ghost color
      ghost_color++;
      if (ghost_color > G_CLYDE) {
        ghost_color = G_BLINKY;
      }
    } else if ((xT >= 230) && (xT <= 310) && (yT >= 153) && (yT <= 183)) {
      buzzer.tok();
      // alarm set button pushed
      t_alarmstatus = !t_alarmstatus;
    } else if ((xT >= 43) && (xT <= 128) && (yT >= 103) && (yT <= 133)) {
      // alarm test button pushed
      // Set off alarm once
      buzzer.play(s_pacman);
    }

    // Should mean changes should scroll if held down
    delay(150);
  }

  if (savetimealarm == true) {
    alarmhour = t_alarmhour;
    alarmminute = t_alarmminute;
    alarmstatus = t_alarmstatus;
    mspacman = t_mspacman;

    // Write the Alarm Time to EEPROM so it can be stored when powered off
    EEPROM.write(EEPROM_HOUR, (byte)alarmhour);
    EEPROM.write(EEPROM_MIN, (byte)alarmminute);
    EEPROM.write(EEPROM_ALARM_SET, alarmstatus ? 1 : 0);
    EEPROM.write(EEPROM_MSPACMAN, mspacman ? 1 : 0);
    EEPROM.write(EEPROM_GHOST, (byte)ghost_color);

    if (timechanged) {
      setTime(t_hour, t_minute, 0, 1, 1, 0);
      myRTC.set(now());
    }
  } else {
    ghost_color = t_ghost_color;
  }

  // Clear Screen
  tft.fillScreen(ILI9341_BLACK);
  buzzer.stop();
  // Initiate the screen
  drawScreen();
  c1 = c2 = c3 = c4 = -1;
  // update value to clock
  updateDisp();
}

// initiateGame - Custom Function
void drawScreen() {
  // Draw Background lines
  int16_t x, y, w, h;
  int16_t x1, x2, x3, x4, x5, x6, x7, x8;
  int16_t y1, y2, y3, y4;

  uint16_t color = tft.color565(33, 33, 222);

  x1 = COL_1 + CHAR_SIZE + 2;
  x2 = COL_3 - 2;
  x3 = COL_3 + CHAR_SIZE + 2;
  x4 = COL_4 - 2;
  x5 = COL_5 + CHAR_SIZE + 2;
  x6 = COL_6 - 2;
  x7 = COL_6 + CHAR_SIZE + 2;
  x8 = COL_8 - 2;
  y1 = ROW_1 + CHAR_SIZE + 2;
  y2 = ROW_2 - 2;
  y3 = ROW_3 + CHAR_SIZE + 2;
  y4 = ROW_4 - 2;
  x = COL_4 + CHAR_SIZE + 2;
  w = COL_5 - 2 - x;

  // Outside wall
  drawDoubleRect(0, 0, TFT_WIDTH, TFT_HEIGHT, color);

  // Four top spacers and wall pillar
  drawDoubleRect(x1, y1, x2 - x1, y2 - y1, color);
  drawDoubleRect(x3, y1, x4 - x3, y2 - y1, color);
  drawDoubleRect(x5, y1, x6 - x5, y2 - y1, color);
  drawDoubleRect(x7, y1, x8 - x7, y2 - y1, color);
  y = 0;
  h = y2;
  drawDoubleRect(x, y, w, h, color);

  // Four bottom spacers and wall pillar
  drawDoubleRect(x1, y3, x2 - x1, y4 - y3, color);
  drawDoubleRect(x3, y3, x4 - x3, y4 - y3, color);
  drawDoubleRect(x5, y3, x6 - x5, y4 - y3, color);
  drawDoubleRect(x7, y3, x8 - x7, y4 - y3, color);
  y = y3;
  h = TFT_HEIGHT - y3;
  drawDoubleRect(x, y, w, h, color);

  // Clear lines on Outside wall
  tft.fillRect(0,           ROW_DOOR, 3, CHAR_SIZE, ILI9341_BLACK);
  tft.fillRect(TFT_WIDTH-3, ROW_DOOR, 3, CHAR_SIZE, ILI9341_BLACK);

  // Four Door Pillars
  x = 0;
  y = ROW_2 + CHAR_SIZE;
  w = COL_2 - x;
  h = ROW_DOOR - y;
  drawDoubleRect(x, y, w, h, color);

  y = ROW_DOOR + CHAR_SIZE;
  h = ROW_3 - y;
  drawDoubleRect(x, y, w, h, color);

  x = COL_7 + CHAR_SIZE;
  y = ROW_2 + CHAR_SIZE;
  w = TFT_WIDTH - x;
  h = ROW_DOOR - y;
  drawDoubleRect(x, y, w, h, color);

  y = ROW_DOOR + CHAR_SIZE;
  h = ROW_3 - y;
  drawDoubleRect(x, y, w, h, color);

  score_changed = true;
}

void drawBell(boolean showit) {
  if (true == showit) {
    drawBitmap(4, ROW_DOOR + CHAR_SIZE + 4, 22, 22, bell);
  } else {
    tft.fillRect(4, ROW_DOOR + CHAR_SIZE + 4, 22, 22, ILI9341_BLACK);
  }
}

void drawDoubleRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  tft.drawRoundRect(x,   y,   w,   h,   RECTROUND, color);
  tft.drawRoundRect(x+2, y+2, w-4, h-4, RECTROUND, color);
}

void drawCharacter(int x, int y, t_dir pd, boolean is_ghost, const unsigned short *pixmap) {
  // Clear trail off graphic before printing new position
  switch (pd) {
    case DIR_RIGHT:
      tft.drawRect(x-2, y, 2, CHAR_SIZE, ILI9341_BLACK);
      break;
    case DIR_DOWN:
      tft.drawRect(x, y-2, CHAR_SIZE, 2, ILI9341_BLACK);
      break;
    case DIR_LEFT:
      tft.drawRect(x+CHAR_SIZE, y, 2, CHAR_SIZE, ILI9341_BLACK);
      break;
    case DIR_UP:
      tft.drawRect(x, y+CHAR_SIZE, CHAR_SIZE, 2, ILI9341_BLACK);
      break;
    case DIR_BEAM:
      if (x == COL_1) {
        tft.fillRect(COL_8, y, CHAR_SIZE, CHAR_SIZE, ILI9341_BLACK);
      }
      if (x == COL_8) {
        tft.fillRect(COL_1, y, CHAR_SIZE, CHAR_SIZE, ILI9341_BLACK);
      }
      break;
    case DIR_NONE:
      // nothing to clear after startup
      break;
  }
  if (true == is_ghost) {
    drawGhost(x, y, CHAR_SIZE, CHAR_SIZE, pixmap);
  } else {
    drawBitmap(x, y, CHAR_SIZE, CHAR_SIZE, pixmap);
  }
}

void drawGhost(int16_t x, int16_t y, int16_t w, int16_t h, const unsigned short *data) {
  uint32_t color = 0xF800; // G_BLINKY
  if (ghost_color == G_PINKY) color = ILI9341_PINK;
  if (ghost_color == G_INKY)  color = ILI9341_CYAN;
  if (ghost_color == G_CLYDE) color = ILI9341_ORANGE;
  if (true == ghostlost)      color = ILI9341_BLACK;

  tft.setAddrWindow(x, y, x + w - 1, y + h - 1);
  for (int i = 0; i < w*h; i++) {
    uint16_t col = pgm_read_word(data++);
    if (col == 0xF800) {
      col = color;
    }
    tft.pushColor(col);
  }
}

void drawBitmap(int16_t x, int16_t y, int16_t w, int16_t h, const unsigned short *data) {
  tft.setAddrWindow(x, y, x + w - 1, y + h - 1);
  for (int i = 0; i < w*h; i++) {
    tft.pushColor(pgm_read_word(data++));
  }
}

void drawChar(uint8_t index, int16_t x, int16_t y, uint32_t fg, uint32_t bg,
            uint8_t font_width, uint8_t font_height, boolean clear_bg,
            const unsigned char *font_data) {
  int16_t temp = index*((font_width/8)*font_height);

  for(int j = 0; j < font_height; j++) {
    if (true == clear_bg) {
      tft.drawFastHLine(x, y+j, font_width, bg);
    }
    for (int zz=0; zz < (font_width / 8); zz++) {
      char ch = pgm_read_byte(&font_data[temp+zz]);
      for(int i = 0; i < 8; i++) {
        if((ch & (1<<(7-i))) != 0) {
          tft.drawPixel(x+i+(zz*8), y+j, fg);
        }
      }
    }
    temp+=font_width/8;
  }
}

void drawXXXLNum(int16_t num, int16_t x, int16_t y, boolean clear_bg) {
  drawChar(num, x, y, ILI9341_WHITE, ILI9341_BLACK, 64, 100, clear_bg, SevenSeg_XXXL_Num+4);
}

void drawCentreString(const char *s, int16_t y, boolean clear_bg, uint32_t color) {
  int16_t len = strlen(s);
  int16_t x = (TFT_WIDTH - 16*len)/2;
  drawString(s, x, y, clear_bg, color);
}

void drawCentreString(const String& s, int16_t y, boolean clear_bg, uint32_t color) {
  int16_t len = s.length();
  int16_t x = (TFT_WIDTH - 16*len)/2;
  drawString(s, x, y, color);
}

void drawString(const char *s, int16_t x, int16_t y, boolean clear_bg, uint32_t color) {
  int16_t len = strlen(s);
  for (int16_t i = 0; i < len; i++) {
    uint8_t index = 16; // space
    if (s[i] >= '0' and s[i] <= '9') {
      index = 0  + s[i] - '0';
    } else if (s[i] >= 'A' and s[i] <= 'Z') {
      index = 17 + s[i] - 'A';
    } else if (s[i] >= 'a' and s[i] <= 'z') {
      index = 17 + s[i] - 'a';
    } else if (s[i] == '.') {
      index = 10;
    } else if (s[i] == '/') {
      index = 11;
    } else if (s[i] == '-') {
      index = 12;
    } else if (s[i] == '+') {
      index = 13;
    } else if (s[i] == '!') {
      index = 14;
    } else if (s[i] == ':') {
      index = 15;
    }
    drawChar(index, x + 16 * i, y, color, ILI9341_BLACK, 16, 16, clear_bg, pacman_font);
  }
}

void drawString(const String& string, int16_t x, int16_t y, uint32_t color) {
  int16_t len = string.length() + 2;
  char buffer[len];
  string.toCharArray(buffer, len);
  drawString(buffer, x, y, true, color);
}

void drawNumber(int16_t num, int16_t x, int16_t y, uint32_t color) {
  char buf[12];
  itoa(num, buf, 10);
  drawString(buf, x, y, true, color);
}

void drawMinutes(uint8_t num, int16_t x, int16_t y, uint32_t color) {
  if (num >= 10) {
    drawNumber(num, x, y, color);
  } else {
    char str[3];
    str[0] = '0';
    str[1] = num+48;
    str[2] = '\0';
    drawString(str, x, y, true, color);
  }
}

t_dir get_dir(t_dir opt1, t_dir opt2, t_dir &wdir, boolean is_pacman, boolean rand2) {

  // only pacman has rand4
  if (!is_pacman) rand2 = true;

  t_dir dir = (random(rand2 ? 2 : 4) == 1 ? opt1 : opt2);

  if (true == is_pacman) {
    if (wdir == opt1) dir = opt1;
    if (wdir == opt2) dir = opt2;
    wdir = DIR_NONE;
  }

  return dir;
}

void stepCharacter(int &x, int &y, t_dir &pdir, t_dir &dir, t_dir &wdir, boolean is_pacman) {
  // Pacman/Ghost wandering Algorithm
  // Note: Keep horizontal and vertical coordinates even numbers only to accomodate increment rate and starting point

  // Capture legacy direction to enable adequate blanking of trail
  pdir = dir;

  if (dir == DIR_RIGHT) {
    x = x + 2;

    if (y == ROW_1) {
      if (x == COL_3) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_4) dir = DIR_DOWN;
      if (x == COL_6) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_8) dir = DIR_DOWN;
    }

    if (y == ROW_2) {
      if (x == COL_2) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_3) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_4) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_5) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_6) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_7) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_8) dir = DIR_UP;
    }

    if (y == ROW_DOOR) {
      if (x == COL_2) dir = get_dir(DIR_DOWN, DIR_UP, wdir, is_pacman, true);
      // Beam me to the other side
      if (x == COL_8+2) {
        x = COL_1;
        pdir = DIR_BEAM;
      }
    }
    if (y == ROW_3) {
      if (x == COL_2) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_3) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_4) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_5) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_6) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_7) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_8) dir = DIR_DOWN;
   }
    if (y == ROW_4) {
      if (x == COL_3) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_4) dir = DIR_UP;
      if (x == COL_6) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_8) dir = DIR_UP;
    }
  } else if (dir == DIR_LEFT) {
    x = x - 2;

    if (y == ROW_1) {
      if (x == COL_1) dir = DIR_DOWN;
      if (x == COL_3) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_5) dir = DIR_DOWN;
      if (x == COL_6) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
    }
    if (y == ROW_2) {
      if (x == COL_1) dir = DIR_UP;
      if (x == COL_2) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_3) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_4) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_5) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_6) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_7) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
    }
    if (y == ROW_DOOR) {
      // Beam me to the other side
      if (x == COL_1-2) {
        x = COL_8;
        pdir = DIR_BEAM;
      }
      if (x == COL_7) dir = get_dir(DIR_DOWN, DIR_UP, wdir, is_pacman, true);
    }
    if (y == ROW_3) {
      if (x == COL_1) dir = DIR_DOWN;
      if (x == COL_2) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_3) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_4) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_5) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_6) dir = get_dir(dir, DIR_DOWN, wdir, is_pacman, true);
      if (x == COL_7) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
    }
    if (y == ROW_4) {
      if (x == COL_1) dir = DIR_UP;
      if (x == COL_3) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
      if (x == COL_5) dir = DIR_UP;
      if (x == COL_6) dir = get_dir(dir, DIR_UP,   wdir, is_pacman, false);
    }
  } else if (dir == DIR_DOWN) {
    y = y + 2;

    if (x == COL_1) {
      if (y == ROW_2) dir = DIR_RIGHT;
      if (y == ROW_4) dir = DIR_RIGHT;
    }
    if (x == COL_2) {
      if (y == ROW_DOOR) dir = get_dir(dir,      DIR_LEFT,  wdir, is_pacman, true);
      if (y == ROW_3)    dir = get_dir(DIR_LEFT, DIR_RIGHT, wdir, is_pacman, true);
    }
    if (x == COL_3) {
      if (y == ROW_2) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
      if (y == ROW_4) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
    }
    if (x == COL_4) {
      if (y == ROW_2) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
      if (y == ROW_4) dir = DIR_LEFT;
    }
    if (x == COL_5) {
      if (y == ROW_2) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
      if (y == ROW_4) dir = DIR_RIGHT;
    }
    if (x == COL_6) {
      if (y == ROW_2) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
      if (y == ROW_4) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
    }
    if (x == COL_7) {
      if (y == ROW_DOOR) dir = get_dir(dir,      DIR_RIGHT, wdir, is_pacman, true);
      if (y == ROW_3)    dir = get_dir(DIR_LEFT, DIR_RIGHT, wdir, is_pacman, true);
    }
    if (x == COL_8) {
      if (y == ROW_2) dir = DIR_LEFT;
      if (y == ROW_4) dir = DIR_LEFT;
    }
  } else if (dir == DIR_UP) {
    y = y - 2;

    if (x == COL_1) {
      if (y == ROW_1) dir = DIR_RIGHT;
      if (y == ROW_3) dir = DIR_RIGHT;
    }
    if (x == COL_2) {
      if (y == ROW_2)    dir = get_dir(DIR_LEFT, DIR_RIGHT, wdir, is_pacman, true);
      if (y == ROW_DOOR) dir = get_dir(dir,      DIR_LEFT,  wdir, is_pacman, true);
    }
    if (x == COL_3) {
      if (y == ROW_1) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
      if (y == ROW_3) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
    }
    if (x == COL_4) {
      if (y == ROW_1) dir = DIR_LEFT;
      if (y == ROW_3) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
    }
    if (x == COL_5) {
      if (y == ROW_1) dir = DIR_RIGHT;
      if (y == ROW_3) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
    }
    if (x == COL_6) {
      if (y == ROW_1) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
      if (y == ROW_3) dir = get_dir(DIR_LEFT,    DIR_RIGHT, wdir, is_pacman, true);
    }
    if (x == COL_7) {
      if (y == ROW_2)    dir = get_dir(DIR_LEFT, DIR_RIGHT, wdir, is_pacman, true);
      if (y == ROW_DOOR) dir = get_dir(dir,      DIR_RIGHT, wdir, is_pacman, true);
    }
    if (x == COL_8) {
      if (y == ROW_1) dir = DIR_LEFT;
      if (y == ROW_3) dir = DIR_LEFT;
    }
  }
} // stepCharacter
