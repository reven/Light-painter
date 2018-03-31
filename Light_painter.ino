
/*
 * Light painter
 * 
 * A sketch that drives a LED strip to create persistance of vision (POV) images
 * from 24-bit bitmap files, that can be used to paint images in long exposure photography.
 * Files are selectable through and LCD menu through witch brightness, speed and delay can
 * also be adjusted. Settings can be saved to EEPROM.
 * 
 * Requires: Arduino, LED strip, SD module, LCD (this uses I2C), momentary push buttons, UBEC
 *           or similar power supply.
 * 
 * Menu navigation: Up, down, select/trigger
 *     - file select
 *     - brightness
 *     - speed
 *     - delay
 *     - save to EEPROM
 * 
 * v1.0 - 14-FEB-18
 * 
 * Files re
 * 
 * © 2018 Robert Sanchez
 * Files released under the terms of a BSD license. Please see included LICENSE file. 
 * 
 * Based on Adafruit Neopixel Light Painter Sketch. Please see README for credits and instructions.
 * 
 */

// DEBUG MODE ---------------------------------------------------------------
//#define DEBUG           // Still not operational, but this compiler flag will turn on debug mode,
                          // which activates Serial and ignores LCD. This needs some work.

// INCLUDES -----------------------------------------------------------------
#include <SdFat.h>
#include <EEPROM.h>
#include "./gamma.h"
#include <LiquidCrystal_I2C.h>
#include "./chars.h"
#include "./menu.h"


// CONFIGURABLE STUFF --------------------------------------------------------

#define N_LEDS       144 // Max value is 170 (fits one SD card block)
#define CARD_SELECT   10 // SD card select pin (some shields use #4, not 10)
#define LCD_COLS      16 // Columns of LCD screen NOTE: Code optimized for 1602 lcd
#define LCD_ROWS       2 // Rows of LCD screen
#define LCD_ADDRESS 0x3F // I2C address of LCD screen --> Yours might differ
#define LED_PIN        6 // NeoPixels connect here
#define TRIGGER       A1 // Playback trigger pin
#define SEL_DN        A2 // Down pin
#define SEL_UP        A3 // Up pin
//#define CONSISTENT     // Scale all images to the same relative brightness
// levels. This can be useful for having frames of different brightness all
// be processed to have consistent levels, very useful for frame painting.
// Otherwise, if = 0, each image brightness will be the maximum that's safe
// for *that* image (and will relatively bump up dim images). 
#define CURRENT_MAX 3500 // Max current from power supply (mA)
// The software does its best to limit the LED brightness to a level that's
// manageable by the power supply.  144 NeoPixels at full brightness can
// draw about 10 Amps(!), while the UBEC (buck converter) sold by Adafruit
// can provide up to 3A continuous, or up to 5A for VERY BRIEF intervals.
// The default CURRENT_MAX setting is a little above the continuous value,
// figuring light painting tends to run for short periods and/or that not
// every scanline will demand equal current.  For extremely long or bright
// images or longer strips, this may exceed the UBEC's capabilities, in
// which case it shuts down (will need to disconnect battery).  If you
// encounter this situation, set CURRENT_MAX to 3000.  Alternately, a more
// powerful UBEC can be substituted (RC hobby shops may have these),
// setting CURRENT_MAX to suit.
#define CORRECTION_FACTOR 1.4 // Adjustment ratio to output power.
// See README. This is a *dangerous* setting. Leave at 1 in case of doubt.

#define NEO_RED        0 // Component order of the LED strip. Mine is RGB
#define NEO_GREEN      1 // (others vary, for example Neopixels are GRB)
#define NEO_BLUE       2

// NON-CONFIGURABLE STUFF ----------------------------------------------------

#define OVERHEAD     150 // Extra microseconds for loop processing, etc.
#define BMP_BLUE       0 // Component order of BMP files. BMP are BGR
#define BMP_GREEN      1
#define BMP_RED        2

uint8_t           sdBuf[512],      // One SD block (also for NeoPixel color data)
                  pinMask,         // NeoPixel pin bitmask
                  fileIndex[30],   // Array of file indexes. Could increase to 60. Test.
                  selected,        // A flag for tracking selected menu option
                  menuState,       // Current page of menu to display
                  nFrames = 0,     // Total # of image files
                  frame   = 0;     // Current image # being painted
uint16_t          maxLPS;          // Max playback lines/sec
uint32_t          firstBlock,      // First block # in temp working file
                  nBlocks;         // Number of blocks in file
SdFat             sd;              // SD filesystem
volatile uint8_t *port;            // NeoPixel PORT register

// Functions defined here to be nice to compiler
void bmpProcess(char *inName, char *outName, uint8_t *brightness);
void getFileName(char* fileName, uint8_t index, uint8_t ext = 0); 

// Default settings. These are overwritten by EEPROM saved values if they exist
uint8_t   SetBrightness = 125,   // Adjustable brightness
          Speed = 125,           // Adjustable speed
          Delay = 0;             // Adjustable initial delay

// Initialize LCD screen instance
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);


// INITIALIZATION ------------------------------------------------------------

