#pragma once
#include "utils.h"
#include <string>
#include <vector>

using namespace std;

#define SERVERPORT 1234
#define LOCALHOST  "127.0.0.1"

// Mismo enum que en server.cpp — deben coincidir en orden
enum Command {
    list_files,
    read_file,
    write_file
};

class FileManager
{
private:
    // Guarda la conexión abierta con el servidor para usarla en cada método
    connection_t conn;

public:
    // Constructor: recibe el path igual que el FileManager real,
    // pero aquí lo ignoramos — lo que hacemos es conectarnos al servidor.
    // Pista: initClient devuelve una connection_t; guárdala en conn.
    FileManager(string path);

    // Destructor: cierra la conexión limpiamente.
    // Pista: closeConnection(conn.serverId)
    ~FileManager();

    // Devuelve la lista de ficheros del servidor.
    // Qué mandar:  Command list_files  (solo eso)
    // Qué recibir: nº ficheros → para cada uno: longitud + nombre
    // Pista: mira el bloque "lls" de client.cpp, pero en vez de imprimir
    //        mete cada nombre en un vector<string> y devuélvelo.
    vector<string> listFiles();

    // Rellena 'data' con el contenido del fichero fileName del servidor.
    // Qué mandar:  Command read_file + longitud nombre + nombre
    // Qué recibir: longitud datos + datos
    // Pista: mira el bloque "download" de client.cpp, pero en vez de
    //        guardar a disco, resize data y unpackv directamente sobre ella.
    void readFile(string fileName, vector<unsigned char> &data);

    // Sube al servidor el fichero fileName con el contenido de data.
    // Qué mandar:  Command write_file + longitud nombre + nombre + longitud datos + datos
    // Qué recibir: bool de confirmación (puedes ignorarlo o lanzar una excepción si es false)
    // Pista: mira el bloque "upload" de client.cpp.
    void writeFile(string fileName, vector<unsigned char> &data);
};
