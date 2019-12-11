#include <utility>
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
#include <chrono>
#include <bits/stdc++.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>

static const int MSS = 508;
static const int ACK_SIZE = 8;
static int SIMULATED_FAIL_RATIO_TO_ONE = static_cast<int>(1e9);

using namespace std;

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


void extract_data_from_server_info();

struct packet* construct_packet(const vector<char> &data, uint32_t seqno);

void handle_request(int client_fd, sockaddr_in client_addr, packet *packet);

bool valid_checksum(struct packet *data_packet);

vector<vector<char>> get_data(const string &file_name);

void send_data(int client_fd, struct sockaddr_in client_addr ,vector<vector<char>> data);

void calculate_checksum(packet* packet);

void print_cwnd_log(vector<unsigned int> &log);

bool failure_simulated();

uint16_t port_number;
unsigned int random_generator_seed;
double probability_of_loss;
socklen_t sin_size = sizeof(struct sockaddr_in);
socklen_t addr_len = sizeof(struct sockaddr);

int main()
{
    int server_fd, client_fd; // listen on sock_fd, new connection on new_fd
    struct sockaddr_in server_addr{}; // my address information
    struct sockaddr_in client_addr{}; // connector’s address information
    int broadcast = 1;

    port_number = 9000;

    // extracting data from server.in
    extract_data_from_server_info();

    srand(random_generator_seed);

    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    server_addr.sin_family = AF_INET; // host byte order
    server_addr.sin_port = htons(port_number); // short, network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY; // automatically fill with my IP
    memset(&(server_addr.sin_zero), '\0', ACK_SIZE); // zero the rest of the struct


    if ((server_fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
        cerr << "Can't create a socket! Quitting" << endl;
        return -6;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &broadcast, sizeof(int))) {

        perror("setsockopt server_fd:");

        return 1;

    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        cerr << "Can't bind the socket! Quitting" << endl;
        return -2;
    }

    cout << "All set. Waiting for incoming connections." << endl;

    char* buf = new char[MSS];

    while(true){
        memset(buf, 0, MSS);

        ssize_t bytesReceived = recvfrom(server_fd, buf, MSS, 0, (struct sockaddr*)&client_addr, &addr_len);
        if (bytesReceived == -1){
            perror("Error in recv(). Quitting\n");
            return -5;
        }
        if (bytesReceived == 0){
            perror("Client disconnected !!! \n");
            return-5;
        }

        auto* data_packet = (struct packet*) buf;

        cout << "Forking a child process to handle the request." << endl;

        //delegate request to child process
        pid_t pid = fork();

        if (pid == 0)
        {
            //child process
            if ((client_fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
                cerr << "Can't create a socket! Quitting" << endl;
                return -6;
            }
            if (setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &broadcast, sizeof(int))) {

                perror("setsockopt server_fd:");

                return 1;
            }

            handle_request(client_fd, client_addr, data_packet);

            exit(0);
        }
    }
}

void handle_request(int client_fd, sockaddr_in client_addr, packet *packet) {

    vector<vector<char>> data;
    if(valid_checksum(packet)){

        uint16_t data_length = packet->len - sizeof(packet->seqno) - sizeof(packet->len)
                          - sizeof(packet->cksum);
        const string file_name = string(packet->data, 0, data_length);
        cout << "Requested file's name: "<<string(packet->data, 0, data_length) << endl;
        data = get_data(file_name);
        send_data(client_fd, client_addr, data);
    }
}

vector<vector<char>> get_data(const string &file_name){
    vector<vector<char>> t;
    vector<char> d;
    char c;
    ifstream fin;
    string path = getenv("HOME");
    fin.open(path +"/"+file_name);

    if(fin){
        int byte_counter = 0;
        while(fin.get(c)){
            if(byte_counter < 500) {
                d.push_back(c);
            }else{
                t.push_back(d);
                d.clear();
                d.push_back(c);
                byte_counter = 0;
                continue;
            }
            byte_counter++;
        }
        if(byte_counter > 0) t.push_back(d);
    }else{
        perror("couldn't find the file requested");
        exit(1);
    }
    fin.close();

    return t;
}

