#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <string>
#include <FastCRC.h>

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
std::string displayzeile[6]; // zeile 0 unbenutzt--> displayzeile[6] ist 5. zeile!
// 1. zeile: Power (wenn einspeisung negativ irgendwie hervorheben???)
// 2. zeile: In / Out
// 3. zeile: per Lora gesendeter Wert
// 4. zeile: Data Age
// 5. zeile: Datum und Zeit
unsigned long long int Data_Age = 0;

int wattprozent = 0; // watt in % von 800 watt
unsigned long _lastmillis = 0;
unsigned long _lastmillisblink = 0;
unsigned long _lastmillisdisplay = 0;
bool display_on; // für display blinkfunktion
std::string identifier = "123456"; // identifier für lora
std::string sendwert = ""; // finaler sendwert muss immer mit identifier beginnen
bool statusTaster = HIGH;         // aktueller Status des Taster an Pin 2
bool statusTasterLetzter = HIGH;  // vorheriger Status des Tasters an Pin 2
bool displayPowerSave; // true = display aus
int32_t powerSaldo; 
uint32_t powerImport;
uint32_t powerExport;
const unsigned int MESSAGE_LENGTH = 338;
byte received_data[MESSAGE_LENGTH];

#define RADIO_SCLK_PIN              5
#define RADIO_MISO_PIN              19
#define RADIO_MOSI_PIN              27
#define RADIO_CS_PIN                18
#define RADIO_DIO0_PIN              26
#define RADIO_RST_PIN               23
#define I2C_SDA                     21
#define I2C_SCL                     22

void displayText() {
   oled.clearDisplay();
   oled.setTextColor(SSD1306_WHITE);
   oled.setCursor(0, 0);
   oled.setTextSize(2);
   oled.println(displayzeile[1].c_str());
   oled.setTextSize(1);
   oled.println(displayzeile[2].c_str());
   oled.println(displayzeile[3].c_str());
   oled.println(displayzeile[4].c_str());
   oled.println(displayzeile[5].c_str());
   oled.display();
}

void displayblinken(){
   if ((millis() - _lastmillisblink) > (500)) {
      _lastmillisblink = millis();
      if (display_on == true){
         oled.ssd1306_command(SSD1306_DISPLAYOFF);
         display_on = false;
      } else {
         oled.ssd1306_command(SSD1306_DISPLAYON);
         display_on = true;
      }
   }
}

void LoraSenden(){
   // powerSaldo kann ca. -1000 bis +20000 Watt sein --> wattprozent begrenzen auf -99 bis +99
   wattprozent = powerSaldo / 8;
   if (wattprozent < -99){
      wattprozent = -99;
   }
   if (wattprozent > 99){
      wattprozent = 99;
   }

   std::string wp = std::to_string(wattprozent);
   if (wattprozent == 0){
      sendwert = "+00";
   }
   if (wattprozent > 0 && wattprozent < 10){
      sendwert = "+0";
      sendwert += wp;
   }
   if (wattprozent == 10 || wattprozent > 10){
      sendwert = "+";
      sendwert += wp;
   }
   if (wattprozent > -10 && wattprozent < 0){
      sendwert = "-0";
      sendwert += wp[wp.length()-1]; // letztes zeichen in wp (also der einstellige wert ohne minuszeichen)
   }
   if (wattprozent < -9){
      sendwert = wp;
   }
   sendwert = identifier + sendwert;
   Serial.print("Sendwert: ");
   Serial.println(sendwert.c_str());
   // --------------------------------- lora send packet -----------------------------
   LoRa.beginPacket();
   LoRa.print(sendwert.c_str());
   LoRa.endPacket();
   // --------------------------------------------------------------------------------
   displayzeile[1] = "P: " + std::to_string(powerSaldo) + " W";
   displayzeile[2] = "In: " + std::to_string(powerImport) + " W / Out: " + std::to_string(powerExport) + " W";
   displayzeile[3] = "Lora: " + sendwert.substr(sendwert.length() - 3);
   sendwert = identifier; // zurücksetzen auf startwert
}

