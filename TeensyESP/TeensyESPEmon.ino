//
//This uses a Teensy 3.2 and ESP8266 with AT firmware
//Modified Emonlib - https://github.com/openenergymonitor/EmonLib
//With help from the PJRC forums - https://forum.pjrc.com/threads/31973-Teensy-3-2-as-a-WiFi-webserver
//
//This takes data from various CTs and voltage waveform sources to get accurate voltage, power factor, real power, apparent power
//Data can be viewed as a webpage directly from the Teensy/ESP8266 combo (it'll serve you a webpage)
//Data is also available in json format upon request - use the url /json
//Data is pushed in a json format compatible with Emoncms - https://emoncms.org/  to an Emoncms server of your choice (a private one or the public one)
//
//
//     Tested with Arduino 1.68 and Teensyduino 1.28
//     Tested with ESP8266 AT Firmware - v 1.6


#include "TeensyESP.h"
#include "EmonLib.h"
#include <ArduinoJson.h>


#define SSID  "SSID"      // change this to match your WiFi SSID
#define PASS  "PASSWORD"  // change this to match your WiFi password
#define PORT  "80"        // using port 8080 by default

#define cms_ip "IP_OR_URL"

//#define Passthrough
//#define SerialOut
#define WebServerMode
#define ReadCTs
#define PushData

//12 is max number of CTs (hardware)
#define num_CTs 11


ESP esp8266;
EnergyMonitor CT[num_CTs];
unsigned long previousMillis = 0;
String PageToServe;
float RealPower[num_CTs] = {0};
float ApparentPower[num_CTs] = {0};
float current[num_CTs] = {0};
float PowerFactor[num_CTs] = {0};
float voltage = 0;

//this stores the names of the CTs
String CTdescs[12] = {0};






void setup() {
    
    //Setup computer to Teensy serial
    Serial.begin(115200);
    //12-bit adc resoluiton
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
    CT[0].current(1, 15);
    CT[1].current(2, 15);
    CT[2].current(3, 15);
    CT[3].current(4, 30);    
    CT[4].current(5, 30);
    CT[5].current(6, 30);
    CT[6].current(7, 30);
    CT[7].current(8, 10);
    CT[8].current(9, 30);
    CT[9].current(10, 30);
    CT[10].current(11, 30);
    CT[11].current(12, 15);         
    
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

        
    //#ifdef WebServerMode
    delay(5000);  //wait for Teensy to come up
    esp8266.setupWiFi();
    //#endif
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
          esp8266.SendContent(header_gen(),homepage_content_gen());
        }
        else if (PageToServe == "powerjson") {
          Serial.println("----json data served----");
          esp8266.SendContent(power_json_header(),json_gen());
        }
        else if (PageToServe == "favicon.ico"){
          Serial.println("----Favicon Served----");
          esp8266.SendContent(PageNotFoundHeader(),PNF_content_gen());
        }
        else {
          Serial.println("----404 Served----");
          esp8266.SendContent(PageNotFoundHeader(),PNF_content_gen());          
        }   
      }
    #endif

    unsigned long currentMillis = millis();

    #ifdef ReadCTs  
    //loop every num_CTs * .1 secs (for six, .6 seconds, for twelve, 1.2 seconds)
    //just to allow waiting for HTTP clients, and to let the emonlib find zero faster
    //reading the CTs is code blocking - nothing else happens during read
    if (currentMillis - previousMillis > num_CTs * 100) {
      //get the data - calcVI(num_crosses, timeout)
      //since this doesn't include any unique values--thats in setup(), we can iterate them as they're all the same
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
    //every six seconds, push data to emoncms
    if (currentMillis - previousMillis > 6000) {
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
   //  this request looks like:  http://192.168.1.34/emoncms/input/post.json?node=1&json={power:200}&apikey=30b68fdbe74aef857d36db58d6cc195b
   String GetReq;
   
   GetReq =  "GET /emoncms/input/post.json?node=1&apikey=30b68fdbe74aef857d36db58d6cc195b&json=";
   //now appending json of all data
   GetReq += json_gen_forcms();
   GetReq += " HTTP/1.1\r\n"; 
   GetReq += "Host:" ;
   GetReq += cms_ip;
   GetReq += "\r\n\r\n";
   
   return GetReq;      
}


String PageNotFoundHeader() {
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

String power_json_header() {
  String header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n";
  header += "Content-Length:";
  header += (int)(json_gen().length());
  header += "\r\n\r\n";
  return(header);
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




String json_gen() {

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

  CTjson.printTo(Serial);
  Serial.println();
  
  char json_content[512];
  CTjson.printTo(json_content,sizeof(json_content));
  
  //json_content =  "{\"CT1\": {\"Desc\": \"WORK\",\"RealP\": 100.2,\"AppP\": 200.2,\"I\": 22.3,\"PF\": 0.59},  \"CT2\": {\"Desc\": \"TIME\",\"RealP\": 200.2,\"AppP\": 100.2,\"I\": 12.3,\"PF\": 0.9} }";
 
  return(json_content);
}


String header_gen() {
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
