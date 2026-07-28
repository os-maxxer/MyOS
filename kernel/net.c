#include <nyx/net.h>
#include <nyx/rtl8139.h>
#include <nyx/pci.h>
#include <nyx/timer.h>
#include <nyx/console.h>
#include <nyx/dbg.h>

#define HTONS(x) __builtin_bswap16(x)
#define HTONL(x) __builtin_bswap32(x)

static uint8_t our_mac[NET_MAC_LEN];
static uint8_t our_ip[NET_IP_LEN];
static uint8_t gateway_ip[NET_IP_LEN];
static uint8_t netmask[NET_IP_LEN];
static uint16_t ip_id_counter = 0;
static int net_ready = 0;

#define PKT_BUF_SIZE 2048

#define ARP_CACHE_SIZE 4
#define ARP_CACHE_TTL 5000
struct arp_cache_entry {
    uint8_t  ip[4];
    uint8_t  mac[6];
    int      valid;
    uint32_t timestamp;
};
static struct arp_cache_entry arp_cache[ARP_CACHE_SIZE];

static uint16_t net_checksum(const void *data, int len) {
    uint32_t sum = 0;
    const uint8_t *bytes = (const uint8_t *)data;
    int i;
    for (i = 0; i < len - 1; i += 2)
        sum += (bytes[i] << 8) | bytes[i + 1];
    if (i < len)
        sum += bytes[i] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

struct eth_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type;
} __attribute__((packed));

struct arp_pkt {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t op;
    uint8_t  sender_mac[6];
    uint8_t  sender_ip[4];
    uint8_t  target_mac[6];
    uint8_t  target_ip[4];
} __attribute__((packed));

struct ip_hdr {
    uint8_t  ver_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src_ip[4];
    uint8_t  dst_ip[4];
} __attribute__((packed));

struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

#define ICMP_TYPE_ECHO_REQ 8
#define ICMP_TYPE_ECHO_REPLY 0

struct tcp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

#define TCP_FLAG_FIN 1
#define TCP_FLAG_SYN 2
#define TCP_FLAG_RST 4
#define TCP_FLAG_PSH 8
#define TCP_FLAG_ACK 16

#define ARP_REQUEST 1
#define ARP_REPLY   2

/* ──────────── HTTP constants ──────────── */

#define HTTP_MAX_REDIRECTS 5
struct {
    uint16_t port;
    uint32_t canary;
} http_src = { 49152, 0xDEADBEEF };

struct http_url {
    char host[64];
    uint16_t port;
    char path[128];
};

struct http_response {
    int status_code;
    int content_length;
    int has_location;
    char location[256];
    int body_offset;
    int is_chunked;
};

static void dbg_print_mac(const uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        dbg_print_hex(mac[i]);
        if (i < 5) dbg_print(":");
    }
}

static void dbg_print_ip(const uint8_t *ip) {
    for (int i = 0; i < 4; i++) {
        dbg_print_dec(ip[i]);
        if (i < 3) dbg_print(".");
    }
}

static int eth_send(const uint8_t *dst_mac, uint16_t type, const uint8_t *data, int len) {
    uint8_t pkt[PKT_BUF_SIZE];
    struct eth_hdr *hdr = (struct eth_hdr *)pkt;
    for (int i = 0; i < 6; i++) hdr->dst[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) hdr->src[i] = our_mac[i];
    hdr->type = HTONS(type);
    for (int i = 0; i < len; i++) pkt[14 + i] = data[i];
    dbg_print("[ETH] TX type=0x");
    dbg_print_hex(type);
    dbg_print(" dst=");
    dbg_print_mac(dst_mac);
    dbg_print(" len=");
    dbg_print_dec(14 + len);
    dbg_print("\n");
    rtl8139_send(pkt, 14 + len);
    return 0;
}

static int eth_recv(uint8_t *src_mac, uint16_t *type, uint8_t *data, int max) {
    uint8_t pkt[PKT_BUF_SIZE];
    int n = rtl8139_recv(pkt, PKT_BUF_SIZE);
    if (n < 14) {
        return 0;
    }
    struct eth_hdr *hdr = (struct eth_hdr *)pkt;
    for (int i = 0; i < 6; i++) src_mac[i] = hdr->src[i];
    *type = HTONS(hdr->type);
    int payload = n - 14;
    if (payload > max) payload = max;
    for (int i = 0; i < payload; i++) data[i] = pkt[14 + i];
    dbg_print("[ETH] RX type=0x");
    dbg_print_hex(*type);
    dbg_print(" src=");
    dbg_print_mac(src_mac);
    dbg_print(" len=");
    dbg_print_dec(n);
    dbg_print("\n");
    return payload;
}

static void arp_cache_add(const uint8_t *ip, const uint8_t *mac) {
    uint32_t now = timer_get_ticks();
    int oldest = 0;
    uint32_t oldest_ts = now;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            oldest = i;
            break;
        }
        if (arp_cache[i].timestamp < oldest_ts) {
            oldest_ts = arp_cache[i].timestamp;
            oldest = i;
        }
        int match = 1;
        for (int j = 0; j < 4; j++)
            if (arp_cache[i].ip[j] != ip[j]) match = 0;
        if (match) {
            for (int j = 0; j < 6; j++) arp_cache[i].mac[j] = mac[j];
            arp_cache[i].timestamp = now;
            return;
        }
    }
    for (int j = 0; j < 4; j++) arp_cache[oldest].ip[j] = ip[j];
    for (int j = 0; j < 6; j++) arp_cache[oldest].mac[j] = mac[j];
    arp_cache[oldest].valid = 1;
    arp_cache[oldest].timestamp = now;
}

static int arp_cache_lookup(const uint8_t *ip, uint8_t *mac) {
    uint32_t now = timer_get_ticks();
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) continue;
        if (now - arp_cache[i].timestamp > ARP_CACHE_TTL) {
            arp_cache[i].valid = 0;
            continue;
        }
        int match = 1;
        for (int j = 0; j < 4; j++)
            if (arp_cache[i].ip[j] != ip[j]) match = 0;
        if (match) {
            for (int j = 0; j < 6; j++) mac[j] = arp_cache[i].mac[j];
            arp_cache[i].timestamp = now;
            return 1;
        }
    }
    return 0;
}

