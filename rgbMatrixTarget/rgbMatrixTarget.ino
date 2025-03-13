/*
 * Lasertag Target Sensor - Nick Reynolds 22/10/2017
 * 
 * This will let you build a LaserWar/WoW/DoT compatible target sensor by setting a few #defines below
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 */

#include <milesTag.h>
#include <EEPROM.h> //Used to store preferences

#define MAJOR_VERSION 0
#define MINOR_VERSION 3
#define PATCH_VERSION 1

#if defined(ESP32)
  #if CONFIG_IDF_TARGET_ESP32C3
    #define IR_RECEIVER_PIN 1
  #endif
#else
  #define IR_RECEIVER_PIN 2 //This is the pin your IR sensor is connected to. It must be capable of attachine a hardware interrupt. On AVR Arduino this is often pins 2 & 3
#endif

#define LED_MATRIX_DISPLAY    //Uncomment these two lines if you have an 8x8 Neopixel array to show info
#define MATRIX_PIN 3

#define BUTTON_CONNECTED    //Uncomment these two lines if you have a button connected
#define buttonPin 6        //Pin used for button
//#define buttonPin A0        //Pin used for button
uint32_t buttonPushTime = 0;

#define BUTTON_STATE_IDLE 0
#define BUTTON_STATE_SHORTPRESS 1
#define BUTTON_STATE_LONGPRESS 2

uint8_t buttonState = BUTTON_STATE_IDLE;

/*
 * All the stuff below here is optional to set, but tweak away if you feel like it
 * 
 */

#define DEBUG_FSM     // Uncomment this line to get state machine (program flow) debugging
#define DEBUG_LED_MATRIX  // Uncomment this line to get matrix debug

//#define DEBUG_INDICATOR_LED   // Uncomment this lint to get matrix debug

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#ifndef PSTR
 #define PSTR // Make Arduino Due happy
#endif

#define MATRIX_PIN 3
#define MATRIX_TYPE_EEPROM_LOCATION 2

#define MAX_LED_MATRIX_BRIGHTNESS_EEPROM_LOCATION 4
#define LED_MATRIX_SHOW_DETAIL_EEPROM_LOCATION 6
#define BLINK_PERIOD_EEPROM_LOCATION 8
#define SCROLL_SPEED_EEPROM_LOCATION 10

uint8_t HEARTBEAT_LED_MATRIX_BRIGHTNESS = 12;
uint8_t MAX_LED_MATRIX_BRIGHTNESS = 128;
uint8_t MIN_LED_MATRIX_BRIGHTNESS = 0;
uint8_t LED_MATRIX_DECAY_RATE = 1;
uint8_t LED_MATRIX_DECAY_FREQUENCY = MAX_LED_MATRIX_BRIGHTNESS/16;
uint8_t currentLedMatrixBrightness = MAX_LED_MATRIX_BRIGHTNESS;
bool LED_MATRIX_SHOW_DETAIL = true;
String textToScroll;
Adafruit_NeoMatrix *ledMatrix;
uint16_t colours[7];

#define MATRIX_STATE_IDLE 0
#define MATRIX_STATE_HIT 1
#define MATRIX_STATE_COUNT_OUT_HITS 2
#define MATRIX_STATE_SHOW_HIT_INFO 3
#define MATRIX_STATE_SHOW_DATA 4
#define MATRIX_STATE_WAITING 5
#define MATRIX_STATE_SCROLL_STRING 6
uint8_t currentMatrixState = MATRIX_STATE_SCROLL_STRING;

#define MENU_STATE_IDLE 0
#define MENU_STATE_MODE 1
#define MENU_STATE_BRIGHTNESS 2
#define MENU_STATE_BLINK 3
#define MENU_STATE_SCROLL_SPEED 4
uint8_t currentMenuState = MENU_STATE_IDLE;
uint32_t lastMenuStateChange = 0;

uint32_t cooldownTime = 10000;  //Target goes 'dark' this long after a hit