void send_data(int client_fd, struct sockaddr_in client_addr , vector<vector<char>> data) {
    unsigned int cwnd = MSS;
    unsigned int ssthresh = 64000;
    unsigned int base_packet_no = 0;
    unsigned int cwnd_head = 0;
    uint32_t seqno;
    
    enum state {slow_start, congestion_avoidance, fast_recovery};

    state state = slow_start;
    int pending;

    auto timer_start = chrono::steady_clock::now();
    auto time_now = timer_start;

    double rttE = 1e8;// 100 ms initially
    double std_devE = 1e6;// 1 ms initially

    uint32_t last_ack_seqno = 0;
    unsigned int duplicate_acks = 0;
    
    char buf[MSS];
    auto ack_packet = (packet*) malloc(sizeof(packet));

    vector<unsigned int> cwnd_log;
    cwnd_log.push_back(cwnd);
    do {
        ioctl(client_fd, SIOCINQ, &pending);
        while (pending != 0) {// Got ack to consume!
            ssize_t bytesReceived = recvfrom(client_fd, buf, ACK_SIZE, 0, (struct sockaddr*)&client_addr, &addr_len);
            if (bytesReceived == -1){
                perror("Error in recv(). Quitting");
                exit(1);
            } else if (bytesReceived != ACK_SIZE) {
                puts("Received unexpected message from the client (not ack)");
                exit(1);
            } else {
                memcpy(ack_packet, buf, ACK_SIZE);
                if (!valid_checksum(ack_packet)) continue;

                auto ack_seqno = ack_packet->seqno;

                if (last_ack_seqno < ack_seqno) {// New ack
                    last_ack_seqno = ack_seqno;

                    unsigned int base_seqno_advance = last_ack_seqno - base_packet_no * MSS;
                    if (cwnd_head < base_seqno_advance) {
                        cwnd_head = 0;
                        perror("State of cwnd_head is inconsistent! The client is playing games with acks!");
                    } else {
                        cwnd_head -= base_seqno_advance;
                    }
                    base_packet_no = last_ack_seqno / MSS;
                    duplicate_acks = 0;

                    /* Reset timer preventing timeout*/
                    timer_start = chrono::steady_clock::now();
                    time_now = chrono::steady_clock::now();

                    /* Update estimation of round trip time and its standard deviation */
                    double elapsed_time_ns =
                            double(chrono::duration_cast<chrono::nanoseconds>(time_now - timer_start).count());
                    rttE = 0.75 * rttE + 0.25 * elapsed_time_ns;
                    std_devE = 0.75 * std_devE + 0.25 * abs(rttE - elapsed_time_ns);

                    switch (state) {
                        case (slow_start):
                            cwnd = cwnd + MSS;
                            cwnd_log.push_back(cwnd);
                            if (cwnd >= ssthresh) {
                                state = congestion_avoidance;
                            }
                            break;
                        case (congestion_avoidance):
                            cwnd += MSS * (MSS / cwnd);
                            cwnd_log.push_back(cwnd);
                            break;
                        case (fast_recovery):
                            cwnd = ssthresh;
                            state = congestion_avoidance;
                            break;
                    }
                } else if (last_ack_seqno == ack_seqno) {// Duplicate ack
                    duplicate_acks++;

                    if (state == fast_recovery) {
                        cwnd += MSS;
                        cwnd_log.push_back(cwnd);
                    } else if (duplicate_acks == 3) {// Go to fast recovery from any state doing same actions
                        duplicate_acks = 0;
                        ssthresh = cwnd / 2;
                        cwnd = ssthresh + 3 * MSS;
                        cwnd_log.push_back(cwnd);
                        state = fast_recovery;

                        /* Retransmit missing segment (at cwnd base) */
                        seqno = base_packet_no * MSS;

                        vector<char> packet_data = data.at(base_packet_no);
                        struct packet p = *construct_packet(packet_data, seqno);
                        memset(buf, 0, MSS);
                        memcpy(buf, &p, sizeof(p));
                        if (!failure_simulated()) {
                            ssize_t bytesSent =
                                    sendto(client_fd, buf, MSS, 0, (struct sockaddr *) &client_addr,
                                           sizeof(struct sockaddr));
                            if (bytesSent == -1) {
                                perror("Failed to retransmit the lost segment. Exiting");
                                exit(1);
                            }
                        }
                        timer_start = chrono::steady_clock::now();
                    }

                }// else old ack, so ignore
            }
            
            ioctl(client_fd, SIOCINQ, &pending);
        }
        time_now = chrono::steady_clock::now();
        double elapsed_time_ns = double(chrono::duration_cast<chrono::nanoseconds>(time_now - timer_start).count());
        if (rttE + 4 * std_devE < elapsed_time_ns) {// Timeout!
            ssthresh = cwnd / 2;
            cwnd = MSS;
            cwnd_log.push_back(cwnd);
            duplicate_acks = 0;

            /* Retransmit missing segment (at cwnd base) */
            seqno = base_packet_no * MSS;

            vector<char> packet_data = data.at(base_packet_no);
            struct packet p = *construct_packet(packet_data, seqno);
            memset(buf, 0, MSS);
            memcpy(buf, &p, sizeof(p));
            if (!failure_simulated()) {
                ssize_t bytesSent = sendto(client_fd, buf, MSS, 0, (struct sockaddr *) &client_addr,
                                           sizeof(struct sockaddr));
                if (bytesSent == -1) {
                    perror("Failed to retransmit the lost segment. Exiting");
                    exit(1);
                }
            }
        }

        /* Transmit as allowed */
        while (cwnd_head < cwnd) {
            seqno = base_packet_no * MSS + cwnd_head;

            vector<char> packet_data = data.at(seqno / MSS);
            struct packet p = *construct_packet(packet_data, seqno);
            memset(buf, 0, MSS);
            memcpy(buf, &p, sizeof(p));
            if (!failure_simulated()) {
                ssize_t bytesSent = sendto(client_fd, buf, MSS, 0, (struct sockaddr *) &client_addr,
                                           sizeof(struct sockaddr));
                if (bytesSent == -1) {
                    perror("Failed to send the packet. Exiting");
                    exit(1);
                }
            }
            if (cwnd_head == 0) {
                timer_start = chrono::steady_clock::now();
            }

            cwnd_head += MSS;
        }


    } while (base_packet_no < data.size());

    print_cwnd_log(cwnd_log);
}

