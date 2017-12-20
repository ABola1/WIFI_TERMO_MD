#include <OneWire.h>
#include <DallasTemperature.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>


//#define SSID "dd-wrt"
//#define PASS "8849884988"
#define DST_IP "narodmon.ru" //baidu.com

SoftwareSerial espSerial (10, 9); // RX, TX
HardwareSerial & dbgTerminal = Serial;

// set pin numbers:
const int ledPin =  13;
const int ESP8266_CHPD = 12;

// Variables will change:
int ledState = HIGH;             // ledState used to set the LED
#define BUFFER_SIZE 64
char buffer[BUFFER_SIZE];

struct TempDev{
  float tempC[3];
  float MINtempC[3];
  float MAXtempC[3];
  DeviceAddress DevAddr[3];
}; 

TempDev TempDev ={{0,0,0},
                {100,100,100},
                {-100,-100,-100}
             };

unsigned long lastTempRequest = 0;
int  delayInMillis = 500;
byte flagT=0;

const unsigned long postingInterval = 300000;   //600000;
unsigned long lastConnectionTime = 0;

const unsigned long postingIntervalMD = 30000;   //600000;
unsigned long lastConnectionTimeMD = 0;

const unsigned long ReloadIInterval = 1800000;
unsigned long lastReloadTime = 0;

#define ONE_WIRE_BUS 12
#define TEMPERATURE_PRECISION 10

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

int numberOfDevices; // Number of temperature devices found

DeviceAddress tempDeviceAddress; // We'll use this variable to store a found device address


//========================================== EEPROM+++++++++++++++++++++++++++++++++++++++++++++++++++++

#define CONFIG_EEPROM_ADDR 0

struct Config{
  char ipStr[16];
  char ssidStr[16];
  char passStr[16];
  byte senddata;          // 1- naromon.ru 2- internal WEB page 3- MojorDomo
  char MDAddr[16];
  char MDObj[16];
} 
Config;

void ConfigReadEEPROM()
{
  for(byte i=0; i< sizeof Config; ++i)
  {
    ((byte*) &Config)[i] = EEPROM.read(CONFIG_EEPROM_ADDR + i);
  }
}

void ConfigSaveEEPROM()
{
  for (byte i = 0; i < sizeof Config; ++i)
  {
    EEPROM.write(CONFIG_EEPROM_ADDR + i, ((byte*) &Config)[i]);
  }
}


void PrintFindAP()
{
  espSerial.println(F("AT+CWLAP"));
  while (espSerial.available()<=11)
  {}
  while (espSerial.available()>0)Serial.write(espSerial.read());
}

//=========================================CONSOLE=====================================================
#define MAX_INBUF  25
#define NUM_COMMAND  9

char InputBufCon[MAX_INBUF];

struct command{
  char *name;
  int cmd;
};

enum cmds{
  cmdVoid, cmdHelp, cmdSetIP, cmdSetSSID, cmdSetPASS, cmdShowAP, cmdMDAddr, cmdMDObj, cmdSendData, SaveConfig};
command commands[NUM_COMMAND] = {
  {
    "help", cmdHelp                    }
  , 
  {
    "setip", cmdSetIP                    }
  , 
  {
    "setssid", cmdSetSSID                    }
  ,
  {
    "setpass", cmdSetPASS                    }
  ,
  {
    "showap", cmdShowAP                    }
  ,
  {
    "mdaddr", cmdMDAddr                    }
  ,
  {
    "mdobj", cmdMDObj                    }
  ,
  {
    "senddata", cmdSendData                   }
  ,
  {
    "saveconfig", SaveConfig                   }
};

inline int isDigit(char a)
{
  return (a >= '0' && a <= '9');
}

inline int isSpace(char a)
{
  return (a == ' ' || a == '\r' || a == '\n' || a == 0);
}

int cmpStr(char *str1, char *str2) // return 1 РµСЃР»Рё СЃС‚СЂРѕРєРё СЂР°РІРЅС‹;
{
  int i = 0;
  while(str1[i] != 0 && str2[i] != 0){
    if(str1[i] != str2[i])
      return 0;
    i++;
  }
  return (str1[i] == 0 && str2[i] == 0);
}