uint32_t loopCounter = millis();

//Display blink period
uint8_t numberOfFlashes = 0; //Used to keep track of display flashes
const uint32_t defaultOnTime = 100;
uint32_t onTime = defaultOnTime;
uint32_t offTime = 1000 - onTime ;


//Matrix scrolling values
int16_t scrollingTextX = 0;
int16_t scrollingTextY = 0;
int16_t scrollingTextWidth = 0;
int16_t scrollingTextHeight = 0;
int scrollXposition = 0;

#define SCROLL_DELAY_FAST 30
#define SCROLL_DELAY_MEDIUM 50
#define SCROLL_DELAY_SLOW 70
uint8_t scrollDelay = SCROLL_DELAY_MEDIUM;

void setup(){
  #if defined DEBUG_FSM || defined DEBUG_LED_MATRIX
    Serial.begin(115200);
  	delay(100);
    Serial.print(F("Starting target sensor: "));
  	Serial.println(__FILE__);
  	Serial.print(F("Compiled: "));
  	Serial.print(__TIME__);
  	Serial.print(' ');
  	Serial.println(__DATE__);
    Serial.print("Software version: v");
    Serial.print(MAJOR_VERSION);
    Serial.print('.');
    Serial.print(MINOR_VERSION);
    Serial.print('.');
    Serial.println(PATCH_VERSION);
  #endif
  #ifdef BUTTON_CONNECTED
    pinMode(buttonPin, INPUT_PULLUP);
    #ifdef DEBUG_FSM
      Serial.print(F("Button connected on pin: "));
      Serial.println(buttonPin);
    #endif
  #endif
  Serial.print(F("LED Matrix variant: "));
  #ifdef BUTTON_CONNECTED
    if(digitalRead(buttonPin) == false)
    {
      if(EEPROM.read(MATRIX_TYPE_EEPROM_LOCATION) == 0)
      {
        EEPROM.update(MATRIX_TYPE_EEPROM_LOCATION, uint8_t(1));
      }
      else if(EEPROM.read(MATRIX_TYPE_EEPROM_LOCATION) == 1)
      {
        EEPROM.update(MATRIX_TYPE_EEPROM_LOCATION, uint8_t(0));
      }
      else
      {
        EEPROM.update(MATRIX_TYPE_EEPROM_LOCATION, uint8_t(0));
      }
      #ifdef DEBUG_FSM
        Serial.print(F("Changed LED Matrix variant: "));
        Serial.println(EEPROM.read(MATRIX_TYPE_EEPROM_LOCATION));
      #endif
    }
  #endif
  //Set matrix type from EEPROM
  if(EEPROM.read(MATRIX_TYPE_EEPROM_LOCATION) == 0)
  {
    Serial.println('1');
    ledMatrix = new Adafruit_NeoMatrix(8, 8, MATRIX_PIN,
      NEO_MATRIX_TOP     + NEO_MATRIX_RIGHT +
      NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE,
      NEO_GRB            + NEO_KHZ800);
  }
  else
  {
    Serial.println('2');
    ledMatrix =  new Adafruit_NeoMatrix(8, 8, MATRIX_PIN,
      NEO_MATRIX_TOP     + NEO_MATRIX_RIGHT +
      NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
      NEO_GRB            + NEO_KHZ800);
  }
  colours[0] = ledMatrix->Color(255, 0, 0); //Red
  colours[1] = ledMatrix->Color(0, 255, 0); //Green
  colours[2] = ledMatrix->Color(255, 255, 0); //Yellow
  colours[3] = ledMatrix->Color(0, 0, 255); //Blue
  colours[4] = ledMatrix->Color(255, 0, 255); //Magenta
  colours[5] = ledMatrix->Color(0, 255, 255); //Cyan
  colours[6] = ledMatrix->Color(255, 255, 255); //White
  MAX_LED_MATRIX_BRIGHTNESS = EEPROM.read(MAX_LED_MATRIX_BRIGHTNESS_EEPROM_LOCATION);
  HEARTBEAT_LED_MATRIX_BRIGHTNESS = MAX_LED_MATRIX_BRIGHTNESS/10;
  //LED_MATRIX_DECAY_RATE = MAX_LED_MATRIX_BRIGHTNESS / 64;
  LED_MATRIX_DECAY_FREQUENCY = 1024/MAX_LED_MATRIX_BRIGHTNESS;
  //Set mode from EEPROM
  if(EEPROM.read(LED_MATRIX_SHOW_DETAIL_EEPROM_LOCATION) == 0)
  {
    LED_MATRIX_SHOW_DETAIL = false;
  }
  else
  {
    LED_MATRIX_SHOW_DETAIL = true;
  }
  //Set blink period from EEPROM
  if(EEPROM.read(BLINK_PERIOD_EEPROM_LOCATION) == 0)
  {
    onTime = 0;
  }
  else if(EEPROM.read(BLINK_PERIOD_EEPROM_LOCATION) == 3)
  {
    onTime = defaultOnTime;
    offTime = 10000 - onTime;
  }
  else if(EEPROM.read(BLINK_PERIOD_EEPROM_LOCATION) == 5)
  {
    onTime = defaultOnTime;
    offTime = 5000 - onTime;
  }
  else if(EEPROM.read(BLINK_PERIOD_EEPROM_LOCATION) == 10)
  {
    onTime = defaultOnTime;
    offTime = 10000 - onTime;
  }
  else
  {
    onTime = defaultOnTime;
    offTime = 1000 - onTime;
  }
  //Set scroll speed from EEPROM
  if(EEPROM.read(SCROLL_SPEED_EEPROM_LOCATION) == 1)
  {
    scrollDelay = SCROLL_DELAY_MEDIUM;
  }
  else if(EEPROM.read(SCROLL_SPEED_EEPROM_LOCATION) == 2)
  {
    scrollDelay = SCROLL_DELAY_SLOW;
  }
  else
  {
    scrollDelay = SCROLL_DELAY_FAST;
  }
  #ifdef DEBUG_FSM  
    Serial.print(F("Matrix brightness: "));
    Serial.println(MAX_LED_MATRIX_BRIGHTNESS);
    Serial.print(F("Starting mode: "));
    if(LED_MATRIX_SHOW_DETAIL == true)
    {
      Serial.println(F("show detail"));
    }
    else
    {
      Serial.println(F("no detail"));
    }
    Serial.print(F("Blink: "));
    if(onTime == 0)
    {
      Serial.println(F("disabled"));
    }
    else
    {
      Serial.println(onTime + offTime);
    }
  #endif
  ledMatrix->begin();
  ledMatrix->setBrightness(MAX_LED_MATRIX_BRIGHTNESS);
  //Set up Laser-tag receiver
  milesTag.debug(Serial);                 //Send milesTag debug output to Serial (optional)
  milesTag.begin(milesTag.receiver);      //Simple single receiver requires basic initialisation
  milesTag.setReceivePin(IR_RECEIVER_PIN);//Set the receive pin, which is mandatory
  //Show the warning message
  scrollText("Lasertag target v" + String(MAJOR_VERSION) + "." + String(MINOR_VERSION) + "." + String(PATCH_VERSION));
}

