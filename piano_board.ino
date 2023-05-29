#include <LiquidCrystal_I2C.h>
#include "pitches.h"
#include "PCF8574.h"
#include "sdios.h"
/* 
This example demonstrates how to use the SDFAT library for audio playback.
Read time and program space are reduced by using SDFAT directly

Requirements:
The SDFAT library must be installed. See http://code.google.com/p/sdfatlib/ 
The line #define SDFAT MUST be uncommented in pcmConfig.h

"error: 'File' has not been declared" means you need to read the above text
*/

#include <SdFat.h>
#include <ctype.h>
SdFat sd;

#define SD_ChipSelectPin 4  //use digital pin 4 on arduino Uno, nano etc, or can use other pins
#include <TMRpcm.h>         //  also need to include this library...
#include <SPI.h>

TMRpcm audio;  // create an object for use in this sketch

// Try max SPI clock for an SD. Reduce SPI_CLOCK if errors occur.
#define SPI_CLOCK SD_SCK_MHZ(50)

// Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else  // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif  // HAS_SDIO_CLASS

#define error(s) sd.errorHalt(&Serial, F(s))
//------------------------------------------------------------------------------

#define BUTTON_1 PD7
#define BUTTON_2 PD3
#define BUTTON_3 PD5
#define BUTTON_4 PD6
#define MODE_BUTTON P0
#define ENTER_BUTTON P1
#define RETURN_BUTTON P2
#define ARDUINO_UNO_INTERRUPTED_PIN 2

#define SPEAKER PB1
#define LED_1 A0
#define LED_2 A1
#define LED_3 A2
#define LED_4 A3

// #define FREE_MODE "FREE"
// #define RECORD_MODE "RECORD"
// #define LEARN_MODE "LEARN"
// #define LISTEN_MODE "LISTEN"

File root;
File myFile;
File file;

// Function interrupt
void keyChangedOnPCF8574();

PCF8574 pcf8574(0x20, ARDUINO_UNO_INTERRUPTED_PIN, keyChangedOnPCF8574);
LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C address 0x27, 16 column and 2 rows
String currentFile = "";

volatile long timeFromPress = millis();
long debounceTime = 150;
volatile int lcdNote = 0;
volatile bool button_1_pressed = false;
volatile int button_1_state = HIGH;

byte buttons[4] = { BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4 };
int notes[4] = { NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4 };
byte leds[4] = { LED_1, LED_2, LED_3, LED_4 };
char modes[4][7] = { "FREE", "RECORD", "LEARN", "LISTEN" };
volatile int currentModeIdx = 0;
volatile bool changeMode = true;
volatile bool recordingToFile = false;
volatile bool learning = false;
volatile bool listenSong = false;
int learningNotes[2] = { 0 };
volatile int learningNotesSize = 0;
volatile int currentLearningNotesIdx = -1;
volatile bool currentLearningNoteChange = false;
volatile bool pcf8574_interrupt = false;
volatile bool navigate = false;

int currentFileIndex = 1;


ISR(PCINT2_vect) {
  // cod întrerupere de tip pin change

  int currentState[4];
  for (int i = 0; i < 4; i++) {
    currentState[i] = PIND & (1 << buttons[i]);
  }

  for (int i = 0; i < 4; i++) {
    if (!currentState[i] && button_1_state == HIGH) {
      // Serial.println("Butonul este acum LOW, iar in trecut era HIGH");
      button_1_pressed = true;
      timeFromPress = actTime;
      lcdNote = notes[i];
    } else if (currentState[i] && button_1_state == LOW) {  // Check for rising edge trigger
      button_1_pressed = false;
      timeFromPress = actTime;
      // Serial.println("Butonul este acum HIGH, iar in trecut era LOW");
    }

    // If learning mode is not activated or is activated and the correct button has been pressed
    if (button_1_pressed && (!learning || (learning && notes[i] == learningNotes[currentLearningNotesIdx]))) {
      digitalWrite(leds[i], HIGH);
      tone(9, notes[i], 250);

      // RECORD CASE
      if (recordingToFile) {
        // open the file for write at end like the Native SD library
        if (!myFile.open("data52.txt", O_WRITE | O_APPEND | O_CREAT)) {
          Serial.println("error");
          // sd.errorHalt("opening test3.txt for write failed");
        } else {
          // if the file opened okay, write to it:
          // Serial.print("Writing to test5...");
          myFile.println(notes[i]);
          myFile.close();
        }
      }


      // LEARNING CASE
      if (learning && notes[i] == learningNotes[currentLearningNotesIdx]) {
        currentLearningNotesIdx++;
        // Go to the next note
        // The entire song has been learned
        if (currentLearningNotesIdx >= learningNotesSize) {
          learning = false;
          learningNotesSize = -1;
          currentModeIdx = 0;
          changeMode = true;
        }
        currentLearningNoteChange = true;
        digitalWrite(leds[i], LOW);

        Serial.println("Current Learning Notes Index:");
        Serial.println(currentLearningNotesIdx);
      }

    } else {
      if (!learning) {
        digitalWrite(leds[i], LOW);
      }
    }

    button_1_state = currentState[i] ? 1 : 0;
  }
}

