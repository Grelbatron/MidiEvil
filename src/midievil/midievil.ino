// Grelbatron Midi Switch
// Based on MIDI Switch v1.2 (2020-08-01) by jimkim.de https://www.jimkim.de
// 2022-06-24
// This MIDI device can receive MIDI program change and control change messages
// For each program (preset) outputs can be individually set (settings are saved to non volatile memory)
// Additionally, a MIDI controller can be assigned to each output (e.g. controller number 4 --> output 1)
// Controller values from 0-63 will switch off the output, values from 64-127 will switch it on
// Program change function and control change function can be globally enabled / disabled in the setup menu
// 5 Momentary Switches for recalling PGM/Preset and Direct Relay states
// 2 Monentary buttons for Edit mode functions and PGM Bank toggle
// FSM Mode PGM: Uses 5 momentary Switches to recall 10 presets over 2 banks. Edit button one toggles between banks.
// FSM Mode Direct: USes 5 momentary buttons to directly recall switch state of associated relay

#include <SoftwareSerial.h>           // use SoftwareSerial lib instead of the serial lib; this lets us control which pins the MIDI interface is connected to. only RX is needed; set RX = TX pin to save one I/O pin
SoftwareSerial midiSerial(12, 12);
#include <EEPROM.h>
#include <Wire.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"
#include <Debouncer.h>
#include "OneButton.h"
#define I2C_ADDRESS 0x3C              // OLED address: 0X3C+SA0 - 0x3C or 0x3D
#define RST_PIN -1                    // define proper RST_PIN if required
SSD1306AsciiWire oled;
#define displayResetTimer (long)1000
unsigned long displayResetTimestamp = 0;    // timestamp when display was updated
byte displayReset = 0;
OneButton button1(A0, true);          // setup a new OneButton on pin A0
OneButton button2(A1, true);          // setup a new OneButton on pin A1
//footswitch pin array
const byte footswitchPins[] = {
  7, 8, 9, 10, 11
};
const byte footswitchCount = 5;
byte thisFootswitch = 0;              // variable to track which footswitch has been engaged via debounce functions
byte debounce_duration_ms = 10;
int rise_count = 0;
int fall_count = 0;
Debouncer debouncer1(7, debounce_duration_ms);
Debouncer debouncer2(8, debounce_duration_ms);
Debouncer debouncer3(9, debounce_duration_ms);
Debouncer debouncer4(10, debounce_duration_ms);
Debouncer debouncer5(11, debounce_duration_ms);
// define output pins
byte outputPins[] = {
  2, 3, 4, 5, 6
};
byte outputCount = 5;                 // set this to the required number of outputs (1-8) if you do not use all 8 outputs. higher output numbers will than be ignored in setup menu etc.
byte outputPinsState;                 // var containing output setting for each output pin
// constants
#define MIDI_LED A2
#define EDIT_LED A3
const long BLINK_INTERVAL = 500;
bool ledState = LOW;                  // ledState used to set the LED
bool previousButtonState = LOW;       // will store last time button was updated
unsigned long previousMillis = 0;     // will store last time LED was updated
const long midiLedTimer = 250;
unsigned long midiLedTimestamp = 0;   // timestamp when led was activated
#define MIDI_PGM_CHANGE 192
#define MIDI_CTL_CHANGE 176
byte programChangeActive = 1;         // switch listens to program change messages
byte controlChangeActive = 1;         // switch listens to control change messages
byte directChangeActive = 1;          // footswitch mode (FSM) 0= PGM Recall, 1 = Switch State Toggle
byte statusByte;
byte dataByte1;
byte dataByte2;
byte midiChannel;                     // MIDI channel data from MIDI in
byte selectedMidiChannel;             // currently selected MIDI channel (which channel to listen to)
char midiChannelChar[4];
char pgmBankChar[4];
byte pgmNumber = 0;                   // init pgmNumber for arduino startup
char pgmNumberChar[4];
byte midiCommand;
byte ctrlNumber = 0;                  // MIDI controller
char ctrlNumberChar[4];
byte ctrlValue = 0;
char ctrlValueChar[4];
byte outputPinsControllerNumber[] = { // MIDI controller number assignment (controller number <--> output pin)
  0, 0, 0, 0, 0, 0,
};
char title[16] = "Where's Ettore?";
byte currentSwitchIndex = 0;
byte currentSwitchState = 0;
byte switchChange = 0;
byte editSettings = 0;
byte editSwitches = 0;
byte editProgramChangeFunction = 0;
byte editControlChangeFunction = 0;
byte editDirectChangeFunction = 0;
byte editControllers = 0;
byte editMidiChannel = 0;
byte currentControllerIndex = 0;
byte controllerNumberIncrement = 0;
byte valueIncrementSize = 0;
byte pgmBank = 0;