void setup() {
  // Conditional to DEBUG flag should go here
  // Serial.begin(57600);

  digitalWrite(TRIGGER, HIGH);           // Enable pullup on trigger button
  digitalWrite(SEL_UP, HIGH);            // Enable pullup on up button
  digitalWrite(SEL_DN, HIGH);            // Enable pullup on down button
  pinMode(LED_PIN, OUTPUT);              // Enable NeoPixel output
  digitalWrite(LED_PIN, LOW);            // Default logic state = low
  port    = portOutputRegister(digitalPinToPort(LED_PIN));
  pinMask = digitalPinToBitMask(LED_PIN);
  memset(sdBuf, 0, N_LEDS * 3);          // Clear LED buffer
  show();                                // Init LEDs to 'off' state
  
  // Load config from EEPROM
  loadConfig();

  // setup LCD
  lcd.init();
  lcd.backlight();
  setup_chars();
  lcd.clear();
  lcd.print(F("*Light painter*"));
  lcd.setCursor(0,1);
  lcd.print(F(" Initializing..."));

  // Init SD card
  if(!sd.begin(CARD_SELECT, SPI_FULL_SPEED)) {
    error(F("Failed! :( Check"), F("card or config.."));
  }
  if (sd.vol()->fatType() == 0) {
    error(F("No FAT partition"), F("Verify format"));
  }

  lcd.setCursor(0,1);
  lcd.print(F("   "));
  lcd.write(7);
  lcd.print(F("Ready   "));
  lcd.write(126);
  lcd.print(F("OK "));
  while (digitalRead(TRIGGER) == HIGH) { } // wait for button press
  
  // Scan or rescan card. Ask User
  lcd.clear();
  lcd.print(F("Rescan SD card?"));
  lcd.setCursor(0,1);
  printMenuStr(6);
  lcd.setCursor(0,1);
  lcd.write(4);
  lcd.setCursor(9,1);
  lcd.write(0);

  // Wait for user input
  while(digitalRead(SEL_UP) == HIGH && digitalRead(SEL_DN) == HIGH);

  // Complete scan
  if (digitalRead(SEL_UP) == LOW) {
    while(digitalRead(SEL_UP) == LOW); // Wait for button release
    completeScan();    

  // Keep current data  
  } else if (digitalRead(SEL_DN) == LOW) { // We don't want a full scan, use existing data.
    while(digitalRead(SEL_DN) == LOW); // Wait for button release
    shortScan(); 

  } // end Scan test

  // Check our max speed with current files
  checkSpeed();
  if(maxLPS > 400) maxLPS = 400; // NeoPixel PWM rate is ~400 Hz
  // Serial.println(maxLPS);

  // Set up Timer1 for 64:1 prescale (250 KHz clock source),
  // fast PWM mode, no PWM out.
  TCCR1A = _BV(WGM11) | _BV(WGM10);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11) | _BV(CS10);
  // Timer1 interrupt is not used; instead, overflow bit is tightly
  // polled.  Very infrequently a block read may inexplicably take longer
  // than normal...using an interrupt, the code would clobber itself.
  // By polling instead, worst case is just a minor glitch where the
  // corresponding row will be a little thicker than normal.

  // Here we would disable Timer0 interrupt, but I've moved it to the
  // trigger() function to be able to use delays and milis().
  //TIMSK0 = 0;

  // We need to preload the image the first time.
  preload();

}

// MENU & PLAYBACK LOOP -------------------------------------------------------------

