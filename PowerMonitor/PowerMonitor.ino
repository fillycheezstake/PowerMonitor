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
//     Tested with Arduino 1.68 and Teensyduino 1.28
//     Tested with ESP8266 AT Firmware - v 1.6


#include "ESP.h"
#include "EmonLib.h"
#include <ArduinoJson.h>


#define SSID  "SSID"      // change this to match your WiFi SSID
#define PASS  "PASSWORD"  // change this to match your WiFi password
#define PORT  "80"        // using port 8080 by default

#define cms_ip "IP_OR_URL"
#define cms_push_freq 6000
#define CT_poll_speed 1000   //if disable webserver mode, you can decrease these both (although it takes a second or two to push the data)
#define cms_apikey "30b68fdbe74aef857d36db58d6cc195b"


#define num_CTs 11        //12 is max number of CTs (hardware). Teensy 3.2 has 21 ADCs.


//#define Passthrough     //To enable direct passthrough from PC > ESP8266 : disable all other modes.
//#define SerialOut
#define WebServerMode
#define ReadCTs
#define PushData




ESP esp8266;
EnergyMonitor CT[num_CTs];
unsigned long previousMillis = 0;
String PageToServe;
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
    //expriemental data found 30 works good for the 30A 1V output CTs
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
    CT[12].current(A13, 30);
    CT[13].current(A14, 30);
    CT[14].current(A15, 30);
    CT[15].current(A16, 15); 
    
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
    CTdescs[9] = "MicOv";
    CTdescs[10] = "BarO";
    CTdescs[11] = "SETME";

    delay(5000);  //wait for Teensy to come up
    //SSID, PASS, Port Number
    esp8266.setupWiFi(SSID,PASS,80);
}