struct packet* construct_packet(const vector<char> &data, uint32_t seqno) {
    auto p = (packet*) malloc(sizeof(packet));

    memset(p->data, 0, 500);

    p->len = sizeof(p->cksum) + sizeof(p->len) + sizeof(p->seqno) + data.size();
    p->seqno = seqno;
    memcpy(p->data, data.data(), data.size());

    calculate_checksum(p);

    return p;
}

void extract_data_from_server_info() {
    ifstream fin;
    string path = getenv("HOME");
    path.append("/server.in");
    fin.open(path);

    if (fin) {
        string line;

        getline(fin, line);
        port_number = (uint16_t) stoi(line);

        getline(fin, line);
        random_generator_seed = (unsigned int) stoul(line);

        getline(fin, line);
        probability_of_loss = stod(line);
        SIMULATED_FAIL_RATIO_TO_ONE = static_cast<int>(1.0 / probability_of_loss);

        cout << "FED IN SERVER PARAMETERS" << endl;
        cout << "Port number: " << port_number << endl;
        cout << "Random generator seed: " << random_generator_seed << endl;
        cout << "Probability of loss: " << probability_of_loss << endl;

        fin.close();
    } else{
        cerr << "Can't find server.in Quitting" << endl;
    }
}

bool valid_checksum(struct packet *data_packet) {
    return true;
}

void calculate_checksum(packet* packet) {
    uint32_t sum = 0;
    int count = packet->len;
    char* addr = packet->data;

    // Main summing loop
    while(count > 1)
    {
        sum = sum + *((uint16_t *) addr);
        count = count - 2;

        addr += 2;
    }

    // Add left-over byte, if any
    if (count > 0)
        sum = sum + *addr;

    // Fold 32-bit sum to 16 bits
    while (sum>>16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    auto calculated_checksum = (uint16_t) sum;

    if (failure_simulated()) calculated_checksum++;

    packet->cksum = calculated_checksum;
}

void print_cwnd_log(vector<unsigned int> &log) {
    cout << "==========================" << endl;
    cout << "CONGESTION WINDOW SIZE LOG" << endl;
    cout << "==========================" << endl;

    for (auto entry : log) {
        cout << entry / MSS << endl;
    }
}

bool failure_simulated () {
    return rand() % SIMULATED_FAIL_RATIO_TO_ONE == 0;
}
