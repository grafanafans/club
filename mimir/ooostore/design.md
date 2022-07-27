# 什么是out-of-order?什么是 out-of-bound?
Prometheus数据写入有两个特性:
1. 同一个series要求数据写入不可以乱序(即：时间相对靠前的数据需要先写入，时间靠后的数据后写入)，若乱序到达的数据则会拒绝写入，这种错误称为out of order
2. 同一个tsdb中的不同series 写入时要求必须再同一个时间窗口（block range）内，否则会拒绝写入，假设一个tsdb的block range为2h，其中最新的sample a写入数据的时间戳为7：03，这时再内存中的block存储的时间范围6：00-7：03(block会进行时间对齐)，但是如果这时写入一个5：50的sample b数据则会被拒绝写入，这种错误称为out of bound

在本文中统称为OOO

# 什么情况下会发生OOO
1. 由于一些用户行为，例如要对数据进行数据聚合，或者iot设备会上传离线数据等场景，使得产生的数据源本身就是out-of-bound 和out-of-order的
2. 当从多个targets采集数据时，其中一些target节点由于网络或者其他原因导致写入Prometheus的时延在1个小时以上时则会造成out-of-bound
3. 当采集的数据通过Kafka等消息队列中间件转发时，由于不同消费者的消费速度不一致则会造成out-of-bound 和out-of-order

