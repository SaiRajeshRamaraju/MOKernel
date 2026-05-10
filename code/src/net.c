// ============================================================
// MOKernel Networking Stack Implementation
// ============================================================
#include "net.h"

// ---- external kernel helpers --------------------------------
extern void  kprint(const char *s);
extern void  kprint_hex(unsigned int v);
extern void  write_port(unsigned short port, unsigned char data);
extern unsigned char read_port(unsigned short port);

// --------------- Utility helpers (no libc) -------------------

static void memset_n(void *dst, u8 val, u16 n) {
    u8 *d = (u8 *)dst;
    while (n--) *d++ = val;
}

static void memcpy_n(void *dst, const void *src, u16 n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
}

static int memcmp_n(const void *a, const void *b, u16 n) {
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        x++; y++;
    }
    return 0;
}

static u16 strlen_n(const char *s) {
    u16 n = 0;
    while (*s++) n++;
    return n;
}

static int isdigit_n(char c) { return c >= '0' && c <= '9'; }

// Print a decimal number
static void kprint_dec(u32 v) {
    char buf[12];
    int i = 0;
    if (v == 0) { kprint("0"); return; }
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) { char s[2] = {buf[i], '\0'}; kprint(s); }
}

// Print an IP address (host byte order big-endian u32)
static void kprint_ip(ip_addr_t ip) {
    kprint_dec((ip >> 24) & 0xFF); kprint(".");
    kprint_dec((ip >> 16) & 0xFF); kprint(".");
    kprint_dec((ip >>  8) & 0xFF); kprint(".");
    kprint_dec((ip      ) & 0xFF);
}

// Print a MAC address
static void kprint_mac(const mac_addr_t *m) {
    for (int i = 0; i < 6; i++) {
        u8 hi = (m->b[i] >> 4) & 0xF;
        u8 lo =  m->b[i]       & 0xF;
        char s[2];
        s[0] = hi < 10 ? '0' + hi : 'A' + hi - 10; s[1] = '\0'; kprint(s);
        s[0] = lo < 10 ? '0' + lo : 'A' + lo - 10; s[1] = '\0'; kprint(s);
        if (i < 5) kprint(":");
    }
}

// ============================================================
// PCI
// ============================================================

// 16-bit I/O helpers for PCI
static u32 pci_inl(u16 port) {
    u32 v;
    // We only have byte-level write_port/read_port, so read 4 bytes
    u8 b0 = read_port(port);
    u8 b1 = read_port(port + 1);
    u8 b2 = read_port(port + 2);
    u8 b3 = read_port(port + 3);
    v = (u32)b0 | ((u32)b1 << 8) | ((u32)b2 << 16) | ((u32)b3 << 24);
    return v;
}

static void pci_outl(u16 port, u32 val) {
    write_port(port,     (u8)(val      ));
    write_port(port + 1, (u8)(val >>  8));
    write_port(port + 2, (u8)(val >> 16));
    write_port(port + 3, (u8)(val >> 24));
}

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 addr = (u32)(1u << 31)
             | ((u32)bus  << 16)
             | ((u32)slot << 11)
             | ((u32)func <<  8)
             | (offset & 0xFC);
    pci_outl(PCI_CONFIG_ADDR, addr);
    return pci_inl(PCI_CONFIG_DATA);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    u32 addr = (u32)(1u << 31)
             | ((u32)bus  << 16)
             | ((u32)slot << 11)
             | ((u32)func <<  8)
             | (offset & 0xFC);
    pci_outl(PCI_CONFIG_ADDR, addr);
    pci_outl(PCI_CONFIG_DATA, value);
}

int pci_find_device(u16 vendor, u16 device, u8 *out_bus, u8 *out_slot) {
    for (u8 bus = 0; bus < 8; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            u32 id = pci_read32(bus, slot, 0, 0);
            u16 v  = (u16)(id & 0xFFFF);
            u16 d  = (u16)((id >> 16) & 0xFFFF);
            if (v == vendor && d == device) {
                *out_bus  = bus;
                *out_slot = slot;
                return 1;
            }
        }
    }
    return 0;
}

