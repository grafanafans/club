Cortex可以被认为是经过大量修改的 Prometheus，有效地将其作为分布式系统运行。它有效地使 Prometheus 水平可扩展、高度可用，并具有可靠的长期数据保留。为了实现这一点，Cortex 添加了处理可扩展存储层（通过 Thanos）和跨多个节点管理配置的功能。
了解过Cortex的同学，都会被它繁多的配置震惊，我们对cortex的写入进行压测，认识一下，这些配置的作用。  
# 集群压测状态图
![image](https://user-images.githubusercontent.com/41465048/200511600-8cba5537-e972-447e-83f6-76138dd12601.png)  
3节点的16C128G容器，target=all,持久化存储到S3。写入14.4M的状态图。每一小时的耗时突增，即我们前期提到的tsdb同步落盘导致的写入阻塞。
# 部署压测方案
## 压测工具avalanche
使用Prometheus社区的metrics压测工具avalanche
https://github.com/prometheus-community/avalanche
压测执行语句如下：
./avalanche --metric-count=30000 --remote-tenant=avalanche --port=9048 --remote-batch-size=3000 --remote-requests-count=10000000 --metric-interval=604800 --series-interval=86400
上述参数表示，单个avalanche进程，产生30000 个不同的指标，默认每个指标有 10 个不同的series，这样每个avalanche可以产生30万series。默认每个series上有 10 个标签，以及每 30 秒更改一次series的值。series-interval表示标签每隔24h更新一次。

## 压测架构
![image](https://user-images.githubusercontent.com/41465048/200511693-3f5fe77f-2cb8-4ec2-8320-049cfa30d983.png)  
每个avalanche会产生稳定的30万series，每16个avalanche为一组scrape的对象，使用Prometheus的agent模式，将数据remote write到Cortex集群。Prometheus 使用Agent模式，只记录WAL文件，不进行数据落盘。
Prometheus 需要关注的配置：
```
queue_config:
  max_samples_per_send: 2000
```
上述配置将抓取的samples进行拆分，减少对后端的网络冲击。
启动参数为：
```
nohup ./prometheus-2.39.1.linux-amd64/prometheus --config.file=config_8c/prometheus_cortex1.yaml --web.listen-address="0.0.0.0:9091"  --enable-feature=agent --storage.agent.path=./data/cortex1 > run1.log 2>&1 &
```
# 一路上遇到的问题
## 问题1：metata、series的限制
err="per-user metric metadata limit of 8000 exceeded, please contact administrator to raise it (local limit: 8000 global limit: 0 actual local limit: 8000)
label元数据个数限制，在runtime.yaml修改配置，
max_metadata_per_user: 8000
改为：max_metadata_per_user: 99999999

err="rpc error: code = Code(400) desc = user=avalanche: per-user series limit of 5000000 exceeded, please contact administrator to raise it (local limit: 5000000 global limit: 0 actual local limit: 5000000)"

修改
max_series_per_user: 99999999
max_series_per_metric: 99999999

## 问题2：节点重启时，状态异常
Distributor 组件进行Ingester数据写入时，收到如下报错：'{"status":"error","errorType":"internal","error":"xx: rpc error: code = Unavailable desc = Starting"}'
节点ring的store后端选择memberlist方式。当节点升级后重启时，节点先加入memberlist，状态为active。
![image](https://user-images.githubusercontent.com/41465048/200512715-20a777f5-ebb8-4d6d-9976-ae507bcd37dc.png)
但是Ingester此时还在读取之前停机时的WAL文件，真正的写入服务还没ready,导致memberlist返回状态为ACTIVE，实际服务不可用。
![image](https://user-images.githubusercontent.com/41465048/200512781-657af67a-357a-473f-8b50-e848c24dd4a6.png)
上图是我们压测16C集群时，15M写入压力下，Ingester节点遇到上述问题时，停机时间大概20min。
此问题在“Send heartbeat during wall replay #4847”得到修复。调整了节点加入memberlist的位置，即在读取完WAL后，再加入memberlist,未读取完WAL文件之前，新启动的节点状态都是INACTIVE的状态。(https://github.com/cortexproject/cortex/pull/4847)

## 问题3：节点重启时，看似脑裂
![image](https://user-images.githubusercontent.com/41465048/200512810-7bcdc5b6-b9c9-4016-93fd-8119fe3b298d.png)  
cortex-1作为主节点，重启后，其他节点没有加入。
解决方法是，修改memberlist节点重新加入集群的探测间隔，这样主节点重启后，子节点也会主动探测主节点状态。 注意default = 0s 表示不探测。
```
# CLI flag: -memberlist.rejoin-interval
[rejoin_interval: <duration> | default = 0s]
```
## 问题4: Distributor & Ingester 的写入队列溢出导致OOM
![image](https://user-images.githubusercontent.com/41465048/200513052-6350c77f-f8c0-416d-9f52-a4d3f1b948c7.png)  
在我们的压测环境中，每天的16:00附近发生一次全量series标签变更，统计到的"In-Memory/Active Series"，"Instance Mem"，"Ingester Inflight push requests"会发生一次冲击。当瞬时压力值较高时，代表写入队列的inflight指标会很高，导致OOM。影响这个队列的配置项为 max_inflight_push_requests。

![image](https://user-images.githubusercontent.com/41465048/200513085-11cc5d48-e207-4e97-a006-054c2ac696c3.png)  
上图看出，在gitlab压测200M数据时，就出现了瞬时压力突增，此时Ingester的inflight达到400k,导致系统OOM。
他们最终设置了参数阈值为20k。
![image](https://user-images.githubusercontent.com/41465048/200513111-03ddb944-5956-4141-b7f8-99523b9de09d.png)  
![image](https://user-images.githubusercontent.com/41465048/200513186-f1603645-f680-4395-931c-ececd2f54315.png)  
需要说明的是，Ingester每次达到阈值时，就会出现500的错误。可以看到，04:00后，系统逐渐稳定。
我们8C32G测试的环境，设置了200。
```
ingester_limits:
  max_inflight_push_requests: 200
```
## 问题5：Ingester 对label多做了一次hash
当series的sample进入时，需要根据label的hash值，快速获取已有ref进行数据写入。当前版本的Push函数，在统计In-Memory/Active Series时，进行了两次hash计算，当百万级series同时写入时，系统CPU耗时较高。火焰图如下：
![image](https://user-images.githubusercontent.com/41465048/200513220-dd2895ff-2298-473c-975a-59c26dbfc0eb.png)  
上图中的ActiveSeries下面的xxhash和右侧的tsdb.seriesHash即是对同一个series进行的两次hash。
我们对这里的逻辑进行了优化，包括使用一个hash，并且采用sync.Pool的方式管理hash时的内存空间。
修改后的一次benchmark数据如下：
![image](https://user-images.githubusercontent.com/41465048/200513238-cb756d73-e291-4df7-bd19-08a8cc46b831.png)  
## 问题6：Ingester 记录series的hash槽需要调整
```
type seriesHashmap map[uint64][]*memSeries
```
seriesHashmap 记录了label set的 hash 信息，这部分需要常驻内存,stripe_size 限制着map的总长度。
```
[stripe_size: <int> | default = 16384] change to 262144
```
默认的16384配置，labels.Equal耗时较多。
![image](https://user-images.githubusercontent.com/41465048/200513270-36bd16f8-a7c7-46da-9b42-a14d41eb4c30.png)  
size越大，range m[hash]的遍历以及labels.Equal的耗时越小，但是内存占用会有增加。此配置需要根据实际series容量进行调整。注意是2的幂次方即可。
```go
func (s *stripeSeries) getByHash(hash uint64, lset labels.Labels) *memSeries {
	i := hash & uint64(s.size-1)

	s.locks[i].RLock()
	series := s.hashes[i].get(hash, lset)
	s.locks[i].RUnlock()

	return series
}
func (m seriesHashmap) get(hash uint64, lset labels.Labels) *memSeries {
	for _, s := range m[hash] {
		if labels.Equal(s.lset, lset) {
			return s
		}
	}
	return nil
}
```


## 其他问题
1）我们还发现了Distributor的snappy.Decode()也可以采用sync.Pool的方式，减少heap的重复申请。  
2）以及Active Series的统计目前是硬编码的hash。  
3）rule使用的一些问题。  
等此类问题我们还在修改验证中。

## 同等规格压测Cortex & Mimir
压测了cortex,mimir在8C32G规格下的写入状态，
![image](https://user-images.githubusercontent.com/41465048/200513327-de83b508-fd2c-4d7c-a1b5-9c0b9560589d.png)  
<center>Cortex 压测数据</center>   
                               
![image](https://user-images.githubusercontent.com/41465048/200513367-ef7aaf6b-c80f-4cc3-8298-c4e1b737064c.png)  
                                Mimir 压测数据  
可以看到Mimir和Cortex的写入性能相当。但是在每小时处还是有一次写入延时。因为两个方案的vendor/Prometheus中虽已经引入了tsdb的异步落盘，但此配置默认都没有打开，如果打开后，写入延时应该都会有改善。
```
	// DefaultWriteQueueSize is the default size of the in-memory queue used before flushing chunks to the disk.
	// A value of 0 completely disables this feature.
	DefaultWriteQueueSize = 0
```

## 总结
通过对cortex的压测，我们有以下体会：
1）cortex的配置繁多，需要根据业务需求，合理配置。根据8C的配置经验，我们也针对16C/32C等规格下的性能做了同步测试，发现随着规格提升，写入性能基本是等倍数增长。
2）cortex和mimir的开源版本，目前写入性能差异不大。但是由于mimir对压缩分片做了扩展，可以预期，进行查询压测时，mimir的性能会更突出。mimir也确实如官宣所述，减少了配置复杂度，提高了运维能力。
3）一些cortex的maintainer提到的性能问题，并没有得到修复。这部分修改在mimir版本中已经合入。

