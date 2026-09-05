#include "tju_tcp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>

#define NSEC_PER_SEC 1000000000ULL
#define INITIAL_RTO_NS NSEC_PER_SEC
#define MIN_RTO_NS NSEC_PER_SEC
#define MAX_RTO_NS (60ULL * NSEC_PER_SEC)
#define LOSS_PROBE_NS (20ULL * 1000ULL * 1000ULL)
#define MAX_LOSS_PROBES 3U
#define FAST_LOSS_BETA_NUM 7U
#define FAST_LOSS_BETA_DEN 10U
#define HANDSHAKE_RESET_RTO_NS (3ULL * NSEC_PER_SEC)
#define TJU_MAX_RETRIES 5U
#define TJU_MSL_SECONDS 5U
#ifndef TJU_INITIAL_CWND
#define TJU_INITIAL_CWND (2U * MAX_DLEN)
#endif

typedef struct tx_segment {
    uint32_t seq;
    uint32_t end;
    uint8_t flags;
    char* data;
    uint16_t len;
    bool sent;
    bool retransmitted;
    unsigned retries;
    uint64_t first_sent_ns;
    uint64_t last_sent_ns;
    struct tx_segment* next;
} tx_segment;

typedef struct rx_segment {
    uint32_t seq;
    uint16_t len;
    char* data;
    struct rx_segment* next;
} rx_segment;

typedef struct accept_node {
    tju_tcp_t* sock;
    struct accept_node* next;
} accept_node;

typedef enum {
    RENO_SLOW_START,
    RENO_CONGESTION_AVOIDANCE,
    RENO_FAST_RECOVERY
} reno_phase;

typedef struct tju_internal {
    pthread_mutex_t lock;
    pthread_cond_t state_cv;
    pthread_cond_t recv_cv;
    pthread_cond_t send_cv;
    pthread_cond_t accept_cv;
    pthread_cond_t timer_cv;
    pthread_cond_t dispatch_cv;
    pthread_t timer_thread;
    bool timer_started;
    bool stop;
    bool destroying;
    unsigned dispatch_refs;
    int error;

    tju_tcp_t* owner;
    tju_tcp_t* listener;
    accept_node* accept_head;
    accept_node* accept_tail;
    bool accept_enqueued;

    uint32_t iss;
    uint32_t irs;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t snd_write;
    uint32_t rcv_nxt;
    uint32_t peer_wnd;
    uint32_t last_adv_window;
    uint64_t cwnd;
    uint64_t ssthresh;
    uint64_t ca_accumulator;
    reno_phase reno;
    bool fast_recovery_pending;
    bool timeout_recovery_pending;
    uint32_t recovery_point;

    tx_segment* tx_head;
    tx_segment* tx_tail;
    size_t tx_buffered;
    rx_segment* rx_head;
    size_t rx_ooo_bytes;

    char* recv_ring;
    size_t recv_head;
    size_t recv_tail;
    size_t recv_used;

    uint32_t dup_ack;
    unsigned dup_ack_count;
    bool have_rtt;
    bool last_sample_valid;
    double last_sample_rtt;
    double srtt;
    double rttvar;
    uint64_t rto_ns;
    bool handshake_retransmitted;
    unsigned loss_probe_count;
    uint64_t persist_deadline_ns;
    uint64_t persist_interval_ns;

    bool send_closed;
    bool recv_fin;
    bool fin_pending;
    uint32_t fin_seq;
    uint64_t time_wait_deadline_ns;
} tju_internal;

static pthread_once_t isn_once = PTHREAD_ONCE_INIT;
static uint64_t isn_secret;
static pthread_mutex_t isn_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t isn_counter;
static pthread_mutex_t port_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t next_client_port = 5678;
static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE* trace_file;

static void destroy_internal(tju_tcp_t* sock, tju_internal* in);
static int finish_connection_locked(tju_tcp_t* sock, tju_internal* in, int result);

int tju_retain_for_dispatch(tju_tcp_t* sock){
    if(sock == NULL || sock->internal == NULL) return 0;
    tju_internal* in = sock->internal;
    pthread_mutex_lock(&in->lock);
    int retained = !in->destroying;
    if(retained) in->dispatch_refs++;
    pthread_mutex_unlock(&in->lock);
    return retained;
}

void tju_release_after_dispatch(tju_tcp_t* sock){
    tju_internal* in = sock->internal;
    pthread_mutex_lock(&in->lock);
    if(--in->dispatch_refs == 0) pthread_cond_broadcast(&in->dispatch_cv);
    pthread_mutex_unlock(&in->lock);
}

static uint64_t monotonic_ns(void){
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * NSEC_PER_SEC + (uint64_t)now.tv_nsec;
}

static struct timespec ns_to_timespec(uint64_t value){
    struct timespec result;
    result.tv_sec = (time_t)(value / NSEC_PER_SEC);
    result.tv_nsec = (long)(value % NSEC_PER_SEC);
    return result;
}

static bool seq_lt(uint32_t a, uint32_t b){ return (int32_t)(a - b) < 0; }
static bool seq_le(uint32_t a, uint32_t b){ return (int32_t)(a - b) <= 0; }
static bool seq_gt(uint32_t a, uint32_t b){ return (int32_t)(a - b) > 0; }
static bool seq_between(uint32_t value, uint32_t left, uint32_t right){
    return !seq_lt(value, left) && !seq_gt(value, right);
}

int tju_seq_before(uint32_t left, uint32_t right){ return seq_lt(left, right); }
int tju_seq_after(uint32_t left, uint32_t right){ return seq_gt(left, right); }

uint16_t tju_window_from_space(size_t available_space){
    return available_space > UINT16_MAX ? UINT16_MAX : (uint16_t)available_space;
}

int tju_validate_packet(const char* pkt, int packet_len){
    if(pkt == NULL || packet_len < DEFAULT_HEADER_LEN || packet_len > MAX_LEN) return 0;
    uint16_t hlen = get_hlen((char*)pkt);
    uint16_t plen = get_plen((char*)pkt);
    return hlen == DEFAULT_HEADER_LEN && plen >= hlen && plen == (uint16_t)packet_len;
}

