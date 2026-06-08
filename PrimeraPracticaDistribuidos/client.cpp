#include "utils.h"
#include <iostream>
#include <string>
#include <thread>
#include <list>
#include <sstream>
#include <fstream>
#include <experimental/filesystem>



#define SERVERPORT 1234
#define LOCALHOST "127.0.0.1"
using namespace std;
using namespace std::experimental::filesystem;

enum Command {
    list_files, // lls --> listFile()
    read_file,  // download --> readFile()
    write_file  // upload --> writeFile()
};

int main(int argc, char** argv)
{
// 1. Conectamos al servidor
connection_t conn = initClient(LOCALHOST, SERVERPORT);

if(conn.socket==-1){
    cout<<"Error connecting to server"<<endl;
    return -1;
}
cout<<"Connected to server with id "<<conn.serverId<<endl;

//Creamos el vector donde se almacenaran los comandos
string command;
string fileName;
string line;

do{
    cout<< ": ";
    getline(cin, line);     // 1. Leer la linea entera
    stringstream ss(line);   // 2. meterla en el "tubo"
    ss >> command;           // 3. Sacamos el comando

    if (command == "lls"){
        vector<unsigned char> buffer;
        pack(buffer, list_files);
        sendMSG<unsigned char>(conn.serverId, buffer);

        // Mostramos la respuesta
        vector<unsigned char> response;
        recvMSG<unsigned char>(conn.serverId, response);

        // Desempaquetamos
        size_t numFiles = unpack<size_t>(response);             // en forma de bucle
        for (size_t i = 0; i < numFiles; i++){
            string name;
            name.resize(unpack<size_t>(response));              // longitud de este nombre
            unpackv(response, (char*)name.data(), name.size()); // su nombre
            cout << name << endl;
        }

    }
    else if(command == "download"){
        ss >> fileName; // Sacamos el nombre del archivo a descargar

        vector<unsigned char> buffer;
        pack(buffer, read_file); // Empaquetamos el comando
        pack(buffer, fileName.size()); // Empaquetamos la longitud del nombre del archivo
        packv(buffer, fileName.data(), fileName.size()); // Empaquetamos el nombre del archivo
        sendMSG<unsigned char>(conn.serverId, buffer); // Enviamos el comando al servidor

        // Recibimos los bytes del fichero
        vector<unsigned char> response;
        recvMSG<unsigned char>(conn.serverId, response);

        vector<unsigned char> fileData;
        fileData.resize(unpack<size_t>(response)); // Desempaquetamos la longitud del fichero
        unpackv(response, fileData.data(), fileData.size()); // Desempaquetamos
        

        ofstream file(fileName, ios::out | ios::binary);
        if(file.is_open()){
            file.write((char*)fileData.data(), fileData.size());
            file.close();
            cout << "Descargado: " << fileName << " (" << fileData.size() << " bytes)" << endl;
        }else{
            cout << "ERROR: no se pudo crear el fichero " << fileName << endl;
        }
    }
    else if(command == "upload"){
        ss >> fileName; // Sacamos el nombre del archivo a subir

        ifstream file(fileName, ios::in | ios::binary);
        if(file.is_open()){
            vector<unsigned char> fileData((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
            file.close();

            vector<unsigned char> buffer;
            pack(buffer, write_file); // Empaquetamos el comando
            pack(buffer, fileName.size()); // Empaquetamos la longitud del nombre del archivo
            packv(buffer, fileName.data(), fileName.size()); // Empaquetamos el nombre del archivo
            pack(buffer, fileData.size()); // Empaquetamos la longitud del fichero
            packv(buffer, fileData.data(), fileData.size()); // Empaquetamos el contenido del fichero
            sendMSG<unsigned char>(conn.serverId, buffer); // Enviamos el comando al servidor

            // El server nos confirma
            vector<unsigned char> response;
            recvMSG<unsigned char>(conn.serverId, response);
            bool ok = unpack<bool>(response);
            if(ok){
                cout << "Subido: " << fileName << " (" << fileData.size() << " bytes)" << endl;
            }else{
                cout << "ERROR: no se pudo subir el fichero " << fileName << endl;
            }
        }
    }
    else if(command == "ls"){
        cout<<"Listing files in local path"<<endl;
			experimental::filesystem::path directorypath="./";

			for (const auto& entry : 
					 directory_iterator(directorypath)) { 
					 if(is_regular_file(entry))
						cout<<entry.path()<<endl; 
				} 
			cout<<endl;
    }
    else if(command == "exit()"){
        cout << "Exiting..." << endl;
        closeConnection(conn.serverId);
    }


}while(command != "exit()");
return 0;
}
