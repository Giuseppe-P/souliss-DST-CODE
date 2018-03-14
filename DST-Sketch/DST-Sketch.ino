
/********************************************
            DTS NEXTION v5.0

  DIGITAL SOULISS THERMOSTAT - Typical T31 -

             NODO GATEWAY

    Connessioni NODEMCU ESP8266-12
  _____________________________________
  GPIO 04 - D2(NODEMCU)     TX   NEXTION
  GPIO 02 - D4(NODEMCU)     RX   NEXTION

  GPIO 05 - D1(NODEMCU)     PIN  RELE'
  GPIO 12 - D6(NODEMCU)     DATA DHT22
  ______________________________________

  ---Giuseppe P.----

*********************************************/

// Let the IDE point to the Souliss framework
#include "SoulissFramework.h"

// Configure the framework
#include "bconf/MCU_ESP8266.h"              // Load the code directly on the ESP8266
#include "conf/Gateway.h"                   // The main node is the Gateway, we have just one node
#include "conf/IPBroadcast.h"

// Define the WiFi name and password
#define WIFICONF_INSKETCH
#define WiFi_SSID               "XXXXXXXXXXXXXXX"// IL NOME DELLA RETE WIFI
#define WiFi_Password           "XXXXXXXXXXXXXXX"// LA PASSWORD DELLA RETE


// **** Define the host name ****
#define HOST_NAME_INSKETCH                   // OTA
#define HOST_NAME "DST"          // OTA


// Include framework code and libraries
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <WiFiClientSecure.h>
#include <ESP8266TelegramBOT.h>
#include <ESP8266WebServer.h>                 //OTA
#include <ESP8266mDNS.h>                      //OTA
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>                       //OTA
#include "Souliss.h"

//NEXTION
#include <SoftwareSerial.h>
#include <Nextion.h>
SoftwareSerial nextion(4, 2);// Nextion TX to pin 2 and RX to pin 3 of Arduino
Nextion myNextion(nextion, 9600); //create a Nextion object named myNextion using the nextion serial port @ 9600bps

//DHT22
#include <DHT.h>

// Include sensor libraries (from Adafruit) Uncomment whatever type you're using!
//#define DHTTYPE DHT11   // DHT 11
#define DHTTYPE DHT22     // DHT 22 (AM2302), AM2321
//#define DHTTYPE DHT21   // DHT 21 (AM2301)
#define DHTPIN  12        //D6 NODEMCU
DHT dht(DHTPIN, DHTTYPE); // for ESP8266 use dht(DHTPIN, DHTTYPE, 11)

// This identify the number of the SLOT logic
#define TEMPERATURA           0
#define HUMIDITY              2
#define TERMOSTATO            4
#define StatoCaldaia          9
#define LEDPWM               10

#define DEADBAND            0.01        // Deadband value 5%  



// Telegram bot
#define botMyChatID "XXXXXXXXXXX" // IL TUO ID CHAT
#define botToken "XXXXXXXXXXXXXXXXXXXXXXXXX" //IL TUO TOKEN
#define botName "XXXXXXXXXXXXXXXXXXXXX"// IL NOME DEL BOT CREATO
#define botUsername "DST_souliss_bot"
TelegramBOT bot(botToken, botName, botUsername);
String DST_Riavviato = "DST Riavviato";
String DST_Spento = "DST Spento";
String DST_Acceso_0 = "DST Attivo - Riscaldamenti spenti";
String DST_Acceso_1 = "DST Attivo - Riscaldamenti Accesi";


float setpoint = 0;
float bloccaLetturaSet = 0;
float bloccaLetturaTemp = 0;
float bloccaLetturaUmy = 0;
float SETpointAvvio = 16.00;
float SETpointECO = 20.00;
float SETpointNORMAL = 22.00;
float SETpointCOMFORT = 25.00;
float SETpointTIMER = 21.00;
float MaxTemp = 35.00;
float MinTemp = 17.50;

int vecchio_stato = 0;
int bloccadimmer = 0;
int dimmer = 0;