void setup() {
  Serial.begin(9600);                 // setup HW serial for debug communication
  pinMode(MIDI_LED, OUTPUT);
  pinMode(EDIT_LED, OUTPUT);
  for (byte thisSwitch = 0; thisSwitch < footswitchCount; thisSwitch++) {        // loop over the pin array and set them all to input_pullup
    pinMode(footswitchPins[thisSwitch], INPUT_PULLUP);
  }
  outputPinsState = EEPROM.read(pgmNumber);       // read output switch setting from EEPROM
  for (byte thisOutput = 0; thisOutput < outputCount; thisOutput++) {           // loop over the pin array and set them all to output:
    pinMode(outputPins[thisOutput], OUTPUT);
    if ( bitRead(outputPinsState, thisOutput) == 1 ) {        // if outputState for this pin is active, set HIGH
      digitalWrite(outputPins[thisOutput], HIGH);
    }
  }
  selectedMidiChannel = EEPROM.read(500);       // read MIDI channel setting from EEPROM
  programChangeActive = EEPROM.read(501);       // read program change function setting from EEPROM
  controlChangeActive = EEPROM.read(502);       // read control change function setting from EEPROM
  directChangeActive = EEPROM.read(503);        // read direct footswitch change function setting from EEPROM
  // at very first startup after sketch upload (before any custom MIDI channel has been actively set by user)
  // the default EEPROM value may be > 15 if so, set value = 0 (which is MIDI channel 1)
  if ( selectedMidiChannel > 15 ) {
    selectedMidiChannel = 0;
  }
  // read MIDI controller number assignment from EEPROM
  // special routine because controller number array is int (2 bytes) and needs two EEPROM addresses for each value
  int startAddress = 400;
  for (byte thisOutput = 0; thisOutput < outputCount; thisOutput++) {
    outputPinsControllerNumber[thisOutput] = eepromReadInt(startAddress);
    startAddress += 2;
  }
  // setup SoftSerial for MIDI control
  midiSerial.begin(31250);
  //MIDI.begin(selectedMidiChannel);        // launch MIDI listening to selected midi channel
  Wire.begin();       // init OLED display
  Wire.setClock(400000L);
#if RST_PIN >= 0
  oled.begin(&Adafruit128x64, I2C_ADDRESS, RST_PIN);
#else // RST_PIN >= 0
  oled.begin(&Adafruit128x64, I2C_ADDRESS);
#endif // RST_PIN >= 0
  oled.setFont(ZevvPeep8x16);
  refreshDisplay(11);
  // DEFAULT OPERATION    |  EDIT MODE
  button1.attachClick(Click1);                        // FSM:PGM Bank Select  |  toggle edit mode paramteres
  button1.attachLongPressStart(longPressStart1);      // start edit mode      |  save and exit edit mode
  button2.attachClick(Click2);                        //                      |  toggle/increment edit parameters values
  button2.attachLongPressStart(longPressStart2);      //                      |  increment controller numbers by 10
}
void loop () {
  if ( millis() - midiLedTimestamp >= midiLedTimer ) {        // turn off MIDI led
    digitalWrite(MIDI_LED, LOW);        // if timer has run out
  }
  if ( displayReset == 1 ) {        // reset display to default view (MIDI channel)
    if ( millis() - displayResetTimestamp >= displayResetTimer ) {        // if timer has run out
      refreshDisplay(11);
      displayReset = 0;
    }
  }
  debouncer1.update();
  debouncer2.update();
  debouncer3.update();
  debouncer4.update();
  debouncer5.update();
  // is there any MIDI waiting to be read?
  if ( midiSerial.available() > 0 && editSettings == 0 ) {
    // read MIDI byte
    statusByte = midiSerial.read();
    // remove program info from status byte (only channel value left)
    midiChannel = statusByte & B00001111;
    // remove channel info from status byte (only program value left)
    midiCommand = statusByte & B11110000;
    if ( midiChannel == selectedMidiChannel ) {
      delay(2);
      switch ( midiCommand ) {     // get the type of the message
        case MIDI_PGM_CHANGE:       // if this is a program change command
          pgmNumber = midiSerial.read() + 1;                // get program number (data byte 1) from MIDI routine
          if ( programChangeActive == 1 ) {         // listen to program change?
            outputPinsState = EEPROM.read(pgmNumber);       // read output switch setting from eeprom for this program number
            for ( byte thisOutput = 0; thisOutput < outputCount; thisOutput++ ) {        // set outputs
              if ( bitRead(outputPinsState, thisOutput ) == 1 ) {       // if outputState for this pin is active, set HIGH
                digitalWrite( outputPins[thisOutput], HIGH );
              } else {
                digitalWrite( outputPins[thisOutput], LOW );
              }
            }
            refreshDisplay(21);
          }
          digitalWrite(MIDI_LED, HIGH);       // flash MIDI led
          midiLedTimestamp = millis();
          break;
        case MIDI_CTL_CHANGE:         // if this is a control change command
          ctrlNumber = midiSerial.read();   // get controller number (data byte 1) from MIDI routine
          delay(2);
          ctrlValue = midiSerial.read();    // get controller value (data byte 2) from MIDI routine
          if ( controlChangeActive == 1 ) {         // listen to control change?
            for (byte thisOutput = 0; thisOutput < outputCount; thisOutput++) {
              if ( outputPinsControllerNumber[thisOutput] == ctrlNumber ) {
                if ( 0 <= ctrlValue && ctrlValue <= 63 ) {
                  digitalWrite( outputPins[thisOutput], LOW );
                  bitClear(outputPinsState, thisOutput);
                }
                if ( 64 <= ctrlValue && ctrlValue <= 127 ) {
                  digitalWrite( outputPins[thisOutput], HIGH );
                  bitSet(outputPinsState, thisOutput);
                }
                if ( !midiSerial.available() ) {
                  refreshDisplay(24);               // update display
                  displayReset = 1;
                  displayResetTimestamp = millis();
                }
              }
            }
          }
          digitalWrite(MIDI_LED, HIGH);     // flash MIDI led
          midiLedTimestamp = millis();
          break;
        // flush other message types
        default:
          break;
      }
    }  // end check midi channel
  } // end receive midi
  // FSM Direct Change
  if ( editSettings == 0 ) {
    // edit switches
    if ( editSwitches == 0 ) {
      // check if output state has been changed
      if (directChangeActive == 1) {
        // check if direct footswitch change is active
        if ( switchChange == 1 ) {
          // toggle switch state
          if ( bitRead(outputPinsState, currentSwitchIndex) == 1 ) {
            digitalWrite(outputPins[currentSwitchIndex], LOW);
            bitClear(outputPinsState, currentSwitchIndex);
          } else {
            digitalWrite(outputPins[currentSwitchIndex], HIGH);
            bitSet(outputPinsState, currentSwitchIndex);
          }
          switchChange = 0;
        }
      }
    }
  } // end FSM Direct Change
  // edit mode
  if ( editSettings == 1 ) {
    // edit switches
    if ( editSwitches == 1 ) {
      // check if output state has been changed
      if ( switchChange == 1 ) {
        // toggle switch state
        if ( bitRead(outputPinsState, currentSwitchIndex) == 1 ) {
          digitalWrite(outputPins[currentSwitchIndex], LOW);
          bitClear(outputPinsState, currentSwitchIndex);
        } else {
          digitalWrite(outputPins[currentSwitchIndex], HIGH);
          bitSet(outputPinsState, currentSwitchIndex);
        }
        switchChange = 0;
      }
    } // end edit switches
    // edit controllers
    if ( editControllers == 1 ) {
      // check if controller number has been incremented (switch pressed)
      if ( controllerNumberIncrement == 1 ) {
        if ( valueIncrementSize == 1 ) {        // increment controller number by 1
          if ( outputPinsControllerNumber[currentControllerIndex] < 127 ) {
            outputPinsControllerNumber[currentControllerIndex] += valueIncrementSize;
          } else {
            outputPinsControllerNumber[currentControllerIndex] = 0;
          }
        }
        if ( valueIncrementSize == 10 ) {        // increment controller number by 10
          if ( outputPinsControllerNumber[currentControllerIndex] < 118 ) {
            outputPinsControllerNumber[currentControllerIndex] += valueIncrementSize;
          } else {
            outputPinsControllerNumber[currentControllerIndex] = 0;
          }
        }
        controllerNumberIncrement = 0;
        refreshDisplay(42);
      }
    } // end edit controllers
    // edit program change function
    if ( editProgramChangeFunction == 1 ) {
      if ( switchChange == 1 ) {        // check if program change function has been changed
        if ( programChangeActive == 1 ) {       // toggle function state
          programChangeActive = 0;
          refreshDisplay(43);
        } else {
          programChangeActive = 1;
          refreshDisplay(43);
        }
        switchChange = 0;
      }
    } // end edit program change function
    // edit control change function
    if ( editControlChangeFunction == 1 ) {
      if ( switchChange == 1 ) {        // check if control change function has been changed
        if ( controlChangeActive == 1 ) {       // toggle function state
          controlChangeActive = 0;
          refreshDisplay(44);
        } else {
          controlChangeActive = 1;
          refreshDisplay(44);
        }
        switchChange = 0;
      }
    } // end edit control change function
    // edit direct footswitch change function
    if ( editDirectChangeFunction == 1 ) {
      if ( switchChange == 1 ) {        // check if control change function has been changed
        if ( directChangeActive == 1 ) {       // toggle function state
          directChangeActive = 0;
          refreshDisplay(45);
        } else {
          directChangeActive = 1;
          refreshDisplay(45);
        }
        switchChange = 0;
      }
    } // end edit direct footswitch change function
  } // end edit
  // keep watching the push buttons:
  button1.tick();
  button2.tick();
  if (debouncer1.edge()) {
    if (debouncer1.falling()) {
      thisFootswitch = 1;
      Footswitch();
    }
  }
  if (debouncer2.edge()) {
    if (debouncer2.falling()) {
      thisFootswitch = 2;
      Footswitch();
    }
  }
  if (debouncer3.edge()) {
    if (debouncer3.falling()) {
      thisFootswitch = 3;
      Footswitch();
    }
  }
  if (debouncer4.edge()) {
    if (debouncer4.falling()) {
      thisFootswitch = 4;
      Footswitch();
    }
  }
  if (debouncer5.edge()) {
    if (debouncer5.falling()) {
      thisFootswitch = 5;
      Footswitch();
    }
  }
  if ( editSettings == 0 ) {
    if ( directChangeActive == 0 ) {
      if ( pgmBank == 1 ) {
        // check to see if it's time to blink the LED; that is, if the difference
        // between the current time and last time you blinked the LED is bigger than
        // the interval at which you want to blink the LED.
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= BLINK_INTERVAL) {
          ledState = (ledState == LOW) ? HIGH : LOW;   // if the LED is off turn it on and vice-versa:
          digitalWrite(A3, ledState);   // set the LED with the ledState of the variable:
          previousMillis = currentMillis;   // save the last time you blinked the LED
        }
      } else {
        digitalWrite(A3, 0);
      }
    }
  }
} // loop

