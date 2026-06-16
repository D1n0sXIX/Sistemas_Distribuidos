#include "filemanager_proxy.h"
#include "utils.h"
#include <string>
#include <vector>

using namespace std;

#define LOCALHOST  "127.0.0.1"
#define BROKERPORT 5000
enum BrokerCommand {
    register_server,
    request_server,
    release_server
};


FileManager::FileManager(string path){
    
    // Iniciamos al conexion con el broker
    connection_t brokerConn = initClient(LOCALHOST, BROKERPORT);
    
    // Hacemos peticion al broker apra obtener info
    vector<unsigned char> request;
    pack(request, request_server);
    sendMSG(brokerConn.serverId, request);

    // Captamos la respuesta
    vector<unsigned char> response;
    recvMSG(brokerConn.serverId, response);
    bool ack = unpack<bool>(response);
    size_t ipLenght = unpack<size_t>(response);
    this->ip.resize(ipLenght);
    unpackv(response, (char*)this->ip.data(), ip.size());
    this->port = unpack<int>(response);

    // Ya tenemos toda la info asi que cortamos la conexion
    closeConnection(brokerConn.serverId);

    // Iniciamos la conexion del servidor
    this->conn = initClient(this->ip, this->port);
    

}

FileManager::~FileManager(){

    // Iniciamos al conexion con el broker
    connection_t brokerConn = initClient(LOCALHOST, BROKERPORT);
    
    // Hacemos peticion al broker apra obtener info
    vector<unsigned char> request;
    pack(request, release_server);
    pack(request, this->ip.size());
    packv(request, this->ip.data(), this->ip.size());
    pack(request, this->port);
    sendMSG(brokerConn.serverId, request);

    closeConnection(brokerConn.serverId);

    closeConnection(this->conn.serverId);
}

vector<string> FileManager::listFiles(){
    vector<unsigned char> buffer;
    pack(buffer, list_files);
    sendMSG<unsigned char>(conn.serverId, buffer);
    
    // Recibimos la respuesta
    vector<unsigned char> response;
    recvMSG<unsigned char>(conn.serverId, response);
    // Desempaquetamos
    size_t numFiles = unpack<size_t>(response);             // en forma de bucle
    vector<string> files;
    for (size_t i = 0; i < numFiles; i++){
        string name;
        name.resize(unpack<size_t>(response));              // longitud de este nombre
        unpackv(response, (char*)name.data(), name.size()); // su nombre
        files.push_back(name);
    }
    return files;
}

void FileManager::readFile(string fileName, vector<unsigned char> &data){
    vector<unsigned char> buffer;
    pack(buffer, read_file); // Empaquetamos el comando
    pack(buffer, fileName.size()); //Tamaño del nombre del archivo
    packv(buffer, fileName.data(), fileName.size()); // Empaquetamos el nombre del archivo
    sendMSG<unsigned char>(conn.serverId, buffer); // Lo mandamos

    // Recibimos los bytes del fichero
    vector<unsigned char> response;
    recvMSG<unsigned char>(conn.serverId, response);

    vector<unsigned char> fileData;
    fileData.resize(unpack<size_t>(response)); // Desempaquetamos la longitud del fichero
    unpackv(response, fileData.data(), fileData.size()); // Desempaquetamos

    data = fileData;


}

void FileManager::writeFile(string fileName, vector<unsigned char> &data){
    vector<unsigned char> buffer;
    
    // Empaquetamos el comando
    pack(buffer, write_file);
    // Empaquetamos el nombre del fichero
    pack(buffer, fileName.size());
    packv(buffer, fileName.data(), fileName.size());
    // Empaquetamos el contenido del archivo
    pack(buffer, data.size());
    packv(buffer, data.data(), data.size());

    // Lo mandamos al server
    sendMSG<unsigned char>(conn.serverId, buffer);

    // Respuesta
    vector<unsigned char> response;
    recvMSG<unsigned char>(conn.serverId, response);

    
}


