//#define PRINT_DEBUG_MESSAGES
#include "data.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include "ThingSpeak.h"
#include <vector>
#include <String>

WiFiMulti wifiMulti;
WiFiClient net;

// Pin de entrada del potenciometro
int pinPot = 35;

// Pin de salida al led
int pinLed = 2;

// Pin de entrada del botón
int pinButton = 4;

//Vector que registra los valores en el periodo antes de enviar
vector<float> pot_values;

// datos offline
Json_Results<10> offline_data;

unsigned long waitCheckWiFi = 0;
unsigned long waitPotRegister = 0;
unsigned long waitWriteResults = 0;
long waitDoubleButton = 0;
unsigned long waitBlink = 0;

unsigned long lastMillis = 0;

int countPressed = 0;
int countDoublePressed = 0;

bool isButtonPressed;

float lastCommandUpdated = 0;
String lastCommandDate = "";
float blinkingAt = 0;

void setup(){
  pinMode(pinLed, OUTPUT);
  pinMode(pinButton, INPUT_PULLDOWN);
  pinMode(pinPot, INPUT);

  Serial.begin(9600);
  while(!Serial){}

  wifiMulti.addAP(ssid_1, password_1);
  wifiMulti.addAP(ssid_2, password_2);
  wifiMulti.addAP(ssid_3, password_3);

  WiFi.mode(WIFI_STA);
  conectar();
}

void loop(){
  delay(10);
  //checar conexion cada segundo
  if(millis() - waitCheckWiFi > 3000){
    waitCheckWiFi = millis();
    conectar();
  }

  // Leer el valor del potenciómetro cada segundo
  if(millis() - waitPotRegister > 1000){
    waitPotRegister = millis();
    int potLectura = analogRead(pinPot);
    pot_values.push_back(map(potLectura, 0.0, 4095.0, 0.0, 100.0));
  }

  // Revisar si hay doble toque de botón o simples y registrarlos
  waitDoubleButton -= millis() - lastMillis;
  if(waitDoubleButton < 0){
    waitDoubleButton = 0;
  }
  //Serial.println(waitDoubleButton);
  if(digitalRead(pinButton) == HIGH && !isButtonPressed){
    isButtonPressed = true;
    countPressed += 1;
    Serial.println("Pressed");
    if (waitDoubleButton > 0){
      Serial.println("Double pressed");
      countDoublePressed += 1;
      waitDoubleButton = 0;
    }
    else{
      waitDoubleButton = 1500;
    }
  }
  if(digitalRead(pinButton) == LOW && isButtonPressed){
    isButtonPressed = false;
  }
  
  // Registrar en thingspeak, de forma local o subir el json cada 20 segundos todos los fields
  if(millis() - waitWriteResults > 20000){
    waitWriteResults = millis();
    write_results();
  }

  if(millis() - waitBlink > (1/blinkingAt)*500.0 && blinkingAt > 0){
    waitBlink = millis();
    if(digitalRead(pinLed) == HIGH){
      digitalWrite(pinLed, LOW);
    }
    else{
      digitalWrite(pinLed, HIGH);
    }
  }

  lastMillis = millis();
}

void conectar(){
  //Checar si está conectado, de lo contrario intentar conectarse
  if(WiFi.status() != WL_CONNECTED){
    Serial.print("\nConectando a internet...");
    wifiMulti.run();
  }
}

void write_results(){

  ThingSpeak.begin(net);
  float min = -1;
  float max = -1;
  float sum = 0;
  float prom = 0;
  String lastLedMessage = String("");

  for(int i = 0; i < pot_values.size(); i++){
    sum +=pot_values[i];
    if(min == -1 || pot_values[i] < min){
      min = pot_values[i];
    }
    if(max == -1 || pot_values[i] > max ){
      max = pot_values[i];
    }
  }
  prom = sum/pot_values.size();

  lastCommandUpdated = ThingSpeak.readFloatField(channel_id, 7, read_API_key);
  lastCommandDate = ThingSpeak.getCreatedAt();
  //OFF
  if(lastCommandUpdated == 0 || lastCommandUpdated < 0){
    lastLedMessage = String("Led OFF.");
    digitalWrite(pinLed, LOW);
    blinkingAt = 0;
  }
  //ON
  else if(lastCommandUpdated == 1){
    lastLedMessage = String("Led ON.");
    digitalWrite(pinLed, HIGH);
    blinkingAt = 0;
  }
  //BLINK
  else{
    lastLedMessage = String("Led BLINK at: ")+blinkingAt+String("Hz.");
    blinkingAt = lastCommandUpdated;
  }


  ThingSpeak.setField(1, prom);
  ThingSpeak.setField(2, max);
  ThingSpeak.setField(3, min);
  if (offline_data.size() > 0){
    ThingSpeak.setField(4, offline_data.get_last_Json());
    offline_data.remove_last_result();
  }
  ThingSpeak.setField(5, countPressed);
  ThingSpeak.setField(6, countDoublePressed);
  if(!lastLedMessage.equals(String(""))){
    ThingSpeak.setField(8, lastLedMessage);
  }
  int code = ThingSpeak.writeFields(channel_id, write_API_key);

  if(code != 200){
    //Serial.println("Erroooooooor ");
    offline_data.add_results(prom, max, min);
  }

  pot_values.clear();
}