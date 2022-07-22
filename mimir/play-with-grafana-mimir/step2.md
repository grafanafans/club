# Mimir 速体验(Part 2)： 使用 Grafana agent 实现多租户数据抓取

本篇文章属于 Mimir 速体验系列第二篇，如果你还没阅读第一篇，请点击 xxx。

我们都知道 Mimir 很大一个卖点是支持多租户，它的多租户管理非常方便，而在上一篇文章中我们已经使用 Prometheus 的 remote write 将数据推送到 `demo` 的租户中，那多租户数据的抓取又该如何实现呢？

这里我们先来了解下 Mimir 的多租户数据写入和 Prometheus remote write 的局限。

## Prometheus remote write 的局限

Mimir 支持 Prometheus 的 remote write 协议，而 Prometheus remote write 其实是一个 HTTP 请求，只需通过配置注入 `X-Scope-OrgID` 的请求头就可以将多租户的数据推送到 Mimir，这个可以参考上一篇文章中的 prometheus.yml 的配置文件：

```yaml
remote_write:
  - url: http://load-balancer:9009/api/v1/push
# Add X-Scope-OrgID header so that Mimir knows what tenant the remote write data should be stored in.
# In this case, our tenant is "demo"
    headers:
      X-Scope-OrgID: demo
```

但这有个问题，因为 Promethes 的 remote write 属于全局配置，一个 Prometheus 实例只允许一个 `remote_write` 配置项；虽然它支持多个后端存储服务地址，但抓取的同一指标数据数据会发送给多个后端地址，无法做到不同指标配置不同 `X-Scope-OrgID` 请求信息来区分不同租户数据。

针对这个问题，您可能想到的解决办法有：

1. 起多个 Prometheus 进程，每个 Prometheus 只抓取一个租户的数据。
2. Fork Prometheus 代码，给抓取job 添加一个 `__tenant__ ` 的标签，然后修改 remote write 逻辑，根据不同 `__tenant__` 标签进行分组和分批发送给 Mimir，并用该标签的 value 作为 `X-Scope-OrgID ` value 信息。

这两个办法，目前都有一定的问题：

1. 每个租户部署单独的 Prometheus 会导致 Prometheus 个数增多，增加运维管理难度，也造成资源使用不均的问题。
2. 如果 fork 代码，需要您对 Prometheus 代码有足够了解，而且开发周期也相对较长。

## Grafana agent 的解法

