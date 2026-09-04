#include "tju_tcp.h"

#include <assert.h>
#include <stddef.h>

static double difference(double left, double right){
    return left > right ? left - right : right - left;
}

static void put16(unsigned char* p, uint16_t value){
    value = htons(value);
    memcpy(p, &value, sizeof(value));
}

static void make_header(unsigned char* packet, size_t actual_len,
                        uint16_t hlen, uint16_t plen){
    memset(packet, 0, actual_len);
    if(actual_len >= DEFAULT_HEADER_LEN){
        put16(packet + 12, hlen);
        put16(packet + 14, plen);
    }
}

int main(void){
    assert(DEFAULT_HEADER_LEN == 20);
    assert(MAX_LEN == 1400);
    assert(MAX_DLEN == 1380);
    assert(TCP_RECVWN_SIZE >= 5000 * MAX_DLEN);
    assert(sizeof(tju_header_t) != DEFAULT_HEADER_LEN);
    assert(offsetof(tju_header_t, source_port) == 0);

    char payload = 'x';
    char* wire = create_packet_buf(0x1234, 0x5678, 0x01020304U, 0xa1a2a3a4U,
                                   20, 21, SYN_FLAG_MASK | ACK_FLAG_MASK,
                                   0xabcd, 0xef, &payload, 1);
    assert(wire != NULL);
    const unsigned char expected[20] = {
        0x12,0x34,0x56,0x78,0x01,0x02,0x03,0x04,0xa1,0xa2,
        0xa3,0xa4,0x00,0x14,0x00,0x15,0x0c,0xab,0xcd,0xef
    };
    assert(memcmp(wire, expected, sizeof(expected)) == 0);
    assert(wire[20] == 'x');
    assert(tju_validate_packet(wire, 21));
    free(wire);

    unsigned char packet[1401];
    make_header(packet, 20, 20, 20);
    assert(tju_validate_packet((char*)packet, 20));
    make_header(packet, 21, 20, 21);
    assert(tju_validate_packet((char*)packet, 21));
    make_header(packet, 1400, 20, 1400);
    assert(tju_validate_packet((char*)packet, 1400));
    assert(!tju_validate_packet((char*)packet, 19));
    assert(!tju_validate_packet((char*)packet, 1399));
    make_header(packet, 1401, 20, 1401);
    assert(!tju_validate_packet((char*)packet, 1401));
    make_header(packet, 20, 19, 20);
    assert(!tju_validate_packet((char*)packet, 20));
    make_header(packet, 20, 21, 20);
    assert(!tju_validate_packet((char*)packet, 20));
    make_header(packet, 20, 20, 21);
    assert(!tju_validate_packet((char*)packet, 20));

    assert(tju_window_from_space(0) == 0);
    assert(tju_window_from_space(1) == 1);
    assert(tju_window_from_space(65535) == 65535);
    assert(tju_window_from_space(65536) == 65535);
    assert(tju_window_from_space((size_t)TCP_RECVWN_SIZE) == 65535);

    assert(tju_seq_before(0, 1));
    assert(tju_seq_before(0xfffffffeU, 1));
    assert(tju_seq_before(0xffffffffU, 0));
    assert(tju_seq_after(0, 0xffffffffU));
    assert(tju_seq_after(1, 0xfffffffeU));
    assert(!tju_seq_before(0x7fffffffU, 0x7fffffffU));

    int have_sample = 0;
    double srtt = 0.0, rttvar = 0.0, rto = 1.0;
    tju_rto_update_values(&have_sample, &srtt, &rttvar, &rto, 0.100);
    assert(have_sample == 1);
    assert(difference(srtt, 0.100) < 1e-9);
    assert(difference(rttvar, 0.050) < 1e-9);
    assert(difference(rto, 1.0) < 1e-9);
    tju_rto_update_values(&have_sample, &srtt, &rttvar, &rto, 0.140);
    assert(difference(srtt, 0.105) < 1e-9);
    assert(difference(rttvar, 0.0475) < 1e-9);
    tju_rto_update_values(&have_sample, &srtt, &rttvar, &rto, 0.080);
    assert(difference(srtt, 0.101875) < 1e-9);
    assert(difference(rttvar, 0.041875) < 1e-9);
    tju_rto_update_values(&have_sample, &srtt, &rttvar, &rto, 0.200);
    assert(difference(srtt, 0.114140625) < 1e-9);
    assert(difference(rttvar, 0.0559375) < 1e-9);

    puts("PASS t1_wire_seq_test");
    return 0;
}