void loop() {
  static uint8_t  inputOption;      // The user received input

  // Display the menu
  menuDisplay(menuState);
  inputOption = getInput();

  // Action on user input. This can probably be abstracted or done programatically, but this works for now.
  switch(menuState){
    case 0: // initial screen
    if (selected == 1 && inputOption == 2) {
      selected = 0;
      break;
    }
    if (selected == 1 && inputOption == 3) {
      menuState = 1;
      selected = 2;
      break;
    }
    if (selected == 0 && inputOption == 1) {
      selected = 1;
      break;
    }
    if (selected == 0 && inputOption == 3) trigger(); // Show the frame
    break;

    case 1: // main menu screen 1
    if (inputOption == 2 && selected == 2) {
      selected = 3;
      break;
    }
    if (inputOption == 2 && selected == 3) {
      menuState = 2;
      selected = 4;
      break;
    }
    if (inputOption == 1 && selected == 3) {
      selected = 2;
      break;
    }
    if (inputOption == 3 && selected == 2) {
      menuState = 8;
      selected = 0;
    }
    if (inputOption == 3 && selected == 3) {
      menuState = 5;
    }
    break;

    case 2: // main menu screen 2
    if (inputOption == 2 && selected == 3) {
      selected = 4;
      break;
    }
    if (inputOption == 2 && selected == 4) {
      menuState = 3;
      selected = 5;
      break;
    }
    if (inputOption == 1 && selected == 4) {
      selected = 3;
      break;
    }
    if (inputOption == 1 && selected == 3) {
      menuState = 1;
      selected = 2;
      break;
    }
    if (inputOption == 3 && selected == 3) {
      menuState = 5;
    }
    if (inputOption == 3 && selected == 4) {
      menuState = 6;
    }
    break;

    case 3: // main menu screen 3
    if (inputOption == 2 && selected == 4) {
      selected = 5;
      break;
    }
    if (inputOption == 2 && selected == 5) {
      menuState = 4;
      selected = 6;
      break;
    }
    if (inputOption == 1 && selected == 5) {
      selected = 4;
      break;
    }
    if (inputOption == 1 && selected == 4) {
      menuState = 2;
      selected = 3;
      break;
    }
    if (inputOption == 3 && selected == 4) {
      menuState = 6;
    }
    if (inputOption == 3 && selected == 5) {
      menuState = 7;
    }
    break;

    case 4: // main menu screen 4
    if (inputOption == 2 && selected == 5) {
      selected = 6;
      break;
    }
    if (inputOption == 1 && selected == 6) {
      selected = 5;
      break;
    }
    if (inputOption == 1 && selected == 5) {
      menuState = 3;
      selected = 4;
      break;
    }
    if (inputOption == 3 && selected == 5) {
      menuState = 7;
      break;
    }
    if (inputOption == 3 && selected == 6) {
      saveConfig(); // save to EEPROM
      menuState = 0;
      selected = 1;
      lcd.clear();
      printMenuStr(9);
      lcd.setCursor(0,0);
      lcd.write(7);
      while(digitalRead(TRIGGER) == HIGH); // Wait for button release
      while(digitalRead(TRIGGER) == LOW);
    }
    break;

    case 5: // Brightness setting
    if (inputOption == 1 && SetBrightness != 255) SetBrightness++;
    if (inputOption == 4 && SetBrightness < 250) SetBrightness += 5;
    if (inputOption == 2 && SetBrightness != 0) SetBrightness--;
    if (inputOption == 5 && SetBrightness > 5) SetBrightness -= 5;
    if (inputOption == 3) {
      // brightness change requires a rescan.
      lcd.clear();
      printMenuStr(7);
      lcd.setCursor(0,1);
      printMenuStr(8);
      while(digitalRead(TRIGGER) == HIGH); // Wait for button release
      while(digitalRead(TRIGGER) == LOW);
      completeScan(); 
      checkSpeed();
      if(maxLPS > 400) maxLPS = 400;
      preload(); // We've changed file so preload() again.
      // Brightness has to be saved because saved files and saved settings have to be in sync
      saveBrightness();
      menuState = 0;
      selected = 1;
    }
    break;

    case 6: // Speed setting
    if (inputOption == 1 && Speed != 255) Speed++;
    if (inputOption == 4 && Speed < 250) Speed += 5;
    if (inputOption == 2 && Speed != 0) Speed--;
    if (inputOption == 5 && Speed > 5) Speed -= 5;
    if (inputOption == 3) {
      menuState = 0;
      selected = 1;
    }
    break;

    case 7: // Delay setting
    if (inputOption == 1 && Delay != 255) Delay++;
    if (inputOption == 4 && Delay < 250) Delay += 5;
    if (inputOption == 2 && Delay != 0) Delay--;
    if (inputOption == 5 && Delay > 5) Delay -= 5;
    if (inputOption == 3) {
      menuState = 0;
      selected = 1;
    }
    break;

    case 8: // Choose file
    if (inputOption == 1 && selected == 0){
      if (frame == 0){
        frame = nFrames - 1;
      }else{
        frame--;
      }
      break;
    }
    if (inputOption == 1 && selected == 1){
      if (frame == 0){
        frame = nFrames - 1;
      }else{
        frame--;
      }
      selected = 0;
      break;     
    }
    if (inputOption == 2 && selected == 0){
      if (frame == nFrames - 1){
        frame = 0;
      }else{
        frame++;
      }
      selected = 1;
      break;
    }
    if (inputOption == 2 && selected == 1){
      if (frame == nFrames - 1){
        frame = 0;
      }else{
        frame++;
      }
      break;
    }
    if (inputOption == 3) { // file chosen
      menuState = 0;
      selected = 0;
      preload();
    }
    break;

  }

} // end loop


/// FUNCTIONS ================================================================
//     - SD SCANNING
//     - MENU INTERFACE
//     - PAINTING FUNCTION
//     - BMP->NEOPIXEL FILE CONVERSION
//     - LCD CUSTOM CHARACTERS SETUP
//     - MISC UTILITY
//     - EEPROM HANDLING
//     - NEOPIXEL FUNCTIONS


// SD SCANNING ---------------------------------------------------------------

// Scans images on SD, creates the index and generates the raw files.
// If CONSISTENT = 1: performs two passes over images. The first counts files, stores
// names and estimates the max (safe) brightness level scaled to the brightest image.
// If CONSISTENT = 0: One pass, and each image is at it's max brightness level.
void completeScan(){
  uint8_t  b, minBrightness;
  char     infile[18], outfile[18];
  uint8_t  i;
  SdFile   tmp;

  lcd.clear();
  lcd.print(F("  ..Scanning.."));
  lcd.setCursor(0,0);
  lcd.write(6);
  
  minBrightness = 255;

  // If CONSISTENT is NOT defined, then we don't need to poll the images to get minBrightness,
  // we can just calculate it from the user seting.
#ifndef CONSISTENT
  // Adjust brightness with user set value between 1 and estimated safe max
  minBrightness = map(SetBrightness, 0, 255, 1, minBrightness);
#endif

  // 1st pass. Scan the SD and count all the bmp files. Populate fileIndex. Check brightness
  // Set back nFrames to 0 in case this is a rescan
  nFrames = 0;
  sd.vwd()->rewind();
  while (tmp.openNext(sd.vwd(), O_READ)) {
    if (!tmp.isSubDir() && !tmp.isHidden() ) {
      tmp.getName(infile, 18);
      if (strstr(infile,"bmp") > 0) {
        fileIndex[nFrames] = tmp.dirIndex();
        
#ifdef CONSISTENT
        b = 255; // Assume frame at full brightness to start. bmpProcess will return safe limit for *this* image.
        bmpProcess(infile, NULL, &b);
#else
        // Only 1 pass: set b to minBrightness on each pass and get outfile.
        b = minBrightness;
        getFileName(infile, i, 0);
        // call to bmpProcess
        bmpProcess(infile, outfile, &b);
#endif
        nFrames++;
        if(b < minBrightness) minBrightness = b; // This doesn't do anything unless CONSISTENT 
      }
    }
    tmp.close();
  }

#ifdef CONSISTENT
  // Adjust brightness with user set value between 1 and estimated safe max
  minBrightness = map(SetBrightness, 0, 255, 1, minBrightness);

  // Second pass now applies brightness adjustment while converting
  // the image(s) from BMP to a raw representation of NeoPixel data
  // (this outputs the file(s) '{filename}.raw' -- any existing file
  // by that name will simply be clobbered, IT DOES NOT ASK).
  lcd.setCursor(2,0);
  lcd.print(F("..Creating.."));
  for (i = 0; i<nFrames; i++) {
    b = minBrightness;  // Reset b to safe limit on each loop iteration
    getFileName(infile, i, 0);
    getFileName(outfile, i, 1);
    // call to bmpProcess
    bmpProcess(infile, outfile, &b);
  }
#endif  

}