// ============================================================
// RTL8139 Driver
// ============================================================

mac_addr_t net_mac;
ip_addr_t  net_ip  = MAKE_IP(10, 0, 2, 15); // QEMU default DHCP lease
u16        net_iobase = 0;

static u8 rx_buf[RTL_RX_BUF_SIZE]  __attribute__((aligned(4)));
static u8 tx_buf[RTL_TX_DESC_NUM][RTL_TX_BUF_SIZE] __attribute__((aligned(4)));
static int tx_cur = 0;
static u16 rx_cur = 0;  // software read pointer (byte offset into rx_buf)

// 16-bit port helpers (RTL8139 uses 16-bit registers too)
static u16 rtl_inw(u8 reg) {
    u8 lo = read_port(net_iobase + reg);
    u8 hi = read_port(net_iobase + reg + 1);
    return (u16)((u16)hi << 8 | lo);
}

static void rtl_outw(u8 reg, u16 val) {
    write_port(net_iobase + reg,     (u8)(val & 0xFF));
    write_port(net_iobase + reg + 1, (u8)((val >> 8) & 0xFF));
}

static u32 rtl_inl(u8 reg) {
    u8 b0 = read_port(net_iobase + reg);
    u8 b1 = read_port(net_iobase + reg + 1);
    u8 b2 = read_port(net_iobase + reg + 2);
    u8 b3 = read_port(net_iobase + reg + 3);
    return (u32)b0 | ((u32)b1 << 8) | ((u32)b2 << 16) | ((u32)b3 << 24);
}

static void rtl_outl(u8 reg, u32 val) {
    write_port(net_iobase + reg,     (u8)(val      ));
    write_port(net_iobase + reg + 1, (u8)(val >>  8));
    write_port(net_iobase + reg + 2, (u8)(val >> 16));
    write_port(net_iobase + reg + 3, (u8)(val >> 24));
}

static void rtl_outb(u8 reg, u8 val) {
    write_port(net_iobase + reg, val);
}

static u8 rtl_inb(u8 reg) {
    return read_port(net_iobase + reg);
}

// Spin-wait for a condition on CR (for reset)
static void rtl_wait_reset(void) {
    unsigned int timeout = 1000000;
    while ((rtl_inb(RTL_CR) & RTL_CR_RST) && timeout--);
}

int net_init(void) {
    u8 bus, slot;

    // ---- Locate RTL8139 on PCI bus ---------------------------
    // Vendor: Realtek 0x10EC, Device: RTL8139 0x8139
    if (!pci_find_device(0x10EC, 0x8139, &bus, &slot)) {
        kprint("[NET] RTL8139 not found on PCI bus.\n");
        return -1;
    }

    kprint("[NET] RTL8139 found at PCI ");
    kprint_dec(bus); kprint(":"); kprint_dec(slot); kprint(".0\n");

    // ---- Enable PCI Bus Mastering & I/O space ----------------
    u32 cmd = pci_read32(bus, slot, 0, 0x04);
    // Enable I/O (bit0) and bus master (bit2)
    cmd |= 0x05;
    pci_write32(bus, slot, 0, 0x04, cmd);

    // ---- Read BAR0 (I/O base) --------------------------------
    u32 bar0 = pci_read32(bus, slot, 0, 0x10);
    net_iobase = (u16)(bar0 & ~0x3); // Strip indicator bits

    kprint("[NET] I/O base: ");
    kprint_hex(net_iobase); kprint("\n");

    // ---- Power on --------------------------------------------
    rtl_outb(RTL_CONFIG1, 0x00);

    // ---- Software Reset --------------------------------------
    rtl_outb(RTL_CR, RTL_CR_RST);
    rtl_wait_reset();

    // ---- Read MAC address ------------------------------------
    for (int i = 0; i < 6; i++)
        net_mac.b[i] = rtl_inb(RTL_IDR0 + i);

    kprint("[NET] MAC: ");
    kprint_mac(&net_mac);
    kprint("\n");

    // ---- Set up Rx ring buffer --------------------------------
    u32 rbstart = (u32)(unsigned long)rx_buf;
    rtl_outl(RTL_RBSTART, rbstart);

    // ---- Set Rx Config: accept broadcast + physical match ----
    rtl_outl(RTL_RCR,
        RTL_RCR_AB | RTL_RCR_APM | RTL_RCR_AAP | RTL_RCR_AM |
        RTL_RCR_WRAP | RTL_RCR_MXDMA_UNLIM |
        RTL_RCR_RBLEN_32K | RTL_RCR_RXFTH_NONE);

    // ---- Set Tx Config ---------------------------------------
    rtl_outl(RTL_TCR, 0x03000700); // IFG normal, max DMA unlimited

    // ---- Set Tx buffer addresses in descriptor regs ----------
    for (int i = 0; i < RTL_TX_DESC_NUM; i++) {
        rtl_outl(RTL_TSAD0 + i * 4, (u32)(unsigned long)tx_buf[i]);
    }

    // ---- Enable Rx + Tx --------------------------------------
    rtl_outb(RTL_CR, RTL_CR_RE | RTL_CR_TE);

    // ---- Unmask ROK + TOK interrupts -------------------------
    rtl_outw(RTL_IMR, RTL_ISR_ROK | RTL_ISR_TOK);

    // ---- Initialize ring buffer pointer ----------------------
    rx_cur = 0;

    kprint("[NET] RTL8139 initialized. IP: ");
    kprint_ip(net_ip);
    kprint("\n");

    return 0;
}