int GetCommand(char **str)
{
  int retCmd = cmdVoid;
  int i = 0;
  char *s = *str;
  while(!isSpace(s[i]))
    i++;
  if(i == 0)
    return retCmd;
  s[i] = 0;
  *str += i + 1;    // РЅРµР±РµР·РѕРїР°СЃРЅС‹Р№ РєРѕРґ!!!
  for(int j = 0; j < NUM_COMMAND; j++)
    if(cmpStr(commands[j].name, s)){
      retCmd = commands[j].cmd;
      break;
    }
  //Serial.print(" retCmdsdd ");
  //Serial.println(retCmd);
  return retCmd;
}


int getNum(char **str, uint8_t *num)
{
  char *s = *str;
  int n = 0;
  int sign = 1;
  if(*s == '-'){
    sign = -1;
    s++;
  }
  while(isDigit(*s)){
    n *= 10;
    n += *s - '0';
    s++;
  }
  if(s == *str)
    return 0;
  *num = n * sign;
  *str = s;
  return 1;
}



int ParseIP(char *str, uint8_t *ip) // ip - Р±СѓС„РµСЂ РґР»СЏ IP Р°РґСЂРµСЃР° (РґРѕР»Р¶РµРЅ Р±С‹С‚СЊ СЂР°Р·РјРµСЂРѕРј РЅРµ РјРµРЅРЅРµРµ 4
{
  char *s = str;
  for(int i = 0; i < 3; i++){
    if(!getNum(&s, &ip[i]))
      return 0;
    if(*s != '.')
      return 0;
    s++;
  }
  if(!getNum(&s, &ip[3]))
    return 0;
  if(!isSpace(*s))
    return 0;
  return 1;  
}



void MenuPrintHelp()
{
    Serial.println( F("\n \n command help"));
    Serial.println( F("help  -  show this text"));
    Serial.println( F("setip X.X.X.X/dhcp setup IP/DHCP"));
    Serial.println( F("setmask X.X.X.X   setup mask"));
    Serial.println( F("setpass -  set access point password"));
    Serial.println( F("showap -   show all access points"));
    Serial.println( F("mdaddr -   set MojorDomo address server"));
    Serial.println( F("mdobj  -   set MojorDomo object"));
    Serial.println( F("senddata - 1- naromon.ru 2- internal WEB page 3- MojorDomo"));
    Serial.println( F("saveconfig   Save configuration \n \n"));
}

void PrintIP()
{
    if (!strncmp(Config.ipStr,"dhcp", 3)) 
    {
       //print the ip addr
      Serial.println();
      dbgTerminal.print(F("ip address : "));
      dbgTerminal.println( GetResponse(F("AT+CIFSR"), 10) );
    }
    
    Serial.println(Config.ipStr); //str); 
}

void MenuProg()//uint8_t *ipadr, uint8_t *mask, uint8_t *gw, uint8_t *dnsadr)
{
  uint8_t ipbuf[4];
  char *str = InputBufCon;
  switch(GetCommand(&str)){
  case cmdVoid:    // РЅР° РЅРµРїСЂР°РІРёР»СЊРЅСѓСЋ РєРѕРјР°РЅРґСѓ РїРѕРєР°Р·С‹РІР°РµРј help
  case cmdHelp:
    MenuPrintHelp();
    break;
  case cmdSetIP:
    strcpy(Config.ipStr, str);
    PrintIP();
    break;
  case cmdSetSSID:
    strcpy(Config.ssidStr, str);
    Serial.println();
    Serial.println(Config.ssidStr); //str)
    break;

  case cmdSetPASS:
    strcpy(Config.passStr, str);
    Serial.println();
    Serial.println(Config.passStr); //str)
    break;
  
  case cmdShowAP:
    PrintFindAP();
    break; 

  case cmdMDAddr:
    strcpy(Config.MDAddr, str);
    Serial.println();
    Serial.println(Config.MDAddr); //str)
    break;

  case cmdMDObj:
    strcpy(Config.MDObj, str);
    Serial.println();
    Serial.println(Config.MDObj); //str)
    break;
  
  case cmdSendData:
    if(str[0]== '1')
    {
      Config.senddata=1;
      Serial.println();
      Serial.println(F("Send data in narodmon.ru"));
      lastConnectionTime= lastConnectionTime + postingInterval; //otpravljem danie na narodmon.ru srazu
      StartWEBClient(); 
    }
    else if(str[0]== '2')
    {
      Config.senddata=2;
      Serial.println();
      Serial.println(F("Send data in intermal WEB page"));
      StertWEBServer();
    }
    else if(str[0]== '3')
    {
      Config.senddata=3;
      Serial.println();
      Serial.println(F("Send data in MojorDomo server"));
      lastConnectionTime= lastConnectionTime + postingInterval; //otpravljem danie na narodmon.ru srazu
      StartWEBClient(); 
    }
    break;

  case SaveConfig:
    ConfigSaveEEPROM();
    Serial.println();
    Serial.println(F("Save config..."));
    break;
  default:
    Serial.println(InputBufCon); // РґР»СЏ РѕС‚Р»Р°РґРєРё РїРµС‡Р°С‚Р°РµРј С‚Рѕ С‡С‚Рѕ РІРІРµР»Рё
  }
}



