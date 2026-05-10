#ifndef NET_H
#define NET_H

// ============================================================
// MOKernel Networking Stack
// Driver: RTL8139 (PCI NIC, standard QEMU virtual NIC)
// Protocols: Ethernet II, ARP, IPv4, ICMP, UDP
// ============================================================

// --------------- Type helpers --------------------------------
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

// --------------- MAC / IP types ------------------------------
typedef struct { u8 b[6]; } mac_addr_t;
typedef u32 ip_addr_t;  // network byte order (big-endian)

// --------------- PCI  ----------------------------------------
#define PCI_CONFIG_ADDR   0xCF8
#define PCI_CONFIG_DATA   0xCFC

u32  pci_read32(u8 bus, u8 slot, u8 func, u8 offset);
void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value);
int  pci_find_device(u16 vendor, u16 device, u8 *out_bus, u8 *out_slot);

// --------------- RTL8139 registers ---------------------------
#define RTL_IDR0          0x00   // MAC address bytes 0-5
#define RTL_MAR0          0x08   // Multicast registers
#define RTL_TSD0          0x10   // Tx status descriptor 0-3
#define RTL_TSAD0         0x20   // Tx start address 0-3
#define RTL_RBSTART       0x30   // Rx buffer start address
#define RTL_ERBCR         0x34   // Early Rx byte count
#define RTL_ERSR          0x36   // Early Rx status register
#define RTL_CR            0x37   // Command register
#define RTL_CAPR          0x38   // Current address of Pkt Read
#define RTL_CBR           0x3A   // Current buffer address
#define RTL_IMR           0x3C   // Interrupt mask register
#define RTL_ISR           0x3E   // Interrupt status register
#define RTL_TCR           0x40   // Tx config register
#define RTL_RCR           0x44   // Rx config register
#define RTL_TCTR          0x48   // Timer count register
#define RTL_MPC           0x4C   // Missed packet counter
#define RTL_9346CR        0x50   // 93C46 command register
#define RTL_CONFIG1       0x52   // Config register 1
#define RTL_MSR           0x58   // Media status register

#define RTL_CR_RST        0x10
#define RTL_CR_RE         0x08   // Receiver enable
#define RTL_CR_TE         0x04   // Transmitter enable
#define RTL_CR_RXBUFEMPTY 0x01

// RCR flags
#define RTL_RCR_AAP       (1<<0) // Accept all packets
#define RTL_RCR_APM       (1<<1) // Accept physical match
#define RTL_RCR_AM        (1<<2) // Accept multicast
#define RTL_RCR_AB        (1<<3) // Accept broadcast
#define RTL_RCR_WRAP      (1<<7) // Wrap Rx buffer
#define RTL_RCR_MXDMA_UNLIM (7<<8)
#define RTL_RCR_RBLEN_32K (2<<11)
#define RTL_RCR_RXFTH_NONE (7<<13)

// ISR flags
#define RTL_ISR_ROK  0x0001  // Rx OK
#define RTL_ISR_TOK  0x0004  // Tx OK

// TSD flags
#define RTL_TSD_OWN  (1<<13) // DMA operation completed
#define RTL_TSD_TOK  (1<<15) // Transmit OK

// --------------- RTL8139 driver API --------------------------
#define RTL_RX_BUF_SIZE  (32*1024 + 16 + 1500)
#define RTL_TX_BUF_SIZE  1536
#define RTL_TX_DESC_NUM  4

extern mac_addr_t net_mac;     // Our MAC address
extern ip_addr_t  net_ip;      // Our IP (host byte order stored as u32 BE)
extern u16        net_iobase;  // RTL8139 I/O base port

int  net_init(void);           // Init PCI + RTL8139, return 0 on success
void net_poll(void);           // Poll & process received packets
int  net_send(const u8 *frame, u16 len); // Send raw Ethernet frame