// ---- Receive a frame from the ring buffer -------------------
// Returns length or 0 if nothing waiting.
static u16 rtl_recv(u8 *out_buf) {
    // Check if Rx buffer is empty
    if (rtl_inb(RTL_CR) & RTL_CR_RXBUFEMPTY)
        return 0;

    // Rx packet header is: [status 2B][length 2B][data...]
    // The chip wraps automatically; rx_cur tracks our read offset.
    u8  *ptr = rx_buf + rx_cur;
    u16  hdr_flags = (u16)ptr[0] | ((u16)ptr[1] << 8);
    u16  pkt_len   = (u16)ptr[2] | ((u16)ptr[3] << 8);

    (void)hdr_flags;

    // Subtract the 4-byte RTL header and 4-byte CRC from length
    u16 data_len = pkt_len - 4;
    if (data_len == 0 || data_len > 1514) {
        // Corrupt / empty — advance by 4 (header only)
        rx_cur = (rx_cur + 4 + 3) & ~3;
        rx_cur %= sizeof(rx_buf);
        rtl_outw(RTL_CAPR, (u16)(rx_cur - 16));
        return 0;
    }

    // Copy packet data
    if (rx_cur + 4 + data_len <= sizeof(rx_buf)) {
        memcpy_n(out_buf, ptr + 4, data_len);
    } else {
        // Wraparound copy
        u16 first  = (u16)(sizeof(rx_buf) - rx_cur - 4);
        u16 second = data_len - first;
        memcpy_n(out_buf, ptr + 4, first);
        memcpy_n(out_buf + first, rx_buf, second);
    }

    // Advance ring pointer (DWORD-aligned, +4 for header)
    rx_cur = (rx_cur + pkt_len + 4 + 3) & ~3;
    rx_cur %= sizeof(rx_buf);
    rtl_outw(RTL_CAPR, (u16)(rx_cur - 16));

    return data_len;
}

// ---- Transmit a raw Ethernet frame --------------------------
int net_send(const u8 *frame, u16 len) {
    if (!net_iobase || len > RTL_TX_BUF_SIZE) return -1;

    // Copy to transmit buffer
    memcpy_n(tx_buf[tx_cur], frame, len);

    // Write address already set in init; write length + OWN to TSD
    // TSD: bits[12:0] = size, bit13 = OWN (0 means NIC owns it)
    rtl_outl(RTL_TSD0 + tx_cur * 4, (u32)len & 0x1FFF);

    // Wait for TOK (transmit OK)
    unsigned int timeout = 100000;
    while (timeout--) {
        u32 tsd = rtl_inl(RTL_TSD0 + tx_cur * 4);
        if (tsd & RTL_TSD_TOK) break;
    }

    tx_cur = (tx_cur + 1) % RTL_TX_DESC_NUM;
    return 0;
}