void CheckConsole(void)
{
  byte inByte = 0;
  static int curpos = 0;
  if(Serial.available() > 0) {
    inByte = Serial.read();
    Serial.write(inByte);   // СЌС…Рѕ
    if(inByte == 13){   // РЅР°Р¶Р°Р»Рё РµРЅС‚РµСЂ
      InputBufCon[curpos] = 0;
      curpos = 0;
      MenuProg();
    }
    else{
      if(curpos < (MAX_INBUF - 1))
        InputBufCon[curpos++] = inByte;
    }
  }
}

void PrintConfig()
{
  Serial.print(F("IP :"));
  Serial.println(Config.ipStr); //st
  Serial.print(F("SSID :"));
  Serial.println(Config.ssidStr); //st\
  Serial.print(F("PASS :"));
  Serial.println(Config.passStr); //st
  Serial.print(F("MDADDR :"));
  Serial.println(Config.MDAddr); //st
  Serial.print(F("MDOBJ :"));
  Serial.println(Config.MDObj); //st
  Serial.print(F("Send data im:"));
  if(Config.senddata==1)Serial.println(F("narodmon.ru"));
  else if(Config.senddata==2)Serial.println(F("internal WEB page"));
  else if(Config.senddata==3)Serial.println(F("MojorDomo"));
  Serial.println();
}


//=========================================== BAROMETR =============================================================================
Adafruit_BMP085 bmp;

unsigned long lastBaroRequest=0;
int delayBaroMillis = 12000;


int32_t lastMicros;


struct BaroDev{
  float temperature;
  float pressure;
  float altitude;
  float MINtemperature;
  float MAXtemperature;
  float MINpressure;
  float MAXpressure;
}; 

BaroDev BaroDev;
/*
={{0,0,0},
                {100,100,100},
                {-100,-100,-100}
             };
         */



void BarometrRead()
{
    
    if (millis() - lastBaroRequest >= delayBaroMillis) 
    {
    
    BaroDev.temperature = bmp.readTemperature();
    //BaroDev.pressure = bmp.readPressure()/133.332;
    BaroDev.pressure = bmp.readSealevelPressure()/133.332;

    // calculate absolute altitude in meters based on known pressure
    // (may pass a second "sea level pressure" parameter here,
    // otherwise uses the standard value of 101325 Pa)
    BaroDev.altitude = bmp.readAltitude();
   
    /*
    Serial.print("Pressure at sealevel (calculated) = ");
    Serial.print(bmp.readSealevelPressure());
    Serial.println(" Pa");

  // you can get a more precise measurement of altitude
  // if you know the current sea level pressure which will
  // vary with weather and such. If it is 1015 millibars
  // that is equal to 101500 Pascals.
    Serial.print("Real altitude = ");
    Serial.print(bmp.readAltitude(101500));
    Serial.println(" meters");
    
    Serial.println(); */
   
    lastBaroRequest= millis(); 
    }

        
}
//===========================================  DHT22 ===============================================================
#define DHTPIN 11     // what pin we're connected to

