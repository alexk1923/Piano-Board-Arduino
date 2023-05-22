#include <LiquidCrystal_I2C.h>
#include "pitches.h"

/* 
This example demonstrates how to use the SDFAT library for audio playback.
Read time and program space are reduced by using SDFAT directly

Requirements:
The SDFAT library must be installed. See http://code.google.com/p/sdfatlib/ 
The line #define SDFAT MUST be uncommented in pcmConfig.h

"error: 'File' has not been declared" means you need to read the above text
*/

#include <SdFat.h>
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
//------------------------------------------------------------------------------

#define BUTTON_1 PD2
#define BUTTON_2 PD3
#define BUTTON_3 PD5
#define BUTTON_4 PD6
#define MODE_BUTTON PD7
#define ENTER_BUTTON PB2
#define RETURN_BUTTON PB0


#define SPEAKER PB1
#define LED_1 A0
#define LED_2 A1
#define LED_3 A2
#define LED_4 A3

#define FREE_MODE "FREE"
#define RECORD_MODE "RECORD"
#define LEARN_MODE "LEARN"
#define LISTEN_MODE "LISTEN"

File root;
File myFile;

LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C address 0x27, 16 column and 2 rows

volatile long timeFromPress = millis();
long debounceTime = 150;
volatile int lcdNote = 0;
volatile bool button_1_pressed = false;
volatile int button_1_state = HIGH;

byte buttons[4] = { BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4 };
int notes[4] = { NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4 };
byte leds[4] = { LED_1, LED_2, LED_3, LED_4 };
String modes[4] = { "FREE", "RECORD", "LEARN", "LISTEN" };
volatile int currentModeIdx = 0;
volatile bool changeMode = true;
volatile bool recordingToFile = false;
volatile bool learning = false;
volatile bool listenSong = false;
int learningNotes[25] = { 0 };
volatile int learningNotesSize = 0;
volatile int currentLearningNotesIdx = -1;
volatile bool currentLearningNoteChange = false;

ISR(INT0_vect) {
  // cod întrerupere externă
  //  PORTD ^= (1 << PD7);
  long actTime = millis();
  if (actTime - timeFromPress > debounceTime) {
    timeFromPress = actTime;
    Serial.println("Intrerupere externa");
  }
}

ISR(PCINT0_vect) {
  long actTime = millis();

  if (actTime - timeFromPress > debounceTime) {
    if ((PINB & (1 << ENTER_BUTTON)) == 0) {
      Serial.println("ENTER");
    }
    timeFromPress = actTime;
  }
}