// ============================================================
// Ethernet
// ============================================================

static u8 pkt_buf[1518];  // Scratch buffer for received frames

static void eth_process(const u8 *frame, u16 len) {
    if (len < (u16)sizeof(eth_hdr_t)) return;

    eth_hdr_t *eth = (eth_hdr_t *)frame;
    u16 etype = ntohs(eth->ethertype);
    const u8 *payload = frame + sizeof(eth_hdr_t);
    u16 plen  = len - (u16)sizeof(eth_hdr_t);

    if (etype == ETH_TYPE_ARP) {
        arp_handle(payload, plen);
    } else if (etype == ETH_TYPE_IP) {
        ip_handle(payload, plen);
    }
}

// Build Ethernet frame header into buf, return header length
static u16 eth_build(u8 *buf, const mac_addr_t *dst, u16 ethertype) {
    eth_hdr_t *h = (eth_hdr_t *)buf;
    memcpy_n(h->dst.b, dst->b, 6);
    memcpy_n(h->src.b, net_mac.b, 6);
    h->ethertype = htons(ethertype);
    return sizeof(eth_hdr_t);
}

// Poll the NIC for incoming frames
void net_poll(void) {
    u16 len;
    while ((len = rtl_recv(pkt_buf)) > 0) {
        eth_process(pkt_buf, len);
    }
}

// ============================================================
// ARP
// ============================================================

arp_entry_t arp_cache[ARP_CACHE_SIZE];

static void arp_cache_init(void) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) arp_cache[i].valid = 0;
}

static void arp_cache_insert(ip_addr_t ip, const mac_addr_t *mac) {
    // Overwrite first invalid slot, or slot 0
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid || arp_cache[i].ip == ip) {
            arp_cache[i].ip    = ip;
            arp_cache[i].mac   = *mac;
            arp_cache[i].valid = 1;
            return;
        }
    }
    // Evict slot 0
    arp_cache[0].ip    = ip;
    arp_cache[0].mac   = *mac;
    arp_cache[0].valid = 1;
}

int arp_lookup(ip_addr_t ip, mac_addr_t *out_mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            *out_mac = arp_cache[i].mac;
            return 1;
        }
    }
    return 0;
}

void arp_send_request(ip_addr_t target_ip) {
    static const mac_addr_t bcast = {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}};
    static const mac_addr_t zero  = {{0,0,0,0,0,0}};
    u8 frame[sizeof(eth_hdr_t) + sizeof(arp_pkt_t)];

    u16 off = eth_build(frame, &bcast, ETH_TYPE_ARP);
    arp_pkt_t *a = (arp_pkt_t *)(frame + off);
    a->htype = htons(ARP_HW_ETHER);
    a->ptype = htons(ETH_TYPE_IP);
    a->hlen  = 6;
    a->plen  = 4;
    a->oper  = htons(ARP_OP_REQ);
    a->sha   = net_mac;
    a->spa   = htonl(net_ip);
    a->tha   = zero;
    a->tpa   = htonl(target_ip);

    net_send(frame, (u16)sizeof(frame));
}

void arp_handle(const u8 *pkt, u16 len) {
    if (len < (u16)sizeof(arp_pkt_t)) return;
    arp_pkt_t *a = (arp_pkt_t *)pkt;

    ip_addr_t sender_ip = ntohl(a->spa);

    // Learn sender
    arp_cache_insert(sender_ip, &a->sha);

    // If it's a request for us, send a reply
    if (ntohs(a->oper) == ARP_OP_REQ) {
        ip_addr_t target_ip = ntohl(a->tpa);
        if (target_ip != net_ip) return;

        u8 frame[sizeof(eth_hdr_t) + sizeof(arp_pkt_t)];
        u16 off = eth_build(frame, &a->sha, ETH_TYPE_ARP);
        arp_pkt_t *r = (arp_pkt_t *)(frame + off);
        r->htype = htons(ARP_HW_ETHER);
        r->ptype = htons(ETH_TYPE_IP);
        r->hlen  = 6;
        r->plen  = 4;
        r->oper  = htons(ARP_OP_REPLY);
        r->sha   = net_mac;
        r->spa   = htonl(net_ip);
        r->tha   = a->sha;
        r->tpa   = a->spa;

        net_send(frame, (u16)sizeof(frame));
    }
}

