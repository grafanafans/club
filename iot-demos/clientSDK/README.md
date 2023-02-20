# mqtt 对接cortex/mimir示例

本示例主要演示从 mqtt 订阅消息，并将传感器上报的遥测数据经过数据转化后，写到cortex/mimir实时数据库中进行存储。

## 代码运行

- 修改config.yaml，：

```
backend: "https://xxx/api/v1/push"  //  cortex/mimir url
backendtoken: "eyJhbGciOiJIUzUxMiIsInR5cCI6IkpXVCJ9.eyJ0ZW5hbnRfaWQiOiJlc3A4MjY2IiwidmVyc2lvbiI6MX0" 后端token
mqtt_broker: "tcp://mqtt.xxx:1883"  mqtt服务器地址
mqtt_username: "mqtt"
mqtt_passwd: "xxxx"   
mqtt_topic: "/home/id/sensordata"  //订阅的主题
```

- 修改以下代码，替换为自己的 cortex/mimir的X-Scope-OrgID：

```
_, err := promclient.Write(ctx, req, promwrite.WriteHeaders(map[string]string{"X-Scope-OrgID": "abcdefxxx"}))
```



- http接入post请求时的处理函数。注意与端侧请求api保持一致即可。
```
/easyapi/v1/push
```

- 运行代码：

```
chmod +x build.sh & ./build.sh
```