static uint64_t mix64(uint64_t x){
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static void initialize_isn_secret(void){
    int fd = open("/dev/urandom", O_RDONLY);
    if(fd >= 0){
        ssize_t got = read(fd, &isn_secret, sizeof(isn_secret));
        close(fd);
        if(got == (ssize_t)sizeof(isn_secret)) return;
    }
    isn_secret = mix64(monotonic_ns() ^ (uint64_t)getpid() ^ (uintptr_t)&isn_secret);
}

static uint32_t generate_isn(tju_sock_addr local, tju_sock_addr remote){
    pthread_once(&isn_once, initialize_isn_secret);
    pthread_mutex_lock(&isn_lock);
    uint64_t count = ++isn_counter;
    pthread_mutex_unlock(&isn_lock);
    uint64_t tuple = ((uint64_t)local.ip << 32) ^ remote.ip;
    tuple ^= ((uint64_t)local.port << 16) ^ remote.port;
    uint64_t varying = monotonic_ns() / 4000ULL;
    return (uint32_t)mix64(isn_secret ^ tuple ^ varying ^ count);
}

static const char* state_name(int state){
    switch(state){
        case CLOSED: return "CLOSED"; case LISTEN: return "LISTEN";
        case SYN_SENT: return "SYN_SENT"; case SYN_RECV: return "SYN_RECV";
        case ESTABLISHED: return "ESTABLISHED"; case FIN_WAIT_1: return "FIN_WAIT_1";
        case FIN_WAIT_2: return "FIN_WAIT_2"; case CLOSE_WAIT: return "CLOSE_WAIT";
        case CLOSING: return "CLOSING"; case LAST_ACK: return "LAST_ACK";
        case TIME_WAIT: return "TIME_WAIT"; default: return "UNKNOWN";
    }
}

static const char* reno_name(reno_phase phase){
    switch(phase){
        case RENO_SLOW_START: return "SLOW_START";
        case RENO_CONGESTION_AVOIDANCE: return "CONGESTION_AVOIDANCE";
        case RENO_FAST_RECOVERY: return "FAST_RECOVERY";
        default: return "UNKNOWN";
    }
}

static void trace_event(tju_internal* in, const char* event, uint32_t seq,
                        uint32_t ack, bool retransmitted){
    pthread_mutex_lock(&trace_lock);
    if(trace_file == NULL){
        char hostname[64] = {0};
        char filename[96] = {0};
        gethostname(hostname, sizeof(hostname) - 1);
        snprintf(filename, sizeof(filename), "%s.event.trace", hostname);
        trace_file = fopen(filename, "a");
        if(trace_file != NULL) setvbuf(trace_file, NULL, _IOFBF, 1024 * 1024);
    }
    if(trace_file != NULL){
        uint64_t now = monotonic_ns();
        uint32_t flight = in->snd_nxt - in->snd_una;
        fprintf(trace_file,
                "timestamp_ns=%llu event=%s state=%s seq=%u ack=%u snd_una=%u "
                "snd_nxt=%u rcv_nxt=%u rwnd=%u recv_used=%zu right_edge=%u "
                "flight=%u cwnd=%llu ssthresh=%llu reno_phase=%s sample_rtt=",
                (unsigned long long)now, event, state_name(in->owner->state), seq, ack,
                in->snd_una, in->snd_nxt, in->rcv_nxt, in->peer_wnd,
                in->recv_used + in->rx_ooo_bytes, in->snd_una + in->peer_wnd,
                flight, (unsigned long long)in->cwnd,
                (unsigned long long)in->ssthresh, reno_name(in->reno));
        if(in->last_sample_valid) fprintf(trace_file, "%.6f", in->last_sample_rtt);
        else fputs("NA", trace_file);
        fprintf(trace_file,
                " srtt=%.6f rttvar=%.6f rto=%.6f retransmitted=%d dupack=%u\n",
                in->srtt, in->rttvar, in->rto_ns / 1e9,
                retransmitted ? 1 : 0, in->dup_ack_count);
    }
    pthread_mutex_unlock(&trace_lock);
}

static void set_state_locked(tju_internal* in, int state, const char* event){
    if(in->owner->state != state){
        in->owner->state = state;
        trace_event(in, event, in->snd_nxt, in->rcv_nxt, false);
        pthread_cond_broadcast(&in->state_cv);
    }
}

static uint16_t advertised_window_locked(tju_internal* in){
    size_t used = in->recv_used + in->rx_ooo_bytes;
    size_t space = used < TCP_RECVWN_SIZE ? TCP_RECVWN_SIZE - used : 0;
    if(space < MAX_DLEN && used != 0) space = 0;
    return tju_window_from_space(space);
}

static void send_packet_locked(tju_internal* in, uint32_t seq, uint32_t ack,
                               uint8_t flags, const char* data, uint16_t len,
                               const char* event, bool retransmitted){
    uint16_t window = advertised_window_locked(in);
    char* packet = create_packet_buf(in->owner->established_local_addr.port,
                                     in->owner->established_remote_addr.port,
                                     seq, ack, DEFAULT_HEADER_LEN,
                                     (uint16_t)(DEFAULT_HEADER_LEN + len), flags,
                                     window, 0, (char*)data, len);
    if(packet != NULL){
        trace_event(in, event, seq, ack, retransmitted);
        sendToLayer3(packet, DEFAULT_HEADER_LEN + len);
        free(packet);
    }
    in->last_adv_window = window;
}

static tx_segment* make_tx(uint32_t seq, uint8_t flags, const char* data, uint16_t len){
    tx_segment* node = calloc(1, sizeof(*node));
    if(node == NULL) return NULL;
    node->seq = seq;
    node->len = len;
    node->flags = flags;
    node->end = seq + len + ((flags & (SYN_FLAG_MASK | FIN_FLAG_MASK)) ? 1U : 0U);
    if(len != 0){
        node->data = malloc(len);
        if(node->data == NULL){ free(node); return NULL; }
        memcpy(node->data, data, len);
    }
    return node;
}

static void append_tx_locked(tju_internal* in, tx_segment* node){
    if(in->tx_tail != NULL) in->tx_tail->next = node;
    else in->tx_head = node;
    in->tx_tail = node;
    in->tx_buffered += node->len;
}

static void send_tx_node_locked(tju_internal* in, tx_segment* node, bool retransmit){
    uint8_t flags = node->flags;
    uint32_t ack = 0;
    if(flags & ACK_FLAG_MASK){ ack = in->rcv_nxt; }
    send_packet_locked(in, node->seq, ack, flags, node->data, node->len,
                       retransmit ? "RETRANSMIT" : "SEND", retransmit);
    uint64_t now = monotonic_ns();
    if(!node->sent){
        node->sent = true;
        node->first_sent_ns = now;
        in->snd_nxt = node->end;
    }else{
        node->retransmitted = true;
        node->retries++;
        if(node->flags & SYN_FLAG_MASK) in->handshake_retransmitted = true;
    }
    node->last_sent_ns = now;
    pthread_cond_signal(&in->timer_cv);
}

static void flush_send_locked(tju_internal* in){
    uint64_t congestion_window = in->cwnd;
    if(!in->fast_recovery_pending && in->dup_ack_count > 0 && in->dup_ack_count < 3)
        congestion_window += (uint64_t)in->dup_ack_count * MAX_DLEN;
    uint64_t send_window = in->peer_wnd < congestion_window ? in->peer_wnd : congestion_window;
    uint32_t right_edge = in->snd_una + (uint32_t)send_window;
    for(tx_segment* node = in->tx_head; node != NULL; node = node->next){
        if(node->sent) continue;
        bool control = (node->flags & (SYN_FLAG_MASK | FIN_FLAG_MASK)) != 0;
        if(!control && (in->peer_wnd == 0 || seq_gt(node->end, right_edge))) break;
        send_tx_node_locked(in, node, false);
    }
    if(in->peer_wnd == 0 && in->tx_head != NULL && in->persist_deadline_ns == 0){
        in->persist_interval_ns = in->rto_ns;
        in->persist_deadline_ns = monotonic_ns() + in->persist_interval_ns;
        pthread_cond_signal(&in->timer_cv);
    }
}

static void reno_ack_locked(tju_internal* in, size_t acknowledged){
    if(acknowledged == 0) return;
    uint64_t credit = acknowledged > MAX_DLEN ? MAX_DLEN : acknowledged;
    if(in->cwnd < in->ssthresh){
        uint64_t next = in->cwnd + credit;
        in->cwnd = next < in->ssthresh ? next : in->ssthresh;
        in->reno = in->cwnd < in->ssthresh ? RENO_SLOW_START : RENO_CONGESTION_AVOIDANCE;
        if(in->reno == RENO_CONGESTION_AVOIDANCE) in->ca_accumulator = 0;
        return;
    }
    in->reno = RENO_CONGESTION_AVOIDANCE;
    uint64_t numerator = (uint64_t)MAX_DLEN * MAX_DLEN;
    in->ca_accumulator += numerator;
    uint64_t increase = in->ca_accumulator / in->cwnd;
    in->ca_accumulator %= in->cwnd;
    if(increase != 0) in->cwnd += increase;
}

void tju_rto_update_values(int* have_sample, double* srtt, double* rttvar,
                           double* rto_seconds, double sample){
    if(!*have_sample){
        *srtt = sample;
        *rttvar = sample / 2.0;
        *have_sample = 1;
    }else{
        double deviation = *srtt > sample ? *srtt - sample : sample - *srtt;
        *rttvar = 0.75 * *rttvar + 0.25 * deviation;
        *srtt = 0.875 * *srtt + 0.125 * sample;
    }
    double variance_term = 4.0 * *rttvar;
    if(variance_term < 0.001) variance_term = 0.001;
    double rto = *srtt + variance_term;
    if(rto < 1.0) rto = 1.0;
    if(rto > 60.0) rto = 60.0;
    *rto_seconds = rto;
}

static void update_rtt_locked(tju_internal* in, double sample){
    int have_sample = in->have_rtt ? 1 : 0;
    double rto = in->rto_ns / 1e9;
    tju_rto_update_values(&have_sample, &in->srtt, &in->rttvar, &rto, sample);
    in->have_rtt = have_sample != 0;
    in->last_sample_valid = true;
    in->last_sample_rtt = sample;
    in->rto_ns = (uint64_t)(rto * 1e9);
}

static void free_tx_node(tx_segment* node){
    free(node->data);
    free(node);
}

static bool acknowledge_locked(tju_internal* in, uint32_t ack, bool eligible_dup){
    if(!seq_between(ack, in->snd_una, in->snd_nxt)) return false;
    if(ack == in->snd_una){
        if(eligible_dup && !in->timeout_recovery_pending &&
           in->tx_head != NULL && in->tx_head->sent){
            if(in->dup_ack == ack) in->dup_ack_count++;
            else { in->dup_ack = ack; in->dup_ack_count = 1; }
            if(in->dup_ack_count == 3){
                uint64_t flight = in->snd_nxt - in->snd_una;
                /* Use CUBIC's 0.7 multiplicative decrease for losses found by
                 * duplicate ACKs.  An RTO still performs Reno's 0.5 decrease. */
                in->ssthresh = flight * FAST_LOSS_BETA_NUM / FAST_LOSS_BETA_DEN;
                if(in->ssthresh < 2U * MAX_DLEN) in->ssthresh = 2U * MAX_DLEN;
                in->cwnd = in->ssthresh + 3U * MAX_DLEN;
                in->reno = RENO_FAST_RECOVERY;
                in->fast_recovery_pending = true;
                in->recovery_point = in->snd_nxt;
                in->ca_accumulator = 0;
                send_tx_node_locked(in, in->tx_head, true);
                trace_event(in, "FAST_RETRANSMIT", in->tx_head->seq, ack, true);
            }else if(in->dup_ack_count > 3 && in->fast_recovery_pending){
                in->cwnd += MAX_DLEN;
                flush_send_locked(in);
            }else if(in->dup_ack_count < 3){
                flush_send_locked(in);
            }
        }
        return false;
    }

    uint64_t now = monotonic_ns();
    in->last_sample_valid = false;
    bool ambiguous_rtt = false;
    uint64_t sample_sent_ns = 0;
    size_t acknowledged_payload = 0;
    for(tx_segment* candidate = in->tx_head;
        candidate != NULL && candidate->sent && seq_le(candidate->end, ack);
        candidate = candidate->next){
        if(candidate->retransmitted) ambiguous_rtt = true;
        if(sample_sent_ns == 0) sample_sent_ns = candidate->first_sent_ns;
    }
    while(in->tx_head != NULL && in->tx_head->sent && seq_le(in->tx_head->end, ack)){
        tx_segment* done = in->tx_head;
        in->tx_head = done->next;
        if(in->tx_head == NULL) in->tx_tail = NULL;
        if(done->len <= in->tx_buffered) in->tx_buffered -= done->len;
        acknowledged_payload += done->len;
        free_tx_node(done);
    }
    if(!ambiguous_rtt && sample_sent_ns != 0)
        update_rtt_locked(in, (now - sample_sent_ns) / 1e9);
    in->snd_una = ack;
    in->loss_probe_count = 0;
    in->dup_ack = ack;
    in->dup_ack_count = 0;
    if(in->timeout_recovery_pending){
        if(seq_lt(ack, in->recovery_point) && in->tx_head != NULL && in->tx_head->sent){
            send_tx_node_locked(in, in->tx_head, true);
            trace_event(in, "ACK_DRIVEN_RETRANSMIT", in->tx_head->seq, ack, true);
        }else{
            in->timeout_recovery_pending = false;
            in->recovery_point = 0;
            reno_ack_locked(in, acknowledged_payload);
        }
    }else if(in->fast_recovery_pending){
        if(seq_lt(ack, in->recovery_point) && in->tx_head != NULL){
            in->cwnd = in->ssthresh + 3U * MAX_DLEN;
            in->reno = RENO_FAST_RECOVERY;
            send_tx_node_locked(in, in->tx_head, true);
            trace_event(in, "FAST_RETRANSMIT", in->tx_head->seq, ack, true);
        }else{
            in->cwnd = in->ssthresh;
            in->reno = RENO_CONGESTION_AVOIDANCE;
            in->fast_recovery_pending = false;
            in->recovery_point = 0;
            in->ca_accumulator = 0;
        }
    }else{
        reno_ack_locked(in, acknowledged_payload);
    }
    in->persist_deadline_ns = 0;
    pthread_cond_broadcast(&in->send_cv);
    pthread_cond_signal(&in->timer_cv);
    flush_send_locked(in);
    trace_event(in, "ACK_ADVANCE", in->snd_nxt, ack, false);
    in->last_sample_valid = false;
    return true;
}

static void send_ack_locked(tju_internal* in, const char* event){
    send_packet_locked(in, in->snd_nxt, in->rcv_nxt, ACK_FLAG_MASK, NULL, 0,
                       event, false);
}

static size_t ring_write_locked(tju_internal* in, const char* data, size_t len){
    size_t free_space = TCP_RECVWN_SIZE - in->recv_used;
    if(len > free_space) len = free_space;
    size_t first = len;
    if(first > TCP_RECVWN_SIZE - in->recv_tail) first = TCP_RECVWN_SIZE - in->recv_tail;
    memcpy(in->recv_ring + in->recv_tail, data, first);
    memcpy(in->recv_ring, data + first, len - first);
    in->recv_tail = (in->recv_tail + len) % TCP_RECVWN_SIZE;
    in->recv_used += len;
    in->owner->received_len = (int)in->recv_used;
    return len;
}

static void recalc_ooo_locked(tju_internal* in){
    size_t total = 0;
    for(rx_segment* node = in->rx_head; node != NULL; node = node->next) total += node->len;
    in->rx_ooo_bytes = total;
}

static void insert_rx_locked(tju_internal* in, uint32_t seq, const char* data, uint16_t len){
    int32_t delta = (int32_t)(seq - in->rcv_nxt);
    if(delta < 0){
        uint32_t trim = (uint32_t)(-delta);
        if(trim >= len) return;
        seq += trim; data += trim; len = (uint16_t)(len - trim); delta = 0;
    }
    size_t occupied = in->recv_used + in->rx_ooo_bytes;
    if(occupied >= TCP_RECVWN_SIZE) return;
    size_t room = TCP_RECVWN_SIZE - occupied;
    if((uint32_t)delta >= room) return;
    if((size_t)delta + len > room) len = (uint16_t)(room - (size_t)delta);
    if(len == 0) return;

    uint32_t new_start = (uint32_t)delta;
    uint32_t new_end = new_start + len;
    rx_segment** link = &in->rx_head;
    while(*link != NULL && (uint32_t)((*link)->seq - in->rcv_nxt) + (*link)->len < new_start)
        link = &(*link)->next;

    uint32_t union_start = new_start, union_end = new_end;
    rx_segment* scan = *link;
    while(scan != NULL){
        uint32_t start = scan->seq - in->rcv_nxt;
        if(start > union_end) break;
        uint32_t end = start + scan->len;
        if(start < union_start) union_start = start;
        if(end > union_end) union_end = end;
        scan = scan->next;
    }
    size_t union_len = union_end - union_start;
    char* merged = calloc(1, union_len);
    if(merged == NULL) return;
    scan = *link;
    while(scan != NULL){
        uint32_t start = scan->seq - in->rcv_nxt;
        if(start > union_end) break;
        memcpy(merged + start - union_start, scan->data, scan->len);
        rx_segment* old = scan;
        scan = scan->next;
        free(old->data); free(old);
    }
    memcpy(merged + new_start - union_start, data, len);
    rx_segment* node = calloc(1, sizeof(*node));
    if(node == NULL){ free(merged); *link = scan; recalc_ooo_locked(in); return; }
    node->seq = in->rcv_nxt + union_start;
    node->len = (uint16_t)union_len;
    node->data = merged;
    node->next = scan;
    *link = node;
    recalc_ooo_locked(in);

    while(in->rx_head != NULL && in->rx_head->seq == in->rcv_nxt){
        rx_segment* ready = in->rx_head;
        in->rx_head = ready->next;
        size_t delivered = ring_write_locked(in, ready->data, ready->len);
        in->rcv_nxt += (uint32_t)delivered;
        free(ready->data); free(ready);
        if(delivered == 0) break;
    }
    recalc_ooo_locked(in);
    if(in->recv_used != 0) pthread_cond_broadcast(&in->recv_cv);
}

static void enter_time_wait_locked(tju_internal* in, const char* event){
    set_state_locked(in, TIME_WAIT, event);
    in->time_wait_deadline_ns = monotonic_ns() + 2ULL * TJU_MSL_SECONDS * NSEC_PER_SEC;
    pthread_cond_signal(&in->timer_cv);
}

static void consume_fin_if_ready_locked(tju_internal* in){
    if(!in->fin_pending || in->fin_seq != in->rcv_nxt) return;
    in->fin_pending = false;
    in->recv_fin = true;
    in->rcv_nxt++;
    if(in->owner->state == ESTABLISHED) set_state_locked(in, CLOSE_WAIT, "RX_FIN");
    else if(in->owner->state == FIN_WAIT_1) set_state_locked(in, CLOSING, "SIMULTANEOUS_FIN");
    else if(in->owner->state == FIN_WAIT_2) enter_time_wait_locked(in, "RX_FIN");
    else if(in->owner->state == TIME_WAIT)
        in->time_wait_deadline_ns = monotonic_ns() + 2ULL * TJU_MSL_SECONDS * NSEC_PER_SEC;
    pthread_cond_broadcast(&in->recv_cv);
}

static void fail_connection_locked(tju_internal* in, const char* event){
    in->error = -1;
    set_state_locked(in, CLOSED, event);
    pthread_cond_broadcast(&in->recv_cv);
    pthread_cond_broadcast(&in->send_cv);
}

static uint64_t next_deadline_locked(tju_internal* in){
    uint64_t deadline = 0;
    if(in->owner->state == TIME_WAIT) deadline = in->time_wait_deadline_ns;
    if(in->tx_head != NULL && in->tx_head->sent){
        uint64_t retransmit = in->tx_head->last_sent_ns + in->rto_ns;
        if(deadline == 0 || retransmit < deadline) deadline = retransmit;
        if(in->owner->state == ESTABLISHED && in->tx_head->len != 0 &&
           in->loss_probe_count < MAX_LOSS_PROBES){
            uint64_t probe = in->tx_head->last_sent_ns + LOSS_PROBE_NS;
            if(probe < deadline) deadline = probe;
        }
    }
    if(in->persist_deadline_ns != 0 && (deadline == 0 || in->persist_deadline_ns < deadline))
        deadline = in->persist_deadline_ns;
    return deadline;
}

static void* timer_main(void* argument){
    tju_internal* in = argument;
    pthread_mutex_lock(&in->lock);
    while(!in->stop){
        if(in->error != 0){
            pthread_cond_wait(&in->timer_cv, &in->lock);
            continue;
        }
        uint64_t deadline = next_deadline_locked(in);
        if(deadline == 0){
            pthread_cond_wait(&in->timer_cv, &in->lock);
            continue;
        }
        uint64_t now = monotonic_ns();
        if(now < deadline){
            struct timespec until = ns_to_timespec(deadline);
            pthread_cond_timedwait(&in->timer_cv, &in->lock, &until);
            continue;
        }
        if(in->owner->state == TIME_WAIT && in->time_wait_deadline_ns <= now){
            set_state_locked(in, CLOSED, "TIME_WAIT_EXPIRE");
            continue;
        }
        if(in->persist_deadline_ns != 0 && in->persist_deadline_ns <= now){
            send_packet_locked(in, in->snd_una - 1U, in->rcv_nxt, ACK_FLAG_MASK,
                               NULL, 0, "ZERO_WINDOW_PROBE", false);
            if(in->persist_interval_ns < MAX_RTO_NS / 2) in->persist_interval_ns *= 2;
            else in->persist_interval_ns = MAX_RTO_NS;
            in->persist_deadline_ns = now + in->persist_interval_ns;
            continue;
        }
        tx_segment* node = in->tx_head;
        if(node != NULL && node->sent && node->len != 0 &&
           in->loss_probe_count < MAX_LOSS_PROBES &&
           node->last_sent_ns + LOSS_PROBE_NS <= now){
            in->loss_probe_count++;
            send_tx_node_locked(in, node, true);
            trace_event(in, "LOSS_PROBE", node->seq, in->rcv_nxt, true);
            continue;
        }
        if(node != NULL && node->sent && node->last_sent_ns + in->rto_ns <= now){
            if(node->retries >= TJU_MAX_RETRIES){
                fail_connection_locked(in, "RETRY_LIMIT");
                continue;
            }
            if(node->len != 0){
                uint64_t flight = in->snd_nxt - in->snd_una;
                in->ssthresh = flight / 2U;
                if(in->ssthresh < 2U * MAX_DLEN) in->ssthresh = 2U * MAX_DLEN;
                in->cwnd = MAX_DLEN;
                in->reno = RENO_SLOW_START;
                in->fast_recovery_pending = false;
                in->timeout_recovery_pending = true;
                in->recovery_point = in->snd_nxt;
                in->ca_accumulator = 0;
                in->dup_ack = in->snd_una;
                in->dup_ack_count = 0;
            }
            send_tx_node_locked(in, node, true);
            if(in->rto_ns < MAX_RTO_NS / 2) in->rto_ns *= 2;
            else in->rto_ns = MAX_RTO_NS;
            trace_event(in, "RTO_FIRE", node->seq, in->rcv_nxt, true);
        }
    }
    pthread_mutex_unlock(&in->lock);
    return NULL;
}

static int initialize_internal(tju_tcp_t* sock){
    tju_internal* in = calloc(1, sizeof(*in));
    if(in == NULL) return -1;
    in->owner = sock;
    in->rto_ns = INITIAL_RTO_NS;
    in->peer_wnd = UINT16_MAX;
    in->cwnd = TJU_INITIAL_CWND;
    in->ssthresh = TCP_RECVWN_SIZE;
    in->reno = RENO_SLOW_START;
    in->recv_ring = malloc(TCP_RECVWN_SIZE);
    if(in->recv_ring == NULL){ free(in); return -1; }
    pthread_mutex_init(&in->lock, NULL);
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&in->state_cv, &attr);
    pthread_cond_init(&in->recv_cv, &attr);
    pthread_cond_init(&in->send_cv, &attr);
    pthread_cond_init(&in->accept_cv, &attr);
    pthread_cond_init(&in->timer_cv, &attr);
    pthread_cond_init(&in->dispatch_cv, &attr);
    pthread_condattr_destroy(&attr);
    sock->internal = in;
    if(pthread_create(&in->timer_thread, NULL, timer_main, in) != 0){
        free(in->recv_ring); free(in); sock->internal = NULL; return -1;
    }
    in->timer_started = true;
    return 0;
}

