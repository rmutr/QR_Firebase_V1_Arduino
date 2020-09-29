#include <WiFi.h> 
#include <FirebaseESP32.h> 

#define WIFI_SSID "mEaNaKoRn_2.4G" 
#define WIFI_PASSWORD "3342417012" 
#define FIREBASE_HOST "https://akearduino.firebaseio.com" 
#define FIREBASE_AUTH "jbkzHb51sACHxRcqnf0tDqR57ag5S8bBdm7yPeGt" 

FirebaseData firebaseData_Rx; 
FirebaseData firebaseData_Tx; 

String system_path = "/Line_01/System"; 
String log_path    = "/Line_01/Log"; 

#define NPN_ON    0 
#define NPN_OFF   1 
#define DIR_FW    1 
#define DIR_RW    0 

#define PROC_INITIAL        0 
#define PROC_STANDBY        1 
#define PROC_COMPLETED      3 
#define PROC_ERROR          6 
#define PROC_PRODUCTION_1   11
#define PROC_PRODUCTION_2   12
#define PROC_PRODUCTION_3   13
#define PROC_PRODUCTION_4   14
#define PROC_PRODUCTION_5   15
#define PROC_PRODUCTION_6   16
#define PROC_PRODUCTION_7   17

#define PIN_MOTOR_ON        16 
#define PIN_MOTOR_DIR       17 
#define PIN_SENSOR_0        4 
#define PIN_SENSOR_1        0 
#define PIN_SENSOR_2        2 
#define PIN_SENSOR_3        13 

#define TMR_DWN_MAX         200 

char str_buff[200] = {0}; 
unsigned long t_old = 0; 
int tmr_dwn = 0; 
int tmr_cnt = 0; 
int pin_sensor[4] = {0}; 
int sensor[4] = {0}; 
int error = 0; 

int status_robot_1      = 0; 
int status_robot_2      = 0; 
int status_robot_3      = 0; 
int status_mobile       = 0; 
int status_mobile_mon   = 0; 
int status_process      = 0; 
int status_process_mon  = 0; 
int status_product      = 0; 
int status_product_mon  = 0; 
int status_conveyer     = 0; 
int status_conveyer_mon = 0; 

String mobile_qrcode    = ""; 
String product_qrcode   = ""; 

//------------------------------------------------------ 
#include <WiFiUdp.h> 

unsigned int udp_localPort = 1000; 

IPAddress timeServerIP;                       // time.nist.gov NTP server address 
const char* ntpServerName = "time.nist.gov";  // 
const int NTP_PACKET_SIZE = 48;               // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE];          // buffer to hold incoming and outgoing packets

WiFiUDP udp;

int ntp_Y, ntp_M, ntp_D, ntp_H, ntp_m, ntp_S; 

unsigned long sendNTPpacket(IPAddress& address); 

//------------------------------------------------------
void Motor_Stop(); 
void Motor_FW(); 
void Motor_RW(); 

void streamCallback(StreamData data); 
void streamTimeoutCallback(bool ptimeout); 
int System_Update(String pitem, String pdespt);
int System_Update_Mobile(int pstatus);
int System_Update_Process(int pstatus); 
int System_Update_Product(int pstatus); 
int System_Update_Conveyer(int pstatus); 
int Log_Add(String pdatetime, String pdetail); 

void printResult(FirebaseData &data); 
void printResult(StreamData &data); 


