#include "utils.h"
#include <iostream>
#include <string>
#include <thread>
#include <mutex>

#define BROKERPORT 5000

using namespace std;


// Struct
struct Servidores_t{
    string ip;
    int port;
    int active = 0;
};
enum BrokerCommand {
    register_server,
    request_server,
    release_server
};

// Definicion de funciones
void atenderConexion(int clientID);

// Gloables apra que sean visibles para todas las funciones
vector<Servidores_t> ServerList;
mutex ServerList_mutex;


int main(int argc, char** argv){
    // 1. Inicializa la lista de servidores y el mutex para los candados
    
    bool salir = false;

    // 2. Iniciamos el broker
    initServer(BROKERPORT);
    cout<<"Broker started on port "<<BROKERPORT<<endl;
    cout<<"Waiting for clients..."<<endl;


    // 3. bucle donde se queda a la conexion de servidores + conexion de usuarios
    while(!salir){
        if(checkClient()){
        int clientID=getLastClientID();
        cout<<"Client "<<clientID<<" connected"<<endl;
        thread* t = new thread(atenderConexion, clientID);
        t->detach();

        }
        else{
            usleep(1000);
        }
    }




}

// Declaracion de funciones
void atenderConexion(int clientID){
    // Creamos y guardamos 
    vector<unsigned char> buffer;
    recvMSG<unsigned char>(clientID, buffer);

    if(buffer.empty()){
        cout<<"Client "<<clientID<<" disconnected"<<endl;
        return;
    }

    BrokerCommand clientCommand = unpack<BrokerCommand>(buffer);

    // Dependiendo del comando hacemos ua ccaion u otra
    switch(clientCommand){
        case register_server:{
            // logica de register
            break;
        }
        case request_server:{
            // Logica de request
            break;
        }
        case release_server:{
            // Logica de release
            break;
        }
        default:{
            cout<<"Unknown command from client "<<clientID<<endl;
        }

    }
}