针对以上问题，Grafana 官方给出的答案是采用 [Grafana agent](https://grafana.com/docs/agent/latest/)。

Grafana agent 是 Grafana 官方推出时的统一数据采集器，它不仅可以抓取时序数据，也能采集日志、trace 以及各种常见中间件指标，属于 all in one 方案，当然它天生支持多租户。

这里我们重点关注时序数据采集，它的配置和 Prometheus 的 scraper 部分很像，下面我们就来看看它是如何支持多租户的。

Grafana agent 时序数据抓取配置如下：

```yaml
metrics:
  wal_directory: /tmp/grafana-agent/wal

  configs:
    - name: tenant1
      scrape_configs:
        .....
      remote_write:
        - url: http://localhost:9009/api/v1/push
          headers:
            X-Scope-OrgID: tenant1
            
    - name: tenant2
      scrape_configs:
        .....
      remote_write:
        - url: http://localhost:9009/api/v1/push
          headers:
            X-Scope-OrgID: tenant2
```

配置说明：

1. 通过 metrics 的 configs 配置多个不同名的抓取配置。
2. 每个抓取配置项都可以配置不同的 remote_write，从而指定不同的 `X-Scope-OrgID` 值。

## 实战练习

下面我们就在第一篇文章的 docker-compose.yml 基础上进行修改，将 Prometheus 替换为 Grafana Agent, 并抓取 `promrandom` 服务指标，并以 `tenant2` 租户将数据推送给 Mimir， 整个部署架构如下：

下面我们就来看下实际步骤。

### 1、制作 promrandom 镜像

- 下载 `client_golang/examples/random `代码, 并编译 linux 版本可执行程序。

```
git clone git@github.com:prometheus/client_golang.git
cd client_golang/examples/random
GOGC=0 GOOS=linux go build -o random main.go
```

此时在 random 目录下,我们可以看到刚编译的 `random` 可执行程序。

- 新建 Dockerfile 文件，内容如下：

```
# Dockerfile
FROM alpine:3.16

COPY random /bin/random

WORKDIR /random

USER       nobody
EXPOSE     9090
ENTRYPOINT [ "/bin/random" ]
CMD        [ "-listen-address=:8080"]
```

- 打包并推送到 docker hub

```
docker build -t songjiaynag/promrandom:v0.1.0 .
docker push songjiaynag/promrandom:v0.1.0
```

### 2、修改 docker-compose.yml 文件

- 新增 promrandom 服务

```
promrandom:
  image: songjiayang/promrandom:v0.1.0
  hostname: promrandom
```

- 删除 Prometheus 配置并新增 Grafana Agent 服务

```yaml
# docker-compose.yml

agent:
  image: grafana/agent:latest
  volumes:
    - ./config/agent.yaml:/etc/agent-config/agent.yaml
  entrypoint:
    - /bin/agent
    - -config.file=/etc/agent-config/agent.yaml
    - -enable-features=integrations-next
    - -config.expand-env
    - -config.enable-read-api
  ports:
    - "12345:12345"
  depends_on:
    - "mimir-1"
    - "mimir-2"
    - "mimir-3"
    - "promrandom"
    
```

新增 config/agent.yaml 配置，内容如下：

```yaml
metrics:
  wal_directory: /tmp/grafana-agent/wal
  
  global:
    scrape_interval: 5s
    external_labels:
      cluster: demo
      namespace: demo
      
  configs:
    - name: demo/mimir
      scrape_configs:
        - job_name: demo/mimir
          static_configs:
            - targets: [ 'mimir-1:8080' ]
              labels:
                pod: 'mimir-1'
            - targets: [ 'mimir-2:8080' ]
              labels:
                pod: 'mimir-2'
            - targets: [ 'mimir-3:8080' ]
              labels:
                pod: 'mimir-3'
      remote_write:
        - url: http://load-balancer:9009/api/v1/push
          headers:
            X-Scope-OrgID: demo
            
    - name: demo/promrandom
      scrape_configs:
        - job_name: promrandom
          static_configs:
            - targets: ["promrandom:8080"]
      remote_write:
        - url: http://load-balancer:9009/api/v1/push
          headers:
            X-Scope-OrgID: tenant2
```

Agent 配置说明：

1.  所有 metrics 都添加了 `cluster: demo; namespace: demo` 全局标签信息。
2. `demo/mimir` 完全替换以前 prometheus.yml 配置。
3. `demo/promrandom` 属于新增的 promrandom 抓取配置，并以 `tenant2` 租户信息进行推送。

更新 config/grafana-provisioning-datasources.yaml 配置，新增 `tenant2` 数据源

```yaml
# config/grafana-provisioning-datasources.yaml
  - name: Tenant2
    type: prometheus
    access: proxy
    orgId: 1
    url: http://load-balancer:9009/prometheus
    version: 1
    editable: true
    jsonData:
      httpHeaderName1: 'X-Scope-OrgID'
      alertmanagerUid: 'alertmanager'
    secureJsonData:
      httpHeaderValue1: 'tenant2'
```

当完成以上相关配置更新，执行 `docker-compose down && docker-compose up -d` 重启服务, 然后再执行 `docker-compose ps` 我们就可以看到新增了一个 Agent 容器，而原始的 Prometheus 容器已经不见了。

![ps.png](https://user-images.githubusercontent.com/1459834/179392549-bea40c60-a9a7-4c7e-8eec-8e7944623997.png)

### 3、配置 promrandom 看板

重新新打开 Grafana ，可以看到新增了一个 Tenant2 的数据源：

![tenant2-datasoruce.png](https://user-images.githubusercontent.com/1459834/179392929-9d91b066-d231-4fba-a478-1bbe83d457b3.png)


根据此数据源，我们容易创建 random 服务的看板：

![random-dashbord-1.png](https://user-images.githubusercontent.com/1459834/179392986-ae79636c-9447-4dbd-aa30-85520bded286.png)

![random-service](https://user-images.githubusercontent.com/1459834/179393065-2330ad5d-65ac-4a81-af81-37fae7f38a92.png)

此时我们还可以通过管理员视角，查看 tenant 数据库相关监控数据：

![tenant2-mimir](https://user-images.githubusercontent.com/1459834/179393130-cabc360a-6fca-4857-b94c-ea9ec0082286.png)

## 总结

我们通过对 docker-compose.yml 部署文件的改造，新增 promrandom 服务，然后使用 Grafana Agent 替换原有 Prometheus，实现多租户数据的抓取和远程写入，最后我们再通过 Grafana 看板查看 promrandom 服务的相关数据。

## 整个系列文章

- Mimir 速体验(Part 1)： 使用 docker-compose 一键部署单体版集群
- Mimir 速体验(Part 2)： 使用 Grafana agent 实现多租户数据抓取
- Mimir 速体验(Part 3)： 通过 runtime 配置实现租户细粒度管理