// Uncomment whatever type you're using!
//#define DHTTYPE DHT11   // DHT 11 
#define DHTTYPE DHT22   // DHT 22  (AM2302)
//#define DHTTYPE DHT21   // DHT 21 (AM2301)

DHT dht(DHTPIN, DHTTYPE);

unsigned long lastHumidityRequest=0;
int delayHumidityMillis = 12000;


struct HumidityDev{
  float temperature;
  float humidity;
  float MINtemperature;
  float MAXtemperature;
  float MINHumidity;
  float MAXHumidity;
}; 

HumidityDev HumidityDev;
/*
={{0,0,0},
                {100,100,100},
                {-100,-100,-100}
             };
         */



void HumidityRead()
{
    
    if (millis() - lastHumidityRequest >= delayHumidityMillis) 
    {
       HumidityDev.humidity = dht.readHumidity();
        HumidityDev.temperature = dht.readTemperature();
        
        lastHumidityRequest= millis(); 
    }

        
}
//================================================GET TEMPERATURE=====================================================================

void CopyDeviceAddress(DeviceAddress dest, DeviceAddress src)
{
  for(int i =0; i < 8; i++)
    dest[i] = src[i];
}



void TemperatureGet()
{
    float temp;
  
  if (millis() - lastTempRequest >= delayInMillis) // waited long enough??
  {
    if(!flagT)
    {
      //Serial.print("Requesting temperatures...");
      sensors.requestTemperatures(); // Send the command to get temperatures
      delayInMillis = 750 / (1 << (12 - TEMPERATURE_PRECISION));
      lastTempRequest = millis();
      flagT=1;
      return; 
    }
    else
    {
      //Serial.println("DONE");
      // Loop through each device, print out temperature data
      for(int i=0;i<numberOfDevices; i++)
      {
        // Search the wire for address
        if(sensors.getAddress(tempDeviceAddress, i))
        {
          // Output the device ID
          //Serial.print("Temperature for device: ");
          //Serial.println(i,DEC);
          temp = sensors.getTempC(tempDeviceAddress);
          if(temp!= 85.00)TempDev.tempC[i] = temp;
          if(TempDev.tempC[i] < TempDev.MINtempC[i]) TempDev.MINtempC[i]= TempDev.tempC[i];
         if(TempDev.tempC[i] > TempDev.MAXtempC[i]) TempDev.MAXtempC[i]= TempDev.tempC[i];
          
        } 
      }

    }
    flagT=0; 
  }
}




// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }

}

void StartTempSensor()
{
    //Start up the library
  sensors.begin();
  // Grab a count of devices on the wire
  numberOfDevices = sensors.getDeviceCount();

  // locate devices on the bus
  Serial.print(F("Locating devices...\n"));
  
  Serial.print(F("Found devices: "));                       //"Found devices: ");
  Serial.print(numberOfDevices, DEC);

  // report parasite power requirements
   Serial.print(F("Parasite power is: ")); 

  if (sensors.isParasitePowerMode()) Serial.println(F("ON"));
  else Serial.println(F("OFF"));

  // Loop through each device, print out address
  for(int i=0;i<numberOfDevices; i++)
  {
    // Search the wire for address
    if(sensors.getAddress(tempDeviceAddress, i))
    {
      Serial.print(F("Found devices: "));
      Serial.print(i, DEC);
      Serial.print(F(" with address: "));

      printAddress(tempDeviceAddress);
      CopyDeviceAddress(TempDev.DevAddr[i],tempDeviceAddress);
      Serial.println();
      // Serial.print("Setting resolution to ");
      // Serial.println(TEMPERATURE_PRECISION, DEC);

      // set the resolution to TEMPERATURE_PRECISION bit (Each Dallas/Maxim device is capable of several different resolutions)
      sensors.setResolution(tempDeviceAddress, TEMPERATURE_PRECISION);

      //Serial.print("Resolution actually set to: ");
      //Serial.print(sensors.getResolution(tempDeviceAddress), DEC); 
      Serial.println();
    }
    else{
      Serial.print(F("NOT")); 
      Serial.print(F("Found devices: "));
      Serial.print(i, DEC);
    }
    
  //server.begin();
  }
  sensors.setWaitForConversion(false);
}