// Does a quick scan to get number of pictures (nFrames) and populates fileIndex.
// Makes sure raw files are contiguous before continuing.
void shortScan() {
  char     infile[18], outfile[18];
  uint8_t  i; 
  uint32_t lastBlock;
  SdFile   tmp;

  lcd.clear();
  lcd.print(F(" Checking files."));
  lcd.setCursor(0,0),
  lcd.write(6);

  // explore SD to get files
  sd.vwd()->rewind();
  while (tmp.openNext(sd.vwd(), O_READ)) {
    if (!tmp.isSubDir() && !tmp.isHidden() ) {
      tmp.getName(infile, 18);
      if (strstr(infile,"bmp") > 0) {
        fileIndex[nFrames] = tmp.dirIndex();
        nFrames++;
      }
    }
    tmp.close();
  }
}

// Prepare for playback from file; make a full read pass through the
// file to estimate block read time (+5% margin) and max playback
// lines/sec.  Not all SD cards perform the same.  This makes sure a
// reasonable speed limit is used.
void checkSpeed(){
  char     outfile[18];
  uint8_t  i;
  uint16_t n; 
  uint32_t lastBlock;
  SdFile   tmp;
  
  for (i = 0; i<nFrames; i++) {                           // Iterate over indexed files
    getFileName(outfile, i, 1);                           // get raw file name
    if(tmp.open(outfile, O_RDONLY)) {                     // open raw file
      if(tmp.contiguousRange(&firstBlock, &lastBlock)) {  // check that it's contiguous
        nBlocks = tmp.fileSize() / 512;
        tmp.close();
        n = (uint16_t)(1000000L /                         // 1 uSec /
        (((benchmark(firstBlock, nBlocks) * 21) / 20) +   // time + 5% +
        (N_LEDS * 30L) + OVERHEAD));                      // 30 uSec/pixel
        if(n > maxLPS) maxLPS = n;
      } else { error(F("raw file error"),F("re-scan needed!"));}
    } else { error(F("Error :("),F("can't open raw file"));}
    tmp.close();
  }
}

// Print file info to LCD
void showInfo(char *fileName) {
  lcd.setCursor(0,1);
  lcd.write(5);
  lcd.write(' ');
  lcd.print(fileName);
  lcd.print(F("      "));
}

// Gets the file name from the index stored in fileIndex
// fileName: the char array where we'll store the file name.
// index: the fileIndex element we want the name of
// ext(optional): extension; 0(default), is bmp; 1 is raw. 2 is none.
void getFileName(char* fileName, uint8_t index, uint8_t ext = 0) {
  SdFile    tmp;

  // Retrieve name. Can't do it without opening file.
  tmp.open(sd.vwd(), fileIndex[index], O_RDONLY); // open file
  tmp.getName(fileName, 18);                      // get name
  tmp.close();                                    // close file

  // Change to raw extension
  if (ext == 1) {
    char *p = strstr(fileName, "bmp");            // get the position of "bmp"
    strcpy (p, "raw");                            // swap for raw
  }
  // Remove extension
  if (ext == 2) {
    char *p = strstr(fileName, ".bmp");           // get the position of ".bmp"
    strcpy (p, "");                               // swap for null
  }
}

// MENU INTERFACE -----------------------------------------------------------

