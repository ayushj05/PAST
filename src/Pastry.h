#ifndef PASTRY_H
#define PASTRY_H

#include <iostream>
#include <ifaddrs.h>
#include <stdio.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <set>
#include <map>
#include <vector>
#include <string.h>
#include <algorithm>
#include <thread>
#include <chrono>
#include <shared_mutex>
#include "SHA1.h"
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

shared_mutex LSet_mtx, RTable_mtx, storage_mtx;
mutex get_mtx;
condition_variable cv;

struct entry{
    string nodeID;
    string ip;
    string port;

    entry(const string &nodeID_ = "_", const string &ip_ = "_", const string &port_ = "_")
    : nodeID(nodeID_), ip(ip_), port(port_) {}

    bool operator<(const entry &other) const {return nodeID < other.nodeID;}
    bool operator==(const entry &other) const {return nodeID == other.nodeID;}
};



string hash4(string, string);
int prefix_length(string, string);
int compare_distance(string, string, string);
string getPublicIP();



class PastryNode{
    entry info;
    map<string, string> storage;
    pair<set<entry>, set<entry>> LSet;
    entry RTable[RT_ROW][4];
    // N = 100, b = 2
public:
    bool status = RUNNING;
    bool ready = false;

    // Connects present node with the node having specified ip and port
    int connectTo(string ip, string port){
        int client_fd;
        struct sockaddr_in server, client;

        char serverIP_array[ip.size()+1];
        char clientIP_array[info.ip.size()+1];
        strcpy(serverIP_array, ip.c_str());
        strcpy(clientIP_array, info.ip.c_str());
        
        // Creating socket file descriptor
        if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
            perror("socket() failed (connectTo)");
            exit(EXIT_FAILURE);
        }
        
        memset(&server, 0, sizeof(server));
        memset(&client, 0, sizeof(client));
        
        server.sin_family = client.sin_family = AF_INET;
        server.sin_addr.s_addr = inet_addr(serverIP_array);
        client.sin_addr.s_addr = inet_addr(clientIP_array);
        server.sin_port = htons(stoi(port));
        client.sin_port = htons(stoi(info.port));
        
        auto started = chrono::high_resolution_clock::now();
        chrono::duration<double> elapsed;
        while(connect(client_fd, (struct sockaddr *)&server, sizeof(server)) < 0){
            auto finished = chrono::high_resolution_clock::now();
            elapsed = finished - started;
            if(elapsed.count() > 1)
                return -1;
        }

