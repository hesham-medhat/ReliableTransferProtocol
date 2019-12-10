#ifndef RELIABLETRANSFERPROTOCOL_UTILS_H
#define RELIABLETRANSFERPROTOCOL_UTILS_H

#include <netinet/in.h>

void sendAck(sockaddr_in &servaddr, int seqno, int sockfd);

#endif //RELIABLETRANSFERPROTOCOL_UTILS_H