// functions
void Footswitch () {
  if ( editSettings == 0) {
    if ( directChangeActive == 1) {   // check if direct change is active
      currentSwitchIndex = (thisFootswitch - 1);
      //    refreshDisplay(41); //refresh display switch change only
      if ( switchChange == 0 ) {
        switchChange = 1;
      }
    } else {
      if ( pgmBank == 0 ) {
        pgmNumber = thisFootswitch; // recall program 1
      } else {
        pgmNumber = (thisFootswitch + 5);
      }
      outputPinsState = EEPROM.read(pgmNumber);       // read output switch setting from eeprom for this program number
      for ( byte thisOutput = 0; thisOutput < outputCount; thisOutput++ ) {        // set outputs
        if ( bitRead(outputPinsState, thisOutput ) == 1 ) {       // if outputState for this pin is active, set HIGH
          digitalWrite( outputPins[thisOutput], HIGH );
        } else {
          digitalWrite( outputPins[thisOutput], LOW );
        }
        refreshDisplay(21);
      }
    }
  } else {
    editSwitches = 1;
    currentSwitchIndex = (thisFootswitch - 1) ;
    refreshDisplay(41); //refresh display switch change only
    if ( switchChange == 0 ) {
      switchChange = 1;
    }
  }
}
// button 1 short press: select switch outputs / MIDI controllers in edit mode
void Click1() {
  if ( editSettings == 1 ) {
    editSwitches = 0;
    // edit program change function
    if ( editProgramChangeFunction == 1 ) {
      editProgramChangeFunction = 0;
      editControlChangeFunction = 1;
      refreshDisplay(44);
      return;
    }
    // edit control change function
    if ( editControlChangeFunction == 1 ) {
      editControlChangeFunction = 0;
      editDirectChangeFunction = 1;
      refreshDisplay(45);
      return;
    }
    // edit direct footswitch change function
    if ( editDirectChangeFunction == 1 ) {
      editDirectChangeFunction = 0;
      editMidiChannel = 1;
      refreshDisplay(46);
      return;
    }
    // edit midi channel
    if ( editMidiChannel == 1 ) {
      editMidiChannel = 0;
      editControllers = 1;
      refreshDisplay(42);
      return;
    }
    // edit all controllers
    if ( editControllers == 1 ) {
      if ( currentControllerIndex < outputCount - 1 ) {
        currentControllerIndex++;
        refreshDisplay(42);
      } else {
        editControllers = 0;
        currentControllerIndex = 0;
        editProgramChangeFunction = 1;
        refreshDisplay(43);
      }
      return;
    }
  } else {
    if (directChangeActive == 0) {
      if ( pgmBank == 0 ) {
        pgmBank = 1;
        refreshDisplay(25);
      } else {
        pgmBank = 0;
        refreshDisplay(25);
      }
    }
  }
}
// button 1 long press: start edit mode / save and exit edit mode
void longPressStart1() {
  // toggle edit mode
  if ( editSettings == 0) {
    // start edit mode
    editSettings = 1;
    editProgramChangeFunction = 1;
    if ( directChangeActive == 0) {
      outputPinsState = EEPROM.read(pgmNumber);
    }
    // reset switch index
    currentSwitchIndex = 0;
    currentControllerIndex = 0;
    // refresh display
    refreshDisplay(43);
    digitalWrite(EDIT_LED, HIGH);
  } else {
    // exit edit mode
    editSettings = 0;
    editSwitches = 0;
    editControllers = 0;
    editProgramChangeFunction = 0;
    editControlChangeFunction = 0;
    editMidiChannel = 0;
    // write settings to EEPROM
    EEPROM.update(pgmNumber, outputPinsState);
    // write controller number assignments to EEPROM
    // special routine because controller number array is int (2 bytes) and needs two EEPROM addresses for each value
    int startAddress = 400;
    for (int thisOutput = 0; thisOutput < outputCount; thisOutput++) {
      eepromWriteInt(startAddress, outputPinsControllerNumber[thisOutput]);
      startAddress += 2;
    }
    // write program change and control change function settings to EEPROM
    EEPROM.update(500, selectedMidiChannel);
    EEPROM.update(501, programChangeActive);
    EEPROM.update(502, controlChangeActive);
    EEPROM.update(503, directChangeActive);
    refreshDisplay(11);
    // switch off edit led
    digitalWrite(EDIT_LED, LOW);
  }
}
// button 2 short press: toggle switch output / increment controller numbers by 1
void Click2() {
  editSwitches = 0;
  if ( editSettings == 1 && editSwitches == 1 ) {
    if ( switchChange == 0 ) {
      switchChange = 1;
    }
  }
  if ( editSettings == 1 && editControllers == 1 ) {
    controllerNumberIncrement = 1;
    valueIncrementSize = 1;
  }
  if ( editSettings == 1 && editProgramChangeFunction == 1 ) {
    if ( switchChange == 0 ) {
      switchChange = 1;
    }
  }
  if ( editSettings == 1 && editControlChangeFunction == 1 ) {
    if ( switchChange == 0 ) editControlChangeFunction; {
      switchChange = 1;
    }
  }
  if ( editSettings == 1 && editDirectChangeFunction == 1 ) {
    if ( switchChange == 0 ) {
      switchChange = 1;
    }
  }
  if ( editSettings == 1 && editMidiChannel == 1 ) {
    selectedMidiChannel++;
    if ( selectedMidiChannel > 15 ) {
      selectedMidiChannel = 0;
    }
    refreshDisplay(46);
  }
}
// button 2 long press: edit MIDI channel / increment controller numbers by 10
void longPressStart2() {
  // if in edit mode (edit controllers) increment controller number by 10
  if ( editSettings == 1 && editControllers == 1 ) {
    controllerNumberIncrement = 1;
    valueIncrementSize = 10;
  }
}

