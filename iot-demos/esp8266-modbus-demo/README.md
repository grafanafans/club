# esp8266读取modbus设备数据
整理流程图：  
![image](https://user-images.githubusercontent.com/41465048/220004955-c8195e25-63ee-46ff-acb1-781457578f3f.png)


# 环境准备
## 使用 SoftwareSerial 启动modbus链接串口
esp8266的默认串口，使用RX0,TX0。同时发现，当再开启软串口 SoftwareSerial 时，RX和TX分别对应D7,D8。
![image](https://user-images.githubusercontent.com/41465048/217773535-98b49587-4fe7-4b57-a088-bacef3913f0d.png)
如下代码：
```
    Serialsoft.begin(9600,SWSERIAL_8N1,D7,D8);   //启动softserial
```
在setup阶段，RX1和TX1和RX0/TX0会有干扰。
一个现象就是，烧录程序的时候，TX1外接的指示灯会闪，且最后会烧录失败，最终报错“ESP32/8266 timed out waiting for packet header”。
规避方法就是：在loop预留足够的延时，再启动 SoftwareSerial 。因为烧录程序和启动已经不再占用默认串口，所以启动SoftwareSerial已不再受干扰。
## 借助arduino制作一个usb转ttl工具
因为作者没有可用的usb转ttl工具，可以[参考](https://www.electronics-lab.com/three-ways-make-arduino-works-usbttl-converter/)，借助arduino快速制作一个usb转ttl的工具。

## 安装 modbusslave 测试软件
下载破解的测试软件，作为modbus的模拟设备。设置为03模式，工作模式，如下图所示:  
![modbusslave_test_software](https://user-images.githubusercontent.com/41465048/219366575-ecea0457-139a-4e44-8b72-b779a4ad02df.png)

# 读取modbus数据
## 读modbus slave设备数据
esp8266作为modbus的master设备，启动后，
1）使用SoftwareSerial先绑定slave设备串口
```
Serialsoft.begin(9600,SWSERIAL_8N1,D7,D8);   //启动softserial
node.begin(slaveid, Serialsoft);  // 链接slaveid=1的串口
```
2）再读取slave设备寄存器的值，
```
result =  node.readHoldingRegisters(0x0000,2);    //读取寄存器地址0的数据，长度为2
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
}
```
esp8266作为modbus的master设备，电脑模拟软件作为modbus的slave设备，  
![esp8266_readdata_from_modbus](https://user-images.githubusercontent.com/41465048/219366928-e8554009-bd17-46fc-adf2-599a2d984451.png)


## 将数据发送到cortex/mimir
### 方式1、使用http/https client发送数据
配置文件config.h配置token等信息，
```
#define WIFI_SSID     "xxx"
#define WIFI_PASSWORD "xxx"

// For more information on where to get these values see: https://github.com/telemetrytower/play-with-tower/tree/master/iot-demo/esp8266-modbus-demo
#define Backend "https://xxx/easyapi/v1/push"
#define USER "esp8266"
#define Backendtoken "xxxx"  
```
```
void sendserver(String data) {
    Serial.println("sendserver,data:");
    Serial.println(data);
    String url = GC_URL;
    //String data = "pst=temperature";
    hc.begin(httpsclient,url);
    hc.addHeader("Content-Type", "application/json");
    hc.addHeader("Host", "xxx");
    hc.addHeader("Content-Length",String(data.length()));
    hc.addHeader("Auth",Backendtoken));
```
### 方式2、使用mqtt发送数据
[参考](https://github.com/telemetrytower/play-with-tower/tree/master/iot-demo/esp8266-temperature-demo)mqtt章节配置ino文件
### 方式3、使用 grafana 提供的 prometheus 工具
[grafana 源码在这里](https://github.com/grafana/prometheus-arduino/)。
注意，此工具适合esp32等高内存开发板，在esp8266上运行会崩溃，并报如下错误：
```
14:41:34.578 -> --------------- CUT HERE FOR EXCEPTION DECODER --------------
14:41:34.644 -> Exception (3):
14:41:34.676 -> epc1=0x4000df1b epc2=0x00000000 epc3=0x00000000 excvaddr=0x4023ec1d depc=0x00000000
```
## 效果图
![image](https://user-images.githubusercontent.com/41465048/219366411-d7ef685d-12e4-438c-8373-04b65152177a.png)