int net_arp_resolve(const uint8_t *target_ip, uint8_t *target_mac) {
    if (arp_cache_lookup(target_ip, target_mac)) {
        dbg_print("[ARP] Cache hit for ");
        dbg_print_ip(target_ip);
        dbg_print("\n");
        return 0;
    }

    uint8_t req[28];
    struct arp_pkt *arp = (struct arp_pkt *)req;
    arp->hw_type = HTONS(1);
    arp->proto_type = HTONS(0x0800);
    arp->hw_len = 6;
    arp->proto_len = 4;
    arp->op = HTONS(ARP_REQUEST);
    for (int i = 0; i < 6; i++) arp->sender_mac[i] = our_mac[i];
    for (int i = 0; i < 4; i++) arp->sender_ip[i] = our_ip[i];
    for (int i = 0; i < 6; i++) arp->target_mac[i] = 0xFF;
    for (int i = 0; i < 4; i++) arp->target_ip[i] = target_ip[i];

    dbg_print("[ARP] Request who-has ");
    dbg_print_ip(target_ip);
    dbg_print("\n");

    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    eth_send(bcast, 0x0806, req, 28);

    uint32_t deadline = timer_get_ticks() + 50;
    while (timer_get_ticks() < deadline) {
        uint8_t reply[PKT_BUF_SIZE];
        uint8_t src_mac[6];
        uint16_t type;
        int n = eth_recv(src_mac, &type, reply, PKT_BUF_SIZE);
        if (n < 28) continue;
        if (type != 0x0806) continue;
        struct arp_pkt *arp_reply = (struct arp_pkt *)reply;
        if (HTONS(arp_reply->op) != ARP_REPLY) continue;
        int match = 1;
        for (int i = 0; i < 4; i++)
            if (arp_reply->sender_ip[i] != target_ip[i]) match = 0;
        if (!match) continue;
        for (int i = 0; i < 6; i++)
            target_mac[i] = arp_reply->sender_mac[i];
        arp_cache_add(target_ip, target_mac);
        dbg_print("[ARP] Reply from ");
        dbg_print_ip(target_ip);
        dbg_print(" at ");
        dbg_print_mac(target_mac);
        dbg_print("\n");
        return 0;
    }
    dbg_print("[ARP] Timeout for ");
    dbg_print_ip(target_ip);
    dbg_print("\n");
    return -1;
}

static int net_route_resolve(const uint8_t *dst_ip, uint8_t *target_mac) {
    if (net_is_local_ip(dst_ip))
        return net_arp_resolve(dst_ip, target_mac);
    else
        return net_arp_resolve(gateway_ip, target_mac);
}

static void ip_send(const uint8_t *dst_ip, uint8_t protocol,
                    const uint8_t *data, int len, uint8_t *out_pkt, int *out_len) {
    int total = 20 + len;
    ip_id_counter++;
    struct ip_hdr *ip = (struct ip_hdr *)out_pkt;
    ip->ver_ihl = 0x45;
    ip->dscp_ecn = 0;
    ip->total_len = HTONS(total);
    ip->id = HTONS(ip_id_counter);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    for (int i = 0; i < 4; i++) ip->src_ip[i] = our_ip[i];
    for (int i = 0; i < 4; i++) ip->dst_ip[i] = dst_ip[i];
    ip->checksum = HTONS(net_checksum(ip, 20));

    for (int i = 0; i < len; i++) out_pkt[20 + i] = data[i];
    *out_len = total;
}

static uint16_t tcp_checksum(const uint8_t *src_ip, const uint8_t *dst_ip,
                              const struct tcp_hdr *tcp, int tcp_len) {
    uint8_t buf[512];
    int pos = 0;
    for (int i = 0; i < 4; i++) buf[pos++] = src_ip[i];
    for (int i = 0; i < 4; i++) buf[pos++] = dst_ip[i];
    buf[pos++] = 0;
    buf[pos++] = IP_PROTO_TCP;
    buf[pos++] = (tcp_len >> 8) & 0xFF;
    buf[pos++] = tcp_len & 0xFF;
    for (int i = 0; i < tcp_len; i++) buf[pos++] = ((const uint8_t *)tcp)[i];
    return net_checksum(buf, pos);
}

static void ip_recv_log(const uint8_t *src_ip, const uint8_t *dst_ip, uint8_t proto, int len) {
    dbg_print("[IP] RX proto=");
    dbg_print_dec(proto);
    dbg_print(" src=");
    dbg_print_ip(src_ip);
    dbg_print(" dst=");
    dbg_print_ip(dst_ip);
    dbg_print(" len=");
    dbg_print_dec(len);
    dbg_print("\n");
}