void loop(){
  /*
   * 
   * State machine for the menu
   * 
   */
  #ifdef BUTTON_CONNECTED
    menuFSM();
  #endif
  /*
   * 
   * State machine for the matrix
   * 
   */
  if(currentMatrixState == MATRIX_STATE_IDLE)
  {
    //
    // Always be ready to take a hit
    //
    if(milesTag.dataReceived())             //There is something in the packet buffer of the first 'busy' receiver. Multiple receivers can be busy and are handled individually.
    {
      if(milesTag.receivedDamage() > 0)     //This is a damage packet
      {
        changeMatrixState(MATRIX_STATE_HIT);
      }
      else
      {
        milesTag.resumeReception();           //Clear the first 'busy' receiver buffer and prepare it for another packet. It is not automatically cleared
      }
    }
    //
    // Flash the LED to show the target
    //
    if(onTime != 0 && numberOfFlashes == 0 && millis()>loopCounter+offTime)
    {
      numberOfFlashes = 1;
      loopCounter = millis();
      ledMatrix->setBrightness(HEARTBEAT_LED_MATRIX_BRIGHTNESS);
      ledMatrix->fillScreen(colours[6]);
      ledMatrix->show();
      /*#ifdef DEBUG_FSM
      Serial.println('.');
      #endif*/
    }
    else if(numberOfFlashes == 1 && millis()>loopCounter+onTime)
    {
      numberOfFlashes = 0;
      loopCounter = millis();
      setMatrixColour();
      ledMatrix->setBrightness(MIN_LED_MATRIX_BRIGHTNESS);
      ledMatrix->show();
    }
  }
  else if(currentMatrixState == MATRIX_STATE_HIT)
  {
    loopCounter = millis();
    #ifdef DEBUG_FSM
      Serial.print(F("Incoming hit: "));
      Serial.println(milesTag.messageDescription());
    #endif
    /*
    if(receiver.validWoW() || receiver.validLaserWar()) //LaserWar tends to have high hit numbers so also show one flash for it
    {
      numberOfFlashes = 1;
    }
    else
    {
      numberOfFlashes = receiver.hitsReceived();
    }
    */
    numberOfFlashes = 1;
    currentLedMatrixBrightness = MAX_LED_MATRIX_BRIGHTNESS;
    if(numberOfFlashes > 0)
    {
      changeMatrixState(MATRIX_STATE_COUNT_OUT_HITS);
    }
    else
    {
      changeMatrixState(MATRIX_STATE_SHOW_HIT_INFO);
    }
  }
  else if(currentMatrixState == MATRIX_STATE_COUNT_OUT_HITS)
  {
    if(numberOfFlashes == 0)
    {
      //
      //  Move on to showing hit detail, if needed
      //
      setMatrixColour();
      ledMatrix->setBrightness(MIN_LED_MATRIX_BRIGHTNESS);
      ledMatrix->show();
      if(LED_MATRIX_SHOW_DETAIL == true)
      {
        changeMatrixState(MATRIX_STATE_SHOW_HIT_INFO);
        loopCounter = millis();
      }
      else
      {
        milesTag.resumeReception();           //Clear the first 'busy' receiver buffer and prepare it for another packet. It is not automatically cleared
        changeMatrixState(MATRIX_STATE_WAITING);
        loopCounter = millis();
      }
    }
    else
    {
      //
      //  Flash the LED/Matrix
      //
      if(currentLedMatrixBrightness == MAX_LED_MATRIX_BRIGHTNESS)
      {
        #ifdef DEBUG_LED_MATRIX
          Serial.print(F("Matrix flash: "));
          Serial.println(numberOfFlashes);
        #endif
        setMatrixColour();
        ledMatrix->setBrightness(currentLedMatrixBrightness);
        ledMatrix->show();
        //Reduce the brightess
        currentLedMatrixBrightness--;
      }
      else if(currentLedMatrixBrightness > MIN_LED_MATRIX_BRIGHTNESS && millis()%LED_MATRIX_DECAY_FREQUENCY == 0)
      {
        //
        //  Decay the brightness of the flash
        //
        if(currentLedMatrixBrightness>LED_MATRIX_DECAY_RATE)
        {
          currentLedMatrixBrightness-=LED_MATRIX_DECAY_RATE;
          #ifdef DEBUG_FSM
            //Serial.print('.');
          #endif
        }
        else
        {
          currentLedMatrixBrightness = MAX_LED_MATRIX_BRIGHTNESS;
          //currentLedMatrixBrightness=0;
          numberOfFlashes--;
          #ifdef DEBUG_FSM
            //Serial.println('.');
          #endif
        }
        setMatrixColour();
        ledMatrix->setBrightness(currentLedMatrixBrightness);
        ledMatrix->show();
      }
    }
  }
  else if(currentMatrixState == MATRIX_STATE_SCROLL_STRING)
  {
    while(--scrollXposition > -(scrollingTextWidth))
    {
      ledMatrix->fillScreen(0);
      ledMatrix->setCursor(scrollXposition, 0);
      ledMatrix->print(textToScroll);
      ledMatrix->show();
      delay(scrollDelay);
      /*
       * 
       * Be ready to take a hit and interrupt scrolling
       * 
       */
      if(milesTag.dataReceived())             //There is something in the packet buffer of the first 'busy' receiver. Multiple receivers can be busy and are handled individually.
      {
        if(milesTag.receivedDamage() > 0)     //This is a damage packet
        {
          changeMatrixState(MATRIX_STATE_HIT);
          loopCounter = millis();
          return;
        }
        else
        {
          milesTag.resumeReception();           //Clear the first 'busy' receiver buffer and prepare it for another packet. It is not automatically cleared
        }
      }
      /*
       * 
       * Accept button inputs
       * 
       */
       menuFSM();
    }
    if(currentMenuState == MENU_STATE_IDLE) //Only stop the text if timed out of the menu
    {
      changeMatrixState(MATRIX_STATE_WAITING);
    }
    else
    {
      loopScrollingText();
    }
    loopCounter = millis();
  }
  else if(currentMatrixState == MATRIX_STATE_SHOW_HIT_INFO)
  {
    currentLedMatrixBrightness = MIN_LED_MATRIX_BRIGHTNESS;
    ledMatrix->fillScreen(colours[7]);
    ledMatrix->setBrightness(0);
    ledMatrix->show();
    if(LED_MATRIX_SHOW_DETAIL == true)
    {
      textToScroll = milesTag.messageDescription();
      #ifdef DEBUG_FSM
        Serial.print(F("Matrix text: "));
        Serial.println(textToScroll);
      #endif
      scrollText(textToScroll);
      milesTag.resumeReception();           //Clear the first 'busy' receiver buffer and prepare it for another packet. It is not automatically cleared
    }
    else
    {
      //attach the interrupt again so we can take more hits
      milesTag.resumeReception();           //Clear the first 'busy' receiver buffer and prepare it for another packet. It is not automatically cleared
      changeMatrixState(MATRIX_STATE_WAITING);
      loopCounter = millis();
    }
  }
  else if(currentMatrixState == MATRIX_STATE_WAITING)
  {
    //
    // Be ready to take a hit and interrupt scrolling
    //
    if(milesTag.dataReceived())             //There is something in the packet buffer of the first 'busy' receiver. Multiple receivers can be busy and are handled individually.
    {
      if(milesTag.receivedDamage() > 0)     //This is a damage packet
      {
        changeMatrixState(MATRIX_STATE_HIT);
        loopCounter = millis();
        return;
      }
      else
      {
        milesTag.resumeReception();           //Clear the first 'busy' receiver buffer and prepare it for another packet. It is not automatically cleared
      }
    }
    if(millis() - loopCounter > cooldownTime)
    {
      loopCounter = millis();
      changeMatrixState(MATRIX_STATE_IDLE);
    }
  }
}