// --------------- Byte-order helpers --------------------------
static inline u16 htons(u16 h) { return (u16)((h >> 8) | (h << 8)); }
static inline u16 ntohs(u16 n) { return htons(n); }
static inline u32 htonl(u32 h) {
    return ((h & 0xFF000000) >> 24) |
           ((h & 0x00FF0000) >>  8) |
           ((h & 0x0000FF00) <<  8) |
           ((h & 0x000000FF) << 24);
}
static inline u32 ntohl(u32 n) { return htonl(n); }

// --------------- Ethernet ------------------------------------
#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806

typedef struct {
    mac_addr_t dst;
    mac_addr_t src;
    u16        ethertype;  // big-endian
} __attribute__((packed)) eth_hdr_t;

// --------------- ARP -----------------------------------------
#define ARP_HW_ETHER  1
#define ARP_OP_REQ    1
#define ARP_OP_REPLY  2

typedef struct {
    u16 htype;   // Hardware type (Ethernet = 1)
    u16 ptype;   // Protocol type (IP = 0x0800)
    u8  hlen;    // Hardware address length (6)
    u8  plen;    // Protocol address length (4)
    u16 oper;    // Operation (1=request, 2=reply)
    mac_addr_t sha; // Sender hardware address
    u32        spa; // Sender protocol address
    mac_addr_t tha; // Target hardware address
    u32        tpa; // Target protocol address
} __attribute__((packed)) arp_pkt_t;

#define ARP_CACHE_SIZE 8
typedef struct {
    ip_addr_t  ip;
    mac_addr_t mac;
    int        valid;
} arp_entry_t;

extern arp_entry_t arp_cache[ARP_CACHE_SIZE];

void arp_handle(const u8 *pkt, u16 len);
int  arp_lookup(ip_addr_t ip, mac_addr_t *out_mac);
void arp_send_request(ip_addr_t target_ip);
void arp_print_cache(void);

// --------------- IPv4 ----------------------------------------
#define IP_PROTO_ICMP  1
#define IP_PROTO_UDP   17

typedef struct {
    u8  ver_ihl;      // Version (4) + IHL (usually 5)
    u8  dscp_ecn;
    u16 total_len;
    u16 id;
    u16 flags_frag;
    u8  ttl;
    u8  protocol;
    u16 checksum;
    u32 src;
    u32 dst;
} __attribute__((packed)) ip_hdr_t;

u16  ip_checksum(const void *data, u16 len);
void ip_handle(const u8 *pkt, u16 len);
int  ip_send(ip_addr_t dst_ip, u8 proto, const u8 *payload, u16 plen);

// --------------- ICMP ----------------------------------------
#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

typedef struct {
    u8  type;
    u8  code;
    u16 checksum;
    u16 id;
    u16 seq;
} __attribute__((packed)) icmp_hdr_t;

void icmp_handle(ip_addr_t src_ip, const u8 *pkt, u16 len);
void icmp_send_echo(ip_addr_t dst_ip, u16 seq);
extern volatile int icmp_echo_received;
extern volatile u16 icmp_last_seq;

// --------------- UDP -----------------------------------------
typedef struct {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} __attribute__((packed)) udp_hdr_t;

void udp_handle(ip_addr_t src_ip, const u8 *pkt, u16 len);
int  udp_send(ip_addr_t dst_ip, u16 src_port, u16 dst_port,
              const u8 *data, u16 dlen);

// --------------- Net shell helpers ---------------------------
void net_cmd_ifconfig(void);
void net_cmd_ping(ip_addr_t target);
void net_cmd_arp(void);

// --------------- Initialization ------------------------------
void net_stack_init(void);  // Call once in kmain

// IP from 4 octets (a.b.c.d) stored big-endian
#define MAKE_IP(a,b,c,d) \
    (((u32)(a) << 24) | ((u32)(b) << 16) | ((u32)(c) << 8) | (u32)(d))

// Parse "a.b.c.d" string into ip_addr_t, return 1 on success
int  parse_ip(const char *s, ip_addr_t *out);

#endif /* NET_H */
