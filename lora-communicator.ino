#include <Adafruit_GFX.h>

#include <SPI.h>
#include <RH_RF95.h>

#include <SdFat.h>

#include <Adafruit_NeoPixel.h>
#include "src\conbadge\featherwing_keyboard_conbadge.h"

#define NEOPIXEL_PIN 11
#define STATUS_TIME 5000

#define SD_CS  5
#define TFT_CS 9
#define TFT_DC 10

#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

#define BUFF_LEN 13
#define TXT_BAR_HEIGHT 20

#define RFM95_CS 8
#define RFM95_RST 4
#define RFM95_INT 3

#define VBATPIN 9

// RFM9x Frequency to operate on
#define RF95_FREQ 915.00

// Text to transmit while in beacon mode
const uint8_t BEACON_TXT[] = "Hello, world!";

// Comment out this line to suppress serial output
#define DEBUG

#define GUESS_PIN 2

int bg_color = BLACK;
int acc_color = BLUE;
int text_color = WHITE;

char num_lines = 0;
int startup_status = 0;
String lines[BUFF_LEN];
String command = "";

int BORDER = 4;

unsigned long last_status_update = 0;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC);

RH_RF95 rf95(RFM95_CS, RFM95_INT);

Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);


BBQ10Keyboard keyboard;
volatile bool dataReady = false;
const int interruptPin = 6;

SdFat SD;
Adafruit_ImageReader reader(SD);
Adafruit_Image img;

Conbadge badge(&tft, &keyboard, &reader, "UwU", acc_color, bg_color, BORDER);

bool haveSdCard = true;

// -- The currently running "program", aka the mode to display and act on -- //
// Programs:
// 1 - LoRa terminal
// 2 - LoRa Beacon
// 4 - Badge dispay
volatile int current_program = 4;
volatile bool switch_programs = true;
volatile bool shown_status = false;



void draw_window() {

  keyboard.setBacklight(0.5f);
  tft.setRotation(1);
  tft.fillScreen(bg_color);
  tft.fillRect(BORDER, BORDER, 320 - BORDER, 240 - BORDER, acc_color);
  tft.fillRect(2*BORDER, 2*BORDER, 320 - (3*BORDER), 240 - (4*BORDER + TXT_BAR_HEIGHT), bg_color);
  tft.fillRect(2*BORDER, 240 - (BORDER + TXT_BAR_HEIGHT), 320 - (3*BORDER), TXT_BAR_HEIGHT, bg_color);
  tft.setCursor(BORDER * 4, BORDER * 3);
}

void reload_lines(){

  int y_pos = BORDER * 3;
  
  for(int i = 0; i < num_lines; i++){
    tft.setCursor(BORDER * 4, y_pos);
    tft.setTextColor(text_color);  
    tft.setTextSize(1);
    tft.println(lines[i]);
    y_pos+=15;
  }
}

void screen_print_ln(String string){

  if (num_lines < BUFF_LEN){
      lines[num_lines] = string;

      tft.setCursor(BORDER * 4, (num_lines * 15) + (BORDER * 3));
      tft.setTextColor(text_color);  
      tft.setTextSize(1);
      tft.println(string);
      num_lines++;
  }
  else {
    for(int i = 0; i < BUFF_LEN - 1; i++){
      lines[i] = lines[i+1];
    }
    lines[BUFF_LEN - 1] = string;

    tft.fillRect(2*BORDER, 2*BORDER, 320 - (3*BORDER), 240 - (4*BORDER + TXT_BAR_HEIGHT), bg_color);

    reload_lines();
  }
}

void cmd_print_ln(char * string){
  tft.fillRect(2*BORDER, 240 - (BORDER + TXT_BAR_HEIGHT), 320 - (3*BORDER), TXT_BAR_HEIGHT, bg_color);
  tft.setCursor(BORDER * 4, 240 - (4*BORDER));
  tft.setTextColor(text_color);  
  tft.setTextSize(1);
  tft.println(string);
}

void KeyIsr(void){
  dataReady = true;
}

float measure_batt_voltage(){

  pinMode(VBATPIN, INPUT);
  float measuredvbat = analogRead(VBATPIN);
  pinMode(VBATPIN, OUTPUT);
  
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage

  return measuredvbat;
}