static int finish_connection_locked(tju_tcp_t* sock, tju_internal* in, int result){
    in->stop = true;
    in->destroying = true;
    pthread_cond_broadcast(&in->timer_cv);
    pthread_mutex_unlock(&in->lock);
    if(in->timer_started) pthread_join(in->timer_thread, NULL);
    kernel_remove_connection(sock);
    pthread_mutex_lock(&in->lock);
    while(in->dispatch_refs != 0) pthread_cond_wait(&in->dispatch_cv, &in->lock);
    pthread_mutex_unlock(&in->lock);
    destroy_internal(sock, in);
    return result;
}

tju_tcp_t* tju_socket(){
    tju_tcp_t* sock = calloc(1, sizeof(*sock));
    if(sock == NULL) return NULL;
    sock->state = CLOSED;
    pthread_mutex_init(&sock->send_lock, NULL);
    pthread_mutex_init(&sock->recv_lock, NULL);
    pthread_cond_init(&sock->wait_cond, NULL);
    sock->window.wnd_send = calloc(1, sizeof(sender_window_t));
    sock->window.wnd_recv = calloc(1, sizeof(receiver_window_t));
    if(sock->window.wnd_send == NULL || sock->window.wnd_recv == NULL ||
       initialize_internal(sock) != 0){
        free(sock->window.wnd_send); free(sock->window.wnd_recv);
        pthread_cond_destroy(&sock->wait_cond);
        pthread_mutex_destroy(&sock->recv_lock);
        pthread_mutex_destroy(&sock->send_lock);
        free(sock);
        return NULL;
    }
    return sock;
}

