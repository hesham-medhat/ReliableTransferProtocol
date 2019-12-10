//#include <iostream>
//#include <netinet/in.h>
//#include <zconf.h>
#include "Utils.h"
#include "bits/stdc++.h"
//#define PORT     8080
//#define MAXLINE 1024
using namespace std;
struct packet {
    /* Header */
    uint16_t cksum; /* Optional bonus part */
    int len;
    int seqno;
    /* Data */
    char data[500] = {'\000'}; /* Not always 500 bytes, can be less */
};
struct ack_packet {
    uint16_t cksum; /* Optional bonus part */
    int len;
    int ackno;
};

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT     8080
#define MAXLINE 1024

string selectiveRepeat(int sockfd, const char *hello, sockaddr_in &servaddr, socklen_t &len, int size);
string goBackN(int sockfd, sockaddr_in &servaddr, socklen_t &len, int size);

void getData(const packet &data, string &chunk);

// Driver code
int main() {
    int sockfd;
    char buffer[MAXLINE];
    char *hello = "Hello from client";
    struct sockaddr_in servaddr;

    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));

    // Filling server information
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = INADDR_ANY;

    int n;
    socklen_t len = sizeof servaddr;

    sendto(sockfd, (const char *) hello, strlen(hello),
           MSG_CONFIRM, (const struct sockaddr *) &servaddr,
           sizeof(servaddr));
    printf("Hello message sent.\n");
    int integerFromClient = 0;

    recvfrom(sockfd, (char *) &integerFromClient, sizeof(int),
             MSG_WAITALL, (struct sockaddr *) &servaddr, (&len));
    sendto(sockfd, (const char *) hello, strlen(hello),
           MSG_CONFIRM, (const struct sockaddr *) &servaddr,
           sizeof(servaddr));


    int size = ntohl(integerFromClient);
    printf("Server : %d\n", size);

//    string concat = selectiveRepeat(sockfd, hello, servaddr, len, size);
    string concat = goBackN(sockfd, servaddr, len, size);
    FILE *file;

    char arrayFile[size];
    for (int j = 0; j < concat.size(); j++)
        arrayFile[j] = concat[j];
    printf("File size: %i\n", size);
    printf(" \n");
    string fileName = "foo.txt";
    file = fopen(&fileName[0], "w");


    fwrite(arrayFile, 1, size, file);
    printf("Written file size: %i\n", size);

    fclose(file);
    printf("File successfully Saved!\n");


    close(sockfd);
    return 0;
}

string goBackN(int sockfd, sockaddr_in &servaddr, socklen_t &len, int size) {
    string respond = "";
    int stat;
    int receivedSize = 0;
    int maxSeq = 0;
    while (receivedSize < size) {
        struct packet data;

        do {

            stat = recvfrom(sockfd, (char *) &data, sizeof(data), 0, (struct sockaddr *) &servaddr, (&len));
        } while (stat < 0);
        if (maxSeq != data.seqno){
            sendAck(servaddr, maxSeq, sockfd);
            cout<<"Expected: "<<maxSeq<<" But Found: "<<data.seqno<<"\n";
            continue;
        }
        receivedSize += 500;
        string chunk = "";
        cout<<"Seq Number is: "<<data.seqno<<endl;
        getData(data, chunk);

        respond += chunk;
        sendAck(servaddr, data.seqno + 1, sockfd);
        maxSeq++;

    }
    cout << "All File Reach\n";
    return respond;

}

void getData(const packet &data, string &chunk) {
    for(int i = 0; i < 500 && data.data[i] != '\000'; i++)
        chunk.push_back(data.data[i]);
}

string selectiveRepeat(int sockfd, const char *hello, sockaddr_in &servaddr, socklen_t &len, int size) {
    string respond = "";
    int stat;
    int receivedSize = 0;
    vector<string> globalBuffer(100000);
    int maxSeq = 0;
    while (receivedSize < size) {
        struct packet data;

        do {

            stat = recvfrom(sockfd, (char *) &data, sizeof(data), 0, (struct sockaddr *) &servaddr, (&len));
        } while (stat < 0);
        receivedSize += 500;
        string chunk = "";
        for(int i = 0; i < 500 && data.data[i] != '\000'; i++)
            chunk.push_back(data.data[i]);
        globalBuffer[data.seqno] = chunk;
        maxSeq = maxSeq < data.seqno ? data.seqno : maxSeq;
        // cout<<data.seqno<<endl;
        sendAck(servaddr, maxSeq, sockfd);
    }
    cout << "All File Reach\n";
    string concat;
    for (int i = 0; i <= maxSeq; i++)
        concat += globalBuffer[i];
    return concat;
}