void loop() {    
    #ifdef Passthrough
        // Send bytes from ESP8266 -> Teensy -> Computer
        if ( Serial1.available() ) {
            Serial.write( Serial1.read() );
        }
        // Send bytes from Computer -> Teensy -> ESP8266
        if ( Serial.available() ) {
            Serial1.write( Serial.read() );
        }
    #endif

    #ifdef WebServerMode
    
     PageToServe = esp8266.ListenForClients();
      
      if (PageToServe != "0") {
        //Serial.print("---PageToServe:   ");
        if (PageToServe == "/") {
          Serial.println("----Homepage Served----");
          esp8266.SendContent(homepage_header_gen(),homepage_content_gen());
        }
        else if (PageToServe == "powerjson") {
          Serial.println("----json data served----");
          esp8266.SendContent(json_out_header_gen(),json_out_gen());
        }
        else if (PageToServe == "favicon.ico"){
          Serial.println("----Favicon Served----");
          esp8266.SendContent(PNF_header_gen(),PNF_content_gen());
        }
        else {
          Serial.println("----404 Served----");
          esp8266.SendContent(PNF_header_gen(),PNF_content_gen());          
        }   
      }
    #endif

    unsigned long currentMillis = millis();

    #ifdef ReadCTs  
    //loop every num_CTs * .1 secs
    //just to allow waiting for HTTP clients
    //reading the CTs is code blocking - nothing else happens during read
    if (currentMillis - previousMillis >  CT_poll_speed) {
      //get the data - calcVI(num_crosses, timeout)
      for (int i=0; i < num_CTs; i++) {
        //loops through all CTs, saving their values in corresponding arrays
        CT[i].calcVI(20,1000);
        RealPower[i] = CT[i].realPower;
        ApparentPower[i] = CT[i].apparentPower;
        current[i] = CT[i].Irms;
        PowerFactor[i] = CT[i].powerFactor;
      }
      //just use voltage from CT1, as the voltage is the same for all (or should be)
      voltage = CT[0].Vrms;
      previousMillis = millis();
    }
    #endif
    
    
    #ifdef PushData
    //every cms_push_freq seconds, push data to emoncms
    if (currentMillis - previousMillis > cms_push_freq) {
      esp8266.sendHTTPRequest(cms_ip,makeHTTPGet());
      previousMillis = millis();
    }   
    #endif


    #ifdef SerialOut 
    CT[0].calcVI(20,1000);
    CT[0].serialprint();
    #endif
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


String PNF_header_gen() {
  String header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n";
  header += "Content-Length:";
  header += (int)(PNF_content_gen().length());
  header += "\r\n\r\n";
  return(header);
}

String PNF_content_gen() {
  String content="";
    content += "<!DOCTYPE html>";
    content += "<html>";
    content += "<body>";

    content += "<h1> 404, Page Not Found </h1>";

    content += " <p> Teensy server uptime ";
    content += "<font color=#0000FF> ";
    content += String(millis()/1000); 
    content += " seconds </font> </p>";
    
    content += "</body>";
    content += "</html>";
    content += "<br />\n";       
    content += "\r\n";

    return(content);
}

String json_out_header_gen() {
  String header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n";
  header += "Content-Length:";
  header += (int)(json_out_gen().length());
  header += "\r\n\r\n";
  return(header);
}


String json_out_gen() {

  StaticJsonBuffer<1500> jsonBuffer;
  JsonObject& CTjson = jsonBuffer.createObject();

  CTjson["voltage"] = voltage;

  for (int i = 0; i < num_CTs; i++) { 
    String z = "CT";
    z += (i+1);
    
    JsonObject& nestedObject = CTjson.createNestedObject(z);
    
    nestedObject["Desc"] = CTdescs[i];
    nestedObject["RealP"] = RealPower[i];
    nestedObject["AppP"] = ApparentPower[i];
    nestedObject["PF"] = PowerFactor[i];
    nestedObject["I"] = current[i];
  }
  
  char json_content[1024];
  CTjson.printTo(json_content,sizeof(json_content));
  
  //json_content =  "{\"CT1\": {\"Desc\": \"WORK\",\"RealP\": 100.2,\"AppP\": 200.2,\"I\": 22.3,\"PF\": 0.59},  \"CT2\": {\"Desc\": \"TIME\",\"RealP\": 200.2,\"AppP\": 100.2,\"I\": 12.3,\"PF\": 0.9} }";
 
  return(json_content);
}


String homepage_header_gen() {
  String header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
  header += "Content-Length:";
  header += (int)(homepage_content_gen().length());
  header += "\r\n\r\n";
  return(header);
}

String homepage_content_gen() {
  String content="";
  
    content += "<!DOCTYPE html>";
    content += "<html>";
    content += "<body>";
    
    content += " <h1> Energy Monitor Page </h1> <br/>  ";
       
    content += "<table style=""width:50%"">";
    
    content += "<tr>";
    content += "<td>Channel</td>";
    content += "<td>Description</td>" ;
    content += "<td>Real Power</td>" ;
    content += "<td>Apparent Power</td>";
    content += "<td>Power Factor</td>";
    content += "<td>Voltage</td>";
    content += "<td>Current</td>";
    content += "</tr>";
    
    for (int i=0; i < num_CTs; i++) {
      content += "<tr>";
      content += "<td>";
      content += i+1;
      content += "</td> ";
      content += "<td>";
      content += CTdescs[i];
      content += "</td> ";
      content += "<td>";
      content += RealPower[i];
      content += "</td> ";
      content += "<td>";
      content += ApparentPower[i];
      content += "</td> ";
      content += "<td>";
      content += PowerFactor[i];
      content += "</td> ";
      content += "<td>";
      content += voltage;
      content += "</td> ";
      content += "<td>";
      content += current[i];
      content += "</td> ";
      content += "</tr>";
    }
    
    
    content += "</table>";
   
    
    content += " <p> Teensy server uptime ";
    
    content += "<font color=#0000FF> ";
    content += String(millis()/1000); 
    content += " seconds </font> </p>";
    
      
    content += "</body>";
    content += "</html>";
    content += "<br />\n";       
    content += "\r\n";

    return(content);
}