int net_tcp_syn_test(const uint8_t *ip, uint16_t port, uint32_t timeout_ms) {
    if (!net_ready) return -1;

    uint8_t target_mac[6];
    if (net_route_resolve(ip, target_mac) < 0) {
        dbg_print("[TCP] Route resolve failed\n");
        return -1;
    }

    uint32_t isn = 1000;
    uint8_t tcp_data[20];
    struct tcp_hdr *tcp = (struct tcp_hdr *)tcp_data;
    tcp->src_port = HTONS(23456);
    tcp->dst_port = HTONS(port);
    tcp->seq = HTONL(isn);
    tcp->ack = 0;
    tcp->offset = 0x50;
    tcp->flags = TCP_FLAG_SYN;
    tcp->window = HTONS(0xFFFF);
    tcp->checksum = 0;
    tcp->urgent = 0;
    tcp->checksum = HTONS(tcp_checksum(our_ip, ip, tcp, 20));

    uint8_t ip_pkt[PKT_BUF_SIZE];
    int ip_len;
    ip_send(ip, IP_PROTO_TCP, tcp_data, 20, ip_pkt, &ip_len);
    eth_send(target_mac, 0x0800, ip_pkt, ip_len);

    dbg_print("[TCP] SYN sent to ");
    dbg_print_ip(ip);
    dbg_print(":");
    dbg_print_dec(port);
    dbg_print("\n");

    uint32_t deadline = timer_get_ticks() + timeout_ms / 10;
    while (timer_get_ticks() < deadline) {
        uint8_t reply[PKT_BUF_SIZE];
        uint8_t src_mac[6];
        uint16_t type;
        int n = eth_recv(src_mac, &type, reply, PKT_BUF_SIZE);
        if (n < 40) continue;
        if (type != 0x0800) continue;

        struct ip_hdr *rip = (struct ip_hdr *)reply;
        int ip_hdr_len = (rip->ver_ihl & 0x0F) * 4;
        if (n < ip_hdr_len + 20) continue;
        if (rip->protocol != IP_PROTO_TCP) continue;

        struct tcp_hdr *rtcp = (struct tcp_hdr *)(reply + ip_hdr_len);
        if (HTONS(rtcp->src_port) == port &&
            HTONS(rtcp->dst_port) == 23456 &&
            (rtcp->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            dbg_print("[TCP] SYN-ACK received from ");
            dbg_print_ip(ip);
            dbg_print(":");
            dbg_print_dec(port);
            dbg_print("\n");
            return 0;
        }
    }
    dbg_print("[TCP] SYN-ACK timeout for ");
    dbg_print_ip(ip);
    dbg_print(":");
    dbg_print_dec(port);
    dbg_print("\n");
    return -1;
}

int net_ping(const uint8_t *ip, uint32_t timeout_ms) {
    if (!net_ready) return -1;

    uint8_t target_mac[6];
    int route_result = net_route_resolve(ip, target_mac);
    dbg_print("[ICMP] Route resolve: ");
    dbg_print_dec(route_result);
    dbg_print("\n");
    if (route_result < 0)
        return -1;

    uint8_t icmp_data[56];
    struct icmp_hdr *icmp = (struct icmp_hdr *)icmp_data;
    icmp->type = ICMP_TYPE_ECHO_REQ;
    icmp->code = 0;
    icmp->id = HTONS(0x1234);
    icmp->seq = HTONS(1);
    icmp->checksum = 0;
    for (int i = 0; i < 48; i++) icmp_data[8 + i] = i;
    icmp->checksum = HTONS(net_checksum(icmp, 56));

    dbg_print("[ICMP] Sending echo request to ");
    dbg_print_ip(ip);
    dbg_print("\n");

    uint8_t ip_pkt[PKT_BUF_SIZE];
    int ip_len;
    ip_send(ip, IP_PROTO_ICMP, icmp_data, 56, ip_pkt, &ip_len);
    eth_send(target_mac, 0x0800, ip_pkt, ip_len);

    uint32_t deadline = timer_get_ticks() + timeout_ms / 10;
    while (timer_get_ticks() < deadline) {
        uint8_t reply[PKT_BUF_SIZE];
        uint8_t src_mac[6];
        uint16_t type;
        int n = eth_recv(src_mac, &type, reply, PKT_BUF_SIZE);
        if (n < 20) continue;
        if (type != 0x0800) continue;

        struct ip_hdr *rip = (struct ip_hdr *)reply;
        int ip_hdr_len = (rip->ver_ihl & 0x0F) * 4;
        if (n < ip_hdr_len + 8) continue;

        ip_recv_log(rip->src_ip, rip->dst_ip, rip->protocol, n);

        if (rip->protocol != IP_PROTO_ICMP) continue;

        struct icmp_hdr *ricmp = (struct icmp_hdr *)(reply + ip_hdr_len);
        dbg_print("[ICMP] RX type=");
        dbg_print_dec(ricmp->type);
        dbg_print(" id=0x");
        dbg_print_hex(ricmp->id);
        dbg_print(" seq=0x");
        dbg_print_hex(ricmp->seq);
        dbg_print("\n");

        if (ricmp->type != ICMP_TYPE_ECHO_REPLY) continue;
        if (ricmp->id != HTONS(0x1234)) continue;
        if (ricmp->seq != HTONS(1)) continue;

        int match = 1;
        for (int i = 0; i < 4; i++)
            if (rip->src_ip[i] != ip[i]) match = 0;
        if (!match) continue;

        dbg_print("[ICMP] Echo reply from ");
        dbg_print_ip(ip);
        dbg_print("\n");
        return 0;
    }
    dbg_print("[ICMP] Timeout for ");
    dbg_print_ip(ip);
    dbg_print("\n");
    return -1;
}

static int tcp_connect(const uint8_t *dst_ip, uint16_t dst_port,
                       uint16_t src_port, uint32_t *snd_seq, uint32_t *snd_ack,
                       uint32_t timeout_ticks) {
    uint8_t target_mac[6];
    if (net_route_resolve(dst_ip, target_mac) < 0) {
        dbg_print("[TCP] Route resolve failed for ");
        dbg_print_ip(dst_ip);
        dbg_print("\n");
        return -1;
    }

    uint32_t isn = 1000;

    uint8_t tcp_data[20];
    struct tcp_hdr *tcp = (struct tcp_hdr *)tcp_data;
    tcp->src_port = HTONS(src_port);
    tcp->dst_port = HTONS(dst_port);
    tcp->seq = HTONL(isn);
    tcp->ack = 0;
    tcp->offset = 0x50;
    tcp->flags = TCP_FLAG_SYN;
    tcp->window = HTONS(0xFFFF);
    tcp->checksum = 0;
    tcp->urgent = 0;
    tcp->checksum = HTONS(tcp_checksum(our_ip, dst_ip, tcp, 20));

    uint8_t ip_pkt[PKT_BUF_SIZE];
    int ip_len;
    ip_send(dst_ip, IP_PROTO_TCP, tcp_data, 20, ip_pkt, &ip_len);
    eth_send(target_mac, 0x0800, ip_pkt, ip_len);

    uint32_t deadline = timer_get_ticks() + timeout_ticks;
    while (timer_get_ticks() < deadline) {
        uint8_t reply[PKT_BUF_SIZE];
        uint8_t src_mac[6];
        uint16_t type;
        int n = eth_recv(src_mac, &type, reply, PKT_BUF_SIZE);
        if (n < 40) continue;
        if (type != 0x0800) continue;

        struct ip_hdr *rip = (struct ip_hdr *)reply;
        int ip_hdr_len = (rip->ver_ihl & 0x0F) * 4;
        if (n < ip_hdr_len + 20) continue;
        if (rip->protocol != IP_PROTO_TCP) continue;

        struct tcp_hdr *rtcp = (struct tcp_hdr *)(reply + ip_hdr_len);
        dbg_print("[TCP] connect: rx port=");
        dbg_print_dec(HTONS(rtcp->src_port));
        dbg_print("->");
        dbg_print_dec(HTONS(rtcp->dst_port));
        dbg_print(" flags=");
        dbg_print_hex(rtcp->flags);
        dbg_print(" (expect dst=");
        dbg_print_dec(src_port);
        dbg_print(")\n");

        if (HTONS(rtcp->src_port) == dst_port &&
            HTONS(rtcp->dst_port) == src_port &&
            (rtcp->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
            HTONL(rtcp->ack) == isn + 1) {
            uint32_t our_seq = isn + 1;
            uint32_t our_ack = HTONL(rtcp->seq) + 1;

            uint8_t ack_data[20];
            struct tcp_hdr *atcp = (struct tcp_hdr *)ack_data;
            atcp->src_port = HTONS(src_port);
            atcp->dst_port = HTONS(dst_port);
            atcp->seq = HTONL(our_seq);
            atcp->ack = HTONL(our_ack);
            atcp->offset = 0x50;
            atcp->flags = TCP_FLAG_ACK;
            atcp->window = HTONS(0xFFFF);
            atcp->checksum = 0;
            atcp->urgent = 0;
            atcp->checksum = HTONS(tcp_checksum(our_ip, dst_ip, atcp, 20));

            ip_send(dst_ip, IP_PROTO_TCP, ack_data, 20, ip_pkt, &ip_len);
            eth_send(target_mac, 0x0800, ip_pkt, ip_len);

            *snd_seq = our_seq;
            *snd_ack = our_ack;
            return 0;
        }
    }
    dbg_print("[TCP] SYN-ACK timeout for ");
    dbg_print_ip(dst_ip);
    dbg_print(":");
    dbg_print_dec(dst_port);
    dbg_print(" src_port=");
    dbg_print_dec(src_port);
    dbg_print("\n");
    return -1;
}

static int tcp_send_data(const uint8_t *dst_ip, uint16_t dst_port,
                         uint16_t src_port, uint32_t seq, uint32_t ack,
                         const uint8_t *data, int len, uint8_t flags) {
    uint8_t target_mac[6];
    if (net_route_resolve(dst_ip, target_mac) < 0) return -1;

    int tcp_len = 20 + len;
    uint8_t tcp_data[PKT_BUF_SIZE];
    struct tcp_hdr *tcp = (struct tcp_hdr *)tcp_data;
    tcp->src_port = HTONS(src_port);
    tcp->dst_port = HTONS(dst_port);
    tcp->seq = HTONL(seq);
    tcp->ack = HTONL(ack);
    tcp->offset = 0x50;
    tcp->flags = flags | TCP_FLAG_ACK;
    tcp->window = HTONS(0xFFFF);
    tcp->checksum = 0;
    tcp->urgent = 0;
    for (int i = 0; i < len; i++) tcp_data[20 + i] = data[i];
    tcp->checksum = HTONS(tcp_checksum(our_ip, dst_ip, tcp, tcp_len));

    uint8_t ip_pkt[PKT_BUF_SIZE];
    int ip_len;
    ip_send(dst_ip, IP_PROTO_TCP, tcp_data, tcp_len, ip_pkt, &ip_len);
    eth_send(target_mac, 0x0800, ip_pkt, ip_len);
    return 0;
}

static int tcp_recv_data(const uint8_t *dst_ip, uint16_t dst_port,
                          uint16_t src_port, uint32_t *seq, uint32_t *ack,
                          uint8_t *buf, int max, uint32_t timeout,
                          int *got_fin) {
    uint32_t deadline = timer_get_ticks() + timeout;
    while (timer_get_ticks() < deadline) {
        uint8_t reply[PKT_BUF_SIZE];
        uint8_t src_mac[6];
        uint16_t type;
        int n = eth_recv(src_mac, &type, reply, PKT_BUF_SIZE);
        if (n < 40) continue;
        if (type != 0x0800) continue;

        struct ip_hdr *rip = (struct ip_hdr *)reply;
        int ip_hdr_len = (rip->ver_ihl & 0x0F) * 4;
        if (n < ip_hdr_len + 20) continue;
        if (rip->protocol != IP_PROTO_TCP) continue;

        struct tcp_hdr *rtcp = (struct tcp_hdr *)(reply + ip_hdr_len);
        if (HTONS(rtcp->src_port) != dst_port ||
            HTONS(rtcp->dst_port) != src_port) continue;

        if (rtcp->flags & TCP_FLAG_RST) return -1;

        int tcp_hdr_len = ((rtcp->offset >> 4) & 0x0F) * 4;
        int ip_total_len = HTONS(rip->total_len);
        if (ip_total_len > n) ip_total_len = n;
        int payload_len = ip_total_len - ip_hdr_len - tcp_hdr_len;
        if (payload_len < 0) payload_len = 0;

        int fin_inc = (rtcp->flags & TCP_FLAG_FIN) ? 1 : 0;
        if (fin_inc && got_fin) *got_fin = 1;
        if (payload_len > 0 || fin_inc) {
            int copy = payload_len;
            if (copy > max) copy = max;
            for (int i = 0; i < copy; i++)
                buf[i] = reply[ip_hdr_len + tcp_hdr_len + i];
            *seq = HTONL(rtcp->ack);
            *ack = HTONL(rtcp->seq) + payload_len + fin_inc;

            uint8_t ack_only[20];
            struct tcp_hdr *atcp = (struct tcp_hdr *)ack_only;
            atcp->src_port = HTONS(src_port);
            atcp->dst_port = HTONS(dst_port);
            atcp->seq = HTONL(*seq);
            atcp->ack = HTONL(*ack);
            atcp->offset = 0x50;
            atcp->flags = TCP_FLAG_ACK;
            atcp->window = HTONS(0xFFFF);
            atcp->checksum = 0;
            atcp->urgent = 0;
            atcp->checksum = HTONS(tcp_checksum(our_ip, dst_ip, atcp, 20));

            uint8_t ip_pkt2[PKT_BUF_SIZE];
            int ip_len2;
            ip_send(dst_ip, IP_PROTO_TCP, ack_only, 20, ip_pkt2, &ip_len2);
            eth_send(src_mac, 0x0800, ip_pkt2, ip_len2);

            return copy;
        }
    }
    return 0;
}

/* ──────────── UDP ──────────── */

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed));

