//
//This uses a Teensy 3.2 and ESP8266 with AT firmware
//Uses a Modified Emonlib - https://github.com/openenergymonitor/EmonLib
//With help from the PJRC forums - https://forum.pjrc.com/threads/31973-Teensy-3-2-as-a-WiFi-webserver
//
//This sketch takes data from multiple CTs and voltage waveform sources to get accurate voltage, power factor, real power, apparent power
//Data can be viewed as a webpage directly from the Teensy/ESP8266 combo (it'll serve you a webpage)
//Data is also available in json format upon request - use the url /powerjson
//Data is pushed in a json format compatible with Emoncms - https://emoncms.org/  to an Emoncms server of your choice (a private one or the public one)
//
//
//     Tested with Arduino 1.8.7 and Teensyduino 1.44
//     Compatible with ESP8266 AT Firmware - AT version:1.6.2.0(Apr 13 2018 11:10:59) SDK version:2.2.1(6ab97e9)



#include "ESP.h"
#include "EmonLib.h"
#include <ArduinoJson.h>


#define SSID  "SSID"      // change this to match your WiFi SSID
#define PASS  "WifiPassword"  // change this to match your WiFi password
#define HOSTNAME "Hostname" //set the hostname of the ESP8266

#define cms_ip "CMS_IP"
#define cms_push_freq 4000
#define CT_poll_speed 1000
#define WiFi_check_freq 300000
#define cms_apikey "APIKEY_HERE"


#define num_CTs 12        //12 is max number of CTs (hardware). Teensy 3.2 has 21 ADCs.

ESP esp8266;
EnergyMonitor CT[num_CTs];
unsigned long previousCTMillis = 0;
unsigned long previousPushMillis = 0;
unsigned long previousWiFiCheckMillis = 0;
float RealPower[num_CTs] = {0};
float ApparentPower[num_CTs] = {0};
float current[num_CTs] = {0};
float PowerFactor[num_CTs] = {0};
float voltage = 0;

String CTdescs[12] = {0};


void setup() {
    
    //Setup computer to Teensy serial
    Serial.begin(115200);
    //12-bit adc resolution
    analogReadResolution(12);
    
    //voltage calibration:
    //voltage(input_pin, volt_scaling_const, phase_shift)  
    //since all CTs use the same voltage source, we iterate them
    for (int i=0; i < num_CTs; i++) {
      CT[i].voltage(13, 124, 1.7);
    }
    
    //current calibration:
    //current(input_pin, i_scaling_const)
    //experimental data found 30 works good for the 30A 1V output CTs
    //divided by 2 to use 15A CTs, by 3 for 10A CTs
    CT[0].current(A1, 15);
    CT[1].current(A2, 15);
    CT[2].current(A3, 15);
    CT[3].current(A4, 30);    
    CT[4].current(A5, 30);
    CT[5].current(A6, 30);
    CT[6].current(A7, 30);
    CT[7].current(A8, 10);
    CT[8].current(A9, 30);
    CT[9].current(A10, 30);
    CT[10].current(A11, 30);
    CT[11].current(A12, 15);         
    
    CTdescs[0] = "OutF";
    CTdescs[1] = "WtrP";
    CTdescs[2] = "Bonus";
    CTdescs[3] = "WtrH";
    CTdescs[4] = "Kitc";
    CTdescs[5] = "Dryr";
    CTdescs[6] = "Garage";
    CTdescs[7] = "KitcLt";    
    CTdescs[8] = "Kitc2";
    CTdescs[9] = "MicOv";
    CTdescs[10] = "BarO";
    CTdescs[11] = "SETME";

    Serial.println("Booting... waiting for ESP8266 WiFi");
    delay(7000);  //wait for Teensy to come up (takes about 7 seconds to boot and get connected to wifi)
    //SSID, PASS
    esp8266.setupWiFi(SSID,PASS,HOSTNAME);
}

void loop() {    

    unsigned long currentMillis = millis();

    if (currentMillis - previousCTMillis >  CT_poll_speed) {
      //get the data - calcVI(num_crosses, timeout)
      //to find each zero crossing is the 1/2 the period  - so for US, 60hz: (1/60) / 2 = .00833 seconds per cross. A 20 cross sample will take .1666 seconds
      //so if you use 20 zero crossings, 12 CTs will take ~2 seconds.
      for (int i=0; i < num_CTs; i++) {
        //loops through all CTs, saving their values in corresponding arrays
        CT[i].calcVI(20,1000);
        RealPower[i] = CT[i].realPower;
        ApparentPower[i] = CT[i].apparentPower;
        current[i] = CT[i].Irms;
        PowerFactor[i] = CT[i].powerFactor;
      }
      //we just use the voltage from CT1, as the voltage is the same for all channels (or should be close enough)
      voltage = CT[0].Vrms;
      previousCTMillis = millis();
    }
    
    
    if (currentMillis - previousPushMillis > cms_push_freq) {
      esp8266.sendHTTPRequest(cms_ip,makeHTTPGet());
      previousPushMillis = millis();
    }   

    if (currentMillis - previousWiFiCheckMillis > WiFi_check_freq) {
      esp8266.reconnectWiFi(SSID,PASS);
      previousWiFiCheckMillis = millis();
    }   
}


String makeHTTPGet(){
   String GetReq;
  
   GetReq =  "GET /emoncms/input/post.json?node=1&apikey=";
   GetReq += cms_apikey;
   GetReq += "&json=";
   GetReq += json_gen_forcms();
   GetReq += " HTTP/1.1\r\n"; 
   GetReq += "Host:" ;
   GetReq += cms_ip;
   GetReq += "\r\n\r\n";
   
   return GetReq;      
}


String json_gen_forcms() {
 
  StaticJsonBuffer<2500> jsonBuffer;
  JsonObject& CTjson = jsonBuffer.createObject();
  
  CTjson["voltage"] = voltage;
  
  for (int i = 0; i < num_CTs; i++) { 
  
    String key = CTdescs[i] + "_Rp";  
    CTjson[key] = RealPower[i];
    key = CTdescs[i] + "_Ap"; 
    CTjson[key] = ApparentPower[i];
    key = CTdescs[i] + "_Pf"; 
    CTjson[key] = PowerFactor[i];
    key = CTdescs[i] + "_i"; 
    CTjson[key] = current[i];
  }

  char json_content[1024];
  CTjson.printTo(json_content,sizeof(json_content));
  Serial.println(json_content);
  return(json_content);
}
