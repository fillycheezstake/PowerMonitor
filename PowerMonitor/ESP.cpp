

#include "ESP.h"


ESP::ESP() {
  
    // Setup Teensy to ESP8266 serial
    // Use baud rate 115200 during firmware update
    Serial1.begin(115200);

}

void ESP::setupWiFi(String Name, String Passw, String newhostname) {
  
  //Note: this assumes the ESP has been set up by turning on the CWAUTOCONN, saving the SSID/PASS in persistent memory, and setting it to station mode.
  
  //this char array is used for waiting for the ESP's responses ie, "OK"
  char OKrn[] = "OK\r\n";
//  char Conrn[] = "WIFI GOT IP\r\n";
  
  //turn on echo
  Serial1.println("ATE1");
  wait_for_esp_response(1000,OKrn);

  //print version info
  Serial1.println("AT+GMR");
  wait_for_esp_response(1000,OKrn);
  
  //for some reason the hostname doesn't save in persistent memory on the ESP, so we set it here
  Serial1.print("AT+CWHOSTNAME=\"");
  Serial1.print(newhostname);
  Serial1.println("\"");
  wait_for_esp_response(1000,OKrn);

  //print IP Addr
  Serial1.println("AT+CIFSR");
  wait_for_esp_response(1000,OKrn);
  
  
  Serial.println("---------------*****##### ESP READY #####*****---------------");
  
}

void ESP::reconnectWiFi(String Name, String Passw){
  char OKrn[] = "OK\r\n";    
    
  //join AP
  Serial1.print("AT+CWJAP_CUR=\"");
  Serial1.print(Name);
  Serial1.print("\",\"");
  Serial1.print(Passw);
  Serial1.println("\"");
  wait_for_esp_response(9000,OKrn);
}


void ESP::sendHTTPRequest(String IP, String data) {
  //this generates a command that looks like:
  //AT+CIPSTART=<link ID>,<type>, <remote IP>, <remote port>[, <TCP keep alive>]
  //When in single connection mode, leave out <link ID>
  //then you wait for "OK", then use command AT+CIPSEND. Then wait for ">" and send data. then "OK", then wait for response & connection closed.
  //waiting for connection close makes sure you don't leave it open
  
  char OKrn[] = "OK\r\n";
  char brn[] = ">";
  char json[] = "json";
  char clsd[] = "CLOSED";
  Serial1.print("AT+CIPSTART=\"TCP\",\"");
  Serial1.print(IP);
  Serial1.println("\",80");
  wait_for_esp_response(5000,OKrn);

  Serial1.print("AT+CIPSEND=");
  Serial1.println(data.length());
  if(wait_for_esp_response(5000,brn)) {
    Serial1.print(data);
    wait_for_esp_response(5000,OKrn);
    wait_for_esp_response(5000,json);
    wait_for_esp_response(8000,clsd);
  } 
  else {
    Serial1.print("AT+CIPCLOSE");
    wait_for_esp_response(1000,OKrn);
 }


}

byte ESP::wait_for_esp_response(int timeout, char* term) {
  //this prints the esp response after waiting for the specified char
  unsigned long t=millis();
  bool found=false;
  int i=0;
  int len=strlen(term);
  // wait for at most timeout milliseconds
  // or if OK\r\n is found
  while(millis()<t+timeout) {
    if(Serial1.available()) {
      buffer[i++]=Serial1.read();
      if(i>=len) {
        if(strncmp(buffer+i-len, term, len)==0) {
          found=true;
          break;
        }
      }
    }
  }
  buffer[i]=0;
  //Serial.println("---wait---");
  Serial.print(buffer);
  //Serial.println("---waitend---");
  return found;
}