int tju_bind(tju_tcp_t* sock, tju_sock_addr bind_addr){
    if(sock == NULL || sock->internal == NULL || bind_addr.port == 0) return -1;
    tju_internal* in = sock->internal;
    pthread_mutex_lock(&in->lock);
    if(sock->state != CLOSED){ pthread_mutex_unlock(&in->lock); return -1; }
    sock->bind_addr = bind_addr;
    pthread_mutex_unlock(&in->lock);
    return 0;
}

int tju_listen(tju_tcp_t* sock){
    if(sock == NULL || sock->internal == NULL || sock->bind_addr.port == 0) return -1;
    tju_internal* in = sock->internal;
    pthread_mutex_lock(&in->lock);
    if(sock->state != CLOSED || kernel_register_listener(sock) != 0){
        pthread_mutex_unlock(&in->lock); return -1;
    }
    set_state_locked(in, LISTEN, "LISTEN");
    pthread_mutex_unlock(&in->lock);
    return 0;
}

tju_tcp_t* tju_accept(tju_tcp_t* sock){
    if(sock == NULL || sock->internal == NULL) return NULL;
    tju_internal* in = sock->internal;
    pthread_mutex_lock(&in->lock);
    while(in->accept_head == NULL && sock->state == LISTEN)
        pthread_cond_wait(&in->accept_cv, &in->lock);
    if(in->accept_head == NULL){ pthread_mutex_unlock(&in->lock); return NULL; }
    accept_node* node = in->accept_head;
    in->accept_head = node->next;
    if(in->accept_head == NULL) in->accept_tail = NULL;
    tju_tcp_t* child = node->sock;
    free(node);
    pthread_mutex_unlock(&in->lock);
    return child;
}

