

#include "ESP.h"


ESP::ESP() {
  
    // Setup Teensy to ESP8266 serial
    // Use baud rate 115200 during firmware update
    Serial1.begin(115200);

}

void ESP::setupWiFi(String Name, String Passw, int pt) {
  //this char is used for waiting for the ESP's responses ie, OK
  char OKrn[] = "OK\r\n";
  
  //turn on echo
  Serial1.println("ATE1");
  wait_for_esp_response(1000,OKrn);

  //show version info
  Serial1.println("AT+GMR");
  wait_for_esp_response(1000,OKrn);
  
  Serial1.println("AT+CWMODE_CUR=1");
  wait_for_esp_response(1000,OKrn);   
  
  // set mode 1 (station mode), and set to get ip address via DHCP
  Serial1.println("AT+CWDHCP_CUR=1,1");
  wait_for_esp_response(1000,OKrn); 

  //join AP
  Serial1.print("AT+CWJAP_CUR=\"");
  Serial1.print(Name);
  Serial1.print("\",\""); 
  Serial1.print(Passw);
  Serial1.println("\"");
  wait_for_esp_response(9000,OKrn);

  //Set multiple connections multiple
  Serial1.println("AT+CIPMUX=1");
  wait_for_esp_response(1000,OKrn);
  
  #ifdef WebServerMode
  
  //Create TCP Server that listens on specified port
  Serial1.print("AT+CIPSERVER=1,"); 
  Serial1.println(pt);
  wait_for_esp_response(1000,OKrn);

  //set TCP server timeout to xx seconds (up to 7200)
  Serial1.println("AT+CIPSTO=30");  
  wait_for_esp_response(1000,OKrn);

  //don't show the remote IP and port upon a HTTP GET request (makes parsing easier)
  Serial1.println("AT+CIPDINFO=0");  
  wait_for_esp_response(1000,OKrn);
  
  #else
  //Turn off TCP Server (if on)
  Serial1.println("AT+CIPSERVER=0"); 
  wait_for_esp_response(2000,OKrn);
  
  #endif

  //shows IP Addr
  Serial1.println("AT+CIFSR");
  wait_for_esp_response(5000,OKrn);
  
  
  Serial.println("---------------*****##### READY TO SERVE #####*****---------------");
  
}


void ESP::sendHTTPRequest(String IP, String data) {
  //this generates a command that looks like:
  //AT+CIPSTART=<link ID>,<type>, <remote IP>, <remote port>[, <TCP keep alive>]
  //then you wait for "OK", then use command AT+CIPSEND. Then wait for ">" and send data. then "OK", then wait for response & connection closed.
  //waiting for connection close makes sure you don't leave it open
  char OKrn[] = "OK\r\n";
  char brn[] = ">";
  char json[] = "json";
  char clsd[] = "CLOSED";
  Serial1.print("AT+CIPSTART=4,\"TCP\",\"");
  Serial1.print(IP);
  Serial1.println("\",80");
  wait_for_esp_response(5000,OKrn);

  Serial1.print("AT+CIPSEND=");
  Serial1.print(4);
  Serial1.print(",");
  Serial1.println(data.length());
  if(wait_for_esp_response(5000,brn)) {
    Serial1.print(data);
    wait_for_esp_response(5000,OKrn);
    wait_for_esp_response(5000,json);
    wait_for_esp_response(8000,clsd);
  } 
  else {
    Serial1.print("AT+CIPCLOSE=");
    Serial1.println(4);
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


bool ESP::read_till_eol() {
  //prints and reads each line till EOL,
  static int i=0;
  if(Serial1.available()) {
    buffer[i++]=Serial1.read();
    if(i==BUFFER_SIZE)  i=0;
    if(i>1 && buffer[i-2]==13 && buffer[i-1]==10) {
      buffer[i]=0;
      i=0;
      //Serial.println("---eol---");
      Serial.print(buffer);
      //Serial.println("---eolend---");
      
      return true;
    }
  }
  return false;
}

String ESP::ListenForClients() {
  char OKrn[] = "OK\r\n";
  /*
    Format:
    +IPD,<ID>,<len>[,<remote IP>,<remote port>]:<data>
    This data comes from the ESP upon a HTTP GET request, so this code looks for it. If you have show remote IP and port enabled, it will look like the second option.
    Only the first line is stored in the buffer from read_till_eol()
   
    +IPD,0,379:GET / HTTP/1.1
    Host: 192.168.1.47
    Connection: keep-alive
    Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,;q=0.8
    Upgrade-Insecure-Requests: 1
    User-Agent: Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/49.0.2623.110 Safari/537.36
    DNT: 1
    Accept-Encoding: gzip, deflate, sdch
    Accept-Language: en-US,en;q=0.8
        
            The start of the data when remote info is shown:
        
            +IPD,0,405,192.168.1.17,62917:GET / HTTP/1.1
            Host: 192.168.1.47
            Connection: keep-alive
            Cache-Control: max-age=0
   */
  
  
  
  //con_id is the connection ID
  int data_len;
  char *pb;  
  if(read_till_eol()) {
    if(strncmp(buffer, "+IPD,", 5)==0) {  //because IPD denotes the start of a request
      sscanf(buffer+5, "%d,%d", &con_id, &data_len);   //pull out the 
      if (data_len > 0) {
        // read serial until the colon, denoting the start of the GET command. 
        pb = buffer+5; 
        while(*pb!=':') pb++;
        pb++;
        if (strncmp(pb, "GET / ", 6) == 0) {
          wait_for_esp_response(1000,OKrn);
          return("/");
        }
        else {
          
          char page[10];
          sscanf(pb+5,"%s", page);
          //Serial.print("---scanf pb: ");
          //Serial.println(pb);
          wait_for_esp_response(1000,OKrn);
          return(page);
        }
      }
    }
  }
  return 0;
}



void ESP::SendContent(String hdr, String cont) {
  char OKrn[] = "OK\r\n";
  Serial1.print("AT+CIPSEND=");
  Serial1.print(con_id);
  Serial1.print(",");
  Serial1.println(hdr.length()+cont.length());
  if(wait_for_esp_response(5000,OKrn)) {
   Serial1.print(hdr);
   Serial1.print(cont);
  } 
  else {
  Serial1.print("AT+CIPCLOSE=");
  Serial1.println(con_id);
 }
}