void show_status(){

  switch(current_program){
    case 1: 
      // --- Display startup message ---//
      switch(startup_status){
        case 0: screen_print_ln("LoRa Terminal Ready"); break;
        case 1: screen_print_ln("Failed to init radio"); while(1); break;
        case 2: screen_print_ln("Invalid frequency"); while(1); break;
        default: screen_print_ln("Unknown Error"); while(1); break;
      }

      last_status_update = millis();
      float measuredvbat = measure_batt_voltage();
    
      String vbat_str = "Batt (V): ";
      vbat_str.concat(measuredvbat);
      screen_print_ln(vbat_str);    
      shown_status = true;
      break;
//    default:
//      // do nothing
//      break;
  }
}

void setup() {

  pixel.begin();
  pixel.setBrightness(10);
  pixel.setPixelColor(0, pixel.Color(128, 0, 255));
  pixel.show();
  
  Serial.begin(115200);

  // --- Keyboard init --- //
  Wire.begin();
  keyboard.begin(); 
  keyboard.attachInterrupt(interruptPin, KeyIsr);

  // --- Screen init --- //
  num_lines = 0;
  tft.begin();

  // -- SD Card init --- // 
  pinMode(GUESS_PIN, INPUT_PULLUP);
  
  if (!SD.begin(SD_CS, SD_SCK_MHZ(1))){
    haveSdCard = false;
    badge.haveSdCard = false;
  }

  if (haveSdCard){
    Serial.println("Saw SD card");
  }
  else{
    Serial.println("No SD card");
  }

  // --- Radio init --- //
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);

  delay(100);
  
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);

  delay(100);

  if (!rf95.init()) {
    startup_status = 1;
  }

  if (!rf95.setFrequency(RF95_FREQ)) {
    startup_status = 2;
  }
  
  rf95.setTxPower(5, false);
  rf95.setPromiscuous(true);
}

void program_handle_keypress(char pressed) {

  switch(current_program){
    case 1: 
      if (pressed == '\b'){
        command = command.substring(0, command.length() -1);
      }
      else if(pressed == 10){
        char cmd_buf[50];
        command.toCharArray(cmd_buf, command.length());
        cmd_buf[command.length()+1] = 0;
        rf95.send((uint8_t *)cmd_buf, command.length()+2);
        if(rf95.waitPacketSent()){
          screen_print_ln(command);
        }
        else{
          screen_print_ln("ERROR SENDING");
        }
        
        command.remove(0);
      }
      else{
        command += pressed;
      }
      
      char Buf[50];
      command.toCharArray(Buf, 50);
      cmd_print_ln(Buf);
      break;
      default:
      break;
       //do nothing
  }
}

void loop() {

  // --- Check if it's time to update status LED --- //
  if (millis() - last_status_update > STATUS_TIME) {
    pixel.begin();
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();

    last_status_update = millis();
  }

  // --- Check for new key presses --- //
  if (dataReady){
    
    const BBQ10Keyboard::KeyEvent key = keyboard.keyEvent();

    if (key.state == BBQ10Keyboard::StatePress) {

      char pressed = key.key;

      if (pressed == 6){
        if(current_program != 1){
          current_program = 1;
          switch_programs = true;
        }
      }
      else if (pressed == 17){
        if(current_program != 2){
          current_program = 2;
          switch_programs = true;
        }
      }
      else if (pressed == 7){
        if(current_program != 3){
          current_program = 3;
          switch_programs = true;
        }
      }
      else if (pressed == 18){
        if(current_program != 4){
          current_program = 4;
          switch_programs = true;
        }
      }
      else {
        program_handle_keypress(pressed);
      }
    }

    keyboard.clearInterruptStatus();
    dataReady = false;
  }

  if (switch_programs){

    switch (current_program){
      case 4 : badge.draw_badge(); break;
      case 2 : 
        draw_window();
        screen_print_ln("Beacon Start");
        break;
      default : 
        draw_window();

        if (!shown_status){
          show_status();
        }
        else{
          reload_lines();
        }

        char Buf[50];
        command.toCharArray(Buf, 50);
        cmd_print_ln(Buf);
    }

    switch_programs = false;
  }

  if(current_program == 2){
    screen_print_ln("Sending beacon pings...");
    for(int i=0; i < 5; i++){
      rf95.setModeTx();
      rf95.send(BEACON_TXT, sizeof(BEACON_TXT));
      rf95.waitPacketSent();
      delay(10);
    }

    delay(2 * 1000);
  }

  // --- Check for new message from radio last --- //
  if (rf95.available()){ 

    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);
    
    if (rf95.recv(buf, &len)){ 
      String message = String((char *)buf);
      screen_print_ln(message);   
      #ifdef DEBUG
      Serial.println(message);
      #endif
    }
    else{
      screen_print_ln("ERR");
    }
  }
  else {
    delay(100);
  }
}
