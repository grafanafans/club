#include <ESP8266HTTPClient.h>

#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "config.h"

//PromLokiTransport transport;
//WiFiClient wificlient;
//PromClient client(transport);
void init_wifi();

WiFiClient wificlient; 
WiFiClientSecure httpsclient;
HTTPClient hc;

int loopCounter = 0;


ModbusMaster node; // instantiate ModbusMaster object
//SoftwareSerial Serialsoft(D7, D8); // 建立SoftwareSerial对象，RX引脚D7, TX引脚D8
SoftwareSerial Serialsoft;
void init_wifi(){
  Serial.println("连接中...");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
   Serial.println(WiFi.localIP()); // 显示WIFI地址
   httpsclient.setInsecure();
}
void setup() {
  init_wifi();
  //pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(9600);    // esp8266 default serial, this should not be modified
   // BTserial.begin(9600);  // 当烧录程序时，会对其他uart端口有依赖，所以这个阶段即使打开softserial，会影响启动。建议放到loop阶段

 //   SomeSerial someSerial(&Serialsoft);
//    someSerial.begin(9600);

}
void sendserver(String data) {
  Serial.println("sendserver,data:");
  Serial.println(data);
  String url = Backend;
  //String data = "pst=temperature";
    hc.begin(httpsclient,url);
    hc.addHeader("Content-Type", "application/json");
    hc.addHeader("Host", "io.telemetrytower.com");
    hc.addHeader("Content-Length",String(data.length()));
    hc.addHeader("Auth",Backendtoken));
   // hc.addHeader("Content-Length", data.length());

  
    int httpCode = hc.POST(data);
    if (httpCode == HTTP_CODE_OK)
    {
        String responsePayload = hc.getString();
        Serial.println("Server Response Payload: ");
        Serial.println(responsePayload);
    }
    else
    {
        Serial.println("Server Respose Code:");
        Serial.println(httpCode);
    }
    
    hc.end();
    //delay(2000);
}
static uint32_t i,start=0;
void loop() {
	uint8_t j,result;   
	int slaveid = 1;
	uint16_t data[6];
	if(start == 0 )
	{
	  delay(60000);    //一定要延时足够时间，让程序烧录完。然后再启动softserial
	} else
	{
	  delay(5000);
	}  
	if (start == 0 || result == node.ku8MBResponseTimedOut )  // 226 means modbus disconnect
	{
		//Serial.swap();
		delay(3000);    
		Serialsoft.begin(9600,SWSERIAL_8N1,D7,D8);   //启动softserial
		node.begin(slaveid, Serialsoft);  // 链接slaveid=1的串口
		start = 1;
	}

	result =  node.readHoldingRegisters(0x0000,2);               //读取寄存器地址0的数据，长度为2
	delay(1000); //读取延时
	if (result != node.ku8MBSuccess)
	{
		Serial.println(result);
	} 
	else {
		Serial.println("ok");
		int respo1 = node.getResponseBuffer(0);
		Serial.println(respo1);     
		int respo2 = node.getResponseBuffer(1);
		Serial.println(respo2); 

    //String messageString ="{wendu:" + String(t_DHT11) + "," + "shidu:" + String(h_DHT11) + "}";
    String messageString = "{";

    // if you want to specify your sensor info (without datas), please add 'label_xxx' tag, short as 'lab',like 'lab_ip','lab_hostname','lab_location'
    messageString +="\"lab_ip\": \"127.0.0.2\"" ;

    messageString +=",\"lab_sourcetype\": \"modbus\"" ;
    // sensor datas part
    // if you want to extend sensor data, extend this part
    //"modbusslave1"
    messageString += ",\"modbusslave1\": " ; 
    messageString += String(respo1);  

    messageString += ",\"modbusslave2\": " ; 
    messageString += String(respo2);  
    messageString += "}"; 
    sendserver(messageString);

	}
	node.clearResponseBuffer();  // 清除缓冲区
  
}