# 如何解决？
1. 引入外部引擎，在尝试写入Prometheus，发生错误时，将数据通过[Remote Write](https://github.com/prometheus/prometheus/blob/main/documentation/examples/remote_storage/remote_storage_adapter/influxdb/client.go) 方式写入到外部引擎（例如：inluxDB），利用外部引擎的特性存储
    这种方案的缺陷：依赖于外部数据库的性能；labels数据会存多份
2. 修改Prometheus的底层存储引擎。 目前改修改方案在mimir_prometheus项目中实现，尚未同步到prometheus库中
后续将主要结合Mimir（2.2.0版本已实现）的实现展示第二种修改方式。

## 写入的设计
原先顺序存储的chunk保持原样，在此基础上增加新的ooo chunk，作为乱序数据的写入块，在head保存为
```goland
	oooMmappedChunks []*mmappedChunk    // Immutable chunks on disk containing OOO samples.
	oooHeadChunk     *oooHeadChunk      // Most recent chunk for ooo samples in memory that's still being built.
	firstOOOChunkID  chunks.HeadChunkID // HeadOOOChunkID for oooMmappedChunks[0]
```
其结构体定义为
```go
type sample struct {
	t int64
	v float64
}

type OOOChunk struct {
	samples []sample
}
```
在每一ooo chunk中 samples都是排序好的，其插入算法如下：
```go
func (o *OOOChunk) Insert(t int64, v float64) bool {
	// find index of sample we should replace
	i := sort.Search(len(o.samples), func(i int) bool { return o.samples[i].t >= t })

	if i >= len(o.samples) {
		// none found. append it at the end
		o.samples = append(o.samples, sample{t, v})
		return true
	}

	if o.samples[i].t == t {
		return false
	}

	// expand length by 1 to make room. use a zero sample, we will overwrite it anyway
	o.samples = append(o.samples, sample{})
	copy(o.samples[i+1:], o.samples[i:])
	o.samples[i] = sample{t, v}

	return true
}
```
而不同的ooo chunk之间可能会存在range的重叠，下图是一个示例
![写入示例图片](image/1.jpg)

一旦chunk中的samples个数大于oooCapMax定义的值，则将其转换为XOR编码的chunk，通过mmap映射到磁盘上。
其中meta的定义如下，通过不同的字段来标注顺序块和乱序块的属性：
```go
type Meta struct {
	Ref   ChunkRef
	Chunk chunkenc.Chunk
	MinTime, MaxTime int64
	
	OOOLastRef                     ChunkRef
	OOOLastMinTime, OOOLastMaxTime int64
}
```

# 查询的设计
每次查询进入时，我们都需要检查所有块并计算哪些块具有给定查询间隔的样本，一旦找到它们，我们需要生成一个逻辑chunk，这里值得注意的一点是，在开始查询之后生成的chunk块则不会被查询到。其核心代码如下
```go
func (s *memSeries) oooMergedChunk(meta chunks.Meta, cdm chunkDiskMapper, mint, maxt int64) (chunk *mergedOOOChunks, err error) {
	...
	for i, c := range s.oooMmappedChunks {
		chunkRef := chunks.ChunkRef(chunks.NewHeadChunkRef(s.ref, s.oooHeadChunkID(i)))
		// We can skip chunks that came in later than the last known OOOLastRef
		if chunkRef > meta.OOOLastRef {
			break
		}

		if chunkRef == meta.OOOLastRef {
			tmpChks = append(tmpChks, chunkMetaAndChunkDiskMapperRef{
				meta: chunks.Meta{
					MinTime: meta.OOOLastMinTime,
					MaxTime: meta.OOOLastMaxTime,
					Ref:     chunkRef,
				},
				ref:      c.ref,
				origMinT: c.minTime,
				origMaxT: c.maxTime,
			})
		} else if c.OverlapsClosedInterval(mint, maxt) {
			tmpChks = append(tmpChks, chunkMetaAndChunkDiskMapperRef{
				meta: chunks.Meta{
					MinTime: c.minTime,
					MaxTime: c.maxTime,
					Ref:     chunkRef,
				},
				ref: c.ref,
			})
		}
	}
	...
}
```
以下是一个查询的例子，假设[0,600)的范围内存在0，1，2，3等4个相互重叠的chunk块，在查询时，则生存一个逻辑块包含所有以上4个重叠块的数据。
![查询示例图片](image/3.jpg)


query查询时，聚合顺序块和乱序块，其核心代码如下
```go
func (db *DB) Querier(_ context.Context, mint, maxt int64) (storage.Querier, error) {
	...
	if inOrderHeadQuerier != nil {
		blockQueriers = append(blockQueriers, inOrderHeadQuerier)
	}
	if outOfOrderHeadQuerier != nil {
		blockQueriers = append(blockQueriers, outOfOrderHeadQuerier)
	}
	return storage.NewMergeQuerier(blockQueriers, nil, storage.ChainedSeriesMerge), nil
}
```

# compact设计
由于在head中可能存在跨多个range的block，因此为了重用Prometheus的compact功能，即新产生的block不会和多个现存的block重叠，因此会将ooo samples拆分成若干个block
如下所示，落盘[1h,5h)的chunks 则会生成[1h,2h),[2h,4h),[4h,5h)范围的3个blocks，
其压缩流程如下：
![压缩示例图片](image/4.jpg)
1. 先对内存中chunk进行m-map映射
2. 计算ooo series的range范围并存储
3. 保存ooo series的posting数据
4. 截断WBL
5. 生成新的blocks

前4步代码如下：
```go
func NewOOOCompactionHead(head *Head) (*OOOCompactionHead, error) {
	newWBLFile, err := head.wbl.NextSegment()
	if err != nil {
		return nil, err
	}

	ch := &OOOCompactionHead{
		chunkRange:  head.chunkRange.Load(),
		mint:        math.MaxInt64,
		maxt:        math.MinInt64,
		lastWBLFile: newWBLFile,
	}

	ch.oooIR = NewOOOHeadIndexReader(head, math.MinInt64, math.MaxInt64)
	n, v := index.AllPostingsKey()

	// TODO: verify this gets only ooo samples.
	p, err := ch.oooIR.Postings(n, v)
	if err != nil {
		return nil, err
	}
	p = ch.oooIR.SortedPostings(p)

	var lastSeq, lastOff int
	for p.Next() {
		seriesRef := p.At()
		ms := head.series.getByID(chunks.HeadSeriesRef(seriesRef))
		if ms == nil {
			continue
		}

		// M-map the in-memory chunk and keep track of the last one.
		// Also build the block ranges -> series map.
		// TODO: consider having a lock specifically for ooo data.
		ms.Lock()

		mmapRef := ms.mmapCurrentOOOHeadChunk(head.chunkDiskMapper)
		if mmapRef == 0 && len(ms.oooMmappedChunks) > 0 {
			// Nothing was m-mapped. So take the mmapRef from the existing slice if it exists.
			mmapRef = ms.oooMmappedChunks[len(ms.oooMmappedChunks)-1].ref
		}
		seq, off := mmapRef.Unpack()
		if seq > lastSeq || (seq == lastSeq && off > lastOff) {
			ch.lastMmapRef, lastSeq, lastOff = mmapRef, seq, off
		}
		if len(ms.oooMmappedChunks) > 0 {
			ch.postings = append(ch.postings, seriesRef)
			for _, c := range ms.oooMmappedChunks {
				if c.minTime < ch.mint {
					ch.mint = c.minTime
				}
				if c.maxTime > ch.maxt {
					ch.maxt = c.maxTime
				}
			}
		}
		ms.Unlock()
	}

	return ch, nil
}
```
第5步的代码如下
```go
func (c *LeveledCompactor) compactOOO(dest string, oooHead *OOOCompactionHead, shardCount uint64) (_ [][]ulid.ULID, err error) {
	...
	outBlocks := make([][]shardedBlock, 0)
	outBlocksTime := ulid.Now() // Make all out blocks share the same timestamp in the ULID.
	blockSize := oooHead.ChunkRange()
	oooHeadMint, oooHeadMaxt := oooHead.MinTime(), oooHead.MaxTime()
	ulids := make([][]ulid.ULID, 0)
	for t := blockSize * (oooHeadMint / blockSize); t <= oooHeadMaxt; t = t + blockSize {
		mint, maxt := t, t+blockSize

		outBlocks = append(outBlocks, make([]shardedBlock, shardCount))
		ulids = append(ulids, make([]ulid.ULID, shardCount))
		ix := len(outBlocks) - 1

		for jx := range outBlocks[ix] {
			uid := ulid.MustNew(outBlocksTime, rand.Reader)
			meta := &BlockMeta{
				ULID:       uid,
				MinTime:    mint,
				MaxTime:    maxt,
				OutOfOrder: true,
			}
			meta.Compaction.Level = 1
			meta.Compaction.Sources = []ulid.ULID{uid}

			outBlocks[ix][jx] = shardedBlock{
				meta: meta,
			}
			ulids[ix][jx] = meta.ULID
		}
		err := c.write(dest, outBlocks[ix], oooHead.CloneForTimeRange(mint, maxt-1))
		
	...
}
```
生成的block如下，值的注意的是：对比老版的Prometheus多了一个out_of_order字段，用于标注block的类型。
```json
{
	"ulid": "01G8TQ2WQ72C79WTHPWRESZN7V",
	"minTime": 1658714400000,
	"maxTime": 1658721600000,
	"stats": {
		"numSamples": 300,
		"numSeries": 1,
		"numChunks": 5
	},
	"compaction": {
		"level": 1,
		"sources": [
			"01G8TQ2WQ72C79WTHPWRESZN7V"
		]
	},
	"version": 1,
	"out_of_order": true
}
```
在同一range内的多个顺序block和乱序block会被压缩成一个block(通过vertical_compact功能)。

# 配置项
- storage.tsdb.out_of_order_time_window 用于配置多久以前的数据可以添加到tsdb中，单位是毫秒。
