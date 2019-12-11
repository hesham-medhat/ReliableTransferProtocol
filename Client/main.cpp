//client.cpp
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <netdb.h>
#include <string>
#include <thread>
#include <ctime>
#include <bits/stdc++.h>

#define TIMEOUT 5

static const int MSS = 508;
/* Data-only packets */
struct packet {
    /* Header */
    uint16_t cksum; /* Optional bonus part */
    uint16_t len;
    uint32_t seqno;
    /* Data */
    char data [500]; /* Not always 500 bytes, can be less */
};

/* Ack-only packets are only 8 bytes */
struct ack_packet {
    uint16_t cksum; /* Optional bonus part */
    uint16_t len;
    uint32_t ackno;
};


using namespace std;

packet create_data_packet(const string& file_name);
void send_file_name_packet(char* buf);
uint16_t calculate_checksum(packet packet);
bool do_checksum(struct packet *data_packet);
void extract_data(struct packet *data_packet);
void send_acknowledgement(uint32_t seqno);


int sockfd;
struct sockaddr_in serv_addr;
uint16_t port_number;
socklen_t sin_size = sizeof(struct sockaddr_in);
socklen_t addr_len = sizeof(struct sockaddr);
map<uint32_t, vector<char>> file_chunks;

int main() {

    memset(&serv_addr, 0, sizeof(serv_addr));
    // reading client.in
    ifstream fin;
    ofstream fout;
    std::string home_path = getenv("HOME");
    std::string file_name;
    std::string line;

//    fin.open(home_path + "/client.in");
//    if(fin)
//    {
//        getline(fin, line);
//        port_number = stoi(line);
//        getline(fin, file_name);
//    }
//    fin.close();


    port_number = 8080;
    file_name = "server.in";


    // server info
    serv_addr.sin_family = AF_INET; // host byte order
    serv_addr.sin_port = htons(port_number); // short, network byte order
    serv_addr.sin_addr.s_addr = INADDR_ANY; // automatically fill with my IP
    memset(&(serv_addr.sin_zero), '\0', 8); // zero the rest of the struct

    // create client socket
    if ((sockfd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
        cerr << "Can't create a socket! Quitting" << endl;
        return -2;
    }

    // send a packet with the filename
    struct packet p = create_data_packet(file_name);

    char* buf = new char[MSS];
    memset(buf, 0, MSS);
    memcpy(buf, &p, sizeof(p));

    send_file_name_packet(buf);

    //receive the data

    int i = 0;
    while(true)
    {
        int sret;
        fd_set readfds;
        struct timeval timeout{};

        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        timeout.tv_sec = TIMEOUT;
        timeout.tv_usec = 0;

        sret = select(sockfd+1, &readfds, nullptr, nullptr, &timeout);

        if(sret == 0){
            cout << "No packets comes from the server! \n Aborting..." << endl;
            break;
        }else if(sret == -1){
            perror("error in select");
            exit(1);
        }

        memset(buf, 0, MSS);
        ssize_t bytesReceived = recvfrom(sockfd, buf, MSS, 0, (struct sockaddr*)&serv_addr, &addr_len);
        if (bytesReceived == -1){
            perror("Error in recv(). Quitting");
//            exit(EXIT_FAILURE);
            break;
        }

        cout <<"packet "<<i<<" received" <<endl;
        //put it into packet
        auto* data_packet = (struct packet*) buf;
        if(do_checksum(data_packet)){
            extract_data(data_packet);
            send_acknowledgement(data_packet->seqno);
        }
        i++;
    }

    fout.open(home_path + "/output.txt");
    cout <<"map size: "<< file_chunks.size() << endl;
    map<uint32_t, vector<char>> :: iterator it;
    cout << "full map: \n[ ";
    for (it=file_chunks.begin() ; it!=file_chunks.end() ; it++){
        for(char c : (*it).second)
        {
            fout << c;
            cout<< c << " ";
        }
        fout.close();
    }
    cout << "]";
    return 0;
}

void send_acknowledgement(uint32_t seqno) {
    struct ack_packet ack;
    ack.cksum = 0;
    ack.len = sizeof(ack);
    ack.ackno = seqno;

    char* buf = new char[MSS];
    memset(buf, 0, MSS);
    memcpy(buf, &ack, sizeof(ack));

    ssize_t bytesSent = sendto(sockfd, buf, MSS, 0, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr));
    if (bytesSent == -1) {
        perror("couldn't send the ack");
        exit(1);
    }
}

void extract_data(struct packet *data_packet) {
    uint32_t seqno = data_packet->seqno;
    vector<char> data;

    size_t size = data_packet->len - sizeof(seqno) - sizeof(data_packet->len)
               - sizeof(data_packet->cksum);

    data.reserve(size);

    for (int i = 0; i < size; ++i) {
        data.push_back(data_packet->data[i]);
    }

    file_chunks.insert(make_pair(seqno, data));
    cout <<"chunk size: "<< data.size() << ", seqno: "<<seqno << endl;
    cout <<"data:\n[ ";
    for(char c : data){
        cout<< c <<" ";
    }
    cout <<"]\n\n";
}

bool do_checksum(struct packet *data_packet) {
    return true;
}

packet create_data_packet(const string& file_name) {
    struct packet p{};
    p.seqno = 0;
    p.len = file_name.length() + sizeof(p.cksum) + sizeof(p.len) + sizeof(p.seqno);
    strcpy(p.data, file_name.c_str());
    p.cksum = calculate_checksum(p);
    return p;
}

uint16_t calculate_checksum(packet packet) {
    return 0;
}

void send_file_name_packet(char* buf){
    int sret;
    fd_set readfds;
    struct timeval timeout{};

    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;

    ssize_t bytesSent = sendto(sockfd, buf, MSS, 0, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr));
    if (bytesSent == -1) {
        perror("couldn't send the packet");
    }

    sret = select(sockfd+1, &readfds, nullptr, nullptr, &timeout);

    if(sret == 0){
        cout << "No response from the server! \n Resending packet..." << endl;
        send_file_name_packet(buf);
    }else if(sret == -1){
        perror("error in select");
        exit(1);
    }
}