// display actions
void refreshDisplay(byte mode) {
  // startup
  if ( mode == 11 ) {
    oled.clear();
    oled.set1X();
    oled.println(title);
    if ( directChangeActive == 1) {
      oled.print(F("Direct"));
    } else {
      sprintf(pgmBankChar, "%01d", pgmBank + 1);
      oled.print(F("Bank"));
      oled.setCursor(36, 2);
      oled.print(pgmBankChar);
    }
    sprintf(midiChannelChar, "%02d", selectedMidiChannel + 1);
    oled.setCursor(52, 2);
    oled.print("Ch");
    oled.setCursor(68, 2);
    oled.print (midiChannelChar);
    oled.setCursor(88, 2);
    if ( programChangeActive == 1 ) {
      oled.print(F("PC:"));
    } else {
      oled.print(F("--:"));
    }
    oled.setCursor(110, 2);
    if ( controlChangeActive == 1 ) {
      oled.print(F("CC"));
    } else {
      oled.print(F("--"));
    }
    oled.set2X();
    oled.setCursor(0, 4);
    oled.print(F("PGM:"));
    sprintf(pgmNumberChar, "%03d", pgmNumber);
    oled.setCursor(80, 4);
    oled.print(pgmNumberChar);
  }
  // update program number
  if ( mode == 21 ) {
    sprintf(pgmNumberChar, "%03d", pgmNumber);
    oled.set2X();
    oled.setCursor(80, 4);
    oled.print(pgmNumberChar);
  }
  // show control change data
  if ( mode == 24 ) {
    oled.set1X();
    sprintf(ctrlNumberChar, "%03d", ctrlNumber);
    sprintf(ctrlValueChar, "%03d", ctrlValue);
    oled.setCursor(0, 2);
    oled.println(F("Ctrl:Val "));
    oled.setCursor(72, 2);
    oled.print(ctrlNumberChar);
    oled.setCursor(96, 2);
    oled.print(F(":"));
    oled.setCursor(104, 2);
    oled.print(ctrlValueChar);
  }
  // update Bank Number
  if ( mode == 25 ) {
    oled.set1X();
    sprintf(pgmBankChar, "%01d", pgmBank + 1);
    oled.setCursor(36, 2);
    oled.print(pgmBankChar);
  }
  // update PGM in edit mode, when switch engaged
  if ( mode == 41 ) {
    oled.set2X();
    oled.setCursor(0, 4);
    oled.print(F("SAVE:"));
    sprintf(pgmNumberChar, "%03d", pgmNumber);
    oled.setCursor(80, 4);
    oled.print(pgmNumberChar);
  }
  // update controller index and number
  if ( mode == 42 ) {
    oled.setCursor(0, 4);
    oled.set2X();
    oled.print(F("CC : "));
    oled.setCursor(32, 4);
    oled.print(currentControllerIndex + 1);
    // display controller number of current controller
    oled.setCursor(80, 4);
    sprintf(ctrlNumberChar, "%03d", outputPinsControllerNumber[currentControllerIndex]);
    oled.println(ctrlNumberChar);
  }
  // update program change function status
  if ( mode == 43 ) {
    oled.clear();
    oled.set1X();
    oled.println(title);
    oled.println(F("Edit Mode:"));
    oled.setCursor(0, 4);
    oled.set2X();
    oled.print(F("PC:     "));
    // display program change function status
    oled.setCursor(64, 4);
    if ( programChangeActive == 1 ) {
      oled.print(F("On "));
    } else {
      oled.print(F("Off"));
    }
  }
  // update control change function status
  if ( mode == 44 ) {
    oled.setCursor(0, 4);
    oled.set2X();
    oled.print(F("CC:     "));
    // display control change function status
    oled.setCursor(64, 4);
    if ( controlChangeActive == 1 ) {
      oled.print(F("On "));
    } else {
      oled.print(F("Off"));
    }
  }
  // update footswitch direct change function status
  if ( mode == 45 ) {
    oled.setCursor(0, 4);
    oled.set2X();
    oled.print(F("FSM:     "));
    // display control change function status
    oled.setCursor(68, 4);
    if ( directChangeActive == 1 ) {
      oled.print(F("DIR"));
    } else {
      oled.print(F("PGM"));
    }
  }
  // update midi channel number
  if ( mode == 46 ) {
    oled.setCursor(0, 4);
    oled.set2X();
    oled.print(F("CHAN:     "));
    // display control change function status
    sprintf(midiChannelChar, "%02d", selectedMidiChannel + 1);
    oled.setCursor(98, 4);
    oled.print(midiChannelChar);
  }
  return;
}
// write two byte integer to EEPROM
// data is written to two consecutive EEPROM adresses
void eepromWriteInt(int adr, int value) {
  byte low, high;
  low = value & 0xFF;
  high = (value >> 8) & 0xFF;
  EEPROM.update(adr, low);
  EEPROM.update(adr + 1, high);
  return;
} //eepromWriteInt
// read two byte integer from EEPROM
// data is read from two consecutive EEPROM adresses
int eepromReadInt(int adr) {
  byte low, high;
  low = EEPROM.read(adr);
  high = EEPROM.read(adr + 1);
  return low + ((high << 8) & 0xFF00);
} //eepromReadInt
