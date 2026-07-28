#ifndef NYX_NET_H
#define NYX_NET_H

#include <nyx/types.h>

#define NET_IP_LEN 4
#define NET_MAC_LEN 6

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP  0x0800

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define HTTP_PORT 80

int  net_init(void);
int  net_available(void);
void net_flush_rx(void);
void net_get_ip(uint8_t *ip);
void net_get_gateway(uint8_t *gw);
int  net_is_local_ip(const uint8_t *ip);

int net_arp_resolve(const uint8_t *target_ip, uint8_t *target_mac);

int net_ping(const uint8_t *ip, uint32_t timeout_ms);
int net_tcp_syn_test(const uint8_t *ip, uint16_t port, uint32_t timeout_ms);

int net_http_get(const uint8_t *ip, uint16_t port,
                 const char *host, const char *path,
                 uint8_t *response, uint32_t max_size);

int net_dns_resolve(const char *hostname, uint8_t *ip_out);

#endif
