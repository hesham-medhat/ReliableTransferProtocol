#include "Utils.h"
#include "bits/stdc++.h"
using namespace std;
struct ack_packet {
    uint16_t cksum; /* Optional bonus part */
    int len;
    int ackno;
};
void sendAck(sockaddr_in &servaddr, int seqno, int sockfd) {
    int stat;
    struct ack_packet ack;
    ack.len = 8;
    ack.ackno = seqno;
    do {
        stat = sendto(sockfd, (char *) &ack, sizeof(ack),
                      MSG_CONFIRM, (const struct sockaddr *) &servaddr,
                      sizeof(servaddr));
    } while (stat < 0);
}