void scrollText(String text)
{
  textToScroll = text;
  scrollXposition = ledMatrix->width();
  ledMatrix->setTextWrap(false);
  ledMatrix->setTextSize(1);
  //ledMatrix->setBrightness(MAX_LED_MATRIX_BRIGHTNESS);
  ledMatrix->setBrightness(HEARTBEAT_LED_MATRIX_BRIGHTNESS);
  ledMatrix->setTextColor(colours[6]);  //Set text to white
  ledMatrix->getTextBounds(textToScroll,0,0,&scrollingTextX,&scrollingTextY,&scrollingTextWidth,&scrollingTextHeight);
  changeMatrixState(MATRIX_STATE_SCROLL_STRING);
  loopCounter = millis();
}

void loopScrollingText()
{
  scrollXposition = ledMatrix->width();  
}

void menuFSM()
{
  manageButton();
  if(currentMenuState == MENU_STATE_IDLE)
  {
    if(longPress()) //Long push
    {
      //changeMenuState(MENU_STATE_MODE);
    }
    else if(shortPress()) //Short push
    {
      changeMenuState(MENU_STATE_MODE);
      showCurrentMode();
    }
  }
  else if(currentMenuState == MENU_STATE_MODE)
  {
    if(longPress()) //Long push
    {
      changeMode();
    }
    else if(shortPress()) //Short push
    {
      changeMenuState(MENU_STATE_BRIGHTNESS);
      showCurrentBrightness();
    }
  }
  else if(currentMenuState == MENU_STATE_BRIGHTNESS)
  {
    if(longPress()) //Long push
    {
      changeBrightness();
    }
    else if(shortPress()) //Short push
    {
      changeMenuState(MENU_STATE_BLINK);
      showBlinkRate();
    }
  }
  else if(currentMenuState == MENU_STATE_BLINK)
  {
    if(longPress()) //Long push
    {
      changeBlinkRate();
    }
    else if(shortPress()) //Short push
    {
      changeMenuState(MENU_STATE_SCROLL_SPEED);
      showScrollSpeed();
    }
  }
  else if(MENU_STATE_SCROLL_SPEED)
  {
    if(longPress()) //Long push
    {
      changeScrollSpeed();
    }
    else if(shortPress()) //Short push
    {
      changeMenuState(MENU_STATE_IDLE);
    }
  }
  if(currentMenuState != MENU_STATE_IDLE && millis() - lastMenuStateChange > 10e3)  //Come out of the menu after an amount of time
  {
    changeMenuState(MENU_STATE_IDLE);
  }
}