ISR(PCINT2_vect) {
  // cod întrerupere de tip pin change
  // TODO


  /*
  Serial.println("Intrerupere de tip pin change");
  Serial.print("currentState=" );
  Serial.println(currentState[0]);
  Serial.print("button_1_state=" );
  Serial.println(button_1_state);
*/

  long actTime = millis();

  if (actTime - timeFromPress > debounceTime) {
    if ((PIND & (1 << MODE_BUTTON)) == 0) {
      currentModeIdx = (currentModeIdx + 1) % 4;
      changeMode = true;

      if(currentModeIdx == 0) {
        audio.stopPlayback();
      }


      if (currentModeIdx == 1) {
        if (recordingToFile) {
          // Serial.println("Closing file");
          // myFile.close();
        } else {
          //  Serial.println("Setting recordingToFile to true");
          recordingToFile = true;
        }
      }

      if (currentModeIdx == 2) {
        // Open the file for reading
        int statusOpen = myFile.open("data52.txt", O_READ);
        if (statusOpen) {
          // Read and print each line from the file
          learningNotesSize = 0;
          while (myFile.available() && learningNotesSize < 25) {
            String line = myFile.readStringUntil('\n');
            learningNotes[learningNotesSize++] = line.toInt();
          }
          myFile.close();

          if (learningNotesSize > 0) {

            Serial.println("My notes:");
            for (int i = 0; i < learningNotesSize; i++) {
              Serial.println(learningNotes[i]);
            }


            learning = true;
            currentLearningNotesIdx = 0;
            currentLearningNoteChange = true;
          }
        } else {
          Serial.println("Error opening file for reading!");
        }
      }

      if(currentModeIdx == 3) {
        listenSong = true;
      }
    }

    /*
    if ((PIND & (1 << ENTER_BUTTON)) == 0) {
      switch (currentModeIdx) {
        // FREE
        case 0:
          Serial.println("Does nothing, it is in free mode");
          break;
        // RECORD
        case 1:
          if (recordingToFile) {
            Serial.println("First branch");
            myFile.close();
          } else {
            Serial.println("Second Branch");
            recordingToFile = true;
          }
          learning = false;
          listenSong = false;
          break;
        // LEARN
        case 2:
          learning = true;
          recordingToFile = false;
          listenSong = false;
          break;
        // LISTEN
        case 3:
          // @TODO, navigate in the SD Card to play a song or back to mode select
          listenSong = true;
          learning = false;
          recordingToFile = false;
          break;
        default:
          Serial.println("Index error");
          break;
      }
    }*/

    timeFromPress = actTime;
  }

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
  // buton 1: PD2 / INT0
  // buton 2: PD4 / PCINT20
  cli();

  // configurare intreruperi
  // intreruperi externe
  // EICRA |= (1 << ISC00);    // set INT0 to trigger on ANY logic change

  // întreruperi de tip pin change (activare vector de întreruperi)

  /*
  PCICR |= (1 << PCIE0); // enable the pin change interrupt, set PCIE0 to enable PCMSK0 scan
  PCMSK0 |= (1 << PCINT2); // Enable interrupt for PCINT0 and PCINT2
  */

  // activare intreruperi
  // intrerupere externa

  // activeaza asta daca vrei sa mearga intreruperea externa pe INT0
  // EIMSK |= (1 << INT0);

  PCICR |= (1 << PCIE2);  // enable the pin change interrupt, set PCIE2 to enable PCMSK2 scan
  // PCICR |= (1 << PCIE0); // enable the pin change interrupt, set PCIE2 to enable PCMSK2 scan

  // întrerupere de tip pin change
  // PCMSK0 |= (1 << PCINT0); // Turns on PCINT0 (PB0)
  // PCMSK0 |= (1 << PCINT2); // Turns on PCINT2 (PB2)


  //PCMSK2 |= (1 << PCINT16); // Turns on PCINT18 (PD2)
  PCMSK2 |= (1 << PCINT18);  // Turns on PCINT18 (PD2)
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
  for (int i = 0; i < 4; i++) {
    // Pin Mode Input Pullup
    DDRD &= ~(1 << buttons[i]);
    // Set default button state to high
    PORTD |= (1 << buttons[i]);
  }

  // Pin Mode Input Pullup
  DDRD &= ~(1 << MODE_BUTTON);
  // Set default button state to high
  PORTD |= (1 << MODE_BUTTON);

  // Pin Mode Input Pullup
  DDRB &= ~(1 << ENTER_BUTTON);
  // Set default button state to high
  PORTB |= (1 << ENTER_BUTTON);

  // Pin Mode Input Pullup
  DDRB &= ~(1 << RETURN_BUTTON);
  // Set default button state to high
  PORTB |= (1 << RETURN_BUTTON);
}

void setup() {


  lcd.print("Starting piano...");
  Serial.begin(9600);

  setup_lcd();
  setup_buttons();
  setup_interrupts();

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

  /*
  // Navigate in SD Card
  File dir = sd.open("/");
  while (true) {
    File file = dir.openNextFile(); 
    if (!file) {
      // no more files
      break;
    }

    char filename[13];
    file.getName(filename, sizeof(filename));
    lcd.print(filename);
    delay(5000);
    lcd.clear();
    Serial.println(filename);
    //Serial.print("Size: ");
    //Serial.println(file.size());
    file.close();
  }*/

  lcd.print("Done!");
  delay(2000);
  lcd.clear();
  lcd.print(modes[currentModeIdx]);
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
    if (modes[currentModeIdx] == "RECORD" && recordingToFile) {
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

  if(listenSong) {
    audio.play("trot.wav");
    listenSong = false;
  }

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'p': audio.play("trot.wav"); break;
      case 's': audio.stopPlayback(); break;
      default: break;
    }
  }
}