// Display the menu
void menuDisplay(uint8_t state) {
  uint8_t solidBlocks, frame1;
  char line0[18];
  char line1[18];
  lcd.clear();
  switch (state){
    case 0: // initial screen
    getFileName(line0,frame,2);
    lcd.write(5);
    lcd.print(F(": "));
    lcd.print(line0);
    lcd.setCursor(0,1);
    printMenuStr(0);
    if (selected == 1) {
      lcd.setCursor(10,1); lcd.write(126);
    } else {
      markBottom();
    }
    break;

    case 1: // main menu screen 1
    printMenuStr(1);
    lcd.setCursor(15,0);
    lcd.write(1);
    if (selected == 2) markTop();
    lcd.setCursor(0,1);
    printMenuStr(2);
    if (selected == 3) markBottom();
    lcd.setCursor(15,1);
    lcd.write(3);
    break;

    case 2: // main menu screen 2
    printMenuStr(2);
    lcd.setCursor(15,0);
    lcd.write(2);
    if (selected == 3) markTop();
    lcd.setCursor(0,1);
    printMenuStr(3);
    if (selected == 4) markBottom();
    lcd.setCursor(15,1);
    lcd.write(3);
    break;

    case 3: // main menu screen 3
    printMenuStr(3);
    lcd.setCursor(15,0);
    lcd.write(3);
    if (selected == 4) markTop();
    lcd.setCursor(0,1);
    printMenuStr(4);
    if (selected == 5) markBottom();
    lcd.setCursor(15,1);
    lcd.write(1);
    break;

    case 4: // main menu screen 4
    printMenuStr(4);
    lcd.setCursor(15,0);
    lcd.write(3);
    if (selected == 5) markTop();
    lcd.setCursor(0,1);
    printMenuStr(5);
    if (selected == 6) markBottom();
    lcd.setCursor(15,1);
    lcd.write(2);
    break;

    case 5: // Brightness setting
    printMenuStr(2);
    lcd.setCursor(12,0);
    lcd.print(SetBrightness);
    lcd.setCursor(0,1);
    solidBlocks = (SetBrightness+1)/16;
    for (int i = 0; i<solidBlocks; i++) { lcd.write(255); }
    for (int i=solidBlocks; i<16; i++) { lcd.write(165); }
    break;

    case 6: // Speed setting
    printMenuStr(3);
    lcd.setCursor(12,0);
    lcd.print(Speed);
    lcd.setCursor(0,1);
    solidBlocks = (Speed+1)/16;
    for (int i = 0; i<solidBlocks; i++) { lcd.write(255); }
    for (int i=solidBlocks; i<16; i++) { lcd.write(165); }  
    break;

    case 7: // Delay setting
    printMenuStr(4);
    lcd.setCursor(12,0);
    lcd.print(Delay);
    lcd.setCursor(0,1);
    solidBlocks = (Delay+1)/16;
    for (int i = 0; i<solidBlocks; i++) { lcd.write(255); }
    for (int i=solidBlocks; i<16; i++) { lcd.write(165); }   
    break;

    case 8: // Choose file
    if (selected == 0 ){
      frame1 = frame + 1;
      frame1 = (frame1 == nFrames) ? frame1 = 0 : frame1;
      getFileName(line0,frame,2);
      getFileName(line1,frame1,2);
      lcd.write(32);
      lcd.print(line0);
      markTop();
      lcd.setCursor(1,1);
      lcd.print(line1);
    } else {
      frame1 = (frame == 0) ? frame1 = nFrames - 1 : frame -1 ;
      getFileName(line0,frame1,2);
      getFileName(line1,frame,2);
      lcd.write(32);
      lcd.print(line0);
      lcd.setCursor(1,1);
      lcd.print(line1);
      markBottom();
    }
    break;

  }
}

// Helper functions to draw selection marks on Top or Bottom row
void markTop(){
  lcd.setCursor(0,0); lcd.write(126);
}
void markBottom(){
  lcd.setCursor(0,1); lcd.write(126);
}

// Recover a menu string from PROGMEM
void printMenuStr (uint8_t i) {
  char c;
  const char * str = (const char *) &menuString[i];
  while ((c = pgm_read_byte(str++)))
    lcd.print (c);
}

// Get input from user
// Waits for button press, checks if it's a long pulse and modifies it. Return the input
// 1 = up, 2 = down, 3 = trigger, 4 = up long, 5 = down long. Timing could be improved...-------> TO DO
int getInput (){
  uint8_t         a;                // The menu option returned
  static uint8_t  buttonActive,     // flag for button active
                  longPress;        // flag for a long press
  static uint32_t pressTimer;       // For timing button presses

  // wait for a button press
  while(digitalRead(SEL_UP) == HIGH && digitalRead(SEL_DN) == HIGH && digitalRead(TRIGGER) == HIGH);
  if (digitalRead(SEL_UP) == LOW) a=1;
  else if (digitalRead(SEL_DN) == LOW) a=2;
  else if (digitalRead(TRIGGER) == LOW) a=3;

  // Output modified depending on menuState
  if ( menuState > 4 && menuState < 8 && a < 3 ) { // These menus care about a "long" press.
    // debounce and give time to detect button release
    delay(300);
    if ( digitalRead(SEL_UP) == LOW || digitalRead(SEL_DN) == LOW ) { // button still pressed, check time
      if ( buttonActive == false ) {
        // record time, change flag
        pressTimer = millis();
        buttonActive = true;
      }
      if ( (millis() - pressTimer > 1000UL) && longPress == false ) { // more than one second
        longPress = true;
      }
      if ( longPress == true ) {
        a += 3;
        //delay(50);
      } else {
        delay(150);
      }
    } else { // buttons are HIGH (i.e. released)
      if (buttonActive == true) {
        if (longPress == true) {
          longPress = false;
        }
        buttonActive = false;
      }
      // slow down before checking again
      delay (150);
    }
  } else if ( menuState == 0 && a == 3 && selected == 0 ){ // This menu wants direct control.
    // Pass on to trigger function
  } else {
    // debounce and wait for release for the rest of menus
    delay(50);
    while(digitalRead(SEL_UP) == LOW || digitalRead(SEL_DN) == LOW || digitalRead(TRIGGER) == LOW);
  } 
  
  // return value
  return a;
}


// PAINTING FUNCTIONS --------------------------------------------------------

