# 一文带你了解 Grafana 最新开源项目 Mimir 的前世今生

就在前几天（2022/3/29）Grafana 宣布正式对外开源其时序数据库 Mimir，一经开源便在社区引起广泛讨论，不仅 hacknews 上的讨论持续发热，而且 Github 上也迅速收获 1K 关注。

究竟是什么让它保持如此大的关注，是实力还是争议？

## Mimir 是什么

>> Grafana Mimir is an open source, horizontally scalable, highly available, multi-tenant, long-term storage for Prometheus.

>> Mimir 官方是如此介绍

你没有看错，它又是一个时序数据库，又是解决 Prometheus 长期存储、横向扩展、多租户的问题。

虽然它号称完全兼容 Prometheus，但其目标绝不仅仅成为一个更好的 Prometheus， 它给自己的定位是成为可观测性中 metrics 后端存储的终极方案，能够兼容各种 metrics 协议，如图：

![mimir-how-does-it-work.svg](/images/mimir-how-does-it-work.svg)

## Mimir 的卖点

你可能要问，Prometheus 的长期存储社区方案那么多，Thanos、Cortex、 M3、Victoriametrics ，为何又来一个，时序数据库真的这么卷么？

我们先来看看 Mimir 自己宣传的一些卖点：

- 超大规模生产验证（超 20 亿活跃指标）
- 部署简单，支持单体/微服务两种部署方式，依赖少（只有 OSS， Cortex 的依赖你懂得）
- 支持高基数的 metrics 压缩和查询
- 支持同一租户 Block 分片压缩（超过 64G 限制）
- 支持查询和计算下层
- 天然支持多租户，并支持全局跨租户查询
- 兼容所有 metircs 协议（目前主要支持 Prometheus）

那这些宣传的卖点真的值得我们迁移么？这个也是大家在 HN 上讨论的一个点，大致结论如下：

- Cortex 太难用了，如果现在用的是 Cortex 可以尝试升级到 Mimir。
- Thanos 目前工作的很好，除非遇到 高基数/ 大 Block 压缩问题等问题，否则没必要迁移。
- VictoriaMetrics 工作的也很好，需要社区中立添加与 VictoriaMetrics 的对比测试，方便做选择。

针对以上观点，个人普遍认同。

## 与 Cortex 的关系

熟悉 Cortex 的同学收到 Grafana Mimir 开源的消息后，可能和我一样，第一直觉 Grafana 可能要放弃 Cortex 了，毕竟 Mimir 和 Cortex 做的事太像，而且 Cortex 以前的核心维护者大多(4/7)来自 Grafana。

![cortex-maintainers.png](/images/cortex-maintainers.png)

首先 Mimir 确实是基于 Cortex 而来的，你可以理解它为 Cortex 的 2.0 版本，这也不难解释为啥 Mimir 开源的第一个版本就是 v2.0。

至于为什么这么做，我们可以从 HN 的讨论中看出原因：

- Cortex 有较大技术债，很难满足 Mimir 中的一些需求
- 开源协议的更改，更满足 Grafana 公司产品的发展，Cortex 属于 Apache-2.0 License, 而 Mimir 协议为 AGPLv3。

个人觉得协议的更改应该是决定因素，因为 AGPLv3 是目前 Grafana 主要使用的开源协议，可以参考【Grafana 开源协议更改 Q&A】 。

不过好像 CNCF 要求孵化的项目须为 Apache-2.0 License， 那未来 Mimir 如何进入 CNCF 进行孵化值得关注。

Grafana 真的要放弃 Cortex 了吗？

目前官方给的解释是，Cortex 会重新寻找新的维护者，原本的 Grafana 维护者需要将更多精力投入到 Mimir 中。

所以答案是肯定的，个人认为 Grafana 确实是要放弃 Cortex了，但这并不意味 Cortex 就停止维护，这很考验后继维护者。

Cortex 目前存在的价值就是存量用户和它 Apache-2.0 协议，万一哪天 Grafana 又来个协议修改，毕竟一家独大总不是太好。

## 是否值得学习

我个人觉得从架构上来看是值得的，原因如下：

完全分布式、良好伸缩能力和集群管理能力，对于我们通过 Thanos 手动搭建多租户的时序数据库有较好的参考意义。

对于一些卖点功能，比如单租户分片压缩、查询算子下推都可以好好学习（预计未来 Thanos 也会跟进）

## 总结

Mimir 其实可以算作 Cortex 2.0 版本，不过它将开源协议从 Apache-2.0 更新到了 AGPLv3，Grafana 的相关维护者后面会从 Cortex 的维护转移到 Mimir 。

Mimir 整体架构相对 Cortex 简化了不少，外部依赖，部署都好很多，这是一个不错的点，如果你以前用的就是 Cortex，建议参考其【Cortex 迁移指南】进行一键迁移 ，如果本身用的是 Thanos，建议再等等看。

Mimir 是否如它愿景--成为云原生时代可观测 metrics 的统一后端存储终极方案，我们拭目以待。

## 参考链接

1. https://grafana.com/oss/mimir
2. https://news.ycombinator.com/item?id=30854734
3. https://grafana.com/blog/2021/04/20/qa-with-our-ceo-on-relicensing/
4. https://grafana.com/docs/mimir/latest/migration-guide/migrating-from-cortex