void setup()
{
  pinMode(PIN_MOTOR_ON,  OUTPUT);
  pinMode(PIN_MOTOR_DIR, OUTPUT);

  pin_sensor[0] = PIN_SENSOR_0;
  pin_sensor[1] = PIN_SENSOR_1;
  pin_sensor[2] = PIN_SENSOR_2;
  pin_sensor[3] = PIN_SENSOR_3;

  for (int bix = 0; bix < 4; bix++) {
    pinMode(pin_sensor[bix],  INPUT_PULLUP);
  }

  Motor_Stop(); 
  
  Serial.begin(115200); 

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); 

  Serial.print("\nConnecting to Wi-Fi"); 
  while (WiFi.status() != WL_CONNECTED) { 
    Serial.print("."); 
    delay(300); 
  } 

  Serial.print("\nConnected with IP: ");
  Serial.println(WiFi.localIP());

  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH); 
  Firebase.reconnectWiFi(true); 

  if (!Firebase.beginStream(firebaseData_Rx, system_path)) {
    Serial.println("------------------------------------");
    Serial.println("Can't begin stream connection...");
    Serial.println("REASON: " + firebaseData_Rx.errorReason());
    Serial.println("------------------------------------");
    Serial.println();
  }

  Firebase.setStreamCallback(firebaseData_Rx, streamCallback, streamTimeoutCallback); 

  error = 0; 
  error += System_Update("Robot_1", "Initial"); 
  error += System_Update("Robot_2", "Initial"); 
  error += System_Update("Robot_3", "Initial"); 
  error += System_Update("Mobile",  "Initial"); 
  error += System_Update("Process", "Initial"); 
  error += System_Update("Product", "No Product"); 
  error += System_Update("Conveyer", "Stop"); 
  if (error > 0) { status_process = PROC_ERROR; } 
  delay(200); 

//------------------------------------------------------
  udp.begin(udp_localPort); 
  ntp_Y = 0; ntp_M = 0; ntp_D = 0; 
  ntp_H = 0; ntp_m = 0; ntp_S = 0; 
} 