void setMatrixColour()
{
  /*
  if(receiver.stunReceived())
  {
    // Set it to blue and show it
    ledMatrix->fillScreen(colours[3]);
  }
  else if(receiver.healingReceived())
  {
    // Set it to green and show it
    ledMatrix->fillScreen(colours[1]);
  }
  else if(receiver.validLaserWar())
  */
  {
    if(milesTag.receivedTeamId() == 0)  //Red team
    {
      ledMatrix->fillScreen(colours[0]);
    }
    else if(milesTag.receivedTeamId() == 1) //Blue team
    {
      ledMatrix->fillScreen(colours[3]);
    }
    else if(milesTag.receivedTeamId() == 2) //Yellow team
    {
      ledMatrix->fillScreen(colours[2]);
    }
    else if(milesTag.receivedTeamId() == 3) //Green team
    {
      ledMatrix->fillScreen(colours[1]);
    }
  }
  /*
  else
  {
    // Set it to red and show it
    ledMatrix->fillScreen(colours[0]);
  }
  */
}

void changeMode()
{
  LED_MATRIX_SHOW_DETAIL = not LED_MATRIX_SHOW_DETAIL;
  EEPROM.update(LED_MATRIX_SHOW_DETAIL_EEPROM_LOCATION, uint8_t(LED_MATRIX_SHOW_DETAIL));
  #ifdef DEBUG_FSM
    Serial.print(F("Mode: "));
    if(LED_MATRIX_SHOW_DETAIL)
    {
      Serial.println(F("show hit detail"));
    }
    else
    {
      Serial.println(F("Flash only"));
    }
  #endif
  lastMenuStateChange = millis();
  showCurrentMode();
}