        return client_fd;
    }



    void node_server(){
        int server_fd;
        struct sockaddr_in address;
        socklen_t addr_len = sizeof(address);
        thread serving_clients[MAX_CLIENTS];

        char serverIP_array[info.ip.size()+1];
        strcpy(serverIP_array, info.ip.c_str());

        // Creating socket file descriptor
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
            perror("socket() failed (node_server)");
            exit(EXIT_FAILURE);
        }
        
        memset(&address, 0, sizeof(address));
        
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))){
            perror("setsockopt() failed (node_server)");
            exit(EXIT_FAILURE);
        }
        
        // Setting to non-blocking mode
        int on = 1;
        if (ioctl(server_fd, FIONBIO, (char *)&on) < 0){
            perror("ioctl() failed (node_server)");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = inet_addr(serverIP_array);
        address.sin_port = htons(stoi(info.port));
        if(bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0){
            perror("bind() failed (node_server)");
            exit(EXIT_FAILURE);
        }
    
        if(listen(server_fd, 100) < 0){
            perror("listen() failed (node_server)");
            exit(EXIT_FAILURE);
        }
        
        int i = 0;
        while(status == RUNNING){
            int new_socket;
            
            struct pollfd pfds;
            pfds.fd = server_fd;
            pfds.events = POLLIN;
            while(status == RUNNING){
                int num_events = poll(&pfds, 1, 1000);  // 1 second timeout
                if(num_events != 0 && (pfds.revents & POLLIN))
                    break;
            }
            if(status == EXIT)
                break;
            
            if((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addr_len)) < 0){
                perror("accept() failed (node_server)");
                exit(EXIT_FAILURE);
            }
            
            serving_clients[i] = thread(&PastryNode::handleRequest, this, server_fd, new_socket);
            i++;
            if(i == MAX_CLIENTS){
                for(auto& th : serving_clients){
                    if(th.joinable())
                        th.join();
                }
                i = 0;
            }
        }
        
        for(int j=0; j<i; j++){
            if(serving_clients[j].joinable())
                serving_clients[j].join();
        }
        close(server_fd);
    }



    // Handles request received by the server.
    void handleRequest(int server_fd, int client_fd){
        char buffer[BUFFER_SIZE];
        
        int length;
        if((length = recv(client_fd, buffer, BUFFER_SIZE, 0)) < 0){
            perror("recv() failed (node_server)");
            exit(EXIT_FAILURE);
        }
        buffer[length] = '\0';
        
        char buffer_tmp[strlen(buffer)];
        strcpy(buffer_tmp, buffer);
        char* saveptr;
        char* request_type = strtok_r(buffer_tmp, " ", &saveptr);
        
        // buffer = "NEW <row_index> <nodeID> <ip> <port>"
        if(strcmp(request_type, "NEW") == 0){
            close(client_fd);
            
            int row_index = atoi(strtok_r(NULL, " ", &saveptr));
            
            // send row (RT) to the new node (send own info in place of corresponding blank entry)
            string response = "RT " + to_string(row_index) + " " + getRow(row_index);
            
            string nodeID = strtok_r(NULL, " ", &saveptr);
            string ip = strtok_r(NULL, " ", &saveptr);
            string port = strtok_r(NULL, " ", &saveptr);
            int l = prefix_length(nodeID, info.nodeID);
            
            route(response.c_str(), nodeID, ip, port);
            
            // send request (NEW) to next node after incrementing "row_index"
            buffer[4] = to_string(row_index+1)[0];
            // if at last node, then route Leaf Set to new node
            bool routed = route(buffer, nodeID);
            
            // if not routed, implies, reached node with closest nodeID
            // send Leaf Set to new node
            if(!routed){
                response = get_LSet();
                route(response.c_str(), nodeID, ip, port);
            }
            
            // update your own RTable by filling the appropriate empty entry with new node
            RTable_mtx.lock();
            if(RTable[l][nodeID[l] - '0'].ip == "_")
                RTable[l][nodeID[l] - '0'] = entry(nodeID, ip, port);
            RTable_mtx.unlock();
            
            // update your own Leaf Set
            insert_LSet(nodeID, ip, port);
        }
        // buffer = "RT <row_index> <row_index^th row for RT>"
        else if(strcmp(request_type, "RT") == 0){
            close(client_fd);
            set_RTable(buffer);
        }
        // buffer = "LSet <sender's nodeID> <sender's ip> <sender's port> <sender's leaf set>"
        else if(strcmp(request_type, "LSet") == 0){
            close(client_fd);
            set_LSet(buffer);
        }
        // buffer = "store <fileID> <owner's ip> <owner's port>" or "store direct <fileID> <content>"
        else if(strcmp(request_type, "store") == 0){
            close(client_fd);
            
            string fileID = strtok_r(NULL, " ", &saveptr);
            
            if(fileID == "direct"){
                fileID = strtok_r(NULL, " ", &saveptr);
                string content = strtok_r(NULL, "", &saveptr);
                store_key_value(fileID, content);
                return;
            }
            else if(route(buffer, fileID))
                return;
            // if not routed, then you are the closest node to fileID
            
            string ip = strtok_r(NULL, " ", &saveptr);
            string port = strtok_r(NULL, " ", &saveptr);
            string response = "get direct " + fileID;
            
            int client = connectTo(ip, port);
            if(client == -1)    return;
            send(client, response.c_str(), strlen(response.c_str()), 0);
            
            // receive file data and save in storage
            int length = recv(client, buffer, BUFFER_SIZE, 0);
            buffer[length] = '\0';
            store_key_value(fileID, buffer);
            
            close(client);
            
            // ask the nodes in your Leaf Set also to store the file
            string message = "store direct " + fileID + " " + buffer;
            shared_lock<shared_mutex> lock(LSet_mtx);
            for(auto it = LSet.left.begin(); it != LSet.left.end(); it++)
                route(message.c_str(), fileID, (*it).ip, (*it).port);
            for(auto it = LSet.right.begin(); it != LSet.right.end(); it++)
                route(message.c_str(), fileID, (*it).ip, (*it).port);
        }
        // buffer = "get (direct) <fileID> <client's ip> <client's port>"
        else if(strcmp(request_type, "get") == 0){
            string fileID = strtok_r(NULL, " ", &saveptr);
            
            if(strcmp(fileID.c_str(), "direct") == 0){
                lock_guard<shared_mutex> lock(storage_mtx);
                
                fileID = strtok_r(NULL, " ", &saveptr);
                string data = storage[fileID];
                storage.erase(fileID);
                send(client_fd, data.c_str(), strlen(data.c_str()), 0);
                
                close(client_fd);
                return;
            }
            close(client_fd);
            
            if(route(buffer, fileID))
                return;
            // if not routed, then you are the closest node to fileID
            
            string ip = strtok_r(NULL, " ", &saveptr);
            string port = strtok_r(NULL, " ", &saveptr);
            
            storage_mtx.lock_shared();
            string response = "save " + fileID + " " + storage[fileID];
            storage_mtx.unlock_shared();
            
            int client = connectTo(ip, port);
            if(client == -1)    return;
            send(client, response.c_str(), strlen(response.c_str()), 0);
            
            close(client);
        }
        // buffer = "save <fileID> <data>"
        else if(strcmp(request_type, "save") == 0){
            close(client_fd);
            
            string fileID = strtok_r(NULL, " ", &saveptr);
            char* data = strtok_r(NULL, "", &saveptr);
            
            if(data == NULL)
                cout << "<error: file not found on network>\n" << endl;
            else
                store_key_value(fileID, data);
            
            // Signal to move to next command
            unique_lock<mutex> lck(get_mtx);
            ready = true;
            cv.notify_one();
        }
        // buffer = "delete (direct) <fileID>"
        else if(strcmp(request_type, "delete") == 0){
            close(client_fd);
            
            string fileID = strtok_r(NULL, " ", &saveptr);
            
            if(fileID == "direct"){
                fileID = strtok_r(NULL, " ", &saveptr);
                delete_key_value(fileID);
                return;
            }
            else if(route(buffer, fileID))
                return;
            // if not routed, then you are the closest node to fileID

            delete_key_value(fileID);
            
            // ask the nodes in your Leaf Set also to delete the file
            string message = "delete direct " + fileID;
            shared_lock<shared_mutex> lock(LSet_mtx);
            for(auto it = LSet.left.begin(); it != LSet.left.end(); it++)
                route(message.c_str(), fileID, (*it).ip, (*it).port);
            for(auto it = LSet.right.begin(); it != LSet.right.end(); it++)
                route(message.c_str(), fileID, (*it).ip, (*it).port);
        }
        // buffer = "check"
        else if(strcmp(request_type, "check") == 0)
            close(client_fd);
        // buffer = "give_LSet"
        else if(strcmp(request_type, "give_LSet") == 0){
            string response = get_LSet();
            
            send(client_fd, response.c_str(), strlen(response.c_str()), 0);
            
            close(client_fd);
        }
        // buffer = "give_RT <row> <column>"
        else if(strcmp(request_type, "give_RT") == 0){
            int i = atoi(strtok_r(NULL, " ", &saveptr));
            int j = atoi(strtok_r(NULL, " ", &saveptr));
            
            string response;
            
            shared_lock<shared_mutex> lock(RTable_mtx);
            if(RTable[i][j].nodeID == "_")
                response = "Empty";
            else
                response = "RT_entry " + RTable[i][j].nodeID + " " + RTable[i][j].ip + " " + RTable[i][j].port;
            
            send(client_fd, response.c_str(), strlen(response.c_str()), 0);
            
            close(client_fd);
        }
    }


    /*
    Routes buffer to node with specified nodeID.
    If IP address and port number are passed, then directly routes buffer to that address.
    Returns true if routed buffer to some node, else returns false.
    */
    bool route(const char* buffer, string nodeID, string ip = "_", string port = "_"){
        if(ip == "_"){
            shared_lock<shared_mutex> lock1(LSet_mtx), lock2(RTable_mtx);
            
            auto it = nodeID < info.nodeID ? LSet.left.find(nodeID) : LSet.right.find(nodeID);
            
            // setting values for ip and port of node to which we have to route
            if(it != LSet.left.end() && it != LSet.right.end()){
                ip = (*it).ip;
                port = (*it).port;
            }
            else{
                int pre_len = prefix_length(nodeID, info.nodeID);
                if(RTable[pre_len][nodeID[pre_len] - '0'].ip != "_"){
                    ip = RTable[pre_len][nodeID[pre_len] - '0'].ip;
                    port = RTable[pre_len][nodeID[pre_len] - '0'].port;
                }
                else{
                    if(nodeID < info.nodeID && !LSet.left.empty()){
                        entry rmost = *--LSet.left.end();
                        if(prefix_length(rmost.nodeID, nodeID) >= prefix_length(info.nodeID, nodeID)
                           && compare_distance(rmost.nodeID, info.nodeID, nodeID) == 1){
                            ip = rmost.ip;
                            port = rmost.port;
                        }
                    }
                    else if(nodeID > info.nodeID && !LSet.right.empty()){
                        entry lmost = *LSet.right.begin();
                        if(prefix_length(lmost.nodeID, nodeID) >= prefix_length(info.nodeID, nodeID)
                           && compare_distance(lmost.nodeID, info.nodeID, nodeID) == 1){
                            ip = lmost.ip;
                            port = lmost.port;
                        }
                    }
                    else{
                        bool found = false;
                        
                        for(auto &x : RTable){
                            for(auto y : x){
                                if(y.nodeID == "_") continue;
                                if(prefix_length(y.nodeID, nodeID) >= prefix_length(info.nodeID, nodeID) 
                                   && compare_distance(y.nodeID, info.nodeID, nodeID) == 1){
                                    ip = y.ip;
                                    port = y.port;
                                    found = true;
                                    break;
                                }
                            }
                            if(found)
                                break;
                        }
                    }
                }
            }
            
            if(ip == "_")
                return false;
        }
        
        // connecting and sending data to that node
        int client_fd = connectTo(ip, port);
        if(client_fd == -1) return false;
        send(client_fd, buffer, strlen(buffer), 0);
        
        close(client_fd);
        
        return true;
    }
    
    
    
    /*
    Checks whether the peers stored in its state tables are live or not.
    Removes the nodes that have departed from the network.
    */
    void check_peers(){
        while(status == RUNNING){
            LSet_mtx.lock();
            
            vector<string> failed_nodes;
            
            // Remove the failed nodes from left Leaf Set and store them in failed_nodes.
            for(auto it = LSet.left.begin(); it != LSet.left.end();){
                int client = connectTo((*it).ip, (*it).port);
                if(client == -1){
                    failed_nodes.push_back((*it).nodeID);
                    it = LSet.left.erase(it);
                    
                    continue;
                }
                string message = "check";
                send(client, message.c_str(), strlen(message.c_str()), 0);
                
                close(client);
                it++;
            }

            /*
            Request Leaf Set from leftmost node in own's Leaf Set to update own's Leaf Set.
            Remove the nodes which were found to have failed in previous step, as they might
            not have been checked by the leftmost node.
            */
            if(!failed_nodes.empty() && !LSet.left.empty()){
                entry leftmost = *(LSet.left.begin());
                int client = connectTo(leftmost.ip, leftmost.port);
                if(client != -1){
                    string message = "give_LSet";
                    send(client, message.c_str(), strlen(message.c_str()), 0);
                    char buffer[BUFFER_SIZE];
                    
                    int length = recv(client, buffer, BUFFER_SIZE, 0);
                    buffer[length] = '\0';
                    
                    close(client);
                    
                    LSet_mtx.unlock();
                    set_LSet(buffer);
                    LSet_mtx.lock();
                    
                    for(string nodeID : failed_nodes)
                        LSet.left.erase(nodeID);
                }
            }
            
            failed_nodes.clear();
            
            // Remove the failed nodes from right Leaf Set and store them in failed_nodes.
            for(auto it = LSet.right.begin(); it != LSet.right.end();){
                int client = connectTo((*it).ip, (*it).port);
                if(client == -1){
                    failed_nodes.push_back((*it).nodeID);
                    it = LSet.right.erase(it);
                    
                    continue;
                }
                string message = "check";
                send(client, message.c_str(), strlen(message.c_str()), 0);
                
                close(client);
                it++;
            }

            /*
            Request Leaf Set from rightmost node in own's Leaf Set to update own's Leaf Set.
            Remove the nodes which were found to have failed in previous step, as they might
            not have been checked by the rightmost node.
            */
            if(!failed_nodes.empty() && !LSet.right.empty()){
                entry rightmost = *--LSet.right.end();
                int client = connectTo(rightmost.ip, rightmost.port);
                if(client != -1){
                    string message = "give_LSet";
                    send(client, message.c_str(), strlen(message.c_str()), 0);
                    char buffer[BUFFER_SIZE];
                    
                    int length = recv(client, buffer, BUFFER_SIZE, 0);
                    buffer[length] = '\0';
                    
                    close(client);
                    
                    LSet_mtx.unlock();
                    set_LSet(buffer);
                    LSet_mtx.lock();
                    
                    for(string nodeID : failed_nodes)
                        LSet.right.erase(nodeID);
                }
            }
            LSet_mtx.unlock();
            
            RTable_mtx.lock();
            for(int i=0; i<RT_ROW; i++){
                for(int j=0; j<4; j++){
                    if(RTable[i][j].nodeID == "_")  continue;
                    
                    int client;
                    if((client = connectTo(RTable[i][j].ip, RTable[i][j].port)) != -1){
                        string message = "check";
                        send(client, message.c_str(), strlen(message.c_str()), 0);
                        
                        close(client);
                        continue;
                    }
                    string departed_nodeID = RTable[i][j].nodeID;
                    
                    RTable[i][j] = entry("_", "_", "_");
                    
                    for(int row = i, col = 0; row < RT_ROW; 
                        row += col > (col+1 + ((col+1)%4 == j))%4, col = (col+1 + ((col+1)%4 == j))%4){
                        
                        if(RTable[row][col].nodeID == "_") continue;
                        
                        client = connectTo(RTable[row][col].ip, RTable[row][col].port);
                        if(client == -1) continue;
                        
                        string message = "give_RT " + to_string(i) + " " + to_string(j);
                        send(client, message.c_str(), strlen(message.c_str()), 0);
                        
                        char buffer[BUFFER_SIZE];
                        int length = recv(client, buffer, BUFFER_SIZE, 0);
                        buffer[length] = '\0';
                        
                        close(client);
                        
                        char* saveptr;
                        char* token = strtok_r(buffer, " ", &saveptr);  // token = "RT_entry" or "Empty"
                        if(strcmp(token, "Empty") == 0) continue;
                        
                        token = strtok_r(NULL, " ", &saveptr);
                        string token_nodeID, token_ip, token_port;
                        while(token != NULL){
                            token_nodeID = token;
                            token = strtok_r(NULL, " ", &saveptr);
                            token_ip = token;
                            token = strtok_r(NULL, " ", &saveptr);
                            token_port = token;
                            token = strtok_r(NULL, " ", &saveptr);
                        }
                        
                        if(token_nodeID == departed_nodeID) continue;
                        
                        RTable[i][j] = entry(token_nodeID, token_ip, token_port);
                    }
                }
            }
            RTable_mtx.unlock();
            
            // sleep for 5 seconds before next round of checking.
            sleep(5);
        }
    }
    
    
    
    // buffer = "LSet <sender's nodeID> <sender's ip> <sender's port> <sender's leaf set>"
    void set_LSet(char* buffer){
        char* saveptr;
        char* token = strtok_r(buffer, " ", &saveptr);  // token = "LSet"
        token = strtok_r(NULL, " ", &saveptr);
        
        string token_nodeID, token_ip, token_port;
        while(token != NULL){
            token_nodeID = token;
            token = strtok_r(NULL, " ", &saveptr);
            token_ip = token;
            token = strtok_r(NULL, " ", &saveptr);
            token_port = token;
            token = strtok_r(NULL, " ", &saveptr);
            
            insert_LSet(token_nodeID, token_ip, token_port);
        }
    }
    
    
    
    void insert_LSet(string nodeID, string ip, string port){
        lock_guard<shared_mutex> lock(LSet_mtx);
        
        if(nodeID < info.nodeID){
            if(LSet.left.size() == SET_SIZE/2){ 
                if(compare_distance(nodeID, (*LSet.left.begin()).nodeID, info.nodeID) == 1){
                    LSet.left.erase(LSet.left.begin());
                    LSet.left.insert(entry(nodeID, ip, port));
                }
            }
            else if(LSet.left.size() < SET_SIZE/2)
                LSet.left.insert(entry(nodeID, ip, port));
        }
        else if(nodeID > info.nodeID){
            if(LSet.right.size() == SET_SIZE/2){
                if(compare_distance(nodeID, (*--LSet.right.end()).nodeID, info.nodeID) == 1){
                    LSet.right.erase(--LSet.right.end());
                    LSet.right.insert(entry(nodeID, ip, port));
                }
            }
            else if(LSet.right.size() < SET_SIZE/2)
                LSet.right.insert(entry(nodeID, ip, port));
        }
    }



    // buffer = "RT <row_index> <row_index^th row for RT>"
    void set_RTable(char* buffer){
        lock_guard<shared_mutex> lock(RTable_mtx);
        
        char* saveptr;
        char* token = strtok_r(buffer, " ", &saveptr);  // token = "RT"
        // token = strtok(NULL, " ");
        int row_index = atoi(strtok_r(NULL, " ", &saveptr));
        
        token = strtok_r(NULL, " ", &saveptr);
        int i = 0;
        string token_nodeID, token_ip, token_port;
        while(token != NULL){
            token_nodeID = token;
            token = strtok_r(NULL, " ", &saveptr);
            token_ip = token;
            token = strtok_r(NULL, " ", &saveptr);
            token_port = token;
            token = strtok_r(NULL, " ", &saveptr);
            // 'i' shouldn't be equal to row_index^th digit
            if(i != info.nodeID[row_index] - '0' && token_nodeID.substr(0, row_index) == info.nodeID.substr(0, row_index))
                RTable[row_index][i] = entry(token_nodeID, token_ip, token_port);
            i++;
        }
    }
    
    
    
    void printRT(){
        shared_lock<shared_mutex> lock(RTable_mtx);
        
        int s[4] = {0};
        for(int i=0; i<4; i++)
            for(int j=0; j<RT_ROW; j++)
                s[i] = max({s[i], (int) RTable[j][i].nodeID.length(), 
                                  (int) RTable[j][i].ip.length(), 
                                  (int) RTable[j][i].port.length()});
        
        cout << "       0"; space(i, s[0]+3); cout << 1; space(i, s[1]+3);
        cout << 2; space(i, s[2]+3); cout << "3\n" << endl;

        for(int i=0; i<RT_ROW; i++){
            cout << "       ";
            for(int j=0; j<3; j++){
                cout << RTable[i][j].nodeID;
                space(k, s[j] + 4 - RTable[i][j].nodeID.length());
            }
            cout << RTable[i][3].nodeID << "\n";

            cout << "    " << i << "  ";
            for(int j=0; j<3; j++){
                cout << RTable[i][j].ip;
                space(k, s[j] + 4 - RTable[i][j].ip.length());
            }
            cout << RTable[i][3].ip << "\n";

            cout << "       ";
            for(int j=0; j<3; j++){
                cout << RTable[i][j].port;
                space(k, s[j] + 4 - RTable[i][j].port.length());
            }
            cout << RTable[i][3].port << "\n" << endl;
        }
    }
    
    
    
    void printLSet(){
        shared_lock<shared_mutex> lock(LSet_mtx);
        
        int i = 1;
        for(auto it = LSet.left.begin(); it != LSet.left.end(); it++, i++)
            cout << "    " << i << ": " << (*it).nodeID << " " << (*it).ip << " " << (*it).port << "\n";
        for(auto it = LSet.right.begin(); it != LSet.right.end(); it++, i++)
            cout << "    " << i << ": " << (*it).nodeID << " " << (*it).ip << " " << (*it).port << "\n";
        if(i == 1) cout << "    <empty>\n";
        cout << endl;
    }
    
    
    
    string getRow(int row_index){
        shared_lock<shared_mutex> lock(RTable_mtx);
        
        string row = "";
        for(int i=0; i<4; i++){
            if(i == info.nodeID[row_index] - '0')
                row.append(info.nodeID + " " + info.ip + " " + info.port + " ");
            else
                row.append(RTable[row_index][i].nodeID + " " + 
                           RTable[row_index][i].ip + " " + 
                           RTable[row_index][i].port + " ");
        }
        row.pop_back();
        
        return row;
    }
    
    
    
    string get_LSet(){
        shared_lock<shared_mutex> lock(LSet_mtx);
        
        string message = "LSet " + info.nodeID + " " + info.ip + " " + info.port;
        for(auto it = LSet.left.begin(); it != LSet.left.end(); it++)
            message.append(" " + (*it).nodeID + " " + (*it).ip + " " + (*it).port);
        for(auto it = LSet.right.begin(); it != LSet.right.end(); it++)
            message.append(" " + (*it).nodeID + " " + (*it).ip + " " + (*it).port);
        return message;
    }
    
    
    
    void set_info(string nodeID, string ip, string port){
        info = {nodeID, ip, port};
    }
    
    
    
    void store_key_value(string key, string value){
        lock_guard<shared_mutex> lock(storage_mtx);
        storage[key] = value;
    }
    
    string get_value(string key){
        shared_lock<shared_mutex> lock(storage_mtx);
        return storage[key];
    }

    void delete_key_value(string key){
        lock_guard<shared_mutex> lock(storage_mtx);
        storage.erase(key);
    }
};