void loop()
{
  for (int bix = 0; bix < 4; bix++) {
    sensor[bix] = !digitalRead(pin_sensor[bix]);
  }

  status_product = 0; 
  if (tmr_dwn > 0) { status_product = 1; } 

  if (status_mobile_mon != status_mobile) { 
    status_mobile_mon = status_mobile; 
    System_Update_Mobile(status_mobile_mon); 
  } 

  if (status_process_mon != status_process) { 
    status_process_mon = status_process; 
    System_Update_Process(status_process_mon); 
  } 

  if (status_product_mon != status_product) { 
    status_product_mon = status_product; 
    System_Update_Product(status_product_mon); 
  } 

  if (status_conveyer_mon != status_conveyer) { 
    status_conveyer_mon = status_conveyer; 
    System_Update_Conveyer(status_conveyer_mon); 
  } 

  switch (status_process) {
    default: 
      status_process = PROC_INITIAL; 
    case PROC_INITIAL: 
      Motor_Stop(); status_conveyer = 0; 

      status_product = 0; 
      mobile_qrcode = ""; 
      product_qrcode   = ""; 

      error = 0; 
      error += System_Update("Robot_1", "Close"); 
      error += System_Update("Mobile",  "Initial"); 

      if (error > 0) { 
        status_process = PROC_ERROR; 
      } else { 
        status_process = PROC_STANDBY; 
      } 
      break; 

    case PROC_STANDBY: 
      Motor_Stop(); status_conveyer = 0; 

      if (sensor[0] == 1) { 
        tmr_dwn = TMR_DWN_MAX; 
      } 

      if (sensor[0] == 1 
       && sensor[1] == 0 
       && sensor[2] == 0 
       && sensor[3] == 0) { 
        error = System_Update("Robot_1", "Open"); 
        status_process = PROC_PRODUCTION_1; 
      } 
      break; 

    case PROC_PRODUCTION_1: 
      Motor_FW(); status_conveyer = 1; 

      if (sensor[0] == 0 
       && sensor[1] == 1 
       && sensor[2] == 0 
       && sensor[3] == 0) { 
        Motor_Stop(); status_conveyer = 0; 
        error = System_Update("Robot_1", "Close"); 
        error = System_Update("Mobile",  "QR Request"); 
        status_process = PROC_PRODUCTION_2; 
      } 
      break; 

    case PROC_PRODUCTION_2: 
      Motor_Stop(); status_conveyer = 0; 

      if (status_mobile == 4) { 
        Motor_FW(); status_conveyer = 1; 
        product_qrcode = mobile_qrcode; 
        status_process = PROC_PRODUCTION_3; 
      } 
      break; 

    case PROC_PRODUCTION_3: 
      Motor_FW(); status_conveyer = 1; 

      if (sensor[2] == 1 
       || sensor[3] == 1) { 
        Motor_Stop(); status_conveyer = 0; 
        status_process = PROC_PRODUCTION_4; 
      } 

      if (sensor[0] == 1) { 
        Motor_Stop(); status_conveyer = 0; 
      } else {
        if (tmr_dwn > 0) { 
          tmr_dwn--; 
        } else { 
          Motor_Stop(); status_conveyer = 0; 
          status_process = PROC_INITIAL; 
        } 
      }
      break; 

    case PROC_PRODUCTION_4: 
      Motor_Stop(); status_conveyer = 0; 

      // Log & Send data to  Poom & Sam robot. 
      sprintf(str_buff, "%04d-%02d-%02d %02d:%02d:%02d", ntp_Y, ntp_M, ntp_D, ntp_H, ntp_m, ntp_S); 
      error = Log_Add(str_buff, product_qrcode); 

      status_process = PROC_PRODUCTION_5; 
      break; 

    case PROC_PRODUCTION_5: 
      Motor_Stop(); status_conveyer = 0; 

      if (sensor[0] == 0 
       && sensor[1] == 0 
       && sensor[2] == 0 
       && sensor[3] == 0) { 
        status_process = PROC_INITIAL; 
      }     
      break; 

    case PROC_PRODUCTION_6: break; 
    case PROC_PRODUCTION_7: break; 
    case PROC_COMPLETED:    break; 

    case PROC_ERROR: 
      Motor_Stop(); 
      break; 
  } 

  tmr_cnt++; 
  if (tmr_cnt == 10) {
    tmr_cnt = 0; 

    int bbytes = udp.parsePacket(); 

    if (bbytes > 0) { 
      // Do nothing. 
    } else { 
      udp.read(packetBuffer, NTP_PACKET_SIZE); 

      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]); 
      unsigned long lowWord  = word(packetBuffer[42], packetBuffer[43]); 
      unsigned long secsSince1900 = highWord << 16 | lowWord; 
      unsigned long secs2020 = 3786825600UL; 

      if (secsSince1900 > 0) { 
        unsigned long epoch = secsSince1900 - secs2020; 
        unsigned long gmt_offset = +7UL; 
        epoch = (epoch + (gmt_offset * 3600UL)); 

        ntp_Y = (int)2020; 
        ntp_M = (int)1; 
        ntp_D = (int)((epoch / 86400UL) + 1UL); 

        ntp_H = (int)((epoch % 86400UL) / 3600UL); 
        ntp_m = (int)((epoch % 3600UL) / 60UL); 
        ntp_S = (int) (epoch % 60UL); 
      } 
    } 

    WiFi.hostByName(ntpServerName, timeServerIP); 
    sendNTPpacket(timeServerIP); 

    sprintf(str_buff, "[ %02d | %d | %d%d%d%d | %d %2d.%1d ] [ %04d-%02d-%02d %02d:%02d:%02d ] [ %d | " 
      , status_process_mon 
      , status_conveyer_mon 
      , sensor[0], sensor[1], sensor[2], sensor[3] 
      , status_product_mon 
      , (tmr_dwn / 10), (tmr_dwn % 10) 
      , ntp_Y, ntp_M, ntp_D, ntp_H, ntp_m, ntp_S 
      , status_mobile_mon 
    ); 
    Serial.println(str_buff + product_qrcode + " ] "); 
  }

  while ((micros() - t_old) < 100000L);
  t_old = micros();

}


void Motor_Stop() { 
  digitalWrite(PIN_MOTOR_ON,  NPN_OFF); 
  digitalWrite(PIN_MOTOR_DIR, DIR_FW); 
} 

void Motor_FW() { 
  digitalWrite(PIN_MOTOR_ON,  NPN_ON); 
  digitalWrite(PIN_MOTOR_DIR, DIR_FW); 
} 

void Motor_RW() { 
  digitalWrite(PIN_MOTOR_ON,  NPN_ON); 
  digitalWrite(PIN_MOTOR_DIR, DIR_RW); 
} 


