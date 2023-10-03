#include <LiquidCrystal_I2C.h>
#include "pitches.h"
#include "PCF8574.h"
#include "sdios.h"
/* 
Requirements:
The SDFAT library must be installed. See http://code.google.com/p/sdfatlib/ 
The line #define SDFAT MUST be uncommented in pcmConfig.h

"error: 'File' has not been declared" means you need to read the above text
*/

#include <SdFat.h>
#include <ctype.h>
SdFat sd;

#define SD_ChipSelectPin 4  // Use digital pin 4 on arduino Uno, nano etc, or can use other pins
#include <TMRpcm.h>         //  Also need to include this library...
#include <SPI.h>

TMRpcm audio;  // Create an object for use in this sketch

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

File root;
File myFile;
File file;

// Function interrupt
void keyChangedOnPCF8574();

PCF8574 pcf8574(0x20, ARDUINO_UNO_INTERRUPTED_PIN, keyChangedOnPCF8574);
LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C address 0x27, 16 column and 2 rows
String currentFile = "";

volatile long timeFromPress = millis();
long debounceTime = 500;
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
int learningNotes[10] = { 0 };
volatile int learningNotesSize = 0;
volatile int currentLearningNotesIdx = -1;
volatile bool currentLearningNoteChange = false;
volatile bool pcf8574_interrupt = false;
volatile bool navigate = false;
volatile bool audioPlay = false;
volatile bool prepareRecorded = false;


int currentFileIndex = 1;


ISR(PCINT2_vect) {
  // PIN Change interrupt

  int currentState[4];
  for (int i = 0; i < 4; i++) {
    currentState[i] = PIND & (1 << buttons[i]);
  }

  for (int i = 0; i < 4; i++) {
    if (!currentState[i] && button_1_state == HIGH) {
      button_1_pressed = true;
      lcdNote = notes[i];
    } else if (currentState[i] && button_1_state == LOW) {  // Check for rising edge trigger
      button_1_pressed = false;
    }

    // If learning mode is not activated or is activated and the correct button has been pressed
    if (button_1_pressed && (!learning || (learning && notes[i] == learningNotes[currentLearningNotesIdx]))) {
      digitalWrite(leds[i], HIGH);
      tone(9, notes[i], 250);

      // RECORD CASE
      if (recordingToFile == true) {
        // open the file for write at end like the Native SD library
        if (!myFile.open("data.txt", O_WRITE | O_APPEND | O_CREAT)) {
          error("er");
        } else {
          // if the file opened okay, write to it:
          delay(1000);
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

  // Interrupts config

  PCICR |= (1 << PCIE2);  // Enable the pin change interrupt, set PCIE2 to enable PCMSK2 scan

  PCMSK2 |= (1 << PCINT19);  // Turns on PCINT19 (PD3)
  PCMSK2 |= (1 << PCINT21);  // Turns on PCINT21 (PD5)
  PCMSK2 |= (1 << PCINT22);  // Turns on PCINT22 (PD6)
  PCMSK2 |= (1 << PCINT23);  // Turns on PCINT23 (PD7)

  sei();
}

void setup_lcd() {
  lcd.begin();      // Initialize the lcd
  lcd.backlight();  // Open the backlight
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

  audio.speakerPin = 9;  // 5,6,11 or 46 on Mega, 9 on Uno, Nano, etc

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


  if (currentModeIdx == 0) {
    if (audio.isPlaying()) {
      audio.stopPlayback();
    }
    navigate = false;
  }

  // Recording
  if (currentModeIdx == 1) {
  }

  // Learning
  if (currentModeIdx == 2) {
    recordingToFile = false;
    prepareRecorded = true;
  }

  if(currentModeIdx == 3) {
    for(int i = 0; i < 4; i++) {
      digitalWrite(leds[i], LOW);
    }
    
    learning = false;
  }
  changeMode = true;
}

void enter_press() {
  switch (currentModeIdx) {
    // FREE
    case 0:
      learning = false;
      listenSong = false;
      recordingToFile = false;
      navigate = false;
      break;
    // RECORD
    case 1:
      if (recordingToFile == true) {
        recordingToFile = false;
      } else {
        learning = false;
        listenSong = false;
        navigate = false;
        recordingToFile = true;
      }
      break;
    // LEARN
    case 2:
      delay(100);
      recordingToFile = false;
      listenSong = false;
      learning = true;
      break;
    // LISTEN
    case 3:
      if (navigate == true) {
        if (audio.isPlaying()) {
          audio.stopPlayback();
        }
        listenSong = false;
        audioPlay = true;
      } else {

        if (audio.isPlaying()) {
          audio.stopPlayback();
        }

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
    if (audio.isPlaying()) {
      audio.stopPlayback();
    }
    navigate_sd(1);
  }
}

void left_button() {
  if (navigate) {
    if (audio.isPlaying()) {
      audio.stopPlayback();
    }
    navigate_sd(-1);
  }
}

void loop() {

  if (lcdNote != 0) {
    delay(200);
    lcdNote = 0;
  }

  if (currentModeIdx == 1 && recordingToFile == true) {
    lcd.clear();
    lcd.print("Recording...");
    delay(200);
  }



  if (changeMode) {
    lcd.clear();
    lcd.print(modes[currentModeIdx]);
    delay(200);

    if (currentModeIdx == 2 && prepareRecorded == true) {
      // Prepare recorded song
      // Open the file for reading
      int statusOpen = myFile.open("data.txt", O_READ);
      if (statusOpen) {
        // Read and print each line from the file
        learningNotesSize = 0;
        while (myFile.available() && learningNotesSize < 10) {
          String line = myFile.readStringUntil('\n');
          learningNotes[learningNotesSize++] = line.toInt();
        }
        myFile.close();

        if (learningNotesSize > 0) {
          Serial.println("My notes:");
          for (int i = 0; i < learningNotesSize; i++) {
            Serial.println(learningNotes[i]);
          }
          currentLearningNotesIdx = 0;
          currentLearningNoteChange = true;
        }
      } else {
        Serial.println("Er");
      }
      prepareRecorded = false;
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
    if (audio.isPlaying()) {
      audio.stopPlayback();
    }
    navigate_sd(-1);
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

  if (audioPlay) {
    String songPath = "/Music/" + currentFile;
    delay(300);
    Serial.println(songPath);

    audio.play(songPath.c_str());
    audioPlay = false;
  }


  if (Serial.available()) {
    switch (Serial.read()) {
      case 'p': audio.play("trot.wav"); break;
      case 's': audio.disable(); break;
      default: break;
    }
  }
}



void keyChangedOnPCF8574() {

  long actTime = millis();
  if (actTime - timeFromPress > debounceTime) {
    pcf8574_interrupt = true;
  }
}


void navigate_sd(int direction) {
  currentFileIndex += direction;

  // Navigate in SD Card
  if (currentFileIndex < 0) {
    currentFileIndex = 0;
  }

  Serial.println(currentFileIndex);

  char fileNames[15];
  int rootFileCount = 0;
  if (!root.open("/Music")) {
    error("open root");
  }
  int noFiles = 0;
  bool foundFile = false;

  while (!foundFile && file.openNext(&root, O_RDONLY)) {
    Serial.println("here");
    file.getName(fileNames, sizeof(fileNames));
    if (noFiles == currentFileIndex) {
      currentFile = fileNames;
      lcd.clear();
      lcd.print(currentFile);
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
    lcd.clear();
    lcd.print(currentFile);
    currentFileIndex = noFiles - 1;
  }
}