//========================================= WIFI=======================================================================

 // Get the data from the WiFi module and send it to the debug serial port
String GetResponse(String AT_Command, int wait){
  String tmpData;
  espSerial.println(AT_Command);
  delay(10);
  while (espSerial.available() >0 )  {
    char c = espSerial.read();
    tmpData += c;
    if ( tmpData.indexOf(AT_Command) > -1 )         
      tmpData = "";
    else
      tmpData.trim();       
   }
   return tmpData;
}

boolean hardReset() {
   String tmpData;
  digitalWrite(ESP8266_CHPD,LOW);
  delay(500);
  digitalWrite(ESP8266_CHPD,HIGH);
  delay(4000);
  while ( espSerial.available() > 0 ) {
    char c = espSerial.read();
    tmpData +=c;
    espSerial.write(c);
    if ( tmpData.indexOf(F("Ready")) > -1 ) {
      Serial.println("Ready");
        clearBuffer();
        return 1;
    } 
  }
}


void clearSerialBuffer(void) {
       while ( espSerial.available() > 0 ) {
         espSerial.read();
       }
}


void clearBuffer(void) {
       for (int i =0;i<BUFFER_SIZE;i++ ) {
         buffer[i]=0;
       }
}



void homepage(int ch_id)
 {
  String Header;
  Header =  F("HTTP/1.1 200 OK\r\n");
  Header += F("Content-Type: text/html\r\n");
  Header += F("Connection: close\r\n");  
  //Header += F("Refresh: 5\r\n");

  String Content;
 
  
          // auto reload webpage every 5 second
          Content = F("<META HTTP-EQUIV=REFRESH CONTENT=5 URL=>");
          // webpage title
          Content += F("<center><p><h1>Meteo station</h1></p><center><hr><br />");

          // read analog pin 1 for the value of photocell
          Content += F("<p><h2>Temperature 1 = <font color=indigo>");
          Content += TempDev.tempC[0];
          Content += F("</font></h2></p>");
          
          Content +=F("<p><h2>Temperature 2 = <font color=indigo>");
          Content += TempDev.tempC[1];
          Content +=F("</font></h2></p>");
          
          // read analog pin 1 for the value of photocell
          Content += F("<p><h2>BAROTEMP = <font color=indigo>");
          Content += BaroDev.temperature;
          Content += F("</font></h2></p>");
          
          Content +=F("<p><h2>HUMTEMP = <font color=indigo>");
          Content += HumidityDev.temperature;
          Content +=F("</font></h2></p>");
          
          // read analog pin 1 for the value of photocell
          Content += F("<p><h2>PRESSURE = <font color=indigo>");
          Content += BaroDev.pressure;
          Content += F("</font></h2></p>");
          
          Content +=F("<p><h2>HUMIDITY = <font color=indigo>");
          Content += HumidityDev.humidity;
          Content +=F("</font></h2></p>");
          
          // read digital pin 13 for the state of PIR sensor
          Content +=F("<p><h2><font color=red>");
          
       //   if(ledState == HIGH) Content +=F( "LED ON");
        //  else if(ledState == LOW) Content +=F("LED OFF");
                  
          Content +=F("</font></h2></p>");
   
   /*       Content += F("<form  method=get name=form>");
          Content += F("<button name=a value=1 type=submit style=height:80px;width:150px>LED On</button>");
          Content += F("<button name=b value=2 type=submit style=height:80px;width:150px>LED Off</button>");
          Content += F("</form><br />"); */
        
          Header += F("Content-Length: ");
          Header += (int)(Content.length());
          Header += F("\r\n\r\n");
        
          espSerial.print(F("AT+CIPSEND="));
          espSerial.print(ch_id);
          espSerial.print(",");
          espSerial.println(Header.length()+Content.length());
          delay(10);


  if (espSerial.find(">")) {
      espSerial.print(Header);
      espSerial.print(Content);
      delay(10);
   }

}
 