// coordinate "progress" bar NON ATTIVO SU QUESTA VERSIONE
//int x = 3;
//int y = 192;
//int width = 115;
//int height = 30;
//int old_sensor_value = 0;

//byte rx = 0;    // variabile per contenere il carattere ricevuto
int val;
int timerPlus, timerMinus;


WiFiUDP ntpUDP;
int16_t utc = 1; //UTC (1 ora solare) (2 ora legale)
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", utc * 3600, 60000);


void setup() {

  myNextion.init();

  myNextion.sendCommand("page page2");//page attendere
  Serial.begin(9600);
  WiFi.begin(WiFi_SSID, WiFi_Password);  //telegram

  //setup souliss
  Initialize();

  // Connect to the WiFi network and get an address from DHCP
  GetIPAddress();
  SetAsGateway(myvNet_dhcp);       // Set this node as gateway for SoulissApp

  // This is the vNet address for this node, used to communicate with other
  // nodes in your Souliss network
  //  SetAddress(0xAB01, 0xFF00, 0x0000);
  //  SetAsPeerNode(0xAB02, 1);

  dht.begin();                                // initialize temperature sensor
  Set_T13(StatoCaldaia);
  Set_Temperature(TEMPERATURA);
  Set_Thermostat(TERMOSTATO);
  Set_Humidity(HUMIDITY);
  Set_DimmableLight(LEDPWM);
  pinMode(5, OUTPUT);      // RELE'
  Souliss_HalfPrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3), &SETpointAvvio);
  timeClient.begin();
  float humidity = dht.readHumidity();
  float temperatura = dht.readTemperature();

  // t o h a volte è nan, non inviare il valore nan agli slot
  while (isnan(humidity) || isnan(temperatura)) {
    Serial.println(temperatura);
    Serial.println(humidity);
  }
  myNextion.sendCommand("page page0");//home
  myNextion.setComponentText("t6", "");
  myNextion.setComponentText("t8", "");
  myNextion.setComponentText("t10", "");
  myNextion.setComponentText("t3", String("----"));
  timeClient.update();
  myNextion.setComponentText("t4", String(timeClient.getFormattedTime()));
  Serial.println(timeClient.getFormattedTime());

  myNextion.sendCommand("dims=100");

  myNextion.setComponentText("t1", String(temperatura));
  myNextion.setComponentText("t2", String(humidity));
  if (WiFi.status() != WL_CONNECTED) {
    myNextion.setComponentText("t6", "");
  } else {
    myNextion.setComponentText("t7", "");
  }

  // Init the OTA
  ArduinoOTA.setHostname(HOST_NAME);
  ArduinoOTA.begin();

  // start bot telegram
  bot.begin();
  bot.sendMessage(botMyChatID, DST_Riavviato, ""); // send to Telegram

  // importa negli slot souliss
  ImportAnalog(HUMIDITY, &humidity);
  ImportAnalog(TEMPERATURA, &temperatura);
  ImportAnalog(TERMOSTATO + 1, &temperatura);
}