void showCurrentMode()
{
  if(LED_MATRIX_SHOW_DETAIL)
  {
    textToScroll = "Showing hit detail";
  }
  else
  {
    textToScroll = "Flash only";
  }
  scrollText(textToScroll);
}
void changeBrightness()
{
  switch (MAX_LED_MATRIX_BRIGHTNESS) {
    case 255:
      MAX_LED_MATRIX_BRIGHTNESS = 64;
      break;
    case 64:
      MAX_LED_MATRIX_BRIGHTNESS = 128;
      break;
    case 128:
      MAX_LED_MATRIX_BRIGHTNESS = 192;
      break;
    case 192:
      MAX_LED_MATRIX_BRIGHTNESS = 255;
      break;
    default:
      MAX_LED_MATRIX_BRIGHTNESS = 128;
      break;
  }
  #ifdef DEBUG_FSM
    Serial.print(F("Brightness changed: "));
    Serial.println(MAX_LED_MATRIX_BRIGHTNESS);
  #endif
  HEARTBEAT_LED_MATRIX_BRIGHTNESS = MAX_LED_MATRIX_BRIGHTNESS/10;
  //LED_MATRIX_DECAY_RATE = MAX_LED_MATRIX_BRIGHTNESS / 64;
  LED_MATRIX_DECAY_FREQUENCY = 1024/MAX_LED_MATRIX_BRIGHTNESS;
  EEPROM.update(MAX_LED_MATRIX_BRIGHTNESS_EEPROM_LOCATION, MAX_LED_MATRIX_BRIGHTNESS);
  ledMatrix->setBrightness(MAX_LED_MATRIX_BRIGHTNESS);
  lastMenuStateChange = millis();
  showCurrentBrightness();
}