// Stages the first block of file, gets ready for trigger
void preload(){
  uint32_t        lastBlock;
  char            outfile[18];
  SdFile          tmp;
  
  // Get existing contiguous tempfile info
  getFileName(outfile, frame, 1);                       // get name
  if(!tmp.open(outfile, O_RDONLY)) {
    error(F("Can't open file"),F("Try rescaning?"));    // Can't open the file
  }
  if(!tmp.contiguousRange(&firstBlock, &lastBlock)) {
    error(F(":( Raw file error"), F("Try rescaning?")); // File is not contiguous
  }
  // Number of blocks needs to be calculated from file size, not the
  // range values.  The contiguous file creation and range functions
  // work on cluster boundaries, not necessarily the actual file size.
  nBlocks = tmp.fileSize() / 512;

  tmp.close(); // File handle is no longer accessed, just block reads

  // Stage first block, but don't display yet -- the loop below
  // does that only when Timer1 overflows.
  sd.card()->readBlock(firstBlock, sdBuf);
  // readBlock is used rather than readStart/readData/readEnd as
  // the duration between block reads may exceed the SD timeout.
}

// Puts the display of the frame into action. Dims the screen, waits for delay,
// trigers the show functions and waits for a button press to return to menu. 
void trigger(){
  boolean         stopFlag = false; // If set, stop playback loop
  uint32_t        block    = 0;     // Current block # within file
  
  // dim lcd screen
  lcd.noBacklight();
  lcd.clear();
  for (uint8_t i = 6; i<10; i++) {
    lcd.setCursor(i,0);
    lcd.write(165);
  } 

  // Delay
  if (Delay > 0) {
    delay (Delay * 1000); // Delay is in seconds.
  }
  
  // Timer0 interrupt is disabled for smoother playback.
  // This means delay(), millis(), etc. won't work after this.
  TIMSK0 = 0;
      
  // Set up timer based on dial input
  uint32_t linesPerSec = map(Speed, 0, 255, 10, maxLPS);
  // Serial.println(linesPerSec);
  OCR1A = (F_CPU / 64) / linesPerSec;          // Timer1 interval

  for(;;) {
    while(!(TIFR1 & _BV(TOV1)));               // Wait for Timer1 overflow
    TIFR1 |= _BV(TOV1);                        // Clear overflow bit

    show();                                    // Display current line
    if(stopFlag) break;                        // Break when done

    if(++block >= nBlocks) {                   // Past last block?
      if(digitalRead(TRIGGER) == HIGH) {       // Trigger released?
        memset(sdBuf, 0, N_LEDS * 3);          // LEDs off on next pass
        stopFlag = true;                       // Stop playback on next pass
        continue;
      }                                        // Else trigger still held
      block = 0;                               // Loop back to start
    }
    sd.card()->readBlock(block + firstBlock, sdBuf); // Load next pixel row
  }

  // Show is over; Wait for click before we turn on screen
  lcd.setCursor(0,0);
  lcd.print(F("Press any button"));
  lcd.setCursor(0,1);
  lcd.print(F("   to return    "));
  while(digitalRead(SEL_UP) == HIGH && digitalRead(SEL_DN) == HIGH && digitalRead(TRIGGER) == HIGH);
  while(digitalRead(SEL_UP) == LOW || digitalRead(SEL_DN) == LOW || digitalRead(TRIGGER) == LOW);
  lcd.backlight();

}
      
// BMP->NEOPIXEL FILE CONVERSION ---------------------------------------------

