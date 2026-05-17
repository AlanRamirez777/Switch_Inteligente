//#define PRINT_DEBUG_MESSAGES
#include "data.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include "ThingSpeak.h"
#include <vector>
#include <String>
#include <Time.h>
#include <Preferences.h>

WiFiMulti wifiMulti;
WiFiClient net;

// Pin de entrada de corriente de voltaje
// 0% = 0V, 100% = 500V
int pinVolt = 35;

// Pin de salida del switch
int pinOutput = 2;

// Pin de entrada del botón
int pinButton = 4;

//Vector que registra los valores en el periodo antes de enviar
vector<float> volt_values;

// datos offline
Json_Results<10> offline_data;

// --------------------------------------------
// Variables para el control de sincronización
unsigned long waitCheckWiFi = 0;
unsigned long waitVoltRegister = 0;
unsigned long waitWriteResults = 0;
// --------------------------------------------

unsigned long lastMillis = 0;

// Triggers
bool isButtonPressed;

float lastCommandUpdated = 0;
String lastCommandDate = "";

// Limite puesto de tolerancia del voltaje
float limit_volt = 150.0;

//


/*
  OUTPUT STATUS estado de salida actual
  0 = off
  1 = on

  SWITCH STATUS (estado o afectación que se le dió a algún cambio de la salida del switch)
  0 = none (on, off)
  1 = voltaje (off)
  2 = button (on, off)
  3 = schedule (on, off)
  4 = command (on, off)

*/
int output_status = 0;
int switch_status = 0;

bool test_mode = false;

void setup(){
  pinMode(pinOutput, OUTPUT);
  pinMode(pinButton, INPUT_PULLDOWN);
  pinMode(pinVolt, INPUT);

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
  if(test_mode){
    //-----------------------
    // Test code

    //-----------------------

    lastMillis = millis();
    return;
  }

  //checar conexion cada segundo
  if(millis() - waitCheckWiFi > 3000){
    waitCheckWiFi = millis();
    conectar();
  }
  
  // Comprobar si el botón fué presionado y cambiar el estado de isButtonPressed (trigger)
  check_button();
  
  // Leer el valor del voltaje cada segundo
  if(millis() - waitVoltRegister > 500){
    waitVoltRegister = millis();
    check_volt();
  }

  // Registrar en thingspeak, de forma local o subir el json cada 20 segundos todos los fields
  if(millis() - waitWriteResults > 20000){
    waitWriteResults = millis();
    write_results();
  }

  // Imprimir al final el valor que se tuvo a la salida del switch
  digitalWrite(pinOutput, output_status);

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

  for(int i = 0; i < volt_values.size(); i++){
    sum +=volt_values[i];
    if(min == -1 || volt_values[i] < min){
      min = volt_values[i];
    }
    if(max == -1 || volt_values[i] > max ){
      max = volt_values[i];
    }
  }
  prom = sum/volt_values.size();

  lastCommandUpdated = ThingSpeak.readFloatField(channel_id, 7, read_API_key);
  lastCommandDate = ThingSpeak.getCreatedAt();
  //OFF
  if(lastCommandUpdated == 0 || lastCommandUpdated < 0){
    lastLedMessage = String("Led OFF.");
    digitalWrite(pinOutput, LOW);
    blinkingAt = 0;
  }
  //ON
  else if(lastCommandUpdated == 1){
    lastLedMessage = String("Led ON.");
    digitalWrite(pinOutput, HIGH);
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

  volt_values.clear();
}

void check_button(){
   if(digitalRead(pinButton) == HIGH && !isButtonPressed){
    isButtonPressed = true;
  }
  if(digitalRead(pinButton) == LOW && isButtonPressed){
    isButtonPressed = false;
    switch_status = 2;
    if(output_status == 1){
      output_status = 0;
    }
    else{
      output_status = 1;
    }
  } 
}

void check_volt(){
  int voltLectura = analogRead(pinVolt);
  volt_values.push_back(map(voltLectura, 0.0, 4095.0, 0.0, 100.0));
}