void arp_print_cache(void) {
    int found = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid) {
            kprint_ip(arp_cache[i].ip);
            kprint("  ->  ");
            kprint_mac(&arp_cache[i].mac);
            kprint("\n");
            found++;
        }
    }
    if (!found) kprint("(ARP cache empty)\n");
}

// ============================================================
// IP
// ============================================================

u16 ip_checksum(const void *data, u16 len) {
    const u16 *p = (const u16 *)data;
    u32 sum = 0;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len) sum += *(const u8 *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)(~sum);
}

void ip_handle(const u8 *pkt, u16 len) {
    if (len < (u16)sizeof(ip_hdr_t)) return;
    ip_hdr_t *ip = (ip_hdr_t *)pkt;

    u8 ihl = (ip->ver_ihl & 0x0F) * 4;
    if (ihl < 20) return;

    ip_addr_t dst_ip = ntohl(ip->dst);
    if (dst_ip != net_ip) return; // Not for us

    const u8 *payload = pkt + ihl;
    u16 plen = ntohs(ip->total_len) - ihl;
    ip_addr_t src_ip = ntohl(ip->src);

    if (ip->protocol == IP_PROTO_ICMP) {
        icmp_handle(src_ip, payload, plen);
    } else if (ip->protocol == IP_PROTO_UDP) {
        udp_handle(src_ip, payload, plen);
    }
}

int ip_send(ip_addr_t dst_ip, u8 proto, const u8 *payload, u16 plen) {
    // Resolve destination MAC (ARP)
    mac_addr_t dst_mac;
    static const mac_addr_t bcast = {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}};

    // Broadcast special case
    if (dst_ip == 0xFFFFFFFF) {
        dst_mac = bcast;
    } else if (!arp_lookup(dst_ip, &dst_mac)) {
        // Send ARP request and wait for reply
        arp_send_request(dst_ip);
        unsigned int wait = 500000;
        while (wait--) {
            net_poll();
            if (arp_lookup(dst_ip, &dst_mac)) break;
        }
        if (!arp_lookup(dst_ip, &dst_mac)) {
            kprint("[NET] ARP failed for ");
            kprint_ip(dst_ip); kprint("\n");
            return -1;
        }
    }

    u16 total = (u16)(sizeof(ip_hdr_t) + plen);
    u16 frame_size = (u16)(sizeof(eth_hdr_t) + total);
    u8 frame[1500] = {0};

    u16 off = eth_build(frame, &dst_mac, ETH_TYPE_IP);
    ip_hdr_t *ip = (ip_hdr_t *)(frame + off);
    ip->ver_ihl    = 0x45;
    ip->dscp_ecn   = 0;
    ip->total_len  = htons(total);
    ip->id         = htons(1);
    ip->flags_frag = 0;
    ip->ttl        = 64;
    ip->protocol   = proto;
    ip->checksum   = 0;
    ip->src        = htonl(net_ip);
    ip->dst        = htonl(dst_ip);
    ip->checksum   = ip_checksum(ip, sizeof(ip_hdr_t));

    memcpy_n(frame + off + sizeof(ip_hdr_t), payload, plen);
    return net_send(frame, frame_size);
}

// ============================================================
// ICMP
// ============================================================

volatile int icmp_echo_received = 0;
volatile u16 icmp_last_seq      = 0;

void icmp_handle(ip_addr_t src_ip, const u8 *pkt, u16 len) {
    if (len < (u16)sizeof(icmp_hdr_t)) return;
    icmp_hdr_t *icmp = (icmp_hdr_t *)pkt;

    if (icmp->type == ICMP_ECHO_REQUEST) {
        // Build a reply
        u8 reply[64];
        if (len > 64) len = 64;
        memcpy_n(reply, pkt, len);
        icmp_hdr_t *r = (icmp_hdr_t *)reply;
        r->type     = ICMP_ECHO_REPLY;
        r->checksum = 0;
        r->checksum = ip_checksum(reply, len);
        ip_send(src_ip, IP_PROTO_ICMP, reply, len);
    } else if (icmp->type == ICMP_ECHO_REPLY) {
        icmp_echo_received = 1;
        icmp_last_seq      = ntohs(icmp->seq);
    }
}