// Convert file from 24-bit Windows BMP format to raw NeoPixel datastream.
// Conversion is bottom-to-top (see notes below)...for horizontal light
// painting, the image is NOT rotated here (the per-pixel file seeking this
// requires takes FOREVER on the Arduino).  Instead, such images should be
// rotated counterclockwise (in Photoshop or other editor) prior to moving
// to SD card.  As currently written, both the input and output files need
// to be in the same directory.  Brightness is set during conversion; there
// aren't enough cycles to do this in realtime during playback.  To change
// brightness, re-process image file using new brightness value.
void bmpProcess(
  char    *inName,
  char    *outName,
  uint8_t *brightness) {

  SdFile    inFile,              // Windows BMP file for input
            outFile;             // NeoPixel raw file for output
  boolean   flip      = false;   // 'true' if image stored top-to-bottom
  int       bmpWidth,            // BMP width in pixels
            bmpHeight,           // BMP height in pixels
            bmpStartCol,         // First BMP column to process (crop/center)
            columns,             // Number of columns to process (crop/center)
            row,                 // Current image row (Y)
            column;              // and column (X)
  uint8_t  *ditherRow,           // 16-element dither array for current row
            pixel[3],            // For reordering color data, BGR to GRB
            b = 0,               // 1 + *brightness
            d,                   // Dither value for row/column
            color,               // Color component index (R/G/B)
            raw,                 // 'Raw' R/G/B color value
            corr,                // Gamma-corrected R/G/B
           *ledPtr,              // Pointer into sdBuf (output)
           *ledStartPtr;         // First LED column to process (crop/center)
  uint16_t  b16;                 // 16-bit dup of b
  uint32_t  bmpImageoffset,      // Start of image data in BMP file
            lineMax   = 0L,      // Cumulative brightness of brightest line
            rowSize,             // BMP row size (bytes) w/32-bit alignment
            sum;                 // Sum of pixels in row

  if(brightness)           b = 1 + *brightness; // Wraps around, fun with maths
  else if(NULL == outName) return false; // MUST pass brightness for power est.

  showInfo(inName);
  if(!inFile.open(inName, O_RDONLY)) {
    error(F("Failed :("), F("can't open file"));
    return false;
  }

  if(inFile.read(sdBuf, 34)             &&    // Load header
    (*(uint16_t *)&sdBuf[ 0] == 0x4D42) &&    // BMP signature
    (*(uint16_t *)&sdBuf[26] == 1)      &&    // Planes: must be 1
    (*(uint16_t *)&sdBuf[28] == 24)     &&    // Bits per pixel: must be 24
    (*(uint32_t *)&sdBuf[30] == 0)) {         // Compression: must be 0 (none)
    // Supported BMP format -- proceed!
    bmpImageoffset = *(uint32_t *)&sdBuf[10]; // Start of image data
    bmpWidth       = *(uint32_t *)&sdBuf[18]; // Image dimensions
    bmpHeight      = *(uint32_t *)&sdBuf[22];
    // That's some nonportable, endian-dependent code right there.

    if(outName) { // Doing conversion?  Need outFile.
      // Delete existing outFile file (if any)
      (void)sd.remove(outName);
      showInfo(outName);
      // NeoPixel working file is always 512 bytes (one SD block) per row
      if(outFile.createContiguous(sd.vwd(), outName, 512L * bmpHeight)) {
        uint32_t lastBlock;
        outFile.contiguousRange(&firstBlock, &lastBlock);
        // Once we have the first block index, the file handle
        // is no longer needed -- raw block writes are used.
        outFile.close();
        nBlocks = bmpHeight; // See note in setup() re: block calcs
      } else {
        error(F(":( error"),F("creating file"));
      }
    }

    rowSize = ((bmpWidth * 3) + 3) & ~3; // 32-bit line boundary
    b16     = (int)b;

    if(bmpHeight < 0) {       // If bmpHeight is negative,
      bmpHeight = -bmpHeight; // image is in top-down order.
      flip      = true;       // Rare, but happens.
    }

    if(bmpWidth >= N_LEDS) { // BMP matches LED bar width, or crop image
      bmpStartCol = (bmpWidth - N_LEDS) / 2;
      ledStartPtr = sdBuf;
      columns     = N_LEDS;
    } else {                 // Center narrow image within LED bar
      bmpStartCol = 0;
      ledStartPtr = &sdBuf[((N_LEDS - bmpWidth) / 2) * 3];
      columns     = bmpWidth;
      memset(sdBuf, 0, N_LEDS * 3); // Clear left/right pixels
    }

    for(row=0; row<bmpHeight; row++) { // For each row in image...
      //Serial.write('.');
      // Image is converted from bottom to top.  This is on purpose!
      // The ground (physical ground, not the electrical kind) provides
      // a uniform point of reference for multi-frame vertical painting...
      // could then use something like a leaf switch to trigger playback,
      // lifting the light bar like making giant soap bubbles.

      // Seek to first pixel to load for this row...
      inFile.seekSet(
        bmpImageoffset + (bmpStartCol * 3) + (rowSize * (flip ?
        (bmpHeight - 1 - row) : // Image is stored top-to-bottom
        row)));                 // Image stored bottom-to-top
      if(!inFile.read(ledStartPtr, columns * 3))  // Load row
        error(F(":( Read error"),F("during raw conv."));

      sum       = 0L;
      ditherRow = (uint8_t *)&dither[row & 0x0F]; // Dither values for row
      ledPtr    = ledStartPtr;
      for(column=0; column<columns; column++) {   // For each column...
        if(b) { // Scale brightness, reorder R/G/B
          pixel[NEO_BLUE]  = (ledPtr[BMP_BLUE]  * b16) >> 8;
          pixel[NEO_GREEN] = (ledPtr[BMP_GREEN] * b16) >> 8;
          pixel[NEO_RED]   = (ledPtr[BMP_RED]   * b16) >> 8;
        } else { // Full brightness, reorder R/G/B
          pixel[NEO_BLUE]  = ledPtr[BMP_BLUE];
          pixel[NEO_GREEN] = ledPtr[BMP_GREEN];
          pixel[NEO_RED]   = ledPtr[BMP_RED];
        }

        d = pgm_read_byte(&ditherRow[column & 0x0F]); // Dither probability
        for(color=0; color<3; color++) {              // 3 color bytes...
          raw  = pixel[color];                        // 'Raw' G/R/B
          corr = pgm_read_byte(&gamma[raw]);          // Gamma-corrected
          if(pgm_read_byte(&bump[raw]) > d) corr++;   // Dither up?
          *ledPtr++ = corr;                           // Store back in sdBuf
          sum      += corr;                           // Total brightness
        } // Next color byte
      } // Next column

      if(outName) {
        if(!sd.card()->writeBlock(firstBlock + row, (uint8_t *)sdBuf))
          error(F(":( Write error"),F("during raw conv."));
      }
      if(sum > lineMax) lineMax = sum;

    } // Next row
    //Serial.println(F("OK"));

    if(brightness) {
      lineMax = (lineMax * 20) / 255; // Est current @ ~20 mA/LED. Why 255?

      // DANGER AHEAD!! ---------------------------------------------------------------------------!!
      lineMax = lineMax / CORRECTION_FACTOR;        // Adjust lineMax to real power draw observed
      
      if(lineMax > CURRENT_MAX) {
        // Estimate suitable brightness based on CURRENT_MAX
        *brightness = (*brightness * (uint32_t)CURRENT_MAX) / lineMax;
      } // Else no recommended change
    }
    
  } else { // end BMP header check
    error(F(":( BMP format"),F(" not recognized."));
  }

  inFile.close();
  return true; // 'false' on various file open/create errors
}