int tju_connect(tju_tcp_t* sock, tju_sock_addr target_addr){
    if(sock == NULL || sock->internal == NULL || target_addr.port == 0) return -1;
    tju_internal* in = sock->internal;
    pthread_mutex_lock(&in->lock);
    if(sock->state != CLOSED){ pthread_mutex_unlock(&in->lock); return -1; }
    pthread_mutex_lock(&port_lock);
    uint16_t local_port = next_client_port++;
    if(next_client_port < 5678) next_client_port = 5678;
    pthread_mutex_unlock(&port_lock);
    sock->established_local_addr.ip = inet_network(CLIENT_IP);
    sock->established_local_addr.port = local_port;
    sock->established_remote_addr = target_addr;
    in->iss = generate_isn(sock->established_local_addr, target_addr);
    in->snd_una = in->iss;
    in->snd_nxt = in->iss;
    in->snd_write = in->iss + 1U;
    if(kernel_register_connection(sock) != 0){ pthread_mutex_unlock(&in->lock); return -1; }
    tx_segment* syn = make_tx(in->iss, SYN_FLAG_MASK, NULL, 0);
    if(syn == NULL){ kernel_remove_connection(sock); pthread_mutex_unlock(&in->lock); return -1; }
    append_tx_locked(in, syn);
    set_state_locked(in, SYN_SENT, "ACTIVE_OPEN");
    send_tx_node_locked(in, syn, false);
    while(sock->state == SYN_SENT && in->error == 0)
        pthread_cond_wait(&in->state_cv, &in->lock);
    int result = sock->state == ESTABLISHED ? 0 : -1;
    if(result != 0) return finish_connection_locked(sock, in, result);
    pthread_mutex_unlock(&in->lock);
    return result;
}

