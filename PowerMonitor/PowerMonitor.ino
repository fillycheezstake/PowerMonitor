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
#include "DHT.h"
#include <OneWire.h>



#define SSID  "AHLERS"      // change this to match your WiFi SSID
#define PASS  "MikeyAhlers"  // change this to match your WiFi password
#define PORT  "80"        // using port 8080 by default

#define cms_ip "192.168.0.44"
#define cms_push_freq 6000
#define CT_poll_speed 1000   //if disable webserver mode, you can decrease these both (although it takes a second or two to push the data)
#define CRAWL_TEMP_poll_speed 60000      // temp & humididity poll speed
#define HEAT_PUMP_TEMP_poll_speed 10000  // heat pump temp poll speed
#define cms_apikey "30b68fdbe74aef857d36db58d6cc195b"

OneWire  ds(2);  // OneWire temperature probes ar on pin 2



#define num_CTs 18        //12 is max number of CTs (hardware). Teensy 3.2 has 21 ADCs.


//#define Passthrough     //To enable direct passthrough from PC > ESP8266 : disable all other modes.
//#define SerialOut
//  #define WebServerMode
#define ReadCTs
#define PushData


#define DHTPIN 3     // DHT Temp/Humidity probe on D3

#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
// Initialize DHT sensor.
DHT dht(DHTPIN, DHTTYPE);

#define CondSumpPumpAStatePIN 4     // Condensate Sump Pump A State Pin
#define CondSumpPumpCStatePIN 5     // Condensate Sump Pump C State Pin
#define CrawlSpacePowerStatePIN 6   // Crawl Space Power State Pin

ESP esp8266;
EnergyMonitor CT[num_CTs];
unsigned long previousMillis = 0;
unsigned long previousPushMillis = 0;
unsigned long previousHeatPumpMillis = 0;
unsigned long previousCrawlMillis = 0;
String PageToServe;
float RealPower[num_CTs] = {0};
float ApparentPower[num_CTs] = {0};
float current[num_CTs] = {0};
float PowerFactor[num_CTs] = {0};
float voltage = 0;
float h=0;
float f=0;
float probe_1_temp, probe_2_temp;
int CondPumpAState = 0;
int CondPumpCState = 0;
int CrawlspacePowerState = 0;


String CTdescs[num_CTs] = {0};