// LCD CUSTOM CHARACTERS SETUP -----------------------------------------------

void setup_chars() {

  /* Register the personalized characters on the LCD */
  lcd.createChar(0, arrow_up);
  lcd.createChar(1, bar1);
  lcd.createChar(2, bar2);
  lcd.createChar(3, bar3);
  lcd.createChar(4, arrow_dn);
  lcd.createChar(5, file);
  lcd.createChar(6, hourglass);
  lcd.createChar(7, check);

}

// MISC UTILITY FUNCTIONS ----------------------------------------------------

// Estimate maximum block-read time for card (microseconds)
static uint32_t benchmark(uint32_t block, uint32_t n) {
  uint32_t t, maxTime = 0L;

  do {
    t = micros();
    sd.card()->readBlock(block++, sdBuf);
    if((t = (micros() - t)) > maxTime) maxTime = t;
  } while(--n);

  return maxTime;
}

// Error handler; doesn't return, just stops.
static void error(const __FlashStringHelper *ptr1, const __FlashStringHelper *ptr2) {
  lcd.clear();
  lcd.print(ptr1);     // Show 1st part message
  lcd.setCursor(0,1);
  lcd.print(ptr2);     // Show 2nd part message
  for(;;);             // and hang
}

/*
// get ammount of free SRAM, Just for testing
extern unsigned int __bss_end;
extern unsigned int __heap_start;
extern void *__brkval;

uint16_t getFreeSram() {
  uint8_t newVariable;
  // heap is empty, use bss as start memory address
  if ((uint16_t)__brkval == 0)
    return (((uint16_t)&newVariable) - ((uint16_t)&__bss_end));
  // use heap end as the start of the memory address
  else
    return (((uint16_t)&newVariable) - ((uint16_t)__brkval));
};
*/


// EEPROM HANDLING ----------------------------------------------------------

// Saves config values to EEPROM. We use update so we only write if changed
// and save precious write cycles. Positions 0,1,2,3 for current version
void saveConfig() {
  EEPROM.update(0,66);               // Current version (v1.0) flag is 66;
  EEPROM.update(1,SetBrightness);
  EEPROM.update(2,Speed);
  EEPROM.update(3,Delay);
}

// Loads configuration variables if position 0 contains the right flag for
// this version ( 66 ). 
void loadConfig() {
  if (EEPROM.read(0) == 66){
    SetBrightness = EEPROM.read(1);
    Speed         = EEPROM.read(2);
    Delay         = EEPROM.read(3);
  }
}

// Brightness change is always written to EEPROM, otherwise on next power up
// saved files would be generated with a diffrent brightness value than what
// we load from the config 
void saveBrightness() {
  EEPROM.update(0,66);               // Current version (v1.0) flag is 66;
  EEPROM.update(1,SetBrightness);
}


// NEOPIXEL FUNCTIONS --------------------------------------------------------

// The normal NeoPixel library isn't used by this project.  SD I/O and
// NeoPixels need to occupy the same buffer, there isn't quite an elegant
// way to do this with the existing library that avoids refreshing a longer
// strip than necessary.  Instead, just the core update function for 800 KHz
// pixels on 16 MHz AVR is replicated here; not handling every permutation.

static void show(void) {
  volatile uint16_t
    i   = N_LEDS * 3; // Loop counter
  volatile uint8_t
   *ptr = sdBuf,      // Pointer to next byte
    b   = *ptr++,     // Current byte value
    hi,               // PORT w/output bit set high
    lo,               // PORT w/output bit set low
    next,
    bit = 8;

  noInterrupts();
  hi   = *port |  pinMask;
  lo   = *port & ~pinMask;
  next = lo;

  asm volatile(
   "head20_%=:"                "\n\t"
    "st   %a[port],  %[hi]"    "\n\t"
    "sbrc %[byte],  7"         "\n\t"
     "mov  %[next], %[hi]"     "\n\t"
    "dec  %[bit]"              "\n\t"
    "st   %a[port],  %[next]"  "\n\t"
    "mov  %[next] ,  %[lo]"    "\n\t"
    "breq nextbyte20_%="       "\n\t"
    "rol  %[byte]"             "\n\t"
    "rjmp .+0"                 "\n\t"
    "nop"                      "\n\t"
    "st   %a[port],  %[lo]"    "\n\t"
    "nop"                      "\n\t"
    "rjmp .+0"                 "\n\t"
    "rjmp head20_%="           "\n\t"
   "nextbyte20_%=:"            "\n\t"
    "ldi  %[bit]  ,  8"        "\n\t"
    "ld   %[byte] ,  %a[ptr]+" "\n\t"
    "st   %a[port], %[lo]"     "\n\t"
    "nop"                      "\n\t"
    "sbiw %[count], 1"         "\n\t"
     "brne head20_%="          "\n"
    : [port]  "+e" (port),
      [byte]  "+r" (b),
      [bit]   "+r" (bit),
      [next]  "+r" (next),
      [count] "+w" (i)
    : [ptr]    "e" (ptr),
      [hi]     "r" (hi),
      [lo]     "r" (lo));

  interrupts();
  // There's no explicit 50 uS delay here as with most NeoPixel code;
  // SD card block read provides ample time for latch!
}