int tju_send(tju_tcp_t* sock, const void* buffer, int len){
    if(sock == NULL || sock->internal == NULL || buffer == NULL || len < 0) return -1;
    if(len == 0) return 0;
    tju_internal* in = sock->internal;
    const char* bytes = buffer;
    int offset = 0;
    pthread_mutex_lock(&in->lock);
    if(sock->state != ESTABLISHED || in->send_closed){ pthread_mutex_unlock(&in->lock); return -1; }
    while(offset < len){
        while(in->tx_buffered >= TCP_RECVWN_SIZE && in->error == 0)
            pthread_cond_wait(&in->send_cv, &in->lock);
        if(in->error != 0 || in->send_closed){ pthread_mutex_unlock(&in->lock); return -1; }
        uint16_t chunk = (uint16_t)(len - offset > MAX_DLEN ? MAX_DLEN : len - offset);
        tx_segment* node = make_tx(in->snd_write, ACK_FLAG_MASK, bytes + offset, chunk);
        if(node == NULL){ pthread_mutex_unlock(&in->lock); return -1; }
        in->snd_write += chunk;
        append_tx_locked(in, node);
        offset += chunk;
        flush_send_locked(in);
    }
    pthread_mutex_unlock(&in->lock);
    return 0;
}

int tju_recv(tju_tcp_t* sock, void* buffer, int len){
    if(sock == NULL || sock->internal == NULL || buffer == NULL || len < 0) return -1;
    if(len == 0) return 0;
    tju_internal* in = sock->internal;
    pthread_mutex_lock(&in->lock);
    while(in->recv_used == 0 && !in->recv_fin && in->error == 0)
        pthread_cond_wait(&in->recv_cv, &in->lock);
    if(in->recv_used == 0){
        int result = in->recv_fin ? 0 : -1;
        pthread_mutex_unlock(&in->lock);
        return result;
    }
    size_t take = (size_t)len < in->recv_used ? (size_t)len : in->recv_used;
    size_t first = take;
    if(first > TCP_RECVWN_SIZE - in->recv_head) first = TCP_RECVWN_SIZE - in->recv_head;
    memcpy(buffer, in->recv_ring + in->recv_head, first);
    memcpy((char*)buffer + first, in->recv_ring, take - first);
    in->recv_head = (in->recv_head + take) % TCP_RECVWN_SIZE;
    in->recv_used -= take;
    sock->received_len = (int)in->recv_used;
    uint16_t window = advertised_window_locked(in);
    if(window > in->last_adv_window &&
       (in->last_adv_window == 0 || window - in->last_adv_window >= MAX_DLEN))
        send_ack_locked(in, "WINDOW_UPDATE");
    pthread_mutex_unlock(&in->lock);
    return (int)take;
}