void setup() {
    
    //Setup computer to Teensy serial
    Serial.begin(115200);
    //12-bit adc resolution
    analogReadResolution(12);
    
    //voltage calibration:
    //voltage(input_pin, volt_scaling_const, phase_shift)  
    //since all CTs use the same voltage source, we iterate them
    for (int i=0; i < num_CTs; i++) {
      CT[i].voltage(A0, 118, 1.7);
    }
    
    // current calibration:
    // current(input_pin, i_scaling_const)
    // Each CT calibrated with reference meter (current loop or KillAWatt) which provided "measured current"
    //    while running with "Reference Calibration" value which was based upon burden resistor (eg: 30, 10 etc)
    // Comment Format: Channel Name -- Reference Calibration (based on burden resistor) * measured current / indicated current with reference calibration value = new calibration value 
    CT[0].current(A1, 29.48);       // AC-A        30 * 12.04 / 12.25 = 29.48
    CT[1].current(A2, 29.14);       // AC-B        30 * 12.52 / 12.89 = 29.14
    CT[2].current(A3, 28.41);       // PoolPump    30 * 6.92 / 7.06 = 28.41
    CT[3].current(A4, 9.9);         // PoolLt      10 * 4.0 / 4.04 = 9.9
    CT[4].current(A5, 181.7);       // Mains2      233 * 64.99 / (83.65-0.33) = 181.7
    CT[5].current(A6, 30.11);       // Generator A 30 * 11.4 / 11.36 = 30.11
    CT[6].current(A7, 90.12);       // Heat A      106 * 64.99 / 76.44 = 90.12
    CT[7].current(A8, 30.00);       // Generator B 30 * 11.41 / 11.41 = 30.0
    CT[8].current(A9, 294.9);       // Barn A      300 * 12.10 / 12.31 = 294.9
    CT[9].current(A10, 296.6);      // Barn B      300 * 12.16 / 12.30 = 296.6
    CT[10].current(A11, 58.75);     // Heat B      71 * 40.88 / 49.40 = 58.75
    CT[11].current(A12, 42.71);     // Heat C      50 * 17.05 / 19.96 = 42.71
    CT[12].current(A13, 343.37);    // Mains 1     233 * 64.99 / (47.5-3.4) = 343.37
    CT[13].current(A14, 0.1);       // Abandon (was Mains 2, failed)
    CT[14].current(A15, 327.99);    // Mains 3     233 * 40.88 / (29.4-4.03) = 215.99 * 1.52 = 327.99
    CT[15].current(A16, 53.56);     // cooktop     50 *31.45 / 29.36 = 53.56
    CT[16].current(A17, 57.52);     // oven        50 * 22.55 / 19.6 = 57.52
    CT[17].current(A18, 324.13);     // Mains 4     233 * 40.88 / (30.10-3.35) = 356.1  256.1 * 1.27 = 324.13
        
    // CT1 = array CT[0]
    CTdescs[0] = "ACA";
    CTdescs[1] = "ACB";
    CTdescs[2] = "PoolPump";
    CTdescs[3] = "PoolLt";
    CTdescs[4] = "Mains2";
    CTdescs[5] = "GenA";
    CTdescs[6] = "HeatA";
    CTdescs[7] = "GenB";    
    CTdescs[8] = "BarnA";
    CTdescs[9] = "BarnB";
    CTdescs[10] = "HeatB";
    CTdescs[11] = "HeatC";
    CTdescs[12] = "Mains1";
    CTdescs[13] = "Abandon";
    CTdescs[14] = "Mains3";
    CTdescs[15] = "CookTp";
    CTdescs[16] = "Oven";
    CTdescs[17] = "Mains4";

    dht.begin();

    delay(5000);  //wait for Teensy to come up
    //SSID, PASS, Port Number
    esp8266.setupWiFi(SSID,PASS,80);

    pinMode(2, INPUT_PULLUP);                         //Tell Teensy to turn on a pullup resistor on the temp probe pin
    pinMode(3, INPUT_PULLUP);                         //Tell Teensy to turn on a pullup resistor on the DHT temp/humidy pin also
    pinMode(CondSumpPumpAStatePIN, INPUT_PULLUP);     //Tell Teensy to turn on a pullup resistor on the condensate A Pump State Pin
    pinMode(CondSumpPumpCStatePIN, INPUT_PULLUP);     //Tell Teensy to turn on a pullup resistor on the condensate C Pump State Pin
    pinMode(CrawlSpacePowerStatePIN, INPUT_PULLUP);   //Crawlspace Power State Pin is digital input

}

