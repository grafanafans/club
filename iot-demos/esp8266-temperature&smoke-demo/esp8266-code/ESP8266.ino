#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "DHT.h"
#define DHTPIN 13     
#define DHTTYPE DHT11  
#define MQ2 12   
int Gas_analog = A0;    // used for ESP8266
int Gas_digital = D6;   // used for ESP8266
// wifi info

const char *SSID = "iphone";
const char *PASSWORD = "150";


// mqtt info
const char *MQTT_SERVER = "mqtt.xxx";
const int MQTT_PROT = 1883;

// mqtt 主题
const char *MQTT_TOPIC_ONLINE = "xapi/home/online";
const char *MQTT_TOPIC_UPDATE = "/home/id/sensordata";
const char *CLIENT_ID = "esp8266-testuser1";   //用户id，尽量修改
const char* mqttUserName = "mqtt";         // 服务端连接用户名(需要修改)
const char* mqttPassword = "123456";          // 服务端连接密码(需要修改)

DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);

void init_wifi();
void mqtt_reconnect();
void mqtt_msg_callback(char *topic, byte *payload, unsigned int length);
void wb_update(); // 温湿度 发送

float h_DHT11 = 0; // 湿度
float t_DHT11 = 0; // 温度

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(9600);
    init_wifi();
    client.setServer(MQTT_SERVER, MQTT_PROT);
    client.setCallback(mqtt_msg_callback);
    dht.begin();
    pinMode(Gas_digital, INPUT);

}

void loop() {
if(!client.connected()){
    mqtt_reconnect();
  }
  client.loop();
  wb_update();
}

void init_wifi(){
  Serial.println("连接中...");
  Serial.println(SSID);

  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
}

void mqtt_reconnect(){
  while (!client.connected())
  {
    Serial.print("正在尝试MQTT连接");

    if(client.connect(CLIENT_ID,mqttUserName,mqttPassword)){

      Serial.println("已连接");
      client.publish(MQTT_TOPIC_ONLINE, "online"); // 发布
    }else{
      Serial.print("错误, rc");
      Serial.print(client.state());
      Serial.println("等待 5s");
      delay(5000);
    }
  }
}

void mqtt_msg_callback(char *topic, byte *payload, unsigned int length){
  Serial.print("Message arrived [");
  Serial.print(topic); // 打印主题信息
  Serial.print("] ");
}

void wb_update(){
  digitalWrite(LED_BUILTIN, LOW);
  delay(2000);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(2000);
  if(client.connected()){
    h_DHT11 = dht.readHumidity();
    t_DHT11 = dht.readTemperature();
    if (isnan(h_DHT11) || isnan(t_DHT11)) {
        h_DHT11 = 0;
        t_DHT11 = 0;
        Serial.println(F("Failed to read from DHT sensor!"));
        return;
    }

	//smoke detector mq-2
	//int mq=analogRead(MQ2);
	int gassensorDigital = digitalRead(Gas_digital);
	int gassensorAnalog = analogRead(Gas_analog);
	Serial.printf("smoke,d:");
	Serial.print(gassensorDigital);
	Serial.printf("smoke,a:");
	Serial.print(gassensorAnalog);

    //String messageString ="{wendu:" + String(t_DHT11) + "," + "shidu:" + String(h_DHT11) + "}";
  String messageString = "{";

  // if you want to specify your sensor info (without datas), please add 'label_xxx' tag, short as 'lab',like 'lab_ip','lab_hostname','lab_location'
  messageString +="\"lab_ip\": \"127.0.0.1\"" ;

  // sensor datas part
  // if you want to extend sensor data, extend this part
  //"shidu"
  messageString += ",\"shidu\": " ; 
  messageString += String(h_DHT11);  

  // "wendu"
  messageString += ",\"wendu\": " ; 
  messageString += String(t_DHT11);  

  // "smoke"
  messageString += ",\"smoke\": " ; 
  messageString += String(gassensorDigital);  

  messageString += "}"; 


    char publishMsg[messageString.length() + 1];   
    strcpy(publishMsg, messageString.c_str());
    
    Serial.print(F("湿度: "));
    Serial.print(h_DHT11);

    Serial.print(F("%  温度: "));
    Serial.print(t_DHT11);
    Serial.print(F("°C "));
    Serial.printf("");


    delay(1000);
    client.publish(MQTT_TOPIC_UPDATE, publishMsg);
    delay(500);
  }
}