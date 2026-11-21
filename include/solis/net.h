#ifndef SOLIS_NET_H
#define SOLIS_NET_H

#include <solis/types.h>

#define NET_IP_LEN 4
#define NET_MAC_LEN 6

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP  0x0800

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define HTTP_PORT 80
#define HTTPS_PORT 443

int  net_init(void);
int  net_configure_dhcp(void);
int  net_available(void);
const char *net_get_device_name(void);
int  net_wifi_adapter_present(void);
void net_flush_rx(void);
void net_get_ip(uint8_t *ip);
void net_get_gateway(uint8_t *gw);
void net_get_dns(uint8_t *ip);
int  net_is_local_ip(const uint8_t *ip);

int net_arp_resolve(const uint8_t *target_ip, uint8_t *target_mac);

int net_ping(const uint8_t *ip, uint32_t timeout_ms);
int net_tcp_syn_test(const uint8_t *ip, uint16_t port, uint32_t timeout_ms);

/* Minimal TCP stream used by the kernel TLS transport. */
struct net_tcp_conn {
    uint8_t ip[4];
    uint16_t port;
    uint16_t local_port;
    uint32_t seq;
    uint32_t ack;
    int fin;
    uint8_t rx_buf[2048];
    int rx_len;
    int rx_off;
};

int net_tcp_open(struct net_tcp_conn *conn, const uint8_t *ip,
                 uint16_t port, uint32_t timeout_ticks);
int net_tcp_write(struct net_tcp_conn *conn, const void *data, int len);
int net_tcp_read(struct net_tcp_conn *conn, void *data, int max_size,
                 uint32_t timeout_ticks);

int net_http_get(const uint8_t *ip, uint16_t port,
                 const char *host, const char *path,
                 uint8_t *response, uint32_t max_size);

int net_https_get(const uint8_t *ip, uint16_t port,
                  const char *host, const char *path,
                  uint8_t *response, uint32_t max_size);

int net_dns_resolve(const char *hostname, uint8_t *ip_out);

#endif
