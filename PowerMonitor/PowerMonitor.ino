//
//This uses a Teensy 3.2 and ESP8266 with AT firmware
//Uses a Modified Emonlib - https://github.com/openenergymonitor/EmonLib
//With help from the PJRC forums - https://forum.pjrc.com/threads/31973-Teensy-3-2-as-a-WiFi-webserver
//
//This sketch takes data from multiple CTs and voltage waveform sources to get accurate voltage, power factor, real power, apparent power
//Data is pushed in a json format compatible with Emoncms - https://emoncms.org/  to an Emoncms server of your choice (a private one or the public one)
//
//
//     Tested with Arduino 1.8.7 and Teensyduino 1.44
//     Tested with ESP8266 AT Firmware - v 1.7


#include "ESP.h"
#include "EmonLib.h"
#include <ArduinoJson.h>
#include "DHT.h"
#include <OneWire.h>
#include <DallasTemperature.h>

#define SSID  "SSID"      // change this to match your WiFi SSID
#define PASS  "PASS"  // change this to match your WiFi password

#define cms_ip "CMS IP"
#define cms_apikey "APIKEY"
#define cms_push_freq 6000

#define CT_poll_speed 1000
#define CRAWL_TEMP_poll_speed 60000      // temp & humididity poll speed
#define HEAT_PUMP_TEMP_poll_speed 10000  // heat pump temp poll speed

#define ONE_WIRE_BUS 2
#define TEMPERATURE_PRECISION 9
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress probe1 = {0x10, 0x5B, 0x6E, 0xD, 0x3, 0x8, 0x0, 0x91} ;
DeviceAddress probe2 = {0x10, 0x27, 0xC8, 0xD, 0x3, 0x8, 0x0, 0xDB} ;

#define DHTPIN 3     // DHT Temp/Humidity probe on D3
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE); // Initialize DHT sensor.

#define num_CTs 18        //Teensy 3.2 has 21 ADC pins. One must be used for a voltage source, so a maximum of 20.

#define CondSumpPumpAStatePIN 4     // Condensate Sump Pump A State Pin
#define CondSumpPumpCStatePIN 5     // Condensate Sump Pump C State Pin
#define CrawlSpacePowerStatePIN 6   // Crawl Space Power State Pin



ESP esp8266;
EnergyMonitor CT[num_CTs];
unsigned long previousMillis = 0;
unsigned long previousPushMillis = 0;
unsigned long previousHeatPumpMillis = 0;
unsigned long previousCrawlMillis = 0;
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
    
    
    Serial.begin(115200); //Setup computer to Teensy serial
    
    analogReadResolution(12);  //12-bit adc resolution
    
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
    CT[17].current(A18, 324.13);    // Mains 4     233 * 40.88 / (30.10-3.35) = 356.1  256.1 * 1.27 = 324.13
       
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

    delay(2000);  //wait for Teensy to come up
    
    esp8266.setupWiFi(SSID,PASS);  //SSID, PASSWORD

    sensors.begin();
    dht.begin();
    
    pinMode(2, INPUT_PULLUP);                         //Tell Teensy to turn on a pullup resistor on the temp probe pin
    pinMode(3, INPUT_PULLUP);                         //Tell Teensy to turn on a pullup resistor on the DHT temp/humidy pin also
    pinMode(CondSumpPumpAStatePIN, INPUT_PULLUP);     //Tell Teensy to turn on a pullup resistor on the condensate A Pump State Pin
    pinMode(CondSumpPumpCStatePIN, INPUT_PULLUP);     //Tell Teensy to turn on a pullup resistor on the condensate C Pump State Pin
    pinMode(CrawlSpacePowerStatePIN, INPUT_PULLUP);   //Crawlspace Power State Pin is digital input

}

void loop() {   
   
    unsigned long currentMillis = millis();

    
    //loop at CT_poll_speed
    if (currentMillis - previousMillis >  CT_poll_speed) {
      
      for (int i=0; i < num_CTs; i++) {  //loop through all CTs
        CT[i].calcVI(20,1000);  //get the data - calcVI(num_crosses, timeout)
        RealPower[i] = CT[i].realPower;
        ApparentPower[i] = CT[i].apparentPower;
        current[i] = CT[i].Irms;
        PowerFactor[i] = CT[i].powerFactor;
      }
      
      voltage = CT[0].Vrms;  //we only store the voltage from the first CT, as the voltage is the same for all (or should be)
      
      previousMillis = millis();
    }

    
    
    
    
    if (currentMillis - previousCrawlMillis >  CRAWL_TEMP_poll_speed) {
      
      h = dht.readHumidity();
      f = dht.readTemperature(true);   // Read temperature as Fahrenheit (isFahrenheit = true)
      
      // Get the state of the condensate pumps and crawlspace power
      CondPumpAState = digitalRead(CondSumpPumpAStatePIN);
      CondPumpCState = digitalRead(CondSumpPumpCStatePIN);
      CrawlspacePowerState = digitalRead(CrawlSpacePowerStatePIN);
      
      previousCrawlMillis = millis();
    }

    
    
    
    
    
    if (currentMillis - previousHeatPumpMillis >  HEAT_PUMP_TEMP_poll_speed) {

      probe_1_temp = sensors.getTempF(probe1);
      probe_2_temp = sensors.getTempF(probe2);

      previousHeatPumpMillis = millis();
    }
    
    
    
    
    
    
    
    //every cms_push_freq seconds, push data
    if (currentMillis - previousPushMillis > cms_push_freq) {
      
      esp8266.sendHTTPRequest(cms_ip,makeHTTPGet());
      
      previousPushMillis = millis();
    }   
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