static uint16_t udp_checksum(const uint8_t *src_ip, const uint8_t *dst_ip,
                              const uint8_t *udp_data, int udp_len) {
    uint8_t buf[512];
    int pos = 0;
    for (int i = 0; i < 4; i++) buf[pos++] = src_ip[i];
    for (int i = 0; i < 4; i++) buf[pos++] = dst_ip[i];
    buf[pos++] = 0;
    buf[pos++] = IP_PROTO_UDP;
    buf[pos++] = (udp_len >> 8) & 0xFF;
    buf[pos++] = udp_len & 0xFF;
    for (int i = 0; i < udp_len; i++) buf[pos++] = udp_data[i];
    return net_checksum(buf, pos);
}

static void udp_send(const uint8_t *dst_ip, uint16_t src_port, uint16_t dst_port,
                      const uint8_t *data, int len) {
    uint8_t udp_data[512];
    struct udp_hdr *udp = (struct udp_hdr *)udp_data;
    int udp_len = 8 + len;
    udp->src_port = HTONS(src_port);
    udp->dst_port = HTONS(dst_port);
    udp->len = HTONS(udp_len);
    udp->checksum = 0;
    for (int i = 0; i < len; i++) udp_data[8 + i] = data[i];
    udp->checksum = HTONS(udp_checksum(our_ip, dst_ip, udp_data, udp_len));

    uint8_t ip_pkt[PKT_BUF_SIZE];
    int ip_len;
    ip_send(dst_ip, IP_PROTO_UDP, udp_data, udp_len, ip_pkt, &ip_len);
    uint8_t target_mac[6];
    if (net_route_resolve(dst_ip, target_mac) == 0)
        eth_send(target_mac, 0x0800, ip_pkt, ip_len);
}

