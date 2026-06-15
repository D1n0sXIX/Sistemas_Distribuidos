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

    // Dependiendo del comando hacemos una accion u otra
    switch(clientCommand){
        case register_server:{
            // 1. Sacar el puerto
            int port = unpack<int>(buffer);

            // 2. Sacar la IP --> tengoq ue mejorar el entendimiento de esta parte
            struct sockaddr_in addr;
            socklen_t addrlen = sizeof(addr);
            int sock = clientList[clientID].socket;
            getpeername(sock, (struct sockaddr*)&addr, &addrlen);
            string serverIp = inet_ntoa(addr.sin_addr);

            // 3. Crear un Servidores_t con esos datos
            Servidores_t Server;
            Server.ip = serverIp;
            Server.port = port;
            // 4. Hacer un push_back al vector ServerList usando mutex
            ServerList_mutex.lock();
            ServerList.push_back(Server);
            ServerList_mutex.unlock();
            // 5. Responder con un ACK
            vector<unsigned char> ack;
            bool ok = true;
            pack(ack, ok);
            sendMSG<unsigned char>(clientID, ack);
                        
            break;
        }
        case request_server:{
            // 1. Creamos la respuesta
            vector<unsigned char> response;
            
            // Para evitar crash comprobamos que ServerList no sea nulo
            if(ServerList.empty()){
                pack(response, false);
                break;
            }
            // 2. Recorremos server list --> Repasar logica ya que me lio con * y &
            Servidores_t* minServer = &ServerList[0];
            for(auto& server : ServerList){
                if(server.active < minServer->active){
                    minServer = &server;
                }
            }
            // 3. Incrementamos el numero de activos mediante un hilo
            ServerList_mutex.lock();
            minServer->active++;
            ServerList_mutex.unlock();
            // 4. Empaquetamos y mandamos a cliente
            pack(response, true); // que hemos encontrado
            pack(response, minServer->ip.size());
            packv(response, minServer->ip.data(), minServer->ip.size());
            pack(response, minServer->port);
            sendMSG<unsigned char>(clientID, response);
            break;
        }
        case release_server:{ // nos mandan IP+Puerto del servidor
            vector <unsigned char> response;
            // 1. Desempaquetamos
            string ipServer;
            ipServer.resize(unpack<size_t>(buffer));
            unpackv(buffer, (char*)ipServer.data(), ipServer.size());
            int portServer = unpack<int>(buffer);
            // 2. Encotramos el servidor del que se van a desconectar
            for(auto& server : ServerList){
                if(server.ip == ipServer && server.port == portServer){
                    // 2.2 Decrementamos su numero de activos
                    ServerList_mutex.lock();
                    server.active--;
                    ServerList_mutex.unlock();
                    break;
                }
            }
            
            break;
        }
        default:{
            cout<<"Unknown command from client "<<clientID<<endl;
        }

    }
}