boolean connectWiFi()
 {
       espSerial.println(F("AT+CWMODE=1"));
       String cmd=F("AT+CWJAP=\"");
       cmd+=Config.ssidStr;
       cmd+=F("\",\"");
       cmd+=Config.passStr;
       cmd+="\"";
       Serial.println(cmd);
       espSerial.println(cmd);
       //delay(2000);
       if(espSerial.find("OK"))
       {
         Serial.println(F("OK, Connected to WiFi."));
         return true;
         }else
         {
           Serial.println(F("Can not connect to the WiFi."));
           return false;
         }
  }
 
 void SetupWiFi()
 {
  
  dbgTerminal.println(F("ESP8266 start."));
  clearSerialBuffer();
  
  //hardReset();
  espSerial.println(F("AT+RST"));
  delay(3000);
       //test if the module is ready
  espSerial.println(F("AT"));
  delay(100);
  if(espSerial.find("OK"))Serial.println(F("Module is ready"));
  else{
       Serial.println(F("Module have no response."));
       return;
       //while(1);
  }
  delay(1000);
     //connect to the wifi
  Serial.println();
    
  boolean connected=false;
  for(int i=0;i<10;i++)
  {
     if(connectWiFi())
     {
         connected = true;
         break;
      }
   }
   //if (!connected){while(1);}
   delay(1000);

  //////////////////////////
  if(Config.senddata==1)      StartWEBClient();
  else if(Config.senddata==2) StertWEBServer();
 
   //print the ip addr
  dbgTerminal.print(F("ip address : "));
  dbgTerminal.println( GetResponse(F("AT+CIFSR"), 10) );
  
}

void StertWEBServer()
{
  //set the multiple connection mode
  dbgTerminal.print(F("AT+CIPMUX=1 : "));
  dbgTerminal.println( GetResponse(F("AT+CIPMUX=1"),10) );
  
  //set the server of port 80 check "no change" or "OK"
  dbgTerminal.print(F("AT+CIPSERVER=1,80 : "));
  dbgTerminal.println( GetResponse(F("AT+CIPSERVER=1,80"), 10) );
  
  delay(200);
  dbgTerminal.println();
  dbgTerminal.println(F("Start Webserver"));
  //digitalWrite(ledPin,ledState);  
  
}

void StartWEBClient()
{
   //set the multiple connection mode
  dbgTerminal.print(F("AT+CIPMUX=0 : "));
  dbgTerminal.println( GetResponse(F("AT+CIPMUX=0"),10) );
}


void RestartWiFi()
{
  if(millis()-lastReloadTime > ReloadIInterval)
  {
    dbgTerminal.println(F("RELOAD..."));
    SetupWiFi();
    lastReloadTime=millis();
  }
  
}

void WorkWEBpage()
{
  
  int ch_id, packet_len;
  char *pb;  
  espSerial.readBytesUntil('\n', buffer, BUFFER_SIZE);
  
  //dbgTerminal.println(buffer);

  if(strncmp(buffer, "+IPD,", 5)==0) {
    //request: +IPD,ch,len:data
    sscanf(buffer+5, "%d,%d", &ch_id, &packet_len);

    if (packet_len > 0) {
      // read serial until packet_len character received
      // start from :
      pb = buffer+5;
      while(*pb!=':') pb++;
      pb++;

      if (strncmp(pb, "GET /?a=1", 9) == 0) {
        dbgTerminal.print(millis());
        dbgTerminal.print(F(" : "));
        dbgTerminal.println(buffer);
        dbgTerminal.print(F("get led from ch :"));
        dbgTerminal.println(ch_id);
        delay(100);
        clearSerialBuffer();
        ledState = HIGH;
        digitalWrite(ledPin, ledState);
        homepage(ch_id);

      }else if (strncmp(pb, "GET /?b=2", 9) == 0) {
        dbgTerminal.print(millis());
        dbgTerminal.print(F(" : "));
        dbgTerminal.println(buffer);
        dbgTerminal.print(F("get Status from ch:"));
        dbgTerminal.println(ch_id);

        //delay(100);
        clearSerialBuffer();
        ledState = LOW; 
        digitalWrite(ledPin, ledState);
        homepage(ch_id);
      }
      else if (strncmp(pb, "GET / ", 6) == 0) {
        dbgTerminal.print(millis());
        dbgTerminal.print(F(" : "));
        dbgTerminal.println(buffer);
        dbgTerminal.print(F("get Status from ch:"));
        dbgTerminal.println(ch_id);

        //delay(100);
        clearSerialBuffer();
        homepage(ch_id);
      }
    }
  }
  clearBuffer();
}