void showCurrentBrightness()
{
  textToScroll = "Brightness: " + String(MAX_LED_MATRIX_BRIGHTNESS);
  scrollText(textToScroll);
}

void changeScrollSpeed()
{
  if(scrollDelay == SCROLL_DELAY_FAST)
  {
    scrollDelay = SCROLL_DELAY_MEDIUM;
    EEPROM.update(SCROLL_SPEED_EEPROM_LOCATION, 1);
  }
  else if(scrollDelay == SCROLL_DELAY_MEDIUM)
  {
    scrollDelay = SCROLL_DELAY_SLOW;
    EEPROM.update(SCROLL_SPEED_EEPROM_LOCATION, 2);
  }
  else if(scrollDelay == SCROLL_DELAY_SLOW)
  {
    scrollDelay = SCROLL_DELAY_FAST;
    EEPROM.update(SCROLL_SPEED_EEPROM_LOCATION, 3);
  }
  lastMenuStateChange = millis();
  showScrollSpeed();
}

void showScrollSpeed()
{
  textToScroll = "Text scroll: ";
  if(scrollDelay == 30)
  {
    textToScroll += "fast";
  }
  else if(scrollDelay == 50)
  {
    textToScroll += "medium";
  }
  else if(scrollDelay == 75)
  {
    textToScroll += "slow";
  }
  scrollText(textToScroll);
}

void changeBlinkRate()
{
  if(onTime == 0)
  {
    onTime = defaultOnTime;
    offTime = 1000 - onTime ;
    EEPROM.update(BLINK_PERIOD_EEPROM_LOCATION, 1);
  }
  else if(onTime + offTime == 1000)
  {
    offTime = 3000 - onTime ;
    EEPROM.update(BLINK_PERIOD_EEPROM_LOCATION, 3);
  }
  else if(onTime + offTime == 3000)
  {
    offTime = 5000 - onTime ;
    EEPROM.update(BLINK_PERIOD_EEPROM_LOCATION, 5);
  }
  else if(onTime + offTime == 5000)
  {
    offTime = 10000 - onTime ;
    EEPROM.update(BLINK_PERIOD_EEPROM_LOCATION, 10);
  }
  else if(onTime + offTime == 10000)
  {
    onTime = 0;
    EEPROM.update(BLINK_PERIOD_EEPROM_LOCATION, 0);
  }
  #ifdef DEBUG_FSM
    Serial.print(F("Blink: "));
    if(onTime > 0)
    {
      Serial.println(onTime + offTime);
    }
    else
    {
      Serial.println("disabled");
    }
  #endif
  lastMenuStateChange = millis();
  showBlinkRate();
}