/* ──────────── DNS ──────────── */

struct dns_hdr {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1

static int dns_build_query(const char *hostname, uint8_t *buf, int max) {
    if (!hostname || !*hostname) return -1;
    struct dns_hdr *hdr = (struct dns_hdr *)buf;
    int pos = sizeof(struct dns_hdr);

    hdr->id = HTONS(0x1234);
    hdr->flags = HTONS(0x0100);
    hdr->qdcount = HTONS(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;

    while (*hostname) {
        const char *dot = hostname;
        while (*dot && *dot != '.') dot++;
        int label_len = dot - hostname;
        if (label_len > 63) return -1;
        if (pos + 1 + label_len > max) return -1;
        buf[pos++] = (uint8_t)label_len;
        for (int i = 0; i < label_len; i++) buf[pos++] = (uint8_t)*hostname++;
        if (*dot == '.') hostname++;
    }
    buf[pos++] = 0;

    if (pos + 4 > max) return -1;
    buf[pos++] = 0; buf[pos++] = DNS_TYPE_A;
    buf[pos++] = 0; buf[pos++] = DNS_CLASS_IN;
    return pos;
}

static int dns_skip_name(const uint8_t *buf, int pos, int max) {
    while (pos < max) {
        uint8_t b = buf[pos];
        if (b == 0) return pos + 1;
        if ((b & 0xC0) == 0xC0) return pos + 2;
        pos += 1 + b;
    }
    return -1;
}

static int dns_parse_response(const uint8_t *buf, int len, uint8_t *ip_out) {
    if (len < (int)sizeof(struct dns_hdr)) return -1;
    const struct dns_hdr *hdr = (const struct dns_hdr *)buf;

    if (HTONS(hdr->id) != 0x1234) return -1;
    uint16_t flags = HTONS(hdr->flags);
    if (!(flags & 0x8000)) return -1;
    if ((flags & 0x0F) != 0) return -1;

    int ancount = HTONS(hdr->ancount);
    if (ancount < 1) return -1;

    int pos = dns_skip_name(buf, sizeof(struct dns_hdr), len);
    if (pos < 0) return -1;
    pos += 4;

    for (int i = 0; i < ancount && pos < len; i++) {
        pos = dns_skip_name(buf, pos, len);
        if (pos < 0) return -1;
        if (pos + 10 > len) return -1;
        uint16_t type = (buf[pos] << 8) | buf[pos+1]; pos += 2;
        pos += 2;
        pos += 4;
        uint16_t rdlength = (buf[pos] << 8) | buf[pos+1]; pos += 2;
        if (type == DNS_TYPE_A && rdlength == 4 && pos + 4 <= len) {
            ip_out[0] = buf[pos];
            ip_out[1] = buf[pos+1];
            ip_out[2] = buf[pos+2];
            ip_out[3] = buf[pos+3];
            return 0;
        }
        pos += rdlength;
    }
    return -1;
}

int net_dns_resolve(const char *hostname, uint8_t *ip_out) {
    if (!net_ready || !hostname || !ip_out) return -1;

    uint8_t query[512];
    int qlen = dns_build_query(hostname, query, sizeof(query));
    if (qlen < 0) return -1;

    static uint16_t dns_src_port = 20000;
    uint16_t my_port = dns_src_port++;

    uint8_t dns_ip[4] = {10, 0, 2, 3};
    udp_send(dns_ip, my_port, 53, query, qlen);

    uint32_t deadline = timer_get_ticks() + 500;
    while (timer_get_ticks() < deadline) {
        uint8_t reply[PKT_BUF_SIZE];
        uint8_t src_mac[6];
        uint16_t type;
        int n = eth_recv(src_mac, &type, reply, PKT_BUF_SIZE);
        if (n < 42) continue;
        if (type != 0x0800) continue;

        struct ip_hdr *rip = (struct ip_hdr *)reply;
        int ip_hdr_len = (rip->ver_ihl & 0x0F) * 4;
        if (rip->protocol != IP_PROTO_UDP) continue;
        if (n < ip_hdr_len + 8) continue;

        struct udp_hdr *rudp = (struct udp_hdr *)(reply + ip_hdr_len);
        if (HTONS(rudp->src_port) != 53) continue;
        if (HTONS(rudp->dst_port) != my_port) continue;

        int udp_len = HTONS(rudp->len);
        if (udp_len <= 8) continue;
        const uint8_t *dns_data = reply + ip_hdr_len + 8;
        int dns_len = udp_len - 8;

        if (dns_parse_response(dns_data, dns_len, ip_out) == 0)
            return 0;
    }
    return -1;
}

int net_init(void) {
    uint16_t slot = pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
    if (slot == 0xFFFF) {
        console_write("[NET] No RTL8139 found\n");
        return -1;
    }

    uint32_t bar = pci_get_bar(0, slot, 0, 0);
    uint16_t io_base = bar & 0xFFFC;

    rtl8139_init(io_base);
    rtl8139_get_mac(our_mac);

    console_write("[NET] MAC: ");
    for (int i = 0; i < 6; i++) {
        console_write_hex(our_mac[i]);
        if (i < 5) console_write(":");
    }
    console_write("\n");

    our_ip[0] = 10; our_ip[1] = 0; our_ip[2] = 2; our_ip[3] = 15;
    gateway_ip[0] = 10; gateway_ip[1] = 0; gateway_ip[2] = 2; gateway_ip[3] = 2;
    netmask[0] = 255; netmask[1] = 255; netmask[2] = 255; netmask[3] = 0;

    console_write("[NET] IP: 10.0.2.15 GW: 10.0.2.2\n");

    net_ready = 1;
    return 0;
}

int net_available(void) {
    return net_ready;
}

void net_flush_rx(void) {
    int count = 0;
    uint32_t deadline = timer_get_ticks() + 100;
    while (timer_get_ticks() < deadline) {
        uint8_t buf[PKT_BUF_SIZE];
        uint8_t mac[6];
        uint16_t type;
        if (eth_recv(mac, &type, buf, PKT_BUF_SIZE) <= 0) {
            if (count > 0) {
                dbg_print("[FLUSH] drained ");
                dbg_print_dec(count);
                dbg_print(" packets\n");
            }
            return;
        }
        count++;
    }
    dbg_print("[FLUSH] drained ");
    dbg_print_dec(count);
    dbg_print(" packets (timeout)\n");
}

void net_get_ip(uint8_t *ip) {
    for (int i = 0; i < 4; i++) ip[i] = our_ip[i];
}

void net_get_gateway(uint8_t *gw) {
    for (int i = 0; i < 4; i++) gw[i] = gateway_ip[i];
}

int net_is_local_ip(const uint8_t *ip) {
    for (int i = 0; i < 4; i++) {
        if ((ip[i] & netmask[i]) != (our_ip[i] & netmask[i]))
            return 0;
    }
    return 1;
}

static int http_parse_response(const uint8_t *resp, int len,
                                uint8_t *body, uint32_t max_body) {
    int body_start = -1;
    for (int i = 0; i < len - 3; i++) {
        if (resp[i] == '\r' && resp[i+1] == '\n' &&
            resp[i+2] == '\r' && resp[i+3] == '\n') {
            body_start = i + 4;
            break;
        }
    }
    if (body_start < 0) return -1;
    int body_len = len - body_start;
    if (body_len > (int)max_body) body_len = (int)max_body;
    for (int i = 0; i < body_len; i++) body[i] = resp[body_start + i];
    return body_len;
}

static int net_parse_ipv4(const char *s, uint8_t *ip) {
    for (int i = 0; i < 4; i++) {
        int octet = 0, part = 0;
        while (*s >= '0' && *s <= '9') {
            octet = octet * 10 + (*s - '0');
            s++; part++;
        }
        if (part == 0 || octet > 255) return -1;
        ip[i] = (uint8_t)octet;
        if (i < 3 && *s != '.') return -1;
        if (i < 3) s++;
    }
    return *s == '\0' ? 0 : -1;
}

static int parse_http_url(const char *url, struct http_url *result) {
    if (url[0] != 'h' || url[1] != 't' || url[2] != 't' || url[3] != 'p' ||
        url[4] != ':' || url[5] != '/' || url[6] != '/')
        return -1;
    url += 7;

    int hi = 0;
    while (*url && *url != ':' && *url != '/' && hi < 63)
        result->host[hi++] = *url++;
    result->host[hi] = '\0';

    result->port = 80;
    if (*url == ':') {
        url++;
        int p = 0;
        while (*url >= '0' && *url <= '9') {
            p = p * 10 + (*url - '0');
            url++;
        }
        if (p <= 0 || p > 65535) return -1;
        result->port = (uint16_t)p;
    }

    int pi = 0;
    if (!*url) {
        result->path[pi++] = '/';
    } else {
        while (*url && pi < 127)
            result->path[pi++] = *url++;
    }
    result->path[pi] = '\0';
    return 0;
}

static int http_parse_response_ex(const uint8_t *raw, int raw_len,
                                   struct http_response *resp) {
    resp->status_code = 0;
    resp->content_length = -1;
    resp->has_location = 0;
    resp->location[0] = '\0';
    resp->body_offset = -1;
    resp->is_chunked = 0;

    int hdr_end = -1;
    for (int i = 0; i < raw_len - 3; i++) {
        if (raw[i] == '\r' && raw[i+1] == '\n' &&
            raw[i+2] == '\r' && raw[i+3] == '\n') {
            hdr_end = i;
            resp->body_offset = i + 4;
            break;
        }
    }
    if (hdr_end < 0) return -1;

    int pos = 0;
    while (pos < hdr_end && raw[pos] != ' ') pos++;
    if (pos >= hdr_end) return -1;
    pos++;

    if (pos + 3 > hdr_end) return -1;
    int sc = 0;
    for (int i = 0; i < 3; i++) {
        if (raw[pos + i] < '0' || raw[pos + i] > '9') return -1;
        sc = sc * 10 + (raw[pos + i] - '0');
    }
    resp->status_code = sc;

    while (pos < hdr_end && !(raw[pos] == '\r' && raw[pos+1] == '\n')) pos++;
    if (pos >= hdr_end) return -1;
    pos += 2;

    while (pos < hdr_end) {
        const char cl_key[] = "content-length:";
        int match = 1;
        for (int mi = 0; cl_key[mi]; mi++) {
            if (pos + mi >= hdr_end || (raw[pos + mi] | 0x20) != cl_key[mi]) {
                match = 0; break;
            }
        }
        if (match) {
            int cp = pos + 15;
            while (cp < hdr_end && raw[cp] == ' ') cp++;
            int cl = 0;
            while (cp < hdr_end && raw[cp] >= '0' && raw[cp] <= '9') {
                cl = cl * 10 + (raw[cp] - '0');
                cp++;
            }
            resp->content_length = cl;
        }

        const char loc_key[] = "location:";
        match = 1;
        for (int mi = 0; loc_key[mi]; mi++) {
            if (pos + mi >= hdr_end || (raw[pos + mi] | 0x20) != loc_key[mi]) {
                match = 0; break;
            }
        }
        if (match) {
            int lp = pos + 9;
            while (lp < hdr_end && raw[lp] == ' ') lp++;
            int li = 0;
            while (lp < hdr_end && raw[lp] != '\r' && li < 255)
                resp->location[li++] = raw[lp++];
            resp->location[li] = '\0';
            resp->has_location = 1;
        }

        const char te_key[] = "transfer-encoding:";
        match = 1;
        for (int mi = 0; te_key[mi]; mi++) {
            if (pos + mi >= hdr_end || (raw[pos + mi] | 0x20) != te_key[mi]) {
                match = 0; break;
            }
        }
        if (match) {
            int tp = pos + 18;
            while (tp < hdr_end && raw[tp] == ' ') tp++;
            if (tp + 6 < hdr_end) {
                int has_chunked = 0;
                for (int si = 0; si < 7; si++) {
                    if ((raw[tp + si] | 0x20) == "chunked"[si]) has_chunked++;
                }
                if (has_chunked == 7) resp->is_chunked = 1;
            }
        }

        while (pos < hdr_end && !(raw[pos] == '\r' && raw[pos+1] == '\n')) pos++;
        if (pos < hdr_end) pos += 2;
    }

    return 0;
}

static int from_hex_char(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int http_decode_chunked(uint8_t *buf, int body_off, int total_len) {
    int rp = body_off;
    int wp = body_off;

    while (rp < total_len) {
        int size = 0;
        while (rp < total_len && buf[rp] != '\r' && buf[rp] != ';') {
            int d = from_hex_char((char)buf[rp]);
            if (d < 0) return -1;
            size = size * 16 + d;
            rp++;
        }

        if (rp + 1 >= total_len || buf[rp] != '\r' || buf[rp+1] != '\n')
            return -1;
        rp += 2;

        if (size == 0) {
            while (rp + 1 < total_len && !(buf[rp] == '\r' && buf[rp+1] == '\n'))
                rp++;
            break;
        }

        if (rp + size > total_len) return -1;
        for (int i = 0; i < size; i++)
            buf[wp++] = buf[rp++];

        if (rp + 1 >= total_len || buf[rp] != '\r' || buf[rp+1] != '\n')
            return -1;
        rp += 2;
    }

    return wp - body_off;
}

int net_http_get(const uint8_t *ip, uint16_t port,
                 const char *host, const char *path,
                 uint8_t *response, uint32_t max_size) {
    if (!net_ready) return -1;

    dbg_print("[HTTP] http_src.port at entry=");
    dbg_print_dec(http_src.port);
    dbg_print(" canary=0x");
    dbg_print_hex(http_src.canary >> 24);
    dbg_print_hex((http_src.canary >> 16) & 0xFF);
    dbg_print_hex((http_src.canary >> 8) & 0xFF);
    dbg_print_hex(http_src.canary & 0xFF);
    dbg_print("\n");

    uint8_t current_ip[4];
    uint16_t current_port = port;
    char current_host[64];
    char current_path[128];

    for (int i = 0; i < 4; i++) current_ip[i] = ip[i];

    if (host && host[0]) {
        int hi = 0;
        while (host[hi] && hi < 63) { current_host[hi] = host[hi]; hi++; }
        current_host[hi] = '\0';
    } else {
        int hi = 0;
        for (int i = 0; i < 4; i++) {
            int n = current_ip[i];
            char tmp[4]; int ti = 0;
            if (n == 0) tmp[ti++] = '0';
            while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
            while (ti > 0) current_host[hi++] = tmp[--ti];
            if (i < 3) current_host[hi++] = '.';
        }
        current_host[hi] = '\0';
    }

    if (path) {
        int pi = 0;
        while (path[pi] && pi < 127) { current_path[pi] = path[pi]; pi++; }
        current_path[pi] = '\0';
    } else {
        current_path[0] = '/'; current_path[1] = '\0';
    }

    for (int redirect = 0; redirect <= HTTP_MAX_REDIRECTS; redirect++) {
        dbg_print("[HTTP] http_src before inc: port=");
        dbg_print_dec(http_src.port);
        dbg_print(" canary=0x");
        dbg_print_hex(http_src.canary >> 24);
        dbg_print_hex((http_src.canary >> 16) & 0xFF);
        dbg_print_hex((http_src.canary >> 8) & 0xFF);
        dbg_print_hex(http_src.canary & 0xFF);
        dbg_print("\n");
        uint16_t my_port = http_src.port++;
        dbg_print("[HTTP] http_src after inc: port=");
        dbg_print_dec(http_src.port);
        dbg_print(" canary=0x");
        dbg_print_hex(http_src.canary >> 24);
        dbg_print_hex((http_src.canary >> 16) & 0xFF);
        dbg_print_hex((http_src.canary >> 8) & 0xFF);
        dbg_print_hex(http_src.canary & 0xFF);
        dbg_print("\n");

        uint32_t snd_seq, snd_ack;
        int conn_rv = tcp_connect(current_ip, current_port, my_port, &snd_seq, &snd_ack, 500);
        dbg_print("[HTTP] tcp_connect=");
        dbg_print_dec(conn_rv);
        dbg_print(" port=");
        dbg_print_dec(my_port);
        dbg_print(" (0x");
        dbg_print_hex(my_port >> 8);
        dbg_print_hex(my_port & 0xFF);
        dbg_print(")\n");
        if (conn_rv < 0)
            return -1;

        char http_req[512];
        int req_len = 0;
        const char *parts[] = {
            "GET ", current_path, " HTTP/1.0\r\nHost: ", current_host, "\r\nConnection: close\r\n\r\n"
        };
        for (int i = 0; i < 5; i++) {
            const char *s = parts[i];
            while (*s) http_req[req_len++] = *s++;
        }

        tcp_send_data(current_ip, current_port, my_port, snd_seq, snd_ack,
                      (const uint8_t *)http_req, req_len, TCP_FLAG_PSH);
        snd_seq += req_len;

        uint8_t buf[8192];
        int total = 0;
        int got_fin = 0;
        uint32_t deadline = timer_get_ticks() + 200;

        while (!got_fin && timer_get_ticks() < deadline) {
            uint32_t new_seq = snd_seq;
            uint32_t new_ack = snd_ack;
            int fin_flag = 0;
            int n = tcp_recv_data(current_ip, current_port, my_port, &new_seq, &new_ack,
                                  buf + total, 8192 - total, 100, &fin_flag);
            if (n > 0) {
                total += n;
                snd_seq = new_seq;
                snd_ack = new_ack;
                deadline = timer_get_ticks() + 100;
            }
            if (fin_flag) {
                got_fin = 1;
                snd_seq = new_seq;
                snd_ack = new_ack;
            }
        }

        dbg_print("[HTTP] parse: total=");
        dbg_print_dec(total);
        dbg_print(" first=");
        dbg_print_hex(buf[0]);
        dbg_print_hex(buf[1]);
        dbg_print_hex(buf[2]);
        dbg_print_hex(buf[3]);
        dbg_print_hex(buf[4]);
        dbg_print_hex(buf[5]);
        dbg_print("\n");

        struct http_response hr;
        if (http_parse_response_ex(buf, total, &hr) < 0) {
            int fallback = http_parse_response(buf, total, response, max_size);
            dbg_print("[HTTP] parse_ex failed, fallback=");
            dbg_print_dec(fallback);
            dbg_print("\n");
            return fallback;
        }

        if (hr.status_code >= 300 && hr.status_code < 400 && hr.has_location) {
            if (hr.location[0] == 'h' && hr.location[1] == 't' &&
                hr.location[2] == 't' && hr.location[3] == 'p' &&
                hr.location[4] == 's') {
                dbg_print("[HTTP] HTTPS redirect not supported: ");
                dbg_print(hr.location);
                dbg_print("\n");
                return -1;
            }

            struct http_url new_url;
            if (parse_http_url(hr.location, &new_url) == 0) {
                int hi = 0;
                while (new_url.host[hi]) { current_host[hi] = new_url.host[hi]; hi++; }
                current_host[hi] = '\0';
                current_port = new_url.port;
                int pi = 0;
                while (new_url.path[pi]) { current_path[pi] = new_url.path[pi]; pi++; }
                current_path[pi] = '\0';
            } else if (hr.location[0] == '/') {
                int pi = 0;
                while (hr.location[pi]) { current_path[pi] = hr.location[pi]; pi++; }
                current_path[pi] = '\0';
            } else {
                int last_slash = 0;
                for (int i = 0; current_path[i]; i++)
                    if (current_path[i] == '/') last_slash = i;
                int pi = 0;
                for (int i = 0; i <= last_slash; i++)
                    current_path[pi++] = current_path[i];
                int li = 0;
                while (hr.location[li] && pi < 127)
                    current_path[pi++] = hr.location[li++];
                current_path[pi] = '\0';
            }

            uint8_t new_ip[4];
            if (net_parse_ipv4(current_host, new_ip) < 0) {
                if (net_dns_resolve(current_host, new_ip) < 0) {
                    dbg_print("[HTTP] DNS failed for redirect: ");
                    dbg_print(current_host);
                    dbg_print("\n");
                    return -1;
                }
            }
            for (int i = 0; i < 4; i++) current_ip[i] = new_ip[i];

            dbg_print("[HTTP] total=");
        dbg_print_dec(total);
        dbg_print(" status=");
        dbg_print_dec(hr.status_code);
        dbg_print(" body_off=");
        dbg_print_dec(hr.body_offset);
        dbg_print(" cl=");
        dbg_print_dec(hr.content_length);
        dbg_print("\n");

        dbg_print("[HTTP] Redirect ");
            dbg_print_dec(hr.status_code);
            dbg_print(" -> ");
            dbg_print(hr.location);
            dbg_print("\n");
            continue;
        }

        int body_len = total - hr.body_offset;
        if (body_len < 0) body_len = 0;

        if (hr.is_chunked) {
            int decoded = http_decode_chunked(buf, hr.body_offset, total);
            if (decoded < 0) {
                dbg_print("[HTTP] Chunk decode failed\n");
                return -1;
            }
            body_len = decoded;
            if (hr.content_length >= 0 && body_len < hr.content_length)
                dbg_print("[HTTP] Chunked body shorter than Content-Length\n");
        } else         dbg_print("[HTTP] body_off=");
        dbg_print_dec(hr.body_offset);
        dbg_print(" body_len=");
        dbg_print_dec(body_len);
        dbg_print(" cl=");
        dbg_print_dec(hr.content_length);
        dbg_print("\n");

        if (hr.content_length >= 0 && body_len < hr.content_length) {
            dbg_print("[HTTP] Truncated: got ");
            dbg_print_dec(body_len);
            dbg_print(" of ");
            dbg_print_dec(hr.content_length);
            dbg_print(" bytes\n");
        }

        if (hr.status_code != 200) {
            dbg_print("[HTTP] Status: ");
            dbg_print_dec(hr.status_code);
            if (hr.status_code == 404) dbg_print(" (Not Found)");
            else if (hr.status_code == 500) dbg_print(" (Server Error)");
            else if (hr.status_code == 301 || hr.status_code == 302 ||
                     hr.status_code == 307 || hr.status_code == 308)
                dbg_print(" (Redirect without Location)");
            dbg_print("\n");
        }

        if (hr.content_length >= 0 && body_len > hr.content_length)
            body_len = hr.content_length;

        int copy = body_len;
        if (copy > (int)max_size) copy = (int)max_size;
        int off = hr.body_offset;
        for (int i = 0; i < copy; i++)
            response[i] = buf[off + i];

        dbg_print("[HTTP] return=");
        dbg_print_dec(copy);
        dbg_print("\n");
        return copy;
    }

    dbg_print("[HTTP] Too many redirects\n");
    return -1;
}
