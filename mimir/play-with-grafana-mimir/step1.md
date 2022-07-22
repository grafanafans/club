# Mimir 速体验(Part 1)： 使用 docker-compose 一键部署单体版集群

## 前言

前面两篇文章我们对 Mimir 产生的背景，它的核心卖点有了一个大概的了解，今天我们开始一个全新系列-【Mimir 速体验】，即通过 docker-compose 搭建 Mimir 单体版集群的方式，来深入掌握它的用法。

本系列文章一共有三篇，这篇文章属于该系列第一篇，主要讲解内容包括：

- 如何使用一键部署 Mimir 单体版集群。
- 整体部署架构详细讲解。
- Mimir/Grafana 管控台尝鲜。

说明，本系列版本依赖如下： 

- Docker Compose version v2.6.1
- Mimir Version v2.1

## 一键部署单体版集群

Mimir 的部署形式分为两种，单体模式（所有服务组件都在一个进程中启动）和微服务模式，因为本系列属于本地测试和功能速体验，所以我们选择单体部署方式即可，如果生产环境更推荐以微服务部署方式。

### 1、下载 Mimir 部署配置文件

```
git clone https://github.com/grafana/mimir.git
cd mimir/docs/sources/tutorials/play-with-grafana-mimir/
```

切换到该目录，我们将看到 docker-compose.yml 文件以及各服务依赖配置，其中 Mimir 单体部署配置如下：

![config.png](https://user-images.githubusercontent.com/1459834/179004867-5ad23760-83c4-4a46-acc9-b9c96cfb7cd2.png)

### 2、使用 docker-compose 命令启动

```
docker-compose up -d
```

如果顺利，此时你使用 `docker ps`  命令可以看到 Mimir 容器有 3 个, Prometheus、Grafana、Nginx、Minio 容器各一个，如图：

![docker-ps](https://user-images.githubusercontent.com/1459834/178768452-6d9b47ea-2b6e-469a-b022-4765b1a89bf7.jpeg)

## 部署架构说明

参考 docker-compose.yml 文件，我们很容易画出如下部署架构图：

![xx](https://user-images.githubusercontent.com/1459834/178768925-86242db5-9257-4804-901b-7f2335da22f2.jpeg)

部署架构详细说明：

1. 使用 Mimir 单体模式，批量部署了三个实例，分别命名为 Mimir1～3。
2. 部署一个 Nginx 容器，作为三个 Mimir 实例的接入服务（LB）。
3. 启动一个 Prometheus 容器， 抓取部署的三个 Mimir 实例的 metrics 数据，并标记为`demo` 租户身份，以 `remote write` 方式推送给 Mimir。
4. 使用 Mimio 搭建本地对象存储，用于 Mimir Block 的存储。
5. 启动一个 Grafana 容器，通过配置 `demo` 租户数据源，从而读取刚 Prometheus 抓取的数据。

## 各服务管理界面

下面我们再访问各个服务的管控界面，掌握这套部署架构各服务的访问方式。

### 1. Mimir 管理界面

通过访问 `http://localhost:9009`，我们可以打开 Mimir 的管理界面：

![mimir-dashaboard.jpg](https://user-images.githubusercontent.com/1459834/178769461-abde39c0-2293-4583-9a03-f94cdec25d18.jpeg)

可以看到 Mimir 提供了一个简要的管理控制台，通过这个控制台，我们可以查看各模块服务运行状况，以及租户的配置详情。

### 2. Grafana 管理界面

因为在 docker-compose.yml 配置文件中，针对 Grafana 已经做了默认数据源、看板等相关配置，所以当我们访问 `http://localhost:9000`， 打开 Grafafa 的管理界面的时候，不需要密码，你就可以看到很多关于 Mimir 和租户管理相关的看板。

这些看板非常有用，在未来实际使用中，我们可能重度依赖这些监控数据，时刻了解系统的运行状况。

![mimir-dashboard](https://user-images.githubusercontent.com/1459834/178769997-8a9d1015-a081-4434-aac9-8fc5afaeb8db.jpeg)

此时我们点开任意一个看板，即可看到我们刚通过 Prometheus 采集的数据，例如：

![采集数据](https://user-images.githubusercontent.com/1459834/178774924-c84b4704-2cce-4a86-9a39-3f35989241c7.jpeg)

### 3. Minio 管理界面

因为默认 docker-compose.yml 配置中，没有对 Minio 做端口映射，但我们可以尝试使用 `docker inspect play-with-grafana-mimir-minio-1` 获取其内网 ip 地址，然后加上 `9000` 端口进行访问。

通过配置的默认账号 `mimir/supersecret` 登录管理控制台，你可以查看到 Mimir 帮我们创建的一些 buckets，如图：

![minio](https://user-images.githubusercontent.com/1459834/178773464-d816b003-10b6-440e-80e1-d64d1353f09a.jpeg)

这里我们主要关注 `mimir-blocks` 这个 bucket，后面我们会用它来查看新写入、分级、分片压缩后的 Block 的 metadata 信息，这个在后续系列文章中会重点讲解。

## 总结

本篇文章属于【Mimir 速体验】 系列文章第一篇， 在本篇文章中，我们主要利用 Mimir 官方提供的模板，使用 docker-compose 命令一键部署 Mimir 单体模式集群，并通过访问相关服务管理界面，加深了从 Prometheus 数据抓取，远程写入 Mimir，再到 Grafana 看板查看数据整个过程的理解。

另外两篇系列文章将在后面陆续补充，敬请期待。

## 整个系列文章

1. Mimir 速体验(Part 1)： 使用 docker-compose 一键部署单体版集群
2. Mimir 速体验(Part 2)： 使用 Grafana agent 实现多租户数据抓取
3. Mimir 速体验(Part 3)： 通过 runtime 配置实现租户细粒度管理