void streamCallback(StreamData data) { 
  //Serial.println("Rx Stream Data available..."); 
  //Serial.println("Rx STREAM PATH: " + data.streamPath()); 
  //Serial.println("Rx EVENT PATH: "  + data.dataPath()); 
  //Serial.println("Rx DATA TYPE: "   + data.dataType()); 
  //Serial.println("Rx EVENT TYPE: "  + data.eventType()); 
  //Serial.print("Rx VALUE: "); 
  //printResult(data); 
  //Serial.println(); 

  if (data.dataPath() == "/Mobile/status") { 
    if (data.dataType() == "int") { status_mobile = data.intData(); } 
  } 

  if (data.dataPath() == "/Mobile/despt") { 
    if (data.dataType() == "string") { mobile_qrcode = data.stringData(); } 
  } 

  if (data.dataPath() == "/Mobile") { 

    String bmobile_despt  = ""; 
    String bmobile_status = ""; 

    FirebaseJson *json = data.jsonObjectPtr(); 

    size_t len = json->iteratorBegin(); 
    String key, value = ""; 
    int type = 0;
    for (size_t i = 0; i < len; i++) 
    { 
      json->iteratorGet(i, type, key, value); 

      if (key == "status") { 
        bmobile_status = value; 
      } 

      if (key == "despt") {
        bmobile_despt = value; 
      }
    }
    json->iteratorEnd();

    if (bmobile_status == "4") {
      
      status_mobile = 4; 
      mobile_qrcode = bmobile_despt; 
    }
    
    //Serial.print(bmobile_status);
    //Serial.print(' ');
    //Serial.println(bmobile_despt);    
  }
} 

void streamTimeoutCallback(bool ptimeout) { 
  if (ptimeout != 0) { Serial.println("Stream timeout, resume streaming..."); }
}


int System_Update(String pitem, String pdespt) { 
  int lresult_int = 0; 
  int lstatus = 6; 
  String ldespt = pdespt; 

  if (pitem == "Robot_1") { 
    if (pdespt == "Initial") { lstatus = 0; } 
    if (pdespt == "Standby") { lstatus = 1; } 
    if (pdespt == "Open")    { lstatus = 2; } 
    if (pdespt == "Close")   { lstatus = 3; } 
    status_robot_1 = lstatus; 
  } 

  if (pitem == "Robot_2") { 
    if (pdespt == "Initial") { lstatus = 0; } 
    status_robot_2 = lstatus; 
  } 
  
  if (pitem == "Robot_3") { 
    if (pdespt == "Initial") { lstatus = 0; } 
    status_robot_3 = lstatus; 
  } 

  if (pitem == "Mobile") { 
    if (pdespt == "Initial")      { lstatus = 0; } 
    if (pdespt == "Mobile Check") { lstatus = 1; } 
    if (pdespt == "Mobile Ready") { lstatus = 2; } 
    if (pdespt == "QR Request")   { lstatus = 3; } 
    if (pdespt == "QR Completed") { lstatus = 4; } 
    status_mobile = lstatus; 
  } 
  
  if (pitem == "Process") { 
    if (pdespt == "Initial")      { lstatus =  0; } 
    if (pdespt == "Standby")      { lstatus =  1; } 
    if (pdespt == "Completed")    { lstatus =  3; } 
    if (pdespt == "Production_1") { lstatus = 11; } 
    if (pdespt == "Production_2") { lstatus = 12; } 
    if (pdespt == "Production_3") { lstatus = 13; } 
    if (pdespt == "Production_4") { lstatus = 14; } 
    if (pdespt == "Production_5") { lstatus = 15; } 
    if (pdespt == "Production_6") { lstatus = 16; } 
    if (pdespt == "Production_7") { lstatus = 17; } 
    status_process = lstatus; 
  } 
  
  if (pitem == "Product") { 
    if (pdespt == "No Product") { lstatus = 0; } 
    if (pdespt == "Product In Line") { lstatus = 1; } 
    status_product = lstatus; 
  } 
  
  if (pitem == "Conveyer") { 
    if (pdespt == "Stop") { lstatus = 0; } 
    if (pdespt == "Run")  { lstatus = 1; } 
    status_conveyer = lstatus; 
  } 

  if (lstatus == 6) { ldespt = "Error"; } 

  FirebaseJson json; 
  json.add("status", lstatus).add("despt", ldespt); 
  if (Firebase.setJSON(firebaseData_Tx, system_path + "/" + pitem, json) != 0) { 
    //Serial.println(" PATH: " + firebaseData_Tx.dataPath()); 
    //Serial.println(" TYPE: " + firebaseData_Tx.dataType()); 
    //Serial.println("VALUE: "); 
    //printResult(firebaseData_Tx); 
  } else { 
    lresult_int = 1; 
    Serial.println("Tx FAILED: REASON: " + firebaseData_Tx.errorReason()); 
  } 

  return lresult_int;
} 

