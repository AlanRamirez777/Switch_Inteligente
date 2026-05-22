//#define PRINT_DEBUG_MESSAGES
#include "data.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include "ThingSpeak.h"
#include <vector>
#include <String>
#include <Time.h>
#include <Preferences.h>

// SE utiliza para poder conectarse al internet (con múltiples opciones de conexión dadas en data.h)
WiFiMulti wifiMulti;
// Define el cliente WIFI para conocer el estado de la conexión a internet
WiFiClient net;

// Pin de entrada de corriente de voltaje
// Rango entr 0v a 500v
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
unsigned long waitCheckWiFi = 0; // Revisa el estado del internet cada 3s
unsigned long waitVoltRegister = 0; // Revisa y registra la entrada de voltaje cada 0.1s
unsigned long waitWriteResults = 0; // Lee y escribe los resultados en ThingSpeak cada 20 segundos (registra los resultados localmente si no está disponible)
// --------------------------------------------

// Variable que nos ayuda a que los contadores funcionen adecuadamente
unsigned long lastMillis = 0;

// Bandera para saber si el botoón fué presionado
bool isButtonPressed;

// --------------------------------------------
// Se registran lso últimos datos leidos de ThingSpeak en estas variables
String lastCommandUpdated = "";
String lastCommandDate = "";
// --------------------------------------------

// Limite puesto de tolerancia del voltaje
float limit_volt = 150.0;

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

// --------------------------------------------
// "OverVolt" equivale a cuando se supera el límite de voltaje
// Contador de tiempo que se efectua para que cuando se supere cambia el estado a las otras 2 vriables los estados
unsigned long timeDelayOverVolt = 0;
int nextOutputStatusOverVolt = 0;
int nextSwitchStatusOverVolt = 0;
// --------------------------------------------

// --------------------------------------------
// Varaibles para el contador (comandos "con" y "coff")
// Contador de tiempo que se asigna inicialmente y decrementa hasta 0
unsigned long timeCounter = 0;
// Al terminar el contador se asigna en output_status el valor de esta variable
int finalOutputStatusTimeCounter = 0;
// bandera que muestra que si el contador está activo
bool isTimeCounter = false;
// --------------------------------------------

// Ayuda a mostrar un mensaje en el field 4 (comandos), guarda un valor hasta que se pueda subir
String lastRetroMessage = String("");

bool test_mode = false;

void setup(){
  pinMode(pinOutput, OUTPUT);
  pinMode(pinButton, INPUT_PULLDOWN);
  pinMode(pinVolt, INPUT);

  Serial.begin(9600);
  while(!Serial){}

  // Asignamos las posibles conexiones a internet a la lista de wifimulti
  wifiMulti.addAP(ssid_1, password_1);
  wifiMulti.addAP(ssid_2, password_2);
  wifiMulti.addAP(ssid_3, password_3);

  // Revisamos que si se puede conectar e intentamos conectarnos a internet
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
    // En caso de que el contador esté activo, se decrementa y cuando llegue a 0 se asigna output_status y se cambia la bandera a "false"
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
  // Si el voltaje está por encima del esperado o está en el tiempo de espera para reactivarse entonces se decrementa su contador y, en caso de terminar define los "status" como estaban antes y se comprueba la lógica de nuevo
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
  // El resultado de todos los anteriores análisis se asigna al valor de salida del Switch
  digitalWrite(pinOutput, real_output);

  // Asignamos el valor del tiempo actual a lastMillis para tener control del tiempo que se ha tomado por ciclo
  lastMillis = millis();
}

// ESta función nos permite checar si la conexión está activa, de lo contrario se intentará conectarse
void conectar(){
  //Checar si está conectado, de lo contrario intentar conectarse
  if(WiFi.status() != WL_CONNECTED){
    Serial.print("\nConectando a internet...");
    wifiMulti.run();
  }
}

// Esta función permite aplicar los cálculos de los voltajes, leer el valor en el field de comando para aplicarlo, y escribir los resultados en ThingSpeak
void write_results(){

  // Preparamos ThingSpeak para poder acceder a este
  ThingSpeak.begin(net);

  // --------------------------------------------
  // Realizamos los cálculos e máximo, mínimo y promedio de voltaje
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
  // --------------------------------------------
  // --------------------------------------------
  // Escribimos los resultados en ThingSpeak
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

  // En caso de que no se haya mandado los resultados entonces los guardamos de forma local temporalmente
  if(code != 200){
    offline_data.add_results(prom, max, min);
  }
  // Si se mandaron los resultados entonces reiniciamos el mensaje próximo del switch
  else{
    lastRetroMessage = String("");
  }
  // --------------------------------------------
  // --------------------------------------------
  // Leemos el field de comandos para conocer si se escribió un comando, en caso de que sí entonces se ejecutará uno de los siguiente comandos si es válido
  lastCommandUpdated = ThingSpeak.readStringField(channel_id, 5, read_API_key);
  lastCommandUpdated.trim();
  lastCommandUpdated.toLowerCase();
  String currentComandDate = ThingSpeak.getCreatedAt();
  currentComandDate.trim();
  Serial.print("Revisando comandos...\t");
  Serial.println(lastCommandUpdated);

  {
    //Comando OFF (apaga la salida del switch)
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
    //Comando ON (habilita la salida del switch)
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
    //Comando COFF (Contador off, se mantiene apagado hasta que el tiempo asignado se supere y se habilite la salida)
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
    //Comando CON (Contador ON, se mantiene prendido hasta que el tiempo asignado se supere y se deshabilite la salida)
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
    //Comando LV (Limite de Voltaje, se le asigna un nuevo límite de voltaje)
    else if(lastCommandUpdated.startsWith("lv") && lastCommandUpdated.indexOf("=") > 0){
      Serial.println("Comando \"lv\" dado...");
      String s_value = lastCommandUpdated.substring(lastCommandUpdated.indexOf("=") + 1);
      float value = s_value.toFloat();
      limit_volt = value;
    }
    //Comando STATUS (Se le envía al usuario el estado actual del switch, en donde se muestra qué lo hizo cambiar de estado y si está habilitado o deshabilitado)
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
    //Comando HELP (Se le envía al usuario los comandos que puede ejecutar)
    else if(lastCommandUpdated.equals(String("help"))){
      Serial.println("Comando \"help\" dado...");
      lastRetroMessage = String("");
      lastRetroMessage += String("COMANDOS: - on - off - coff=<seg> - con=<seg> - lv=<volt> - status");
    }
  }
  lastCommandDate = ThingSpeak.getCreatedAt();
  lastCommandDate.trim();
  // --------------------------------------------

  // Se reinicia los valores de los voltajes registrados para poder realizar un nuevo análisis de promedio
  volt_values.clear();
}

// Esta función analiza el estado del botón y, en base a este, habilita o deshabilita la salida del Switch
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

// Esta función revisa el estado de la entrada de voltaje y, en caso de que sea mayor al límite de voltaje, se cierra la salida y, cuando vuelva a la normalidad, se vuelve a abrir en 5s después de que haya bajado el voltaje
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

// Esta función permite reiniciar el contador de manera efectiva sin que provoque bugs
void reset_time_counter(){
  timeCounter = 0;
  isTimeCounter = false;
}

// Esta función permite establecer el contador de la salida de forma segura
void set_time_counter(float time, int finalOutputResult){
  timeCounter = String(time).toDouble();
  finalOutputStatusTimeCounter = finalOutputResult;
  isTimeCounter = true;
}