// -------------- DATA PARSER ---------------
int BytesToInt(byte bytes[], unsigned int left, unsigned int right) {
   int result = 0;
   for (unsigned int i = left; i < right; i++) {
     result = result * 256 + bytes[i];
   }
   return result;
}

// --------------- CRC VALIDATION ---------------
FastCRC16 CRC16;
bool ValidateCRC() {
   byte message[59];
   memcpy(message, received_data + 276, 59);
   //Serial.print("CRC: ");
   //for (size_t i = 0; i < 59; i++){
   //   if (message[i] < 0x10) Serial.print("0");
   //   Serial.print(message[i], HEX);
   //   Serial.print(' ');
   //}
   //Serial.println(' ');

   int crc = CRC16.x25(message, 59);
   int expected_crc = received_data[336] * 256 + received_data[335];
   if (crc != expected_crc) {
      Serial.println("WARNING: CRC check failed");
      return false;
   } else {
      return true;
   }
}

void ParseReceivedData() {
   if (!ValidateCRC()) {
     return;  // Discard message if CRC check fails
   }

   // received_data wird in decrypted_message reinkopiert
   byte decrypted_message[MESSAGE_LENGTH];
   std::copy(received_data, received_data + MESSAGE_LENGTH, decrypted_message); // kopiert received_data in decrypted_message
   // copy ende

   // Extract time and date from decrypted message
   int year = BytesToInt(decrypted_message, 25, 27);
   int month = BytesToInt(decrypted_message, 27, 28);
   int day = BytesToInt(decrypted_message, 28, 29);
   int hour = BytesToInt(decrypted_message, 30, 31);
   int minute = BytesToInt(decrypted_message, 31, 32);
   int second = BytesToInt(decrypted_message, 32, 33);
   char timestamp[20];
   sprintf(timestamp, "%02d.%02d.%04d %02d:%02d:%02d", day, month, year, hour, minute, second);
   displayzeile[5] = timestamp;

   // +P (= Strombezug): Byte 296 - 299
   // -P (= Einspeisung): Byte 301 - 304
   Serial.print("+P: ");
   Serial.print(decrypted_message[296], HEX);
   Serial.print(decrypted_message[297], HEX);
   Serial.print(decrypted_message[298], HEX);
   Serial.println(decrypted_message[299], HEX);
   Serial.print("-P: ");
   Serial.print(decrypted_message[301], HEX);
   Serial.print(decrypted_message[302], HEX);
   Serial.print(decrypted_message[303], HEX);
   Serial.println(decrypted_message[304], HEX);

   powerImport = decrypted_message[296] << 24 | decrypted_message[297] << 16 | decrypted_message[298] << 8 | decrypted_message[299]; // [W]
   powerExport = decrypted_message[301] << 24 | decrypted_message[302] << 16 | decrypted_message[303] << 8 | decrypted_message[304]; // [W]
   powerSaldo = powerImport - powerExport;
   Serial.print("PowerSaldo: ");
   Serial.println(powerSaldo);

   LoraSenden();
   _lastmillis = millis(); // data-age-sekunden-zähler auf 0 setzen
}

void ReadSerialData() {
   static byte start_byte = 0;
   static byte previous_byte = 0;
   static bool receiving = false;
   static unsigned int pos;

   while (Serial1.available()){
      byte current_byte = Serial1.read();
      //if (current_byte < 0x10) Serial.print("0");
      //Serial.print(current_byte, HEX);
      //Serial.print(' ');

      if (receiving == false) {
         if (start_byte == 0x7E && previous_byte == 0xA0 && current_byte == 0x84) { // Starting sequence is 7E A0 84
            receiving = true;
            received_data[0] = 0x7E;
            received_data[1] = 0xA0;
            received_data[2] = 0x84;
            pos = 3;
         } 
      } else {
         if (pos < MESSAGE_LENGTH) {
            received_data[pos] = current_byte;
            pos++;
         } else {
   
            // nur zum ausgeben der erhaltenen hex-werte ---------
            //Serial.print("received-data: ");
            //for (size_t i = 0; i < MESSAGE_LENGTH; i++){
            //   if (received_data[i] < 0x10) Serial.print("0");
            //   Serial.print(received_data[i], HEX);
            //   Serial.print(' ');
            //}
            //Serial.print('\n');
            // ----ausgeben ende ----------------------------------
            if (received_data[275] == 0x7E && received_data[276] == 0xA0){
               ParseReceivedData(); // zum auslesen der erhaltenen hex-werte
            }
            receiving = false;
         }
      }
      start_byte = previous_byte;
      previous_byte = current_byte;
   }
}