//============================================================= narodmon.ru==================================================================

static byte mac[] = { 
  0x54, 0x55, 0x58, 0x10, 0x00, 0x24 }; // MAC-Р°РґСЂРµСЃ

//char replyBuffer[160];

//const unsigned long postingInterval = 180000; //600000;
//unsigned long lastConnectionTime = 0;           // время последней передачи данных
//boolean lastConnected = false;                  // состояние подключения


void ConvertSensorAddr(byte *addr, char *addrBuffer)
{
  addrBuffer[0]=0; 
  for (int k=0; k<=7; k++)
  {
    int b1=addr[k]/16;
    int b2=addr[k]%16;
    char c1[2],c2[2];

    if (b1>9) c1[0]=(char)(b1-10)+'A';
    else c1[0] = (char)(b1) + '0';
    if (b2>9) c2[0]=(char)(b2-10)+'A';
    else c2[0] = (char)(b2) + '0';

    c1[1]='\0';
    c2[1]='\0';

    strcat(addrBuffer,c1);
    strcat(addrBuffer,c2);
  }
  addrBuffer[16]=0; 
}



void SendNarodmon()
{
  if(millis()-lastConnectionTime > postingInterval)
  {
     String hcmd = F("AT+CIPSTART=\"TCP\",\"");
     hcmd += DST_IP;
     hcmd += F("\",80");
     espSerial.println(hcmd);
     Serial.println(hcmd);
     if(espSerial.find("Error")) return;

     String vcmd = F("ID=54-55-58-10-01-25"); //23.53"; 
     
     for(int i=0; i<numberOfDevices; i++)
      {
        char buf[18];
        vcmd += "&";
        ConvertSensorAddr(TempDev.DevAddr[i], buf);
        vcmd += buf;
        vcmd += "=";
        vcmd += TempDev.tempC[i];
      }
      vcmd += "&";
      vcmd += F("BART");
      vcmd += "=";
      vcmd += BaroDev.temperature;
      
      vcmd += "&";
      vcmd += F("PR");
      vcmd += "=";
      vcmd += BaroDev.pressure;
      
      vcmd += "&";
      vcmd += F("HUMT");
      vcmd += "=";
      vcmd += HumidityDev.temperature;
      
      vcmd += "&";
      vcmd += F("HUM");
      vcmd += "=";
      vcmd += HumidityDev.humidity;
      
      
    hcmd =  F("POST http://narodmon.ru/post.php HTTP/1.0 \r\n");
    hcmd += F("Host: narodmon.ru\r\n");
    hcmd += F("Content-Type: application/x-www-form-urlencoded \r\n");
    hcmd += F("Content-Length: ");
    hcmd += vcmd.length();
    hcmd += F("\r\n");
    hcmd += F("\r\n");
     
    espSerial.print(F("AT+CIPSEND="));
    espSerial.println(vcmd.length()+hcmd.length());

    if(espSerial.find(">"))
     {
       Serial.print(">");
       espSerial.print(hcmd);
       espSerial.print(vcmd);
       Serial.println(hcmd);
       Serial.println(vcmd);
      
       if(espSerial.find("SEND OK"))Serial.println(F("SEND OK"));
       else if(espSerial.find("ERROR"))Serial.println(F("ERROR"));
     }else 
       {
         Serial.println(F("connect timeout"));
         delay(1000);

         dbgTerminal.println(F("RELOAD..."));
         SetupWiFi();
       }
      espSerial.println(F("AT+CIPCLOSE"));
      lastConnectionTime=millis();
    }
 }

//=====================================SEND

