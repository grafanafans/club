#include <Arduino.h>
#include <ESP8266WiFi.h>  // for wifi
#include <PubSubClient.h>  // for mqtt

#include "DHT.h"   // for dht11 sensor
#define DHTPIN 13     
#define DHTTYPE DHT11  

// wifi info
const char *SSID = "xx";
const char *PASSWORD = "xx";


// mqtt info
const char *MQTT_SERVER = "xx";
const int MQTT_PROT = 1883;

// mqtt 主题
const char *MQTT_TOPIC_ONLINE = "xapi/home/online";
const char *MQTT_TOPIC_UPDATE = "xapi/home/update";
const char *CLIENT_ID = "esp8266-xx";

DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);

void init_wifi();
void mqtt_reconnect();
void mqtt_msg_callback(char *topic, byte *payload, unsigned int length);
void wb_update(); // temper send

float h_DHT11 = 0; // 湿度
float t_DHT11 = 0; // 温度

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(9600);
    init_wifi();
    client.setServer(MQTT_SERVER, MQTT_PROT);
    client.setCallback(mqtt_msg_callback);
    dht.begin();
}

void loop() {
if(!client.connected()){
    mqtt_reconnect();
  }
  client.loop();
  wb_update();
}

void init_wifi(){
  Serial.println("connecting...");
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

    if(client.connect(CLIENT_ID)){
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

	//String messageString ="{wendu:" + String(t_DHT11) + "," + "shidu:" + String(h_DHT11) + "}";
	String messageString = "{\"shidu\": " ;
	messageString += String(h_DHT11);  
	messageString += ",\"wendu\": " ; 
	messageString += String(t_DHT11);  
	messageString += "\}"; 


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