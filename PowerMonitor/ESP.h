#include "Arduino.h"

#ifndef ESP_h
#define ESP_h

#define BUFFER_SIZE 2048

class ESP
{
    
  public:
    ESP();
    void setupWiFi(String,String,String);
    void sendHTTPRequest(String,String);
    void reconnectWiFi(String,String);
    
  private:
    char buffer[BUFFER_SIZE];
    byte wait_for_esp_response(int,char*);
    int con_id;

};
#endif