void loop()
{

  EXECUTEFAST() {
    UPDATEFAST();

    FAST_10ms() {
      Logic_DimmableLight(LEDPWM);

      Serial.println((memory_map[MaCaco_OUT_s + LEDPWM + 1] + 45) / 3);

      dimmer = ((memory_map[MaCaco_OUT_s + LEDPWM + 1] + 45) / 3);// 100

      if ( dimmer != bloccadimmer) {

        if ((dimmer >= 0) && (dimmer < 35)) {
          myNextion.sendCommand("dims=25");
          Serial.println("25");
        } else if
        (( dimmer > 35) && (dimmer < 65)) {
          myNextion.sendCommand("dims=50");
          Serial.println("50");
        } else if
        (( dimmer > 65) && (dimmer < 85)) {
          myNextion.sendCommand("dims=75");
          Serial.println("75");
        } else if
        (dimmer > 85) {
          myNextion.sendCommand("dims=100");
          Serial.println("100");
        }
        bloccadimmer = dimmer;
      }
    }

    FAST_70ms() {

      String message = myNextion.listen(); //check for message
      if (message != "") { // if a message is received...
        Serial.println(message); //...print it out
      }
      if (message == "65 1 7 1 ff ff ff") {
        refreshTheme();
      }
      if (message == "65 1 1 1 ff ff ff") {
        Souliss_HalfPrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3), &SETpointTIMER);
        timerMinus = 3600 - timerPlus;///1 ora = 3600 sec
        mInput(TERMOSTATO) = Souliss_T3n_Heating;
      }
      if (message == "65 1 2 1 ff ff ff") {
        Souliss_HalfPrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3), &SETpointTIMER);
        timerMinus = 7200 - timerPlus;///2 ore = 7200 sec
        mInput(TERMOSTATO) = Souliss_T3n_Heating;
      }
      if (message == "65 1 3 1 ff ff ff") {
        Souliss_HalfPrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3), &SETpointTIMER);
        timerMinus = 10800 - timerPlus;///3 ore = 10800 sec
        mInput(TERMOSTATO) = Souliss_T3n_Heating;
      }
      if ((message == "65 1 1 1 ff ff ff") && (memory_map[MaCaco_OUT_s + TERMOSTATO] == 3) || (message == "65 1 1 1 ff ff ff") && (memory_map[MaCaco_OUT_s + TERMOSTATO] == 1)) {
        refreshTheme();
      }
      if ((message == "65 1 2 1 ff ff ff") && (memory_map[MaCaco_OUT_s + TERMOSTATO] == 3) || (message == "65 1 2 1 ff ff ff") && (memory_map[MaCaco_OUT_s + TERMOSTATO] == 1)) {
        refreshTheme();
      }
      if ((message == "65 1 3 1 ff ff ff") && (memory_map[MaCaco_OUT_s + TERMOSTATO] == 3) || (message == "65 1 3 1 ff ff ff") && (memory_map[MaCaco_OUT_s + TERMOSTATO] == 1)) {
        refreshTheme();
      }
      if (message == "65 0 2 1 ff ff ff") {
        mInput(TERMOSTATO) = Souliss_T3n_InSetPoint;
      }
      if (message == "65 0 3 1 ff ff ff") {
        mInput(TERMOSTATO) = Souliss_T3n_OutSetPoint;
      }
      if (( memory_map[MaCaco_OUT_s + TERMOSTATO] == 0) && (message == "65 0 10 1 ff ff ff")) {
        mInput(TERMOSTATO) = Souliss_T3n_Heating;
      }
      if (( memory_map[MaCaco_OUT_s + TERMOSTATO] == 1) && (message == "65 0 10 1 ff ff ff")) {
        timerPlus = 0;
        timerMinus = 0;
        mInput(TERMOSTATO) = Souliss_T3n_ShutDown;
      }
      if (( memory_map[MaCaco_OUT_s + TERMOSTATO] == 3) && (message == "65 0 10 1 ff ff ff")) {
        timerPlus = 0;
        timerMinus = 0;
        mInput(TERMOSTATO) = Souliss_T3n_ShutDown;
      }
      if (message == "65 1 4 1 ff ff ff") {
        mInput(TERMOSTATO) = Souliss_T3n_Heating;
        Souliss_HalfPrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3), &SETpointECO);
        myNextion.sendCommand("b4.picc=3");
        myNextion.sendCommand("b5.picc=2");
        myNextion.sendCommand("b6.picc=2");
      }
      if (message == "65 1 5 1 ff ff ff") {
        mInput(TERMOSTATO) = Souliss_T3n_Heating;
        Souliss_HalfPrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3), &SETpointNORMAL);
        myNextion.sendCommand("b5.picc=3");
        myNextion.sendCommand("b6.picc=2");
        myNextion.sendCommand("b4.picc=2");
      }
      if (message == "65 1 6 1 ff ff ff") {
        mInput(TERMOSTATO) = Souliss_T3n_Heating;
        Souliss_HalfPrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3), &SETpointCOMFORT);
        myNextion.sendCommand("b6.picc=3");
        myNextion.sendCommand("b5.picc=2");
        myNextion.sendCommand("b4.picc=2");
      }
      if (message == "65 0 1 1 ff ff ff") {
        if (Souliss_SinglePrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3)) == (SETpointECO)) {
          myNextion.sendCommand("b4.picc=3");
          myNextion.sendCommand("b5.picc=2");
          myNextion.sendCommand("b6.picc=2");
        }
        if (Souliss_SinglePrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3)) == (SETpointNORMAL)) {
          myNextion.sendCommand("b5.picc=3");
          myNextion.sendCommand("b6.picc=2");
          myNextion.sendCommand("b4.picc=2");

        }
        if (Souliss_SinglePrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3)) == (SETpointCOMFORT)) {
          myNextion.sendCommand("b6.picc=3");
          myNextion.sendCommand("b5.picc=2");
          myNextion.sendCommand("b4.picc=2");
        }
      }
    }

    ///////////////////////////LOGIC T31///////////////////////////////////////////////
    FAST_110ms() {
      Logic_Thermostat(TERMOSTATO);
      nDigOut(5, Souliss_T3n_HeatingOn, TERMOSTATO);
      // We are not handling the cooling mode, if enabled by the user, force it back
      // to disable
      if (mOutput(TERMOSTATO) & Souliss_T3n_CoolingOn)
        mOutput(TERMOSTATO) &= ~Souliss_T3n_CoolingOn;

      //////////////////////////////////AGG. SETPOINT NEXTION///////////////////////////////////////////////

      setpoint = Souliss_SinglePrecisionFloating(memory_map + MaCaco_OUT_s + TERMOSTATO + 3);
      if (setpoint  == bloccaLetturaSet)  {
      } else {
        if ( setpoint != bloccaLetturaSet) {
          myNextion.setComponentText("t0", String(setpoint));
        }
        bloccaLetturaSet = setpoint;
      }
    }


    FAST_1110ms() {

      if (memory_map[MaCaco_OUT_s + TERMOSTATO] == 3) {
        timerPlus++;
        timerMinus--;
      }
      if (timerMinus == 0 && memory_map[MaCaco_OUT_s + TERMOSTATO] == 3) {
        mInput(TERMOSTATO) = Souliss_T3n_ShutDown;
        timerPlus = 0;
        timerMinus = 0;
      }
    }

    FAST_2110ms()  {
      Logic_T13(StatoCaldaia);
      Serial.println(memory_map[MaCaco_OUT_s + TERMOSTATO]);
      if ( memory_map[MaCaco_OUT_s + TERMOSTATO] == vecchio_stato)  {
      } else
      {
        if ( memory_map[MaCaco_OUT_s + TERMOSTATO] == 0) {
          mInput(StatoCaldaia) = Souliss_T1n_OffCmd;
          Souliss_HalfPrecisionFloating((memory_map + MaCaco_OUT_s + TERMOSTATO + 3), &SETpointAvvio);
          timerPlus = 0;
          timerMinus = 0;
          myNextion.sendCommand("page page3");//page attendere
          yield();
          bot.sendMessage(botMyChatID, DST_Spento, ""); // send to Telegram
          myNextion.sendCommand("page page0");//home
          myNextion.setComponentText("t0", String(setpoint));
          timeClient.update();
          myNextion.setComponentText("t4", String(timeClient.getFormattedTime()));
          myNextion.sendCommand("b0.picc=0");//power off
          myNextion.setComponentText("t8", "");//fiamma off
          myNextion.setComponentText("t3", String(timerMinus / 60) + String("m"));
          float hum = mOutputAsFloat(HUMIDITY);
          float temp = mOutputAsFloat(TEMPERATURA);
          myNextion.setComponentText("t1", String(temp));
          myNextion.setComponentText("t2", String(hum));
        }
        if ( memory_map[MaCaco_OUT_s + TERMOSTATO] == 1) {
          mInput(StatoCaldaia) = Souliss_T1n_OffCmd;
          timerPlus = 0;
          timerMinus = 0;
          myNextion.sendCommand("page page3");//page attendere
          yield();
          bot.sendMessage(botMyChatID, DST_Acceso_0, ""); // send to Telegram
          myNextion.sendCommand("page page0");//home
          myNextion.setComponentText("t0", String(setpoint));
          timeClient.update();
          myNextion.setComponentText("t4", String(timeClient.getFormattedTime()));
          myNextion.sendCommand("b0.picc=1");//power on
          myNextion.setComponentText("t8", "");//fiamma off
          myNextion.setComponentText("t3", String(timerMinus / 60) + String("m"));
          float hum = mOutputAsFloat(HUMIDITY);
          float temp = mOutputAsFloat(TEMPERATURA);
          myNextion.setComponentText("t1", String(temp));
          myNextion.setComponentText("t2", String(hum));
        }
        if ( memory_map[MaCaco_OUT_s + TERMOSTATO] == 3) {
          mInput(StatoCaldaia) = Souliss_T1n_OnCmd;
          myNextion.sendCommand("page page3");//page attendere
          yield();
          bot.sendMessage(botMyChatID, DST_Acceso_1, ""); // send to Telegram
          myNextion.sendCommand("page page0");//home
          myNextion.setComponentText("t0", String(setpoint));
          timeClient.update();
          myNextion.setComponentText("t4", String(timeClient.getFormattedTime()));
          myNextion.sendCommand("b0.picc=1");//power on
          myNextion.setComponentText("t9", "");//fiamma
          myNextion.setComponentText("t3", String(timerMinus / 60) + String("m"));
          float hum = mOutputAsFloat(HUMIDITY);
          float temp = mOutputAsFloat(TEMPERATURA);
          myNextion.setComponentText("t1", String(temp));
          myNextion.setComponentText("t2", String(hum));

        }
        vecchio_stato = memory_map[MaCaco_OUT_s + TERMOSTATO];
      }
    }

    ////////////////////////////LOGIC T13 - AGG. NEXTION////////////////////////////////////////////
    FAST_7110ms() {

      Logic_Humidity(HUMIDITY);
      Logic_Temperature(TEMPERATURA);
    }
    FAST_91110ms() {
      int tent = 0;
      // Serial.println("Verifico connessione

      while ((WiFi.status() != WL_CONNECTED) && tent < 4)
      {
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        WiFi.begin(WiFi_SSID , WiFi_Password);

        int ritardo = 0;
        while ((WiFi.status() != WL_CONNECTED) && ritardo < 20)
        {
          delay(500);
          ritardo += 1;
          Serial.println(ritardo);
        }

        if (WiFi.status() != WL_CONNECTED )
          delay(2000);
        tent += 1;
      }
      if (tent > 4)Serial.println("tentativo non riuscito");
    }
    // Here we handle here the communication with Android
    FAST_GatewayComms();
  }
  EXECUTESLOW() {
    UPDATESLOW();

    SLOW_10s() {  // Read temperature and humidity from DHT every 110 seconds
      Timer_DimmableLight(LEDPWM);

    }

    SLOW_90s() {
      if (timerMinus < 0) { // if timer is to the minimum, you see timerPlus on the display
        Serial.println(String(timerPlus / 60) + String("m"));
        myNextion.setComponentText("t3", String ("+") + String(timerPlus / 60) + String("m"));
      } else {
        myNextion.setComponentText("t3", String(timerMinus / 60) + String("m"));
        Serial.println(String ("-") + String(timerMinus / 60) + String("m"));
      }
    }

    //////////////////////////////LETTURA DHT22 IMPORT T52 T53 T31 E NEXTION/////////////////////////////////////////////
    SLOW_110s() {
      float humidity = dht.readHumidity();
      float temperatura = dht.readTemperature();

      // t o h a volte è nan, non inviare il valore nan agli slot
      if (isnan(humidity) || isnan(temperatura)) {
        Serial.println(temperatura);
        Serial.println(humidity);
      } else {
        ImportAnalog(HUMIDITY, &humidity);
        ImportAnalog(TEMPERATURA, &temperatura);
        ImportAnalog(TERMOSTATO + 1, &temperatura);

        //SE CAMBIA LA TEMP. AGGIRNA IL NEXTION
        if (temperatura  == bloccaLetturaTemp)  {
          //NON FARE NULLA
        } else {
          if ( temperatura != bloccaLetturaTemp) {
            myNextion.setComponentText("t1", String(temperatura));
          }
          /////////////////////PROGRESSBAR//////////////////////////////////

          //                  int sensor = temperatura;
          //                  if (abs(sensor - old_sensor_value) > 10) {
          //                    old_sensor_value = sensor;
          //                    int scaled_value = map(sensor, 0, 30, 0, 100); // always map value from 0 to 1023
          //                    myNextion.updateProgressBar(x, y, width, height, scaled_value, 0, 1); // update the progress bar
          //                  }
          bloccaLetturaTemp = temperatura;
        }
        if (temperatura > MinTemp) {
          myNextion.setComponentText("t10", "");
        } else {
          myNextion.setComponentText("t11", "");
        }

        //SE CAMBIA UMY AGGIORNA IL NEXTION
        if (humidity  == bloccaLetturaUmy) {
        } else {
          if ( humidity != bloccaLetturaUmy) {
            myNextion.setComponentText("t2", String(humidity));
          }
          bloccaLetturaUmy = humidity;
        }
      }
      timeClient.update();
      myNextion.setComponentText("t4", String(timeClient.getFormattedTime()));
    }
    SLOW_510s() {
      if (WiFi.status() != WL_CONNECTED) {
        myNextion.setComponentText("t6", "");
      } else {
        myNextion.setComponentText("t7", "");
      }
    }
  }
  // Look for a new sketch to update over the air
  ArduinoOTA.handle();
}
void refreshTheme() {

  if ( memory_map[MaCaco_OUT_s + TERMOSTATO] == 0) {
    myNextion.sendCommand("page page2");//page attendere
    delay(500);
    myNextion.sendCommand("page page0");//home
    myNextion.setComponentText("t0", String(setpoint));
    myNextion.setComponentText("t3", String(timerMinus / 60) + String("m"));
    timeClient.update();
    myNextion.setComponentText("t4", String(timeClient.getFormattedTime()));
    myNextion.sendCommand("b0.picc=0");//power off
    myNextion.setComponentText("t8", "");//fiamma off
  }
  if ( memory_map[MaCaco_OUT_s + TERMOSTATO] == 1) {
    myNextion.sendCommand("page page2");//page attendere
    delay(500);
    myNextion.sendCommand("page page0");//home
    myNextion.setComponentText("t0", String(setpoint));
    myNextion.setComponentText("t3", String(timerMinus / 60) + String("m"));
    timeClient.update();
    myNextion.setComponentText("t4", String(timeClient.getFormattedTime()));
    myNextion.sendCommand("b0.picc=1");//power on
    myNextion.setComponentText("t8", "");//fiamma off
  }
  if ( memory_map[MaCaco_OUT_s + TERMOSTATO] == 3) {
    myNextion.sendCommand("page page2");//page attendere
    delay(500);
    myNextion.sendCommand("page page0");//home
    myNextion.setComponentText("t0", String(setpoint));
    myNextion.setComponentText("t3", String(timerMinus / 60) + String("m"));
    timeClient.update();
    myNextion.setComponentText("t4", String(timeClient.getFormattedTime()));
    myNextion.sendCommand("b0.picc=1");//power on
    myNextion.setComponentText("t9", "");//fiamma
  }
  float hum = mOutputAsFloat(HUMIDITY);
  float temp = mOutputAsFloat(TEMPERATURA);
  myNextion.setComponentText("t1", String(temp));
  myNextion.setComponentText("t2", String(hum));
}