void loop() {   
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte probeaddr1[8] = {0x10, 0x5B, 0x6E, 0xD, 0x3, 0x8, 0x0, 0x91} ;
  byte probeaddr2[8] = {0x10, 0x27, 0xC8, 0xD, 0x3, 0x8, 0x0, 0xDB} ;
  
  float celsius;
  int16_t raw;
   
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

    if (currentMillis - previousCrawlMillis >  CRAWL_TEMP_poll_speed) {
      // Read the current humidity
      h = dht.readHumidity();
      // Read temperature as Fahrenheit (isFahrenheit = true)
      f = dht.readTemperature(true);
      previousCrawlMillis = millis();

      // Get the state of the condensate pumps and crawlspace power
      CondPumpAState = digitalRead(CondSumpPumpAStatePIN);
      CondPumpCState = digitalRead(CondSumpPumpCStatePIN);
      CrawlspacePowerState = digitalRead(CrawlSpacePowerStatePIN);
    }

    if (currentMillis - previousHeatPumpMillis >  HEAT_PUMP_TEMP_poll_speed) {
      // the first ROM byte indicates which chip
      type_s = 1;

      ds.reset();
      ds.select(probeaddr1);
      ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
      delay(1000);     // maybe 750ms is enough, maybe not
      // we might do a ds.depower() here, but the reset will take care of it.
  
      present = ds.reset();
      ds.select(probeaddr1);    
      ds.write(0xBE);         // Read Scratchpad

      for ( i = 0; i < 9; i++) {           // we need 9 bytes
        data[i] = ds.read();
      }

      // Convert the data to actual temperature
      // because the result is a 16 bit signed integer, it should
      // be stored to an "int16_t" type, which is always 16 bits
      // even when compiled on a 32 bit processor.
      raw = (data[1] << 8) | data[0];
      if (type_s) {
        raw = raw << 3; // 9 bit resolution default
        if (data[7] == 0x10) {
          // "count remain" gives full 12 bit resolution
          raw = (raw & 0xFFF0) + 12 - data[6];
        }
      } else {
        byte cfg = (data[4] & 0x60);
        // at lower res, the low bits are undefined, so let's zero them
        if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
        else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
        else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
        //// default is 12 bit resolution, 750 ms conversion time
      }
      celsius = (float)raw / 16.0;
      probe_1_temp = celsius * 1.8 + 32.0;

      ds.reset();
      ds.select(probeaddr2);
      ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
      delay(1000);     // maybe 750ms is enough, maybe not
      // we might do a ds.depower() here, but the reset will take care of it.
  
      present = ds.reset();
      ds.select(probeaddr2);    
      ds.write(0xBE);         // Read Scratchpad

      for ( i = 0; i < 9; i++) {           // we need 9 bytes
        data[i] = ds.read();
      }

      // Convert the data to actual temperature
      // because the result is a 16 bit signed integer, it should
      // be stored to an "int16_t" type, which is always 16 bits
      // even when compiled on a 32 bit processor.
      raw = (data[1] << 8) | data[0];
      if (type_s) {
        raw = raw << 3; // 9 bit resolution default
        if (data[7] == 0x10) {
          // "count remain" gives full 12 bit resolution
          raw = (raw & 0xFFF0) + 12 - data[6];
        }
      } else {
        byte cfg = (data[4] & 0x60);
        // at lower res, the low bits are undefined, so let's zero them
        if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
        else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
        else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
        //// default is 12 bit resolution, 750 ms conversion time
      }
      celsius = (float)raw / 16.0;
      probe_2_temp = celsius * 1.8 + 32.0;

      previousHeatPumpMillis = millis();
    }
    
    #endif
    
    
    #ifdef PushData
    //every cms_push_freq seconds, push data to emoncms
    if (currentMillis - previousPushMillis > cms_push_freq) {
      esp8266.sendHTTPRequest(cms_ip,makeHTTPGet());
      previousPushMillis = millis();
    }   
    
    #endif


    #ifdef SerialOut 
    CT[0].calcVI(20,1000);
    CT[0].serialprint();
    #endif
}






String makeHTTPGet(){
   String GetReq;
  
   GetReq =  "GET /emoncms/input/post.json?node=2&apikey=";
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

  // add the temp & humidy keys
  CTjson["CrawlspaceTemp"] = f;
  CTjson["CrawlspaceHumid"] = h;
  CTjson["HeatpumpInletTemp"] = probe_1_temp;
  CTjson["HeatpumpOutletTemp"] = probe_2_temp;

  // add the condensate pump states & crawlspace power state
  CTjson["CondPumpAState"] = CondPumpAState;
  CTjson["CondPumpCState"] = CondPumpCState;
  CTjson["CrawlspacePowerState"] = CrawlspacePowerState;

  char json_content[2048];
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
