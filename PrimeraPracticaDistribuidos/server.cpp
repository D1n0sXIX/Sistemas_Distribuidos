#include "utils.h"
#include <iostream>
#include <string>
#include <thread>
#include <list>
#include "filemanager.h"
#include <mutex>
#define BROKERIP "127.0.0.1"
#define BROKERPORT 5000

enum BrokerCommand {
    register_server,
    request_server,
    release_server
};

using namespace std;

FileManager fm("FileManagerDir");
mutex fm_mutex;

// Definicion de funciones
void atenderCliente(int clientID);

enum Command {
    list_files, // lls --> listFile()
    read_file,  // download --> readFile()
    write_file  // upload --> writeFile()
};

int main(int argc, char** argv){
// Variables de apoyo
bool salir=false;
// 1. Obtenemos por parametros de entrada para mayor comodidad
int serverPort = atoi(argv[1]);

// 2. iniciamos como "cliente" al broker
connection_t brokerConn = initClient(BROKERIP, BROKERPORT);
cout << "Conectado al broker con IP " << BROKERIP << " y puerto " << BROKERPORT << endl;
// Mandamos la peticion a registrarnos como servidor
vector <unsigned char> buffer;
pack(buffer, register_server);
pack(buffer, serverPort);
sendMSG<unsigned char>(brokerConn.serverId, buffer);

// 3. Obtenemos el ACK del broker
vector<unsigned char> response;
recvMSG<unsigned char>(brokerConn.serverId, response);
bool ack = unpack<bool>(response);
if(!ack){
    cout << "No se pudo conectar al broker" << endl;
    return -1;
}
// 4. Iniciamos como servidor que somos
initServer(serverPort);

// 5. Bucle de aceptacion de clientes
while(!salir){
    if(checkClient()){
        int clientID=getLastClientID();
        cout<<"Client "<<clientID<<" connected"<<endl;
        thread* t = new thread(atenderCliente, clientID);
        t->detach();
    }
    else{
        usleep(1000);
    }
}
 return 0;
}

void atenderCliente(int clientID){
    while(true){
        // 1. Recibir el comando del cliente
        vector<unsigned char> buffer;
        // recvMSG, unpack del Command
        recvMSG<unsigned char>(clientID, buffer);

        
        if(buffer.empty()){
            cout<<"Client "<<clientID<<" disconnected"<<endl;
            return;
        }

        

        Command cmd = unpack<Command>(buffer);
        cout << "Cliente " << clientID << " pidio comando: " << cmd << endl;

        // 2. Ejecutar el comando y enviar la respuesta al cliente
        switch(cmd){

            case list_files:{
                fm_mutex.lock();
                vector<string> files = fm.listFiles();
                fm_mutex.unlock();
                // Empaquetar la respuesta
                vector<unsigned char> response;
                pack(response, files.size());
                    for (const string& f : files) {
                        pack(response, f.size());
                        packv(response, f.data(), f.size());
                    }
                sendMSG<unsigned char>(clientID, response);
                break;
                }

            case read_file:{
                // Obtenemos el nombre del archivo a leer
                string fileName;
                fileName.resize(unpack<size_t>(buffer));
                unpackv(buffer, (char*)fileName.data(), fileName.size());

                // Leemos el archivo
                vector<unsigned char> fileData;
                fm_mutex.lock();
                fm.readFile(fileName, fileData);
                fm_mutex.unlock();

                // Empaquetamos la respuesta
                vector<unsigned char> response;
                pack(response, fileData.size());
                packv(response, fileData.data(), fileData.size());
                sendMSG<unsigned char>(clientID, response);
                break;
                }

            case write_file:{
                // Sacamos el nombre del archivo a escribir
                string fileName;
                fileName.resize(unpack<size_t>(buffer));
                unpackv(buffer, (char*)fileName.data(), fileName.size());
                    
                // Sacamos el contenido del archivo a escribir
                vector<unsigned char> fileData;
                fileData.resize(unpack<size_t>(buffer));
                unpackv(buffer, fileData.data(), fileData.size());
                    
                // Escribimos el archivo
                fm_mutex.lock();
                fm.writeFile(fileName, fileData);
                fm_mutex.unlock();

                // Enviamos una respuesta de confirmacion (puede ser un mensaje vacio)
                cout << "Cliente " << clientID << " escribio el archivo: " << fileName << endl;
                vector<unsigned char> response;
                bool ok = true;
                pack(response, ok);
                sendMSG<unsigned char>(clientID, response);
            
                break;
                }

                default:{
                    cout<<"Unknown command from client "<<clientID<<endl;
                break;
                }
        }
    }
}
