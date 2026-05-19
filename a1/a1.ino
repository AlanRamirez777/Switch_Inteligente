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

String lastCommandUpdated = "";
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
  4 = command (on, off)

*/
int output_status = 0;
int switch_status = 0;


unsigned long timeDelayOverVolt = 0;
int nextOutputStatusOverVolt = 0;
int nextSwitchStatusOverVolt = 0;

unsigned long timeCounter = 0;
int finalOutputStatusTimeCounter = 0;
bool isTimeCounter = false;

String lastRetroMessage = String("");

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
  if(millis() - waitVoltRegister > 100){
    waitVoltRegister = millis();
    check_volt();
  }

  // Registrar en thingspeak, de forma local o subir el json cada 20 segundos todos los fields
  if(millis() - waitWriteResults > 20000){
    waitWriteResults = millis();
    write_results();
  }

  // Imprimir al final el valor que se tuvo a la salida del switch
  REP_CHECK:
  int real_output = output_status;
  if(timeDelayOverVolt == 0){
    if(isTimeCounter){
      if(timeCounter > millis() - lastMillis){
        timeCounter = timeCounter - (millis() - lastMillis);
      }
      else{
        timeCounter = 0;
        isTimeCounter = false;
        output_status = finalOutputStatusTimeCounter;
        real_output = output_status;
        switch_status = 0;
      }
    }
  }
  else{
    output_status = 0;
    switch_status = 1;
    real_output = output_status;
    if(timeDelayOverVolt > millis() - lastMillis) {
      timeDelayOverVolt = timeDelayOverVolt - (millis() - lastMillis);
    }
    else{
      timeDelayOverVolt = 0;
      output_status = nextOutputStatusOverVolt;
      switch_status = nextSwitchStatusOverVolt;
      goto REP_CHECK;
    }
  }
  digitalWrite(pinOutput, real_output);

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


  ThingSpeak.setField(1, prom);
  ThingSpeak.setField(2, max);
  ThingSpeak.setField(3, min);
  if (offline_data.size() > 0){
    ThingSpeak.setField(4, offline_data.get_last_Json());
    offline_data.remove_last_result();
  }
  if(!lastRetroMessage.equals(String(""))){
    ThingSpeak.setField(5, String("MENSAJE DE LA SWITCH:\n")+lastRetroMessage);
  }
  else{
    ThingSpeak.setField(5, " ");
  }
  int code = ThingSpeak.writeFields(channel_id, write_API_key);

  if(code != 200){
    //Serial.println("Erroooooooor ");
    offline_data.add_results(prom, max, min);
  }
  else{
    lastRetroMessage = String("");
  }

  // Leer el último comando en el field de comandos
  lastCommandUpdated = ThingSpeak.readStringField(channel_id, 5, read_API_key);
  lastCommandUpdated.trim();
  lastCommandUpdated.toLowerCase();
  String currentComandDate = ThingSpeak.getCreatedAt();
  currentComandDate.trim();
  Serial.print("Revisando comandos...\t");
  Serial.println(lastCommandUpdated);

  {
    //OFF
    if(lastCommandUpdated.equals(String("off"))){
      Serial.println("Comando \"off\" dado...");
      reset_time_counter();
      if(timeDelayOverVolt == 0){
        output_status = 0;
        switch_status = 4;
      }
      else{
        nextOutputStatusOverVolt = 0;
        nextSwitchStatusOverVolt = 4;
      }
    }
    //ON
    else if(lastCommandUpdated.equals(String("on"))){
      Serial.println("Comando \"on\" dado...");
      reset_time_counter();
      if(timeDelayOverVolt == 0){
        output_status = 1;
        switch_status = 4;
      }
      else{
        Serial.println(timeDelayOverVolt);
        nextOutputStatusOverVolt = 1;
        nextSwitchStatusOverVolt = 4;
      }
    }
    //contador OFF
    else if(lastCommandUpdated.startsWith("coff") && lastCommandUpdated.indexOf("=") > 0){
      Serial.println("Comando \"coff\" dado...");
      String s_value = lastCommandUpdated.substring(lastCommandUpdated.indexOf("=") + 1);
      float value = s_value.toFloat();
      if(timeDelayOverVolt == 0){
        output_status = 0;
        switch_status = 4;
      }
      else{
        nextOutputStatusOverVolt = 0;
        nextSwitchStatusOverVolt = 4;
      }
      set_time_counter(value*1000.0, 1);
    }
    //contador ON
    else if(lastCommandUpdated.startsWith("con") && lastCommandUpdated.indexOf("=") > 0){
      Serial.println("Comando \"con\" dado...");
      String s_value = lastCommandUpdated.substring(lastCommandUpdated.indexOf("=") + 1);
      float value = s_value.toFloat();
      if(timeDelayOverVolt == 0){
        output_status = 1;
        switch_status = 4;
      }
      else{
        nextOutputStatusOverVolt = 1;
        nextSwitchStatusOverVolt = 4;
      }
      set_time_counter(value*1000.0, 0);
    }
    //limite de voltaje
    else if(lastCommandUpdated.startsWith("lv") && lastCommandUpdated.indexOf("=") > 0){
      Serial.println("Comando \"lv\" dado...");
      String s_value = lastCommandUpdated.substring(lastCommandUpdated.indexOf("=") + 1);
      float value = s_value.toFloat();
      limit_volt = value;
    }
    //Pedir status
    else if(lastCommandUpdated.startsWith(String("status"))){
      Serial.println("Comando \"status\" dado...");
      lastRetroMessage = String("");
      switch (switch_status) {
        case 1:{
          lastRetroMessage += String("Apagado por subida de voltaje\n");
          break;
        }
        case 2:{
          lastRetroMessage += String("Ultimo cambio por el boton fisico\n");
          break;
        }
        case 4:{
          lastCommandDate += String("Ultimo cambio por comando\n");
          break;
        }
      }

      if(isTimeCounter){
        lastRetroMessage = String("Contador asignado con un tiempo restante de: ");
        lastRetroMessage += String(timeCounter);
        lastRetroMessage += String("ms\n");
      }

      if(output_status){
        lastRetroMessage += String("Salida: HABILITADA\n");
      }
      else{
        lastRetroMessage += String("Salida: DESHABILITADA\n");
      }
    }
    // comando HELP
    else if(lastCommandUpdated.equals(String("help"))){
      Serial.println("Comando \"help\" dado...");
      lastRetroMessage = String("");
      lastRetroMessage += String("COMANDOS: - on - off - coff=<seg> - con=<seg> - lv=<volt> - status");
    }
  }
  lastCommandDate = ThingSpeak.getCreatedAt();
  lastCommandDate.trim();

  volt_values.clear();
}

void check_button(){
  if(digitalRead(pinButton) == HIGH && !isButtonPressed){
    Serial.println("boton: 1");
    isButtonPressed = true;
  }
  if(digitalRead(pinButton) == LOW && isButtonPressed){
    Serial.println("boton: 0");
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
  float volt_val = map(voltLectura, 0.0, 4095.0, 0.0, 500.0);
  volt_values.push_back(volt_val);
  if(volt_val >= limit_volt){
    if(timeDelayOverVolt == 0){
      nextOutputStatusOverVolt = output_status;
      nextSwitchStatusOverVolt = switch_status;
    }
    timeDelayOverVolt = 5000;
    output_status = 0;
    switch_status = 1;
  }
}

void reset_time_counter(){
  timeCounter = 0;
  isTimeCounter = false;
}

void set_time_counter(float time, int finalOutputResult){
  timeCounter = String(time).toDouble();
  finalOutputStatusTimeCounter = finalOutputResult;
  isTimeCounter = true;
}