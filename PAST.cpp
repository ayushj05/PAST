#include "Pastry.h"
#include <thread>

int main(){
    PastryNode node;
    string command, node_id, node_ip = getPublicIP(), node_port;
    
    cout << "Enter port: ";
    cin >> node_port;
    
    node_id = hash4(node_ip, node_port);
    node.set_info(node_id, node_ip, node_port);
    
    cout << "\n    NodeID: " << node_id << " | IP: " << node_ip  << " | Port: " << node_port << "\n" << endl;
    
    thread run_server(&PastryNode::node_server, &node);
    
    while(true){
        cin >> command;
        cout << "\n";
        
        if(command == "join"){
            string ip, port;
            cin >> ip >> port;
            string join_request = "NEW 0 " + node_id + " " + node_ip + " " + node_port;
            // Passed node_id just for namesake
            node.route(join_request.c_str(), node_id, ip, port);
        }
        else if(command == "printRT")
            node.printRT();
        else if(command == "printLS")
            node.printLSet();
        else if(command == "printNS")
            node.printNSet();
        else if(command == "store" || command == "get"){
            string file_name, content;
            getline(cin, file_name);
            string fileID = hash4(file_name + node_id);
            
            if(command == "store"){
                cout << "Enter file content: ";
                getline(cin, content);
                cout << "\n";
                if(content.empty())
                    cout << "    <error: file content cannot be empty>\n" << endl;
                else
                    node.store_key_value(fileID, content);
            }
            
            string request = command + " " + fileID + " " + node_ip + " " + node_port;
            node.route(request.c_str(), fileID);
        }
        else if(command == "view"){
            string file_name;
            getline(cin, file_name);
            string value = node.get_value(hash4(file_name + node_id));
            if(value.empty())
                cout << "    <error: file not found on device>\n" << endl;
            else
                cout << value << "\n" << endl;
        }
        else if(command == "exit")
            break;
        else
            cout << "    <invalid command>\n" << endl;
    }
    
    return 0;
}