void setup_interrupts() {
  cli();

  // configurare intreruperi

  PCICR |= (1 << PCIE2);  // enable the pin change interrupt, set PCIE2 to enable PCMSK2 scan

  PCMSK2 |= (1 << PCINT19);  // Turns on PCINT19 (PD3)
  PCMSK2 |= (1 << PCINT21);  // Turns on PCINT21 (PD5)
  PCMSK2 |= (1 << PCINT22);  // Turns on PCINT22 (PD6)
  PCMSK2 |= (1 << PCINT23);  // Turns on PCINT23 (PD7)

  sei();
}

void setup_lcd() {
  lcd.begin();      //initialize the lcd
  lcd.backlight();  //open the backlight
}


void setup_leds() {
  for (int i = 0; i < 4; i++) {
    pinMode(leds[i], OUTPUT);
    digitalWrite(leds[i], LOW);
  }
}

void setup_buttons() {

  pcf8574.pinMode(P0, INPUT_PULLUP, HIGH);
  pcf8574.pinMode(P1, INPUT_PULLUP, HIGH);
  pcf8574.pinMode(P2, INPUT_PULLUP, HIGH);
  pcf8574.pinMode(P3, INPUT_PULLUP, HIGH);

  for (int i = 0; i < 4; i++) {
    // Pin Mode Input Pullup
    DDRD &= ~(1 << buttons[i]);
    // Set default button state to high
    PORTD |= (1 << buttons[i]);
  }
}

void setup() {

  lcd.clear();
  lcd.print("Starting piano...");
  Serial.begin(9600);

  setup_lcd();
  setup_buttons();
  setup_interrupts();



  Serial.print("Init pcf8574...");
  if (pcf8574.begin()) {
    Serial.println("OK");
  } else {
    Serial.println("KO");
  }

  // Check for SPI Communication with SD Card
  if (!sd.begin(4, SPI_FULL_SPEED)) {
    return;
  } else {
    Serial.println("SD OK");
  }

  audio.speakerPin = 9;  //5,6,11 or 46 on Mega, 9 on Uno, Nano, etc
  // pinMode(10,OUTPUT); //Pin pairs: 9,10 Mega: 5-2,6-7,11-12,46-45

  Serial.println("done!");
  audio.setVolume(5);
  // audio.play("3.wav");

  lcd.print("Done!");
  delay(2000);
  lcd.clear();
  lcd.print(modes[currentModeIdx]);
}

void resetPCF(bool val0, bool val1, bool val2, bool val3) {
  if (val0 == HIGH) {
    pcf8574.digitalWrite(P0, LOW);
  } else {
    pcf8574.digitalWrite(P0, HIGH);
  }

  if (val1 == HIGH) {
    pcf8574.digitalWrite(P1, LOW);
  } else {
    pcf8574.digitalWrite(P1, HIGH);
  }

  if (val2 == HIGH) {
    pcf8574.digitalWrite(P2, LOW);
  } else {
    pcf8574.digitalWrite(P2, HIGH);
  }

  if (val3 == HIGH) {
    pcf8574.digitalWrite(P3, LOW);
  } else {
    pcf8574.digitalWrite(P3, HIGH);
  }
}


void mode_change() {

  currentModeIdx = (currentModeIdx + 1) % 4;
  changeMode = true;

  if (currentModeIdx == 0) {
    audio.stopPlayback();
  }


  if (currentModeIdx == 1) {
    if (recordingToFile) {
    } else {
      // recordingToFile = true;
    }
  }

  if (currentModeIdx == 2) {
    // Open the file for reading
    // int statusOpen = myFile.open("data52.txt", O_READ);
    // if (statusOpen) {
    //   // Read and print each line from the file
    //   learningNotesSize = 0;
    //   while (myFile.available() && learningNotesSize < 25) {
    //     String line = myFile.readStringUntil('\n');
    //     learningNotes[learningNotesSize++] = line.toInt();
    //   }
    //   myFile.close();

    //   if (learningNotesSize > 0) {

    //     Serial.println("My notes:");
    //     for (int i = 0; i < learningNotesSize; i++) {
    //       Serial.println(learningNotes[i]);
    //     }


    //     learning = true;
    //     currentLearningNotesIdx = 0;
    //     currentLearningNoteChange = true;
    //   }
    // } else {
    //   Serial.println("Error opening file for reading!");
    // }
  }

  if (currentModeIdx == 3) {
    // listenSong = true;
  }
}