static void enqueue_accepted_child(tju_internal* child){
    if(child->listener == NULL || child->accept_enqueued) return;
    tju_internal* listener = child->listener->internal;
    accept_node* node = calloc(1, sizeof(*node));
    if(node == NULL) return;
    node->sock = child->owner;
    pthread_mutex_lock(&listener->lock);
    if(listener->accept_tail != NULL) listener->accept_tail->next = node;
    else listener->accept_head = node;
    listener->accept_tail = node;
    child->accept_enqueued = true;
    pthread_cond_signal(&listener->accept_cv);
    pthread_mutex_unlock(&listener->lock);
}

static void handle_listen_syn(tju_tcp_t* listener_sock, const char* pkt){
    tju_internal* listener = listener_sock->internal;
    uint16_t remote_port = get_src((char*)pkt);
    uint32_t remote_seq = get_seq((char*)pkt);
    tju_tcp_t* child_sock = tju_socket();
    if(child_sock == NULL) return;
    tju_internal* child = child_sock->internal;
    pthread_mutex_lock(&child->lock);
    child_sock->established_local_addr = listener_sock->bind_addr;
    child_sock->established_remote_addr.ip = inet_network(CLIENT_IP);
    child_sock->established_remote_addr.port = remote_port;
    child->listener = listener_sock;
    child->irs = remote_seq;
    child->rcv_nxt = remote_seq + 1U;
    child->peer_wnd = get_advertised_window((char*)pkt);
    child->iss = generate_isn(child_sock->established_local_addr,
                              child_sock->established_remote_addr);
    child->snd_una = child->iss;
    child->snd_nxt = child->iss;
    child->snd_write = child->iss + 1U;
    set_state_locked(child, SYN_RECV, "RX_SYN");
    if(kernel_register_connection(child_sock) != 0){
        fail_connection_locked(child, "REGISTER_COLLISION");
        pthread_mutex_unlock(&child->lock);
        return;
    }
    tx_segment* syn_ack = make_tx(child->iss, SYN_FLAG_MASK | ACK_FLAG_MASK, NULL, 0);
    if(syn_ack != NULL){ append_tx_locked(child, syn_ack); send_tx_node_locked(child, syn_ack, false); }
    pthread_mutex_unlock(&child->lock);
    (void)listener;
}