void showBlinkRate()
{
  if(onTime > 0)
  {
    textToScroll = "Blink: " + String(uint8_t((onTime + offTime)/1e3)) + String("s");
  }
  else
  {
    textToScroll = "Blink disabled";
  }
  scrollText(textToScroll);
}

#ifdef BUTTON_CONNECTED
  /*
   * 
   * Simple button debounce/management
   * 
   */
  void manageButton()
  {
    if(buttonState == BUTTON_STATE_IDLE)
    {
      if(digitalRead(buttonPin) == false && buttonPushTime == 0)
      {
        buttonPushTime = millis();
      }
      if(digitalRead(buttonPin) == true && buttonPushTime != 0)
      {
        if(millis() - buttonPushTime > 750 && millis() - buttonPushTime < 3000)
        {
          buttonPushTime = 0;
          buttonState = BUTTON_STATE_LONGPRESS;
        }
        else if(millis() - buttonPushTime > 20 && millis() - buttonPushTime <= 750)
        {
          buttonPushTime = 0;
          buttonState = BUTTON_STATE_SHORTPRESS;
        }
        else
        {
          buttonPushTime = 0;
        }
      }
    }
  }
  bool longPress()
  {
    if(buttonState == BUTTON_STATE_LONGPRESS)
    {
      #ifdef DEBUG_FSM
        Serial.println(F("Long button press"));
      #endif
      buttonState = BUTTON_STATE_IDLE;
      return true;
    }
    return false;
  }
  bool shortPress()
  {
    if(buttonState == BUTTON_STATE_SHORTPRESS)
    {
      #ifdef DEBUG_FSM
        Serial.println(F("Short button press"));
      #endif
      buttonState = BUTTON_STATE_IDLE;
      return true;
    }
    return false;
  }
#endif



void changeMatrixState(uint8_t newState){
  if(currentMatrixState != newState){
    currentMatrixState = newState;
    #ifdef DEBUG_FSM
      Serial.print(F("Matrix state: "));
      switch(currentMatrixState) {
        case MATRIX_STATE_IDLE:
            Serial.println(F("Idle"));
          break;
        case MATRIX_STATE_HIT:
            Serial.println(F("Hit"));
          break;
        case MATRIX_STATE_COUNT_OUT_HITS:
            Serial.println(F("Counting out hits"));
          break;
        case MATRIX_STATE_SHOW_HIT_INFO:
            Serial.println(F("Showing hit info"));
          break;
        case MATRIX_STATE_SHOW_DATA:
            Serial.println(F("Show Data"));
          break;
        case MATRIX_STATE_WAITING:
            Serial.println(F("Waiting"));
          break;
        case MATRIX_STATE_SCROLL_STRING:
            Serial.println(F("Scroll text"));
          break;
        default:
            Serial.println(F("Unknown"));
          break;
      }
    #endif
  }
}

void changeMenuState(uint8_t newState){
  if(currentMenuState != newState){
    currentMenuState = newState;
    #ifdef DEBUG_FSM
      Serial.print(F("Menu state: "));
      switch(currentMenuState) {
        case MENU_STATE_IDLE:
            Serial.println(F("Idle"));
          break;
        case MENU_STATE_MODE:
            Serial.println(F("choose mode"));
          break;
        case MENU_STATE_BRIGHTNESS:
            Serial.println(F("set brightness"));
          break;
        case MENU_STATE_BLINK:
            Serial.println(F("set blink rate"));
          break;
        case MENU_STATE_SCROLL_SPEED:
            Serial.println(F("set scroll speed"));
          break;
        default:
            Serial.println(F("Unknown"));
          break;
      }
    #endif
    lastMenuStateChange = millis();
  }
}