// Returns the most significant 'RT_ROW' digits (in base-4) of SHA-1 hash of string A+B
string hash4(string A, string B = ""){
    SHA1 checksum;
    checksum.update(A.append(B));
    string hexd_result = checksum.final(), base4_result = "";
    
    for(int i=0; i<RT_ROW/2; i++){
        base4_result.push_back((hexd_result[i]<'a') ? (hexd_result[i]-'0')/4 + '0' : (hexd_result[i]-'a'+10)/4 + '0');
        base4_result.push_back((hexd_result[i]<'a') ? (hexd_result[i]-'0')%4 + '0' : (hexd_result[i]-'a'+10)%4 + '0');
    }
    
    return base4_result;
}



// Returns the length of the prefix shared among A and B
int prefix_length(string A, string B){
    int length = 0;
    while(A[length] == B[length] && length != min(A.length(), B.length()))
        length++;
    return length;
}



/*
Compares distance of A & B from C.
Returns 1 if A is closer, -1 if B is closer, 0 otherwise.
*/
int compare_distance(string A, string B, string C){
    for(int i=0; i<A.length(); i++){
        if(abs(A[i] - C[i]) < abs(B[i] - C[i]))
            return 1;
        else if(abs(A[i] - C[i]) > abs(B[i] - C[i]))
            return -1;
    }
    return 0;
}




// Returns IP address of wireless interface
string getPublicIP(){
    struct ifaddrs* ifAddrStruct = NULL;
    struct ifaddrs* ifa = NULL;
    void* tmpAddrPtr = NULL;
    string ret;

    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next){
        if (!ifa->ifa_addr)
            continue;
        if (ifa->ifa_addr->sa_family == AF_INET){ 
            // IPv4
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            if(ifa->ifa_name[0] == 'w'){
                ret = string(addressBuffer);
                if (ifAddrStruct!=NULL) freeifaddrs(ifAddrStruct);
                return ret;
            }
        }
    }
    return ret;
}

#endif /* PASTRY_H */
