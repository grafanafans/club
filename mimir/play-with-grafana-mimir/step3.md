# Mimir 速体验(Part 3)： 通过 runtime 配置实现租户细粒度管理

在前面两篇文章中，我们已经搭建了 Mimir 集群，并通过 Grafna Agent 实现多租户数据抓取，今天再来讲讲如何通过 Mimir 的 runtime 配置对租户进行细粒度管理。

## 为什么需要租户细粒度管理

我们都知道 Mimir 的多租户属于软隔离，即同一时间不同租户的数据会共享集群的硬件资源，而一个 Mimir 集群其写入/查询吞吐量能力往往也是有限的。

所以我们需要针对不同数据库（租户数据）做细粒度管理，其目的主要有两个：

- 限流：针对不同数据库配置不同的写入/查询 QPS 限制，避免不同数据库之间资源竞争带来的影响。
- 功能性配置：主要包括数据库分片大小、数据存储最大保留时间、乱序数据写入时间范围等，这些配置不仅影响数据库的功能和性能，还能有效降低数据存储成本。

## Mimir runtime 配置实现原理

- 首先在 Mimir 的全局配置中，新增了一个 runtime config，用于租户级别管理：

```go

# grafana/mimir/pkg/mimir/mimir.go#L88

type Config struct {
  ...
  RuntimeConfig       runtimeconfig.Config                       `yaml:"runtime_config"`
  ...
}
```

- runtimeconfig.Config 结构体如下：

```go

# grafana/dskit/runtimeconfig/manager.go#L30

type Config struct {
	ReloadPeriod time.Duration `yaml:"period" category:"advanced"`
	// LoadPath contains the path to the runtime config files.
	// Requires a non-empty value
	LoadPath flagext.StringSliceCSV `yaml:"file"`
	Loader   Loader                 `yaml:"-"`
}
```

- runtime_config.runtimeConfigValues 结构体如下：

```go

# grafana/mimir/runtime_config.go#L29

type runtimeConfigValues struct {
	TenantLimits map[string]*validation.Limits `yaml:"overrides"`

	Multi kv.MultiRuntimeConfig `yaml:"multi_kv_config"`

	IngesterChunkStreaming *bool `yaml:"ingester_stream_chunks_when_using_blocks"`

	IngesterLimits *ingester.InstanceLimits `yaml:"ingester_limits"`
}
```

这里我们主要关注 `overrides` 字段，所有配置项请参考源代码 [validation/limits.go#L68](https://github.com/grafana/mimir/blob/main/pkg/util/validation/limits.go#L68)。

所以我们根据 Mimir 配置代码，很容易得出以下两点：

1. 租户 runtime 配置模版如下：

```yaml
# mimir.yml

runtime_config:
  file: /etc/overrides.yaml
```

```yaml
# overrides.yaml
overrides:
  tenant1:
    ingestion_rate: 50000
  tenant2:
    ingestion_rate: 75000
```

2. Mimir 的租户 `overrides` 配置只能通过指定的本地文件进行覆盖。

## 实战练习

下面我们就在上一篇文章基础上，进行配置修改实现 `tenant2` 租户的配置覆盖。

### 配置更新

- 修改 config/mimir.yaml 文件，添加 `runtime_config` 配置：

```
# config/mimir.yaml
runtime_config:
  file: /etc/overrides.yaml
```

- 添加 config/overrides.yaml 文件，内容如下：

```
overrides:
  tenant2:
    ingestion_rate: 50000
    max_queriers_per_tenant: 1000
    compactor_split_and_merge_shards: 2
    compactor_split_groups: 2
    compactor_blocks_retention_period: 30d
```

这里我们主要 `tenant2` 配置了如下限制：

1. samples 写入 QPS 和 查询 QPS 限制。
2. 压缩分片大小。
3. 数据存储周期。

更多配置字段说明请参考 [reference-configuration-parameters/#limits](https://grafana.com/docs/mimir/latest/operators-guide/configuring/reference-configuration-parameters/#limits)。

- 最后修改 docker-compose.yml 文件，将 `./config/overrides.yaml:/etc/overrides.yaml` 添加都 mimir 实例的 volumes 中：

```
mimir-1:
    ....
    volumes:
      - ./config/mimir.yaml:/etc/mimir.yaml
      -  ./config/overrides.yaml:/etc/overrides.yaml
      - ./config/alertmanager-fallback-config.yaml:/etc/alertmanager-fallback-config.yaml
      - mimir-1-data:/data
```

备注：mimir-2/3 修改相同。

### 查看效果

当执行 `docker-compose down && docker-compose up -d` 命令后，通过 `http://localhost:9009/runtime_config?mode=diff` 链接可以查看到最新的覆盖配置。

![image](https://user-images.githubusercontent.com/1459834/181286367-d4035b5b-a43a-4c9e-81db-c4479c184c8a.png)

可以看到此内容和我们配置文件中的一致，说明最新配置已经生效。

## 总结

本文我们主要通过 Mimir 的 runtime 配置，实现单租户默认限制配置的覆盖，但因为当前 Mimir 针对租户限制的配置只支持本地文件定时扫描的加载方式，这可能会在实际使用中带来一点小麻烦。不过可以考虑通过中心式的配置服务来实现，希望Mimir 未来可以支持。 

到此【Mimir 速体验】系列文章已经全部完成，接下来我们将开始一个全新的系列 --【为什么选择 Mimir】，主要通过源码走读的方式，深刻理解 Mimir 一些实现细节，敬请期待。 

## 整个系列文章

- Mimir 速体验(Part 1)： 使用 docker-compose 一键部署单体版集群
- Mimir 速体验(Part 2)： 使用 Grafana agent 实现多租户数据抓取
- Mimir 速体验(Part 3)： 通过 runtime 配置实现租户细粒度管理