void SendMDdata(String ObjectsMD, String PropertyMD, String ValueMD, String MD_IP)
{ 
    String hcmd = F("AT+CIPSTART=\"TCP\",\"");
    hcmd += MD_IP;
    hcmd += F("\",80");
    espSerial.println(hcmd);
    Serial.println(hcmd);
    if(espSerial.find("Error")) return;
  
  //hcmd =  F("GET /objects/?object=MYMETEO&op=set&p=mytemp&v=22&p=mytemp3&v=76 HTTP/1.1\r\n");
    hcmd =  F("GET /objects/?object=");
    hcmd += ObjectsMD;   //F("MYMETEO");
    hcmd += F("&op=set&p=");
    hcmd += PropertyMD;
    hcmd += F("&v=");
    hcmd += ValueMD;
    hcmd += F(" HTTP/1.1\r\n");
    hcmd += F("Host:");
    hcmd += MD_IP;
    //hcmd += F("Connection: close \r\n");
    hcmd += F("\r\n\r\n");
     
    espSerial.print(F("AT+CIPSEND="));
    espSerial.println(hcmd.length());      

    if(espSerial.find(">"))
     {
       Serial.print(">");
       espSerial.print(hcmd);
       Serial.println(hcmd);
          
       if(espSerial.find("SEND OK"))Serial.println(F("SEND OK"));
       else if(espSerial.find("ERROR"))Serial.println(F("ERROR"));
     }else 
     {
       Serial.println(F("connect timeout"));
       delay(1000);

       dbgTerminal.println(F("RELOAD..."));
       SetupWiFi();
     }
      espSerial.println(F("AT+CIPCLOSE"));
}

 //=================================== MojorDomo =============================================================
void SendMojorDomo()
{
  if(millis()-lastConnectionTimeMD > postingIntervalMD)
  {
     String vcmd ; 
     String MojorDomoIP = Config.MDAddr;  // F("192.168.1.125");
     String MojorDomoObj = Config.MDObj;   //F("MYMETEO");
     //String MojorDomoIP = F("192.168.1.125");
     //String MojorDomoObj =F("MYMETEO");
     //Config.senddata=3;
     
     for(int i=0; i<numberOfDevices; i++)
      {
        String tname;
        tname = F("DS18B20_");
        tname += i;
        vcmd = TempDev.tempC[i];
        SendMDdata(MojorDomoObj, tname, vcmd, MojorDomoIP);
        delay(2000);
      }      
        
      vcmd = BaroDev.temperature;
      SendMDdata(MojorDomoObj, F("PressureTemp"), vcmd, MojorDomoIP);
      delay(2000);

      vcmd = BaroDev.pressure;
      SendMDdata(MojorDomoObj, F("Pressure"), vcmd, MojorDomoIP);
      delay(2000);
      
      vcmd = HumidityDev.temperature;
      SendMDdata(MojorDomoObj, F("HumidityTemp"), vcmd, MojorDomoIP);
      delay(2000);
      
      vcmd = HumidityDev.humidity;
      SendMDdata(MojorDomoObj, F("Humidity"), vcmd, MojorDomoIP);
      delay(2000);
      
      vcmd = "1";
      SendMDdata(MojorDomoObj, F("Change"), vcmd, MojorDomoIP);
      lastConnectionTimeMD=millis();
    }
 }

//=====================================================================================================================
void setup() {
  
  pinMode(ESP8266_CHPD, OUTPUT);  
  digitalWrite(ESP8266_CHPD,HIGH);
  Serial.begin(57600);
  espSerial.begin(19200); // ESP8266
  ConfigReadEEPROM(); 
  PrintConfig(); 
  StartTempSensor();
  SetupWiFi();
 // delayInMillis = 750 / (1 << (12 - TEMPERATURE_PRECISION));
//  lastTempRequest = millis();
 // lastLCDPrintTemperature=lastTempRequest;
//  PrintIPParam();
  Serial.print("\n");
  //InitSensorPin();
  //MenuPrintHelp();
 
  Wire.begin();
  //barometer.initializ*e();
  if (!bmp.begin())Serial.println(F("Barometer BMP085 ERROR"));
  dht.begin();
  //lastConnectionTime=millis()-postingInterval;
  MenuPrintHelp();

}


void loop() {
  // put your main code here, to run repeatedly:
  CheckConsole();
  TemperatureGet();
  HumidityRead();
  BarometrRead();
  if(Config.senddata==1)  SendNarodmon();
  else if(Config.senddata==2)
  {
    WorkWEBpage();
    RestartWiFi();
  }else if(Config.senddata==3) SendMojorDomo();
  
//RestartWiFi();
}
