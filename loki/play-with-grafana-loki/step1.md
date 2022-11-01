# 安装

本文内容主要来自https://github.com/grafana/loki/tree/main/examples/getting-started  
如果为了验证、测试等功能，可以使用 Docker 或者 Docker Compose部署。生产环境，建议直接使用Tanka或者Helm部署。  
本篇分享Docker部署方式，后续再分享Helm部署方式。  
Loki的运行依赖于2个核心组件：  
Promtail：负责pull抓取日志，并且将内容push到loki服务器。  
loki:进行具体的日志处理。  
其中loki的数据流转如下：
![image](https://user-images.githubusercontent.com/41465048/179923958-e8f0fa25-a780-4a3e-bda6-1f2ddd0cd2d5.png)  

在任意目录下，创建loki_exam目录，然后执行  
```
wget https://raw.githubusercontent.com/grafana/loki/main/examples/getting-started/loki-config.yaml -O loki-config.yaml
wget https://raw.githubusercontent.com/grafana/loki/main/examples/getting-started/promtail-local-config.yaml -O promtail-local-config.yaml
wget https://raw.githubusercontent.com/grafana/loki/main/examples/getting-started/docker-compose.yaml -O docker-compose.yaml
```
如果下载有问题，直接访问上面链接拷贝出来文件即可。  
下载后在当前目录下执行，
```
docker-compose up -d
```
# 使用grafana查看日志信息  
打开grafana面板，http://localhost:3000 配置loki后端地址，  
![image](https://user-images.githubusercontent.com/41465048/179924716-6d31b4d1-42da-4537-8ca8-beddf903d2ef.png)  
本环境同时部署了mimir单体模式集群，搜索mimir中的ingester关键词，可以看到返回日志信息如下：
![image](https://user-images.githubusercontent.com/41465048/179924845-b2defd82-567a-4028-95e3-5dda327667ca.png)

上述查询使用的是Builder模式，也可以使用Code模式，使用Logql语句查询：
```
{container="play-with-grafana-mimir-mimir-1-1"} |= `ingester`
```
# minio 查看日志存储
![image](https://user-images.githubusercontent.com/41465048/179925538-bc494321-d013-402f-9997-5e4164ba8493.png)

# 直接将log push到loki
可以不依赖promtail,参考api接口（https://grafana.com/docs/loki/latest/api/ 直接将日志内容push到loki。
这里直接发送一个curl请求:  
```
curl -v -H "Content-Type: application/json" -H 'X-Scope-OrgID:tenant1' -XPOST -s "http://localhost:3100/loki/api/v1/push" --data '{"streams": [{ "stream": { "foo": "bar3" }, "values": [ [ "1658284798000000000", "fizzbuzz" ] ] }]}'
```
注意在多租户时，需要携带“X-Scope-OrgID”参数。
发送完成后，在grafana查看日志结果，
![image](https://user-images.githubusercontent.com/41465048/179925777-c8b5ccc5-67ce-4578-ae66-d0f4b621c641.png)

# 后续计划
1、多租户部署  
2、helm部署  
3、loki&mimir联动观察  