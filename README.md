# GrafanaFans

GrafanaFans 是由南京多位 GrafanaLabs 产品重度使用者一起发起的 Grafana 开源产品学习兴趣小组，联合发起人为 Johnson, Jupiter, Filos，致力于 GrafanaLabs 相关技术栈在国内的应用和普及，并定期分享各种技术文章和应用实践。

更多请参考 [GrafanaFans 宣言](/About.md)。

# 学习路线

![path.png](/images/learn.jpeg)

# 索引

- Grafana
  - 基础知识
    - 可观测性白皮书
    - 可观测性策略
  - 诞生背景
  - 快速体验
  - 为什么选择
- Mimir
  - 诞生背景
    - [一文带你了解 Grafana 最新开源项目 Mimir 的前世今生](/mimir/basic/from.md)
    - [Grafana CEO 访谈，为什么要用 Mimir](/mimir/basic/why.md)
  - 快速体验
    - [Mimir 速体验(Part 1)： 使用 docker-compose 一键部署单体版集群](/mimir/play-with-grafana-mimir/step1.md)
    - [Mimir 速体验(Part 2)： 使用 Grafana agent 实现多租户数据抓取](/mimir/play-with-grafana-mimir/step2.md)
    - [Mimir 速体验(Part 3)： 通过 runtime 配置实现租户细粒度管理](/mimir/play-with-grafana-mimir/step3.md)
    - [Mimir 速体验(Part 4): 使用 HATracker 实现 Prometheus 数据抓取高可靠](/mimir/usecase/hatracker.md)
  - 为什么选择
    - [Mimir 源码分析（一）：海量series chunk 同时落盘带来的挑战](/mimir/why-billion-series/01-chunk-queue.md)
    - [Mimir 源码分析（二）：效率爆棚的分片压缩](/mimir/why-billion-series/02-%E6%95%88%E7%8E%87%E7%88%86%E6%A3%9A%E7%9A%84%E7%BA%B5%E5%90%91%E5%8E%8B%E7%BC%A9.md)
    - [Mimir 源码分析（三）：任意时间范围乱序数据写入](/mimir/ooostore/design.md)
    
- Loki
  - 诞生背景
  - 快速体验
    - [Loki 速体验(Part 1)：docker-compose 本地部署](/loki/play-with-grafana-loki/step1.md)
  - 为什么选择
- Tempo
  - 诞生背景
  - 快速体验
  - 为什么选择
- eBPF
  - [如何使用 eBPF 加速云原生应用程序](/ebpf/ebpf/01.md)
- 基于 SLO 告警
  - [基于SLO告警（part 1）：基础](/slo/basic.md)
  - [基于SLO告警（part 2）：为什么使用 MWMR 方法](/slo/mwmb.md)
  - [基于SLO告警（part 3）：开源项目 sloth 使用](/slo/sloth.md)
- 实战练习
  - [基于 Grafana LGTM 可观测性平台的快速构建](/lgtm/demo.md)
  - [Arduino IoT 玩家也可以轻松实现数据可观测](/iot-demos/mqttclient/README.md)

# 联系我们

- 微信公号： grafanafans
![qrocode_weixin](/images/qrcode_weixin.jpeg)
- B站: [grafanafans](https://space.bilibili.com/108263255)
