#ifndef PASTRY_H
#define PASTRY_H

#include <set>
#include <map>
#include <vector>
#include <string.h>
#include <thread>
using namespace std;

#define BUFFER_SIZE 1025
#define SET_SIZE 8
#define RT_ROW 10
#define MAX_CLIENTS 10
#define space(i,n) for(int i=0; i<n; i++) cout << " ";
#define left first
#define right second

#define RUNNING true
#define EXIT false

// Returns the most significant 'RT_ROW' digits (in base-4) of SHA-1 hash of string A+B
string hash4(string A, string B = "");

// Returns the length of the prefix shared among A and B
int prefix_length(string A, string B);

/*
Compares distance of A & B from C.
Returns 1 if A is closer, -1 if B is closer, 0 otherwise.
*/
int compare_distance(string A, string B, string C);

// Returns IP address of wireless interface
string getIP();

struct entry{
    string nodeID;
    string ip;
    string port;

    entry(const string &nodeID_ = "_", const string &ip_ = "_", const string &port_ = "_")
    : nodeID(nodeID_), ip(ip_), port(port_) {}

    bool operator<(const entry &other) const {return nodeID < other.nodeID;}
    bool operator==(const entry &other) const {return nodeID == other.nodeID;}
};

class PastryNode{
    entry info;
    map<string, string> storage;
    vector<thread> threads;
    pair<set<entry>, set<entry>> LSet;
    entry RTable[RT_ROW][4];
    // N = 100, b = 2
public:
    bool status = RUNNING;
    bool ready = false;

    // Sets the IP address and port number, and starts the node server and maintenance threads
    void start_node(string ip, string port);

    void stop_node();

    // Connects present node with the node having specified ip and port
    int connectTo(string ip, string port);

    void node_server();

    // Handles request received by the server.
    void handleRequest(int server_fd, int client_fd);

    /*
    Routes buffer to node with specified nodeID.
    If IP address and port number are passed, then directly routes buffer to that address.
    Returns true if routed buffer to some node, else returns false.
    */
    bool route(const char* buffer, string nodeID, string ip = "_", string port = "_");    
    
    /*
    Checks whether the peers stored in its state tables are live or not.
    Removes the nodes that have departed from the network.
    */
    void check_peers();
    
    void set_LSet(char* buffer);
    void insert_LSet(string nodeID, string ip, string port);
    void set_RTable(char* buffer);
    
    void printRT();
    void printLSet();
    
    string getRow(int row_index);    
    string get_LSet();
    
    void store_key_value(string key, string value);
    string get_value(string key);
    void delete_key_value(string key);

    string getID();
};

#endif /* PASTRY_H */
