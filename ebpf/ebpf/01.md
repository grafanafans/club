# 如何使用 eBPF 加速云原生应用程序
近年来，eBPF的使用量急剧上升。因为eBPF运行在内核态，且基于事件触发，所以代码的运行速度极快（不涉及上下文切换），更准确。eBPF 被广泛用于内核跟踪（kprobes/tracecing）的可观测性，此外还被用于一些基于IP地址的传统安全监控和访问控制不足的环境（例如，在基于容器的环境，如Kubernetes）中。 我们会在后续文章讨论如何使用ebpf进行k8s的network policy过滤，以及APP无侵入的观测。
![image](https://user-images.githubusercontent.com/41465048/215236999-853d526b-3736-4566-b120-baa481dbc426.png)

图1 BPF 内核钩子  
在图 1 中，可以看到 Linux 内核中的各种钩子，其中可以钩住 eBPF 程序以执行。如在linux-master\tools\include\uapi\linux\bpf.h 可以看到所有 sock_ops相关的监听事件
```
enum {
	BPF_TCP_ESTABLISHED = 1,
	BPF_TCP_SYN_SENT,
	BPF_TCP_SYN_RECV,
	BPF_TCP_FIN_WAIT1,
	BPF_TCP_FIN_WAIT2,
	BPF_TCP_TIME_WAIT,
	BPF_TCP_CLOSE,
	BPF_TCP_CLOSE_WAIT,
	BPF_TCP_LAST_ACK,
	BPF_TCP_LISTEN,
	BPF_TCP_CLOSING,	/* Now a valid state */
	BPF_TCP_NEW_SYN_RECV,
	BPF_TCP_MAX_STATES	/* Leave at the end! */
};
```
# 使用 eBPF 进行网络加速
现在，我们将使用 eBPF 中提供的各种功能深入了解套接字数据重定向的机制。完整的代码[在这里](https://github.com/cyralinc/os-eBPF)。我们提供了有关如何为 eBPF 开发设置 Linux 环境的详细说明。

通常，eBPF程序有两个部分：  

(1)内核空间组件，其中需要根据某些内核事件进行决策或数据收集，例如 NIC 上的数据包 Rx、生成 shell 的系统调用等。  
(2)用户空间组件，可以访问内核代码共享的数据结构（映射等）中写入的数据。  
我们重点解释的代码是内核空间组件，我们使用 bpftool 命令行工具将代码加载到内核中，然后卸载它。

Linux 内核支持不同类型的 eBPF 程序，每个程序都可以附加到内核中可用的特定钩子（参见图 1）。当与这些钩子关联的事件被触发时，这些程序将执行，例如，进行诸如setsockopt()之类的系统调用，进入数据包缓冲区DMA之后的网络驱动程序钩子XDP等。
```
enum bpf_prog_type {
	BPF_PROG_TYPE_UNSPEC,
	BPF_PROG_TYPE_SOCKET_FILTER,
	BPF_PROG_TYPE_KPROBE,
	BPF_PROG_TYPE_SCHED_CLS,
	BPF_PROG_TYPE_SCHED_ACT,
	BPF_PROG_TYPE_TRACEPOINT,
	BPF_PROG_TYPE_XDP,
	BPF_PROG_TYPE_PERF_EVENT,
	BPF_PROG_TYPE_CGROUP_SKB,
	BPF_PROG_TYPE_CGROUP_SOCK,
	BPF_PROG_TYPE_LWT_IN,
	BPF_PROG_TYPE_LWT_OUT,
	BPF_PROG_TYPE_LWT_XMIT,
	BPF_PROG_TYPE_SOCK_OPS,
	BPF_PROG_TYPE_SK_SKB,
	BPF_PROG_TYPE_CGROUP_DEVICE,
	BPF_PROG_TYPE_SK_MSG,
	BPF_PROG_TYPE_RAW_TRACEPOINT,
	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
	BPF_PROG_TYPE_LWT_SEG6LOCAL,
	BPF_PROG_TYPE_LIRC_MODE2,
	BPF_PROG_TYPE_SK_REUSEPORT,
	BPF_PROG_TYPE_FLOW_DISSECTOR,
	BPF_PROG_TYPE_CGROUP_SYSCTL,
	BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE,
	BPF_PROG_TYPE_CGROUP_SOCKOPT,
	BPF_PROG_TYPE_TRACING,
	BPF_PROG_TYPE_STRUCT_OPS,
	BPF_PROG_TYPE_EXT,
	BPF_PROG_TYPE_LSM,
	BPF_PROG_TYPE_SK_LOOKUP,
	BPF_PROG_TYPE_SYSCALL, /* a program that can execute syscalls */
};
```
所有类型都在 UAPI bpf.h 头文件中枚举，其中包含 eBPF 程序所需的面向用户的定义。
在这篇博文中，我们对 BPF_PROG_TYPE_SOCK_OPS 和 BPF_PROG_TYPE_SK_MSG 类型的 eBPF 程序感兴趣，它们允许我们将 BPF 程序连接到套接字操作，例如，当发生 TCP 连接事件时，在套接字的 sendmsg 调用时等。SK_MSG执行套接字数据重定向。

在这篇博文中，我们将用 C 编写 eBPF 代码，并使用 LLVM Clang 前端编译它，以生成 ELF 字节码，注入内核。我们将采用自下而上的方法，深入研究实际执行套接字数据重定向的代码，并向上移动以演练所有代码操作。

![image](https://user-images.githubusercontent.com/41465048/215310087-6a478013-193a-4cfb-b8ef-d36fb1509c75.png)  
 socket重定向的整体流程
## 执行套接字 socket 数据重定向 -- 引入BPF_PROG_TYPE_SK_MSG类型的 eBPF 程序
sk_msg 程序在用户态执行 sendmsg 时执行，但必须附加到套接字映射，特别是 BPF_MAP_TYPE_SOCKMAP 或 BPF_MAP_TYPE_SOCKHASH。这些映射是键值存储，其中主键是五元组信息（下文详述），值只能是套接字。一旦SK_MSG程序和MAP进行绑定，MAP中的所有套接字都将继承SK_MSG程序。本实例中，sk_msg的部分逻辑如下：
```
__section("sk_msg")
int bpf_tcpip_bypass(struct sk_msg_md *msg)
{
 struct sock_key key = {};
 sk_msg_extract4_key(msg, &key);
 msg_redirect_hash(msg, &sock_ops_map, &key, BPF_F_INGRESS);
 return SK_PASS;
}
char ____license[] __section("license") = "GPL";
```
上面的代码使用编译器的section 属性,被放置在 ELF 目标代码的sk_msg部分中。本节告诉加载器有关 BPF 程序类型的信息，该程序类型决定了程序可以在内核中附加的位置以及它可以访问哪些内核帮助程序函数。

msg_redirect_hash() 是 BPF 帮助程序函数 bpf_msg_redirect_hash()的封装函数，因为帮助程序函数不能直接访问，必须通过 BPF_FUNC_msg_redirect_hash 形式的预定义帮助程序访问。 BPF 程序的内核验证程序仅允许从“枚举bpf_func_id”中定义的 UAPI linux/bpf.h 调用这些预定义的帮助程序（请参阅宏定义的代码）。这种间接寻址允许 BPF 后端在看到对全局函数或外部符号的调用时发出错误提示。

msg_redirect_hash() 将指向 sk_msg_md() 的指针SK_MSG作为第一个参数，该程序附加到的 sockhash 映射，将用于索引到映射的键，以及告诉 api 将数据重定向到何处，无论是重定向到 rx 队列还是从 sockhash 检索的套接字的出口 tx 队列。因此，当在附加了此程序的套接字上执行 sendmsg 时，内核将执行此程序。然后 bpf_socket_redirect_hash()使用标识目标套接字并从消息元数据派生的密钥从 sockhash 映射获取对套接字的访问，并根据标志将数据包数据重定向到套接字的相应队列（仅定义 BPF_F_INGRESS 用于 消息入口rx 队列, 0 意味着 转发消息tx 队列）。

## 填充sockmap hash -- 引入 BPF_PROG_TYPE_SOCK_OPS 类型的 eBPF 程序

现在让我们把注意力转向如何填充sk_msg程序使用的 sockhash 映射。我们希望在发生 TCP 连接事件时（但在sk_msg程序运行之前）填充此sockhash，以便在建立连接后可以立即重定向数据。
现在，它是第二种 eBPF 程序类型 SOCK_OPS，它在 TCP 事件（如连接建立、TCP 重传等）时被调用,可以在建立 TCP 连接时捕获套接字详细信息。

在下面的代码片段中，我们创建了这个程序，该程序在套接字操作时触发，并处理主动（源套接字发送 SYN）和被动（目标套接字响应 SYN 的 ACK）TCP 连接的事件。
```
__section("sockops")
int bpf_sockops_v4(struct bpf_sock_ops *skops)
{
 uint32_t family, op;
 family = skops->family;
 op = skops->op;
 switch (op) {
 case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
 case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
  if (family == 2) { //AF_INET
   bpf_sock_ops_ipv4(skops);
  }
  break;
 default:
  break;
 }
 return 0;
}
char ____license[] __section("license") ="GPL";
int _version __section("version") = 1;
```
eBPF sockops 程序驻留在 ELF 部分 sockops 中，因此代码使用编译器部分属性放置在 sockops 部分中。bpf_sock_ops结构中的套接字操作枚举值用于处理 TCP 事件以及标识事件源。当 TCP 连接事件发生时，我们调用 SK_MSG 程序的以下方法来访问 BPF sockhash 映射数据结构：
```
static inline
void bpf_sock_ops_ipv4(struct bpf_sock_ops *skops)
{
 struct sock_key key = {};
 sk_extractv4_key(skops, &key);
 int ret = sock_hash_update(skops, &sock_ops_map, &key, BPF_NOEXIST);
 printk("<<< ipv4 op = %d, port %d --> %d ",
  skops->op, skops->local_port, bpf_ntohl(skops->remote_port));
 if (ret != 0) {
  printk("FAILED: sock_hash_update ret: %d ", ret);
 }
}
```
在上面的代码中，我们使用 sock_hash_update()封装函数访问 BPF 帮助程序函数 bpf_sock_hash_update()。创建 TCP 事件的套接字的引用存储在 sockhash 映射数据结构sock_ops_map中。定义如下：
```
struct sock_key {
        uint32_t sip4;
        uint32_t dip4;
        uint8_t  family;
        uint32_t sport;
        uint32_t dport;
} __attribute__((packed));

struct bpf_map_def __section("maps") sock_ops_map = {
        .type           = BPF_MAP_TYPE_SOCKHASH,
        .key_size       = sizeof(struct sock_key),
        .value_size     = sizeof(int),
        .max_entries    = 65535,
        .map_flags      = 0,
};
```
其中的 sock_key 作为 sock_ops_map 的key,
因此，对于同一主机上的两个应用程序之间完全建立的 TCP 连接，我们存储源套接字（客户端）以及目标套接字（服务端）的key和value，key对应五元组信息，value就是socket。  
![image](https://user-images.githubusercontent.com/41465048/215237607-e5b4cc2a-b9a5-4a51-bc32-6d74a4e5e520.png)  
  图2 BPF_MAP_TYPE_SOCKHASH 存储的数据示意图
## SK_MSG 程序引用 sockops 中的套接字
```
static inline
void sk_msg_extract4_key(struct sk_msg_md *msg,
        struct sock_key *key)
{
        key->sip4 = msg->remote_ip4;
        key->dip4 = msg->local_ip4;
        key->family = 1;

        key->dport = (bpf_htonl(msg->local_port) >> 16);
        key->sport = FORCE_READ(msg->remote_port) >> 16;
}
```

现在，上面描述的SK_MSG程序可以通过 sk_msg_extract4_key 将源ip和目的ip互换，源端口和目的端口互换，这样就得到了对端的五元组信息，并以此信息作为key，通过 msg_redirect_hash 重定向到新的套接字。  
![image](https://user-images.githubusercontent.com/41465048/215309940-385beef3-99a6-4238-8e5f-602bb356857f.png)    
   图3 socket转换示意图
 
总结起来就是：sockops负责记录客户端和服务端的socket映射信息，填充到sock_ops_map；sk_msg引用sock_ops_map查找对端socket，进行转发。
## 在内核中注入 eBPF
在这篇博客中，我们将使用（非常有用！）bpftool命令行工具将BPF字节码注入内核。它是一个用户空间调试实用程序，还可以加载 eBPF 程序、创建和操作映射以及收集有关 eBPF 程序和映射的信息。它是 Linux 内核树的一部分，可在 tools/bpf/bpftool 中找到。

要在内核中注入 eBPF 代码，我们需要在内核中为 eBPF VM 生成字节码。我们将使用 LLVM Clang 前端编译SOCK_OPS并SK_MSG代码来获取目标代码：
```
clang -O2 -g -target bpf -I/usr/include/linux/ -I/usr/src/linux-source-5.15.0/linux-source-5.15.0/include
-c bpf_sockops_v4.c -o bpf_sockops_v4.o
clang -O2 -g -target bpf -I/usr/include/linux/ -I/usr/src/linux-source-5.15.0/linux-source-5.15.0/include
-c bpf_tcpip_bypass.c -o bpf_tcpip_bypass.o
```
让我们加载并附加 SOCK_OPS 程序的 BPF 目标代码。注意/usr/src/linux-source-5.15.0/是运行ubuntu环境下载的linux内核。
```
sudo bpftool prog load bpf_sockops_v4.o "/sys/fs/bpf/bpf_sockops" type sockops
```
这会在内核中加载目标代码（尽管尚未附加到钩子）
加载的代码被固定到 BPF 虚拟文件系统以进行持久性，以便我们获得程序的句柄，以便稍后用于访问加载的程序。（挂载 BPF 虚拟文件系统使用： mount -t bpf bpf /sys/fs/bpf/）
默认情况下，bpftool 将创建新映射，如正在加载的 ELF 对象中声明的那样（在本例中为 sock_ops_map）
```
sudo bpftool cgroup attach "/sys/fs/cgroup/" sock_ops pinned "/sys/fs/bpf/bpf_sockops"
```
这会将加载的SOCK_OPS程序附加到 cgroup
它附加到 cgroup 上，以便程序适用于放置在 cgroup 中的所有任务的所有套接字。
现在目标代码已经加载并附加到 SOCK_OPS 钩子上，我们需要找到 SOCK_OPS 程序使用的 map 的 id，该 id 可用于附加 SK_MSG 程序：
```
MAP_ID=$(sudo bpftool prog show pinned "/sys/fs/bpf/bpf_sockops" | grep -o -E 'map_ids [0-9]+'| cut -d '' -f2-)
sudo bpftool map pin id $MAP_ID "/sys/fs/bpf/sock_ops_map"
```
接下来，让我们加载并附加 SK_MSG 程序的 BPF 目标代码。
```
sudo bpftool prog load bpf_tcpip_bypass.o "/sys/fs/bpf/bpf_tcpip_bypass" map name sock_ops_map pinned "/sys/fs/bpf/sock_ops_map"
```
这会在内核中加载SK_MSG对象代码，将其固定到 /sys/fs/bpf/bpf_tcpip_bypass 的虚拟文件系统，并重用固定在 /sys/fs/bpf/sock_ops_map 的现有映射sock_ops_map。
```
sudo bpftool prog attach pinned "/sys/fs/bpf/bpf_tcpip_bypass" msg_verdict pinned "/sys/fs/bpf/sock_ops_map"
```
这会将加载的SK_MSG程序附加到固定在虚拟文件系统中的 sockhash 映射sock_ops_map，因此现在映射中的任何套接字条目都将执行SK_MSG程序
一旦程序被加载并附加到它们各自的钩子上，我们就可以使用 bpftool 列出加载的程序：
```
sudo bpftool prog show
Output:
15: sock_ops name bpf_sockops_v4 tag 73add043b61539db gpl
loaded_at 2020-04-06T11:31:08-0700 uid 0
19: sk_msg name bpf_tcpip_bypass tag 550f6d3cfcae2157 gpl
loaded_at 2020-04-06T11:31:08-0700 uid 0
```
我们还可以获取 sockhash map及其内容的详细信息（-p 代表 prettyprint）：
```
sudo bpftool -p map show id 13
Output:
{
 "id": 13,
 "type":"sockhash",
 "name":"sock_ops_map",
 "flags": 0,
 "bytes_key": 24,
 "bytes_value": 4,
 "max_entries": 65535,
 "bytes_memlock": 0,
 "frozen": 0
}
sudo bpftool -p map dump id 13
Output:
[{
  "key":
["0x7f", "0x00", "0x00", "0x01", "0x7f", "0x00", "0x00", "0x01", "0x01", "0x00", "0x00", "0x00", "0x00", "0x00", "0x00", "0x00", "0x03", "0xe8", "0x00", "0x00", "0xa1", "0x86", "0x00", "0x00"
  ],
  "value": {
   "error":"Operation not supported"
  }
 },{
  "key":
["0x7f", "0x00", "0x00", "0x01", "0x7f", "0x00", "0x00", "0x01", "0x01", "0x00", "0x00", "0x00", "0x00", "0x00","0x00", "0x00", "0xa1", "0x86", "0x00", "0x00", "0x03", "0xe8", "0x00", "0x00"
  ],
  "value": {
   "error":"Operation not supported"
  }
 }
]
```
显示错误消息是因为 sockhash 映射不支持从用户空间查找其值。

## 功能验证
### socket链接时打印建链信息
为了快速验证 sockhash 映射被填充并被SK_MSG程序查找的数据路径，我们可以使用 SOCAT 启动 TCP 侦听器并使用 netcat 发送 TCP 连接请求。SOCK_OPS程序在获得TCP连接事件时将打印日志，可以从内核的跟踪文件中查找trace_pipe：
```
# start a TCP listener at port 1004, and echo back the received data
sudo socat TCP4-LISTEN:1004,fork exec:cat
# connect to the local TCP listener at port 1004
nc localhost 1004
sudo cat /sys/kernel/debug/tracing/trace_pipe
Output:
nc-212660  [001] d...1 43063.771047: bpf_trace_printk: <<< ipv4 op = 4, port 57642 --> 1004 //客户端主动请求链接
nc-212660  [001] d.s11 43063.771058: bpf_trace_printk: <<< ipv4 op = 5, port 1004 --> 57642  //客户端收到服务端的链接信息

```
### socket 转发时，绕过内核（源代码功能）
```
nc-212660  [001] d...1 43912.157053: bpf_trace_printk: #1 bpf_tcpip_bypass before:src port:57642,dst port:1004
nc-212660  [001] d...1 43912.157081: bpf_trace_printk: #2 bpf_tcpip_bypass transfer:src port:1004,dst port:57642
socat-212661  [000] d...1 43912.157445: bpf_trace_printk: #1 bpf_tcpip_bypass before:src port:1004,dst port:57642
socat-212661  [000] d...1 43912.157447: bpf_trace_printk: #2 bpf_tcpip_bypass transfer:src port:57642,dst port:1004
 ```
经过源代码的 sk_msg_extract4_key 处理，从 57642->1004的转发socket，找到了1004->57642的socket并将消息挂载到该socket的处理队列，省去了原有L4层以下的网络协议栈的的转发（如图3所示）。
原文对socket转发绕过tcp/ip内核底层协议栈做了多种性能对比， 其中  
![image](https://user-images.githubusercontent.com/41465048/215301683-07d8e515-7443-498a-88f7-4b675db18191.png)      
图 4 吞吐量：TCP/IP（禁用 Nagle 算法）与使用 eBPF 绕过 TCP/IP 

### 模拟socket 建链（转发）时，直接DROP特定端口的链接
修改 bpf_tcpip_bypass 函数，假设增加对端口1000的拒绝访问。这里为了验证功能，没有指明ingress的label，实际k8s配置policy时，会指定ingress的label，match的入口请求才会drop。
```
int bpf_tcpip_bypass(struct sk_msg_md *msg)
{
 struct sock_key key = {};
 int dstport=bpf_ntohl((msg->remote_port) >> 16)>>16;
 //增加对端口1000的拒绝访问
 if ( dstport == 1000 )
 {
  printk("#bpf_tcpip_bypass sk_drop:src port:%d,dst port:%d\n",msg->local_port,dstport);
  return SK_DROP;
 }
 printk("#1 bpf_tcpip_bypass before:src port:%u,dst port:%u\n",msg->local_port,bpf_ntohl((msg->remote_port) >> 16)>>16);
 sk_msg_extract4_key(msg, &key);
 int srcport=bpf_ntohl(key.sport)>>16;
 printk("#2 bpf_tcpip_bypass transfer:src port:%u,dst port:%u\n",srcport,bpf_ntohl(key.dport)>>16);
 msg_redirect_hash(msg, &sock_ops_map, &key, BPF_F_INGRESS);
 return SK_PASS;
}
```
客户端尝试发送内容
```
nc localhost 1000
123
```
看到客户端输出如下信息：
```
nc-219949  [001] dN..1 44810.171008: bpf_trace_printk: <<< ipv4 op = 4, port 40046 --> 1000
nc-219949  [001] dNs11 44810.171022: bpf_trace_printk: <<< ipv4 op = 5, port 1000 --> 40046
nc-219949  [001] d...1 44823.289448: bpf_trace_printk: #bpf_tcpip_bypass sk_drop:src port:40046,dst port:1000
```
在客户端侧，对目的端口1000进行了drop处理，      
![image](https://user-images.githubusercontent.com/41465048/215309962-41012953-7e72-4640-9167-44df138fc5ef.png)      
图 5 请求目标端口1000的请求被drop

## 卸载 eBPF 程序
```
sudo bpftool prog detach pinned "/sys/fs/bpf/bpf_tcpip_bypass" msg_verdict pinned "/sys/fs/bpf/sock_ops_map" sudo rm "/sys/fs/bpf/bpf_tcpip_bypass"
```
这会将SK_MSG程序与 sockhash 映射分离，并将其从虚拟文件系统中取消固定，从而导致由于引用计数而对其进行清理。
```
sudo bpftool cgroup detach "/sys/fs/cgroup/unified/" sock_ops pinned "/sys/fs/bpf/bpf_sockops_v4"
sudo rm "/sys/fs/bpf/bpf_sockops"
```
这会将SOCK_OPS程序与 cgroup 分离，并将其从虚拟文件系统中取消固定，从而导致由于引用向下计数而对其进行清理。
```
sudo rm "/sys/fs/bpf/sock_ops_map"
```
这将删除sock_ops_map