int tju_handle_packet_len(tju_tcp_t* sock, char* pkt, int packet_len){
    if(sock == NULL || sock->internal == NULL || !tju_validate_packet(pkt, packet_len)) return -1;
    uint16_t hlen = get_hlen(pkt), plen = get_plen(pkt);
    uint8_t flags = get_flags(pkt);
    uint32_t seq = get_seq(pkt), ack = get_ack(pkt);
    uint16_t data_len = (uint16_t)(plen - hlen);

    tju_internal* in = sock->internal;
    pthread_mutex_lock(&in->lock);
    trace_event(in, "RECEIVE", seq, ack, false);
    if(sock->state == LISTEN){
        bool syn_only = (flags & SYN_FLAG_MASK) != 0 && (flags & ACK_FLAG_MASK) == 0;
        pthread_mutex_unlock(&in->lock);
        if(syn_only) handle_listen_syn(sock, pkt);
        return syn_only ? 0 : -1;
    }

    in->peer_wnd = get_advertised_window(pkt);
    if(in->peer_wnd != 0) in->persist_deadline_ns = 0;

    if(sock->state == SYN_SENT){
        if((flags & (SYN_FLAG_MASK | ACK_FLAG_MASK)) == (SYN_FLAG_MASK | ACK_FLAG_MASK) &&
           ack == in->iss + 1U){
            acknowledge_locked(in, ack, false);
            in->irs = seq;
            in->rcv_nxt = seq + 1U;
            in->snd_write = in->snd_nxt;
            if(in->handshake_retransmitted) in->rto_ns = HANDSHAKE_RESET_RTO_NS;
            send_ack_locked(in, "HANDSHAKE_ACK");
            set_state_locked(in, ESTABLISHED, "RX_SYN_ACK");
        }
        pthread_mutex_unlock(&in->lock);
        return 0;
    }

    if(sock->state == SYN_RECV){
        if((flags & SYN_FLAG_MASK) != 0 && in->tx_head != NULL){
            send_tx_node_locked(in, in->tx_head, true);
        }else if((flags & ACK_FLAG_MASK) != 0 && ack == in->iss + 1U){
            acknowledge_locked(in, ack, false);
            in->snd_write = in->snd_nxt;
            set_state_locked(in, ESTABLISHED, "RX_HANDSHAKE_ACK");
            pthread_mutex_unlock(&in->lock);
            enqueue_accepted_child(in);
            return 0;
        }
        pthread_mutex_unlock(&in->lock);
        return 0;
    }

    if(sock->state == ESTABLISHED && (flags & (SYN_FLAG_MASK | ACK_FLAG_MASK)) ==
       (SYN_FLAG_MASK | ACK_FLAG_MASK) && seq == in->irs){
        send_ack_locked(in, "DUP_SYN_ACK_RESPONSE");
        pthread_mutex_unlock(&in->lock);
        return 0;
    }

    bool ack_advanced = false;
    if(flags & ACK_FLAG_MASK)
        ack_advanced = acknowledge_locked(in, ack,
                                          data_len == 0 && (flags & (SYN_FLAG_MASK | FIN_FLAG_MASK)) == 0);

    if(ack_advanced){
        if(sock->state == FIN_WAIT_1){
            if(in->recv_fin) enter_time_wait_locked(in, "FIN_ACK_AFTER_PEER_FIN");
            else set_state_locked(in, FIN_WAIT_2, "FIN_ACK");
        }else if(sock->state == CLOSING){
            enter_time_wait_locked(in, "CLOSING_ACK");
        }else if(sock->state == LAST_ACK){
            set_state_locked(in, CLOSED, "LAST_ACK_COMPLETE");
        }
    }

    if(data_len != 0){
        insert_rx_locked(in, seq, pkt + hlen, data_len);
        consume_fin_if_ready_locked(in);
        send_ack_locked(in, "DATA_ACK");
    }

    if(flags & FIN_FLAG_MASK){
        uint32_t fin_seq = seq + data_len;
        if(seq_lt(fin_seq, in->rcv_nxt)){
            send_ack_locked(in, "DUP_FIN_ACK");
            if(sock->state == TIME_WAIT)
                in->time_wait_deadline_ns = monotonic_ns() + 2ULL * TJU_MSL_SECONDS * NSEC_PER_SEC;
        }else{
            in->fin_pending = true;
            in->fin_seq = fin_seq;
            consume_fin_if_ready_locked(in);
            send_ack_locked(in, "FIN_ACK");
        }
    }
    if(data_len == 0 && flags == ACK_FLAG_MASK && seq == in->rcv_nxt - 1U)
        send_ack_locked(in, "ZERO_WINDOW_RESPONSE");
    flush_send_locked(in);
    pthread_mutex_unlock(&in->lock);
    return 0;
}

int tju_handle_packet(tju_tcp_t* sock, char* pkt){
    if(pkt == NULL) return -1;
    return tju_handle_packet_len(sock, pkt, get_plen(pkt));
}

static void destroy_internal(tju_tcp_t* sock, tju_internal* in){
    tx_segment* tx = in->tx_head;
    while(tx != NULL){ tx_segment* next = tx->next; free_tx_node(tx); tx = next; }
    rx_segment* rx = in->rx_head;
    while(rx != NULL){ rx_segment* next = rx->next; free(rx->data); free(rx); rx = next; }
    free(in->recv_ring);
    pthread_cond_destroy(&in->state_cv);
    pthread_cond_destroy(&in->recv_cv);
    pthread_cond_destroy(&in->send_cv);
    pthread_cond_destroy(&in->accept_cv);
    pthread_cond_destroy(&in->timer_cv);
    pthread_cond_destroy(&in->dispatch_cv);
    pthread_mutex_destroy(&in->lock);
    free(in);
    sock->internal = NULL;
    free(sock->window.wnd_send); sock->window.wnd_send = NULL;
    free(sock->window.wnd_recv); sock->window.wnd_recv = NULL;
    pthread_cond_destroy(&sock->wait_cond);
    pthread_mutex_destroy(&sock->recv_lock);
    pthread_mutex_destroy(&sock->send_lock);
}

int tju_close(tju_tcp_t* sock){
    if(sock == NULL || sock->internal == NULL) return -1;
    tju_internal* in = sock->internal;
    pthread_mutex_lock(&in->lock);
    if(sock->state != ESTABLISHED && sock->state != CLOSE_WAIT){
        pthread_mutex_unlock(&in->lock); return -1;
    }
    in->send_closed = true;
    while(in->tx_head != NULL && in->error == 0)
        pthread_cond_wait(&in->send_cv, &in->lock);
    if(in->error != 0) return finish_connection_locked(sock, in, -1);
    uint32_t fin_seq = in->snd_write;
    tx_segment* fin = make_tx(fin_seq, FIN_FLAG_MASK | ACK_FLAG_MASK, NULL, 0);
    if(fin == NULL){ pthread_mutex_unlock(&in->lock); return -1; }
    in->snd_write++;
    append_tx_locked(in, fin);
    if(sock->state == CLOSE_WAIT) set_state_locked(in, LAST_ACK, "PASSIVE_CLOSE");
    else set_state_locked(in, FIN_WAIT_1, "ACTIVE_CLOSE");
    flush_send_locked(in);
    while(sock->state != CLOSED && in->error == 0)
        pthread_cond_wait(&in->state_cv, &in->lock);
    int result = in->error == 0 ? 0 : -1;
    return finish_connection_locked(sock, in, result);
}
