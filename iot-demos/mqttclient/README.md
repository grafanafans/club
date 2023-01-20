# mqtt 对接cortex/mimir示例

本示例主要演示从 mqtt 订阅消息，并将传感器上报的遥测数据经过数据转化后，写到cortex/mimir实时数据库中进行存储。

## 代码运行

- 修改以下代码，替换为自己的 emqx broker 和账号信息：

```
opts.AddBroker("emqx.broker.host")
opts.SetUsername("emqx.user.name")
opts.SetPassword("emqx.user.password")
```

- 修改以下代码，替换为自己的 cortex/mimir的X-Scope-OrgID：

```
_, err := promclient.Write(ctx, req, promwrite.WriteHeaders(map[string]string{"X-Scope-OrgID": "abcdefxxx"}))
```

- 修改以下代码，替换为自己的 cortex/mimir url：

```
	promclient = promwrite.NewClient(
		"https://cortex:8004/api/v1/push",
		promwrite.HttpClient(&http.Client{
			Timeout: 30 * time.Second,
		}),
	)
```

- 运行代码：

```
go run mqttclient.go
```