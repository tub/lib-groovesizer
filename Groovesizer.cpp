#include <Arduino.h>
#include "Groovesizer.h"

// LED shift register pins
const byte LED_DATA_PIN  = 4;
const byte LED_CLOCK_PIN = 6;
const byte LED_LATCH_PIN = 2;

// button shift register pins
const byte BUTTON_LATCH_PIN = 7;
const byte BUTTON_CLOCK_PIN = 8;
const byte BUTTON_DATA_PIN  = 9;

// analog multiplexer 4051 for the potentiometers
// array of pins used to select 1 of 8 inputs on multiplexer
const byte MUX_CHAN_PINS[] = { 17, 16, 15 }; // pins connected to the 4051 input select lines
const byte MUX_INPUT_PIN = 0;      // the analog pin connected to multiplexer output
const byte MUX_CHAN_PIN_COUNT = 3;

const byte Groovesizer::NUM_POTS = 6;

// range for analog readings, max range is 0-1023
const int ANALOG_RANGE_MIN = 25;
const int ANALOG_RANGE_MAX = 1000;

// debounce duration (millis)
const byte DEBOUNCE_MILLIS = 10;

// coords start bottom left, 0,0

const byte Groovesizer::MATRIX_COLS = 8;
const byte Groovesizer::MATRIX_ROWS = 5;

// for the rows of LEDs
// the byte that will be shifted out 
byte ledRows[Groovesizer::MATRIX_ROWS];

// booleans for whether button is currently pressed or not
byte buttonState[Groovesizer::MATRIX_ROWS];
// was it previously pressed or not?
byte prevButtonState[Groovesizer::MATRIX_ROWS];
// when was it pressed last? used for 'hold' time
unsigned int buttonDownTime[Groovesizer::MATRIX_ROWS * Groovesizer::MATRIX_COLS];

int prevPotState[Groovesizer::NUM_POTS];

// Keep track of the number of times update is called.
// Used for timing & led duty cycle purposes.
unsigned long loopCount = 0;

// Used for debouncing the button reading.
unsigned long lastButtonRead = 0;


/**
 * Constructor, sets up the pinModes for required IO
 * Does initial read of state to avoid triggering callbacks on first loop.
 */
Groovesizer::Groovesizer(){
    // setup
    pinMode(LED_DATA_PIN,  OUTPUT);
    pinMode(LED_CLOCK_PIN, OUTPUT);
    pinMode(LED_LATCH_PIN, OUTPUT);

    pinMode(BUTTON_DATA_PIN,  INPUT);
    pinMode(BUTTON_CLOCK_PIN, OUTPUT);
    pinMode(BUTTON_LATCH_PIN, OUTPUT);

    _buttonUpCallback = NULL;
    _buttonDownCallback = NULL;
    _potChangeCallback = NULL;

    for(byte bit = 0; bit < MUX_CHAN_PIN_COUNT; bit++){
      pinMode(MUX_CHAN_PINS[bit], OUTPUT);  // set the three 4051 select pins to output
    }

    // set up initial values before we have any callbacks registered to trigger
    this->read();
}

/**
 * Destructor
 */ 
Groovesizer::~Groovesizer(){
  _buttonUpCallback = NULL;
  _buttonDownCallback = NULL;
  _potChangeCallback = NULL;
}

/**
 * Sets the specified LED on or off according to val.
 * 
 * Row 0 is bottom row, col 0 is left-most column.
 */
void Groovesizer::setLed(byte row, byte col, bool val){
  bitWrite(ledRows[row], Groovesizer::MATRIX_COLS - 1 - col, val);
}

/**
 * sets a whole row of LEDs, each bit of `val` corresponds to an LED. 
 * LSB is right-most, MSB is left-most.
 */
void Groovesizer::setLedRow(byte row, byte val){
  ledRows[row] = val;
}

/**
 * Gets the current button-state of the specified button.
 * Row 0 is bottom, col 0 is left-most.
 */
bool Groovesizer::getButton(byte row, byte col){
  return bitRead(buttonState[row], col);
}

/**
 * Reads any values from pots, buttons, etc
 * triggers callback functions
 * MUST be called at start of loop()
 */
void Groovesizer::read(){
    this->readButtons();
    this->readPots();
}

/** 
 * Writes values to LEDs
 * MUST be called at end of loop()
 */ 
void Groovesizer::write(){
    this->writeLeds();
    loopCount++;
}

/**
 * Reads button values
 * only collects data every DEBOUNCE_MILLIS ms to debounce buttons.
 */
