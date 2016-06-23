
#ifndef ESP_h
#define ESP_h

#define BUFFER_SIZE 2048

class ESP
{
    
  public:
    ESP();
    void setupWiFi();
    void sendHTTPRequest(String,String);
    String ListenForClients();
    void SendContent(String,String);
    
  private:
    char buffer[BUFFER_SIZE];
    byte wait_for_esp_response(int,char*);
    bool read_till_eol();
    int con_id;

};
#endif