void enter_press() {
  switch (currentModeIdx) {
    // FREE
    case 0:
      Serial.println("Does nothing, it is in free mode");
      break;
    // RECORD
    case 1:
      Serial.println("Setting record mode to true");
      learning = false;
      listenSong = false;
      recordingToFile = true;
      break;
    // LEARN
    case 2:
      //Serial.println("Setting learning mode to true");
      delay(100);
      recordingToFile = false;
      listenSong = false;
      learning = true;
      break;
    // LISTEN
    case 3:
      // @TODO, navigate in the SD Card to play a song or back to mode select
      // Serial.println("Setting listen mode to true");
      if (navigate) {
        audio.play("/Music/3.wav");
        currentModeIdx = 0;
        changeMode = true;
        navigate = false;
        listenSong = false;
      } else {
        navigate = true;
        learning = false;
        recordingToFile = false;
        listenSong = true;
      }

      break;
    default:
      Serial.println("Index error");
      break;
  }
}

void right_button() {
  if (navigate) {
    navigate_sd(1);
  }
}

void left_button() {
  if (navigate) {
    navigate_sd(-1);
  }
}

void loop() {

  if (lcdNote != 0) {
    // lcd.clear();
    // lcd.print(lcdNote);
    delay(200);
    lcdNote = 0;
  }


  if (changeMode) {
    lcd.clear();
    if (strcmp(modes[currentModeIdx], "RECORD") == 0 && recordingToFile) {
      lcd.print("Recording...");
      delay(200);

    } else {
      lcd.print(modes[currentModeIdx]);
      delay(200);
    }
    changeMode = false;
  }

  if (learning && currentLearningNoteChange) {
    for (int i = 0; i < 4; i++) {
      // Check what LED needs to be set to HIGH
      if (notes[i] == learningNotes[currentLearningNotesIdx]) {
        digitalWrite(leds[i], HIGH);
      }
    }
    currentLearningNoteChange = false;
  }


  if (listenSong) {
    navigate_sd(-1);
    //audio.play("5.wav");
    listenSong = false;
  }

  if (pcf8574_interrupt) {
    bool val0 = pcf8574.digitalRead(P0);
    bool val1 = pcf8574.digitalRead(P1);
    bool val2 = pcf8574.digitalRead(P2);
    bool val3 = pcf8574.digitalRead(P3);


    if (val0 == LOW) {
      mode_change();
    }

    if (val1 == LOW) {
      enter_press();
    }

    if (val2 == LOW) {
      left_button();
    }

    if (val3 == LOW) {
      right_button();
    }

    resetPCF(val0, val1, val2, val3);
    pcf8574_interrupt = false;
  }


  if (Serial.available()) {
    switch (Serial.read()) {
      case 'p': audio.play("trot.wav"); break;
      case 's': audio.stopPlayback(); break;
      default: break;
    }
  }
}



void keyChangedOnPCF8574() {

  // bool val1 = pcf8574.digitalRead(P1);
  // bool val2 = pcf8574.digitalRead(P2);
  long actTime = millis();
  if (actTime - timeFromPress > debounceTime) {
    //uint8_t val0 = pcf8574.digitalRead(P0);
    pcf8574_interrupt = true;
  }
}


void navigate_sd(int direction) {
  currentFileIndex += direction;
  // Navigate in SD Card

  if (currentFileIndex < 0) {
    currentFileIndex = 0;
  }


  if (listenSong == true) {
    // dir = sd.open("/original", O_READ);
  }

  if (recordingToFile == true) {
    // dir = sd.open("/original", O_READ);
  }

  Serial.println(currentFileIndex);


  char fileNames[15];
  int rootFileCount = 0;
  if (!root.open("/Music")) {
    error("open root");
  }
  // strcpy(fileNames[0], "ddd");
  int noFiles = 0;
  bool foundFile = false;

  while (!foundFile && file.openNext(&root, O_RDONLY)) {
    Serial.println("here");
    file.getName(fileNames, sizeof(fileNames));
    if (noFiles == currentFileIndex) {
      currentFile = fileNames;
      // strncat(currentFile, fileNames, 14);
      lcd.clear();
      lcd.print(currentFile);

      // currentFile[i] = '\0';
      foundFile = true;
    }
    noFiles++;

    file.close();
    if (rootFileCount > 10) {
      error("Too many files in root. Please use an empty SD.");
    }
  }

  root.close();

  // Adjust the current file index if it goes out of bounds
  if (!foundFile) {
    currentFile = fileNames;
    // strncat(currentFile, fileNames, 14);
    lcd.clear();
    lcd.print(currentFile);
    currentFileIndex = noFiles - 1;
  }

}
