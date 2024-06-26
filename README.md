# A simple implementation of PAST: P2P storage utility
PAST is a large-scale, distributed, persistent storage system based on the Pastry peer-to-peer overlay network.\
Following research papers were referred to while building this project:\
PAST: https://www.cs.cornell.edu/people/egs/615/past.pdf \
Pastry: http://rowstron.azurewebsites.net/PAST/pastry.pdf

## Commands
* ```join <ip> <port>```: connects your node to the Pastry network to which the node with specified ip and port is connected
* ```printRT```: prints Routing Table of your node
* ```printLS```: prints Leaf Set of your node
* ```printNS```: prints Neighbourhood Set of your node
* ```store <file_name>```: stores the content of that file in the network
* ```get <file_name>```: retrieves the content of that file from the network
* ```view <file_name>```: displays the content of that file
* ```delete <file_name>```: deletes the file from the network
* ```exit```: exit from the Pastry network

## OS used
Ubuntu 18.04 LTS

## Compiler requirement
g++ v6.1 or newer

## To Build
```bash
make clean
make
```

## To run PAST
```bash
bin/PAST
```