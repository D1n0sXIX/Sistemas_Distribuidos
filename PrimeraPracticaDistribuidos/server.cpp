#include "utils.h"
#include <iostream>
#include <string>
#include <thread>
#include <list>
#include "filemanager.h"
#include <mutex>

using namespace std;
#define SERVERPORT 1234
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

// 1. Arrancar la escucha en un puerto
initServer(SERVERPORT);
cout<<"Server started on port "<<SERVERPORT<<endl;
cout<<"Waiting for clients..."<<endl;

// 2. Bucle de aceptacion de clientes
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
    // 1. Recibir el comando del cliente
    vector<unsigned char> buffer;
        // recvMSG, unpack del Command
        recvMSG<unsigned char>(clientID, buffer);
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
            
            case write_file:
            break;

            default:
                cout<<"Unknown command from client "<<clientID<<endl;
            break;
        }
    // 3. Empaquetar la respuesta y enviarla al cliente
}