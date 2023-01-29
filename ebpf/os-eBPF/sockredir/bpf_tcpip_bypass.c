#include <uapi/linux/bpf.h>

#include "bpf_sockops.h"


/* extract the key that identifies the destination socket in the sock_ops_map */
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

__section("sk_msg")
int bpf_tcpip_bypass(struct sk_msg_md *msg)
{
    struct  sock_key key = {};
    int dstport=bpf_ntohl((msg->remote_port) >> 16)>>16;
     //增加对端口1000的拒绝访问
     if ( dstport == 1000 )
      {
          printk("#bpf_tcpip_bypass sk_drop:src port:%d,dst port:%d\n",msg->local_port,msg->remote_port);
          return SK_DROP;
     }
 
    printk("#1 bpf_tcpip_bypass before:src port:%u,dst port:%u\n",msg->local_port,bpf_ntohl((msg->remote_port) >> 16)>>16);
    sk_msg_extract4_key(msg, &key);
    int srcport=bpf_ntohl(key.sport)>>16;
    printk("#2 bpf_tcpip_bypass transfer:src port:%u,dst port:%u\n",srcport,bpf_ntohl(key.dport)>>16);


    msg_redirect_hash(msg, &sock_ops_map, &key, BPF_F_INGRESS);
    return SK_PASS;
}

char ____license[] __section("license") = "GPL";