int System_Update_Mobile(int pstatus) { 
  int lresult_int = 0; 
  String ldespt = "Error"; 

  if (pstatus == 0) { ldespt = "Initial";      } 
  if (pstatus == 1) { ldespt = "Mobile Check"; } 
  if (pstatus == 2) { ldespt = "Mobile Ready"; } 
  if (pstatus == 3) { ldespt = "QR Request";   } 
  if (pstatus == 4) { ldespt = "QR Completed"; } 

  lresult_int = System_Update("Mobile", ldespt); 

  return lresult_int; 
}

int System_Update_Process(int pstatus) { 
  int lresult_int = 0; 
  String ldespt = "Error"; 

  if (pstatus ==  0) { ldespt = "Initial";      } 
  if (pstatus ==  1) { ldespt = "Standby";      } 
  if (pstatus ==  3) { ldespt = "Completed";    } 
  if (pstatus == 11) { ldespt = "Production_1"; } 
  if (pstatus == 12) { ldespt = "Production_2"; } 
  if (pstatus == 13) { ldespt = "Production_3"; } 
  if (pstatus == 14) { ldespt = "Production_4"; } 
  if (pstatus == 15) { ldespt = "Production_5"; } 
  if (pstatus == 16) { ldespt = "Production_6"; } 
  if (pstatus == 17) { ldespt = "Production_7"; } 

  lresult_int = System_Update("Process", ldespt); 

  return lresult_int; 
}

int System_Update_Product(int pstatus) { 
  int lresult_int = 0; 
  String ldespt = "Error"; 

  if (pstatus == 0) { ldespt = "No Product";      } 
  if (pstatus == 1) { ldespt = "Product In Line"; } 

  lresult_int = System_Update("Product", ldespt); 

  return lresult_int; 
}

int System_Update_Conveyer(int pstatus) { 
  int lresult_int = 0; 
  String ldespt = "Error"; 

  if (pstatus == 0) { ldespt = "Stop"; } 
  if (pstatus == 1) { ldespt = "Run";  } 

  lresult_int = System_Update("Conveyer", ldespt); 

  return lresult_int; 
}

int Log_Add(String pdatetime, String pdetail) { 
  int lresult_int = 0; 
  String llog_name = pdatetime + " " + pdetail;
  FirebaseJson json; 
  json.add("datetime", pdatetime).add("detail", pdetail); 
  if (Firebase.setJSON(firebaseData_Tx, log_path + "/" + llog_name, json) != 0) { 
    // Do nothing.
  } else { 
    lresult_int = 1; 
    Serial.println("Tx FAILED: REASON: " + firebaseData_Tx.errorReason()); 
  } 

  return lresult_int;
} 