// --------------- SETUP ---------------
void setup() {
   delay(1500);
   Serial.begin(9600);
   delay(1500);
   Serial.println(""); //neue zeile
   Serial.println("Serial Monitor started.");

   pinMode(2, INPUT_PULLUP); // pin 2 für display-ein-aus-taster

   SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
   Wire.begin(I2C_SDA, I2C_SCL);
   delay(1500);
   LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);

   if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
      Serial.println("SSD1306 OLED-Display allocation failed.");
      return;
   } else {
      oled.setRotation(2); // bildschirm upside-down
      Serial.println("SSD1306 OLED-Display okay.");
      displayzeile[1] = "OLED-Display okay.";
      displayText();
   }

   if (!LoRa.begin(868E6)) {
      Serial.println("Starting LoRa failed!");
      displayzeile[2] = "Starting LoRa failed!";
      displayText();
      delay(2000);
      return;
   } else {
      LoRa.setSpreadingFactor(7);           // ranges from 6-12, default 7, see API docs
      // LoRa.setSignalBandwidth(125E3);
      // signal bandwidth in Hz, defaults to `125E3`
      // Supported values are `7.8E3`, `10.4E3`, `15.6E3`, `20.8E3`, `31.25E3`, `41.7E3`, `62.5E3`, `125E3`, `250E3`, and `500E3`.
      Serial.println("LoRa was started.");
      displayzeile[2] = "LoRa was started.";
      displayText();
   }

   // Note the format for setting a serial port is as follows: Serial2.begin(baud-rate, protocol, RX pin, TX pin);
   // Serial RX-Pin set to 4 (TX to 2, but not used)
   pinMode(4, INPUT_PULLDOWN);
   Serial1.begin(2400, SERIAL_8E1, 4, 2);
   Serial1.setTimeout(3000);
   Serial.println("Serialport1 was started.");
   Serial.println("Setup done.");
}


// --------------- LOOP ---------------
void loop() {
  ReadSerialData();

   // Taster abfragen, wenn Taster gedrückt wurde, Display ein oder ausschalten
   statusTaster = digitalRead(2); 
   if (statusTaster == !statusTasterLetzter) {// Wenn aktueller Tasterstatus anders ist als der letzte Tasterstatus
      if (statusTaster == LOW) {// Wenn Taster gedrückt
         if (displayPowerSave == true){ // Display an- bzw. ausschalten
            displayPowerSave = false;
            oled.ssd1306_command(SSD1306_DISPLAYON);
            Serial.println("Display an.");

         } else {
            displayPowerSave = true;
            oled.ssd1306_command(SSD1306_DISPLAYOFF);
            Serial.println("Display aus.");
         } 
      }            
   }
   statusTasterLetzter = statusTaster; // merken des letzten Tasterstatus

   Data_Age = (millis() - _lastmillis)/1000;
   displayzeile[4] = "Data Age: "+ std::to_string(Data_Age) + " s";
   if ((millis() - _lastmillisdisplay) > (1000)) { // display jede sekunde aktualisieren
      displayText();
      _lastmillisdisplay = millis();

   }
   if (Data_Age > 30){ // wenn länger als 30 s keine daten --> display blinken
      displayblinken();
   } else{
      if (display_on = false){ // damit, wenn blink-phase endet, display an ist
         oled.ssd1306_command(SSD1306_DISPLAYON);
         display_on = true;
      }            
   }

}