void icmp_send_echo(ip_addr_t dst_ip, u16 seq) {
    u8 payload[sizeof(icmp_hdr_t) + 8];
    memset_n(payload, 0, sizeof(payload));
    icmp_hdr_t *icmp = (icmp_hdr_t *)payload;
    icmp->type     = ICMP_ECHO_REQUEST;
    icmp->code     = 0;
    icmp->id       = htons(0x4D4F);  // 'MO'
    icmp->seq      = htons(seq);
    icmp->checksum = 0;
    icmp->checksum = ip_checksum(payload, sizeof(payload));
    ip_send(dst_ip, IP_PROTO_ICMP, payload, sizeof(payload));
}

// ============================================================
// UDP
// ============================================================

void udp_handle(ip_addr_t src_ip, const u8 *pkt, u16 len) {
    if (len < (u16)sizeof(udp_hdr_t)) return;
    udp_hdr_t *udp = (udp_hdr_t *)pkt;
    (void)src_ip;
    (void)udp;
    // Future: dispatch by dst_port to registered listeners
}

int udp_send(ip_addr_t dst_ip, u16 src_port, u16 dst_port,
             const u8 *data, u16 dlen) {
    u16 udp_len = (u16)(sizeof(udp_hdr_t) + dlen);
    u8  buf[sizeof(udp_hdr_t) + 512];
    if (dlen > 512) return -1;

    udp_hdr_t *udp = (udp_hdr_t *)buf;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons(udp_len);
    udp->checksum = 0;
    memcpy_n(buf + sizeof(udp_hdr_t), data, dlen);

    return ip_send(dst_ip, IP_PROTO_UDP, buf, udp_len);
}

// ============================================================
// Shell helpers
// ============================================================

void net_cmd_ifconfig(void) {
    kprint("eth0: flags=UP BROADCAST RUNNING\n");
    kprint("      inet ");
    kprint_ip(net_ip);
    kprint("  netmask 255.255.255.0\n");
    kprint("      ether ");
    kprint_mac(&net_mac);
    kprint("\n");
    if (!net_iobase) kprint("      [NIC not found]\n");
}

void net_cmd_ping(ip_addr_t target) {
    kprint("PING ");
    kprint_ip(target);
    kprint(" with 4 packets:\n");

    for (u16 seq = 1; seq <= 4; seq++) {
        icmp_echo_received = 0;
        icmp_send_echo(target, seq);

        // Poll up to ~1 second for the reply
        unsigned int wait = 2000000;
        while (wait-- && !icmp_echo_received) net_poll();

        kprint("  seq=");
        kprint_dec(seq);
        if (icmp_echo_received) {
            kprint(" reply received\n");
        } else {
            kprint(" request timeout\n");
        }
    }
}

void net_cmd_arp(void) {
    kprint("ARP cache:\n");
    arp_print_cache();
}

// ============================================================
// Parse "a.b.c.d" -> ip_addr_t
// ============================================================

int parse_ip(const char *s, ip_addr_t *out) {
    u32 ip  = 0;
    int dots = 0;
    u32 octet = 0;

    while (*s) {
        if (isdigit_n(*s)) {
            octet = octet * 10 + (*s - '0');
            if (octet > 255) return 0;
        } else if (*s == '.') {
            ip = (ip << 8) | octet;
            octet = 0;
            dots++;
            if (dots > 3) return 0;
        } else {
            return 0;
        }
        s++;
    }
    if (dots != 3) return 0;
    ip = (ip << 8) | octet;
    *out = ip;
    return 1;
}

// ============================================================
// Called once during kernel init
// ============================================================
void net_stack_init(void) {
    arp_cache_init();
    net_init();
}