void Groovesizer::readButtons(){
    //debounce - only read buttons every DEBOUNCE_MILLIS ms
    if(lastButtonRead + DEBOUNCE_MILLIS < millis()){
      // skip this time
      return;
    }

    //Collect button data from the 4901s
    //set latch to 0 to transmit data serially  
    digitalWrite(BUTTON_LATCH_PIN,0);

    //while the shift register is in serial mode
    //collect each shift register into a byte
    //the register attached to the chip comes in first 
    for (byte i = 0; i < Groovesizer::MATRIX_ROWS; i++){
      byte row = Groovesizer::MATRIX_ROWS - (i + 1);
      // store previous state so we can figure out pressed, released
      prevButtonState[row] = buttonState[row];
      //read in new data
      buttonState[row] = this->shiftIn(BUTTON_DATA_PIN, BUTTON_CLOCK_PIN);

      if(buttonState[row] != prevButtonState[row]){
          for(byte col = 0; col < MATRIX_COLS; col++){
            //see if we have a button down or up event for each button in the row
            bool prev = bitRead(prevButtonState[row], col);
            bool curr = bitRead(buttonState[row], col);
            if(_buttonDownCallback && !prev && curr){
              // button down
              buttonDownTime[(row * Groovesizer::MATRIX_COLS) + col] = millis();
              _buttonDownCallback(row, col);
            }else if(_buttonUpCallback && !curr && prev){
              //button up
              unsigned int heldFor = millis() - buttonDownTime[(row * Groovesizer::MATRIX_COLS) + col];
              _buttonUpCallback(row, col, heldFor);
            }
          }
      }
    }

     //set it to 1 to collect parallel data & wait
    digitalWrite(BUTTON_LATCH_PIN,1);

    lastButtonRead = millis();
}

void Groovesizer::readPots(){
  for(byte i = 0; i < NUM_POTS; i++){
    int potVal = this->getPotValue(i);

    if(potVal >> 3 != prevPotState[i]){
      if(_potChangeCallback){
        _potChangeCallback(i, potVal);
      }
      prevPotState[i] = potVal >> 3;
    }
  }
}

// POTS
// reading from 4051
// this function returns the analog value for the given channel
int Groovesizer::getPotValue(byte channel) {
  // set the selector pins HIGH and LOW to match the binary value of channel
  for(int bit = 0; bit < MUX_CHAN_PIN_COUNT; bit++) {
    int pin = MUX_CHAN_PINS[bit]; // the pin wired to the multiplexer select bit
    int isBitSet = bitRead(channel, bit); // true if given bit set in channel
    digitalWrite(pin, isBitSet);
  }
  // map range between ANALOG_RANGE_MIN and ANALOG_RANGE_MAX, extremes of pots are unreliable
  return map((constrain(analogRead(MUX_INPUT_PIN), ANALOG_RANGE_MIN, ANALOG_RANGE_MAX)),
          ANALOG_RANGE_MIN, ANALOG_RANGE_MAX, 0, 1023);
}


void Groovesizer::writeLeds(){
    //ground LED_LATCH_PIN and hold low for as long as you are transmitting
    digitalWrite(LED_LATCH_PIN, 0);
    for(int i = 0; i < Groovesizer::MATRIX_ROWS; i++){
      shiftOut(LED_DATA_PIN, LED_CLOCK_PIN, LSBFIRST, (loopCount % 2 == 0) ? ledRows[i] : B00000000);
    }

    //return the latch pin high to signal chip that it 
    //no longer needs to listen for information
    digitalWrite(LED_LATCH_PIN, 1);
}

byte Groovesizer::shiftIn(int dataPin, int clockPin) { 
  byte data;

  //at the begining of each loop when we set the clock low, it will
  //be doing the necessary low to high drop to cause the shift
  //register's DataPin to change state based on the value
  //of the next bit in its serial information flow.
  //The register transmits the information about the pins from pin 7 to pin 0
  //so that is why our function counts down
  for (byte i = 0; i <= 7; i++)
  {
    digitalWrite(clockPin, LOW);
    delayMicroseconds(2);
    bitWrite(data, i, digitalRead(dataPin));
    digitalWrite(clockPin, HIGH);
  }
  return data;
}

void Groovesizer::setButtonUpListener(void (*buttonUpCallback)(byte col, byte row, unsigned int millisHeldFor)){
  _buttonUpCallback = buttonUpCallback;
}

void Groovesizer::setButtonDownListener(void (*buttonDownCallback)(byte col, byte row)){
  _buttonDownCallback = buttonDownCallback;
}

void Groovesizer::setPotChangeListener(void (*potChangeCallback)(byte pot, int val)){
  _potChangeCallback = potChangeCallback;
}