void printResult(FirebaseData &data)
{
  Serial.println("FirebaseData");

  if (data.dataType() == "int")
    Serial.println(data.intData());
  else if (data.dataType() == "float")
    Serial.println(data.floatData(), 5);
  else if (data.dataType() == "double")
    printf("%.9lf\n", data.doubleData());
  else if (data.dataType() == "boolean")
    Serial.println(data.boolData() == 1 ? "true" : "false");
  else if (data.dataType() == "string")
    Serial.println(data.stringData());
  else if (data.dataType() == "json")
  {
    Serial.println();
    FirebaseJson &json = data.jsonObject();
    //Print all object data
    Serial.println("Pretty printed JSON data:");
    String jsonStr;
    json.toString(jsonStr, true);
    Serial.println(jsonStr);
    Serial.println();
    Serial.println("Iterate JSON data:");
    Serial.println();
    size_t len = json.iteratorBegin();
    String key, value = "";
    int type = 0;
    for (size_t i = 0; i < len; i++)
    {
      json.iteratorGet(i, type, key, value);
      Serial.print(i);
      Serial.print(", ");
      Serial.print("Type: ");
      Serial.print(type == JSON_OBJECT ? "object" : "array");
      if (type == JSON_OBJECT)
      {
        Serial.print(", Key: ");
        Serial.print(key);
      }
      Serial.print(", Value: ");
      Serial.println(value);
    }
    json.iteratorEnd();
  }
  else if (data.dataType() == "array")
  {
    Serial.println();
    //get array data from FirebaseData using FirebaseJsonArray object
    FirebaseJsonArray &arr = data.jsonArray();
    //Print all array values
    Serial.println("Pretty printed Array:");
    String arrStr;
    arr.toString(arrStr, true);
    Serial.println(arrStr);
    Serial.println();
    Serial.println("Iterate array values:");
    Serial.println();
    for (size_t i = 0; i < arr.size(); i++)
    {
      Serial.print(i);
      Serial.print(", Value: ");

      FirebaseJsonData &jsonData = data.jsonData();
      //Get the result data from FirebaseJsonArray object
      arr.get(jsonData, i);
      if (jsonData.typeNum == JSON_BOOL)
        Serial.println(jsonData.boolValue ? "true" : "false");
      else if (jsonData.typeNum == JSON_INT)
        Serial.println(jsonData.intValue);
      else if (jsonData.typeNum == JSON_DOUBLE)
        printf("%.9lf\n", jsonData.doubleValue);
      else if (jsonData.typeNum == JSON_STRING ||
               jsonData.typeNum == JSON_NULL ||
               jsonData.typeNum == JSON_OBJECT ||
               jsonData.typeNum == JSON_ARRAY)
        Serial.println(jsonData.stringValue);
    }
  }
}

void printResult(StreamData &data)
{
  Serial.println("StreamData");

  if (data.dataType() == "int")
    Serial.println(data.intData());
  else if (data.dataType() == "float")
    Serial.println(data.floatData(), 5);
  else if (data.dataType() == "double")
    printf("%.9lf\n", data.doubleData());
  else if (data.dataType() == "boolean")
    Serial.println(data.boolData() == 1 ? "true" : "false");
  else if (data.dataType() == "string")
    Serial.println(data.stringData());
  else if (data.dataType() == "json")
  {
    Serial.println();
    FirebaseJson *json = data.jsonObjectPtr();
    //Print all object data
    Serial.println("Pretty printed JSON data:");
    String jsonStr;
    json->toString(jsonStr, true);
    Serial.println(jsonStr);
    Serial.println();
    Serial.println("Iterate JSON data:");
    Serial.println();
    size_t len = json->iteratorBegin();
    String key, value = "";
    int type = 0;
    for (size_t i = 0; i < len; i++)
    {
      json->iteratorGet(i, type, key, value);
      Serial.print(i);
      Serial.print(", ");
      Serial.print("Type: ");
      Serial.print(type == JSON_OBJECT ? "object" : "array");
      if (type == JSON_OBJECT)
      {
        Serial.print(", Key: ");
        Serial.print(key);
      }
      Serial.print(", Value: ");
      Serial.println(value);
    }
    json->iteratorEnd();
  }
  else if (data.dataType() == "array")
  {
    Serial.println();
    //get array data from FirebaseData using FirebaseJsonArray object
    FirebaseJsonArray *arr = data.jsonArrayPtr();
    //Print all array values
    Serial.println("Pretty printed Array:");
    String arrStr;
    arr->toString(arrStr, true);
    Serial.println(arrStr);
    Serial.println();
    Serial.println("Iterate array values:");
    Serial.println();

    for (size_t i = 0; i < arr->size(); i++)
    {
      Serial.print(i);
      Serial.print(", Value: ");

      FirebaseJsonData *jsonData = data.jsonDataPtr();
      //Get the result data from FirebaseJsonArray object
      arr->get(*jsonData, i);
      if (jsonData->typeNum == JSON_BOOL)
        Serial.println(jsonData->boolValue ? "true" : "false");
      else if (jsonData->typeNum == JSON_INT)
        Serial.println(jsonData->intValue);
      else if (jsonData->typeNum == JSON_DOUBLE)
        printf("%.9lf\n", jsonData->doubleValue);
      else if (jsonData->typeNum == JSON_STRING ||
               jsonData->typeNum == JSON_NULL ||
               jsonData->typeNum == JSON_OBJECT ||
               jsonData->typeNum == JSON_ARRAY)
        Serial.println(jsonData->stringValue);
    }
  }
}


unsigned long sendNTPpacket(IPAddress& address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}
