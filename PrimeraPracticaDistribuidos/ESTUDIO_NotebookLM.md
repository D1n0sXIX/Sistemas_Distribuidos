# Práctica 1 — Sistemas Distribuidos: FileManager por Red
## Documento de estudio completo para Google NotebookLM

---

## 1. VISIÓN GENERAL DEL SISTEMA

### ¿Qué hace esta práctica?

Se toma una clase C++ llamada `FileManager` (que gestiona ficheros en disco) y se **distribuye por red** usando sockets TCP. El objetivo es que el programa original que la usa (`main_fm.cpp`) funcione sin ningún cambio, aunque en realidad la clase esté ejecutándose en otra máquina.

Este patrón se llama **objeto remoto transparente** y es la base de tecnologías como RPC (Remote Procedure Call), gRPC, o Java RMI.

### Los tres roles del sistema

| Rol | Archivo | Qué hace |
|---|---|---|
| **Servidor de objetos** | `server.cpp` | Tiene el FileManager real. Recibe peticiones por red, las ejecuta y devuelve el resultado |
| **Proxy (cliente transparente)** | `filemanager_proxy.cpp` | Finge ser un FileManager. En realidad serializa las llamadas y las manda por red al servidor |
| **Broker** | `broker.cpp` | Intermediario de descubrimiento. Sabe qué servidores existen y cuáles tienen menos carga |

### Lo que NO se modifica (suministrado por el profesor)

| Archivo | Contenido |
|---|---|
| `filemanager.h` | Declaración de la clase FileManager real (contrato de interfaz) |
| `libFileManager.a` | Implementación compilada del FileManager real (toca el disco) |
| `main_fm.cpp` | Programa de prueba — usa FileManager sin saber si es real o proxy |
| `utils.h / utils.cpp` | Capa de red: sockets TCP, serialización pack/unpack, send/recv |

---

## 2. ARQUITECTURA DEL SISTEMA

### Arquitectura sin broker (Fases 1-4)

```
┌─────────────────────────────────┐          TCP (puerto 1234)       ┌─────────────────────────────────┐
│           CLIENTE               │ ──────────────────────────────►  │           SERVIDOR               │
│                                 │                                   │                                  │
│  main_fm.cpp                    │  ◄──────────────────────────────  │  server.cpp                      │
│    │                            │                                   │    │                             │
│    └──► FileManager proxy       │  envía: Command + argumentos      │    └──► FileManager real         │
│         (filemanager_proxy.cpp) │  recibe: resultado serializado    │         (libFileManager.a)        │
│         · serializa llamadas    │                                   │         · listFiles()             │
│         · gestiona conexión TCP │                                   │         · readFile()              │
│                                 │                                   │         · writeFile()             │
└─────────────────────────────────┘                                   └─────────────────────────────────┘
```

### Arquitectura completa con broker (Fase 5)

```
                         ┌──────────────────────────────┐
                         │           BROKER              │
                         │        broker.cpp             │
                         │    puerto fijo: 5000          │
                         │                               │
                         │  ServerList[] = [             │
                         │    { ip, port, active=2 },    │
                         │    { ip, port, active=0 },    │
                         │  ]                            │
                         └───────────────┬───────────────┘
                                         │
              ┌──────────────────────────┴─────────────────────────────┐
              │ register_server                                         │ request_server
              │ (server → broker al arrancar)                           │ release_server
              │                                                         │ (proxy → broker en ctor/dtor)
              │                                                         │
┌─────────────▼──────────────┐                           ┌─────────────▼──────────────┐
│          SERVER             │◄────────── TCP ──────────►│    PROXY (dentro de        │
│       server.cpp            │      (puerto asignado     │    filemanager_proxy.cpp)  │
│  puerto: arg. de línea cmd  │       por el broker)      │                            │
│  FileManager real           │                           │  main_fm.cpp llama         │
│  libFileManager.a           │                           │  fm.listFiles() etc.       │
└─────────────────────────────┘                           └────────────────────────────┘
```

### Flujo de arranque del sistema completo

```
Paso 1: broker arranca → initServer(5000) → espera conexiones

Paso 2: server arranca
        → initClient(broker, 5000)       [actúa como cliente del broker]
        → envía: register_server + su puerto
        → recibe: ACK (bool true)
        → initServer(su_puerto)          [ahora sí arranca como servidor]

Paso 3: proxy se construye (FileManager::FileManager)
        → initClient(broker, 5000)
        → envía: request_server
        → recibe: IP:puerto del servidor con menos carga
        → closeConnection(broker)
        → initClient(servidor asignado)  [conexión que durará toda la sesión]

Paso 4: proxy se destruye (FileManager::~FileManager)
        → initClient(broker, 5000)
        → envía: release_server + IP + puerto  [para que el broker decremente el contador]
        → closeConnection(broker)
        → closeConnection(servidor)
```

---

## 3. LA CAPA DE RED: utils.h

Esta capa la proporciona el profesor. Es la abstracción sobre TCP que usan todos los componentes.

### Tipos de datos clave

```cpp
connection_t conn;   // resultado de initClient()
                     // conn.socket   → descriptor de socket Unix
                     // conn.serverId → ID interno de utils (índice para send/recv)
```

### Funciones del servidor

```cpp
initServer(int puerto);        // arranca el servidor, empieza a aceptar conexiones
bool checkClient();            // true si hay un cliente esperando ser aceptado
int getLastClientID();         // devuelve el ID del último cliente aceptado
```

### Funciones del cliente

```cpp
connection_t initClient(string ip, int puerto);  // conecta a un servidor remoto
                                                  // devuelve connection_t
closeConnection(int serverId);                    // cierra la conexión
```

### Funciones de mensajería

```cpp
sendMSG<unsigned char>(int id, vector<unsigned char> buffer);  // envía el buffer completo
recvMSG<unsigned char>(int id, vector<unsigned char> &buffer); // recibe en el buffer
// Si buffer queda vacío tras recvMSG → el cliente se ha desconectado
```

### Funciones de serialización

```cpp
pack(vector<unsigned char> &buf, T valor);                      // añade sizeof(T) bytes al buffer
packv(vector<unsigned char> &buf, const void* datos, size_t n); // añade n bytes raw

T val = unpack<T>(vector<unsigned char> &buf);                  // extrae sizeof(T) bytes del FRENTE
unpackv(vector<unsigned char> &buf, void* dest, size_t n);      // extrae n bytes raw del frente
```

> **Importante:** `unpack` y `unpackv` consumen el buffer desde el principio (tienen un cursor interno). Cada llamada avanza el cursor al siguiente dato. Se deben llamar en el **mismo orden** en que se empaquetaron.

---

## 4. SERIALIZACIÓN: EL PATRÓN PACK/UNPACK

### ¿Por qué es necesario serializar?

TCP transmite un flujo continuo de bytes, sin separadores entre mensajes. Si mandamos dos strings seguidos:

```
"hola" + "mundo" → 686f6c61 6d756e64 6f  (9 bytes, sin separación)
```

El receptor no puede saber dónde termina uno y empieza el otro.

### La solución: longitud antes del dato ("framing")

```
[4 bytes: longitud]["hola"][5 bytes: longitud]["mundo"]
   pack(4)          packv   pack(5)             packv
```

El receptor lee la longitud primero, y sabe exactamente cuántos bytes debe leer a continuación.

### Tamaños de tipos en C++ de 64 bits

| Tipo | sizeof() | Uso |
|---|---|---|
| `int` | 4 bytes | puertos, contadores pequeños |
| `size_t` | 8 bytes | longitudes de strings y buffers |
| `bool` | 1 byte | ACKs y confirmaciones |
| `enum` (Command, BrokerCommand) | 4 bytes | identificador de operación |

### Ejemplo completo: empaquetar una petición `read_file`

**En el cliente (o proxy):**
```cpp
vector<unsigned char> buffer;

pack(buffer, read_file);            // 4 bytes: el comando (enum → int)
pack(buffer, fileName.size());      // 8 bytes: longitud del nombre (size_t)
packv(buffer, fileName.data(), fileName.size());  // N bytes: el nombre

sendMSG<unsigned char>(conn.serverId, buffer);    // envía de golpe
```

**Representación del buffer en memoria:**
```
┌─────────────┬──────────────────┬──────────────────────────────┐
│ Command (4B)│  len nombre (8B) │  nombre (len bytes)          │
│  read_file  │       7          │  "prueba"                    │
└─────────────┴──────────────────┴──────────────────────────────┘
   pack(cmd)    pack(name.size())   packv(name.data(), name.size())
```

**En el servidor (desempaquetar):**
```cpp
vector<unsigned char> buffer;
recvMSG<unsigned char>(clientID, buffer);

Command cmd = unpack<Command>(buffer);    // extrae los primeros 4 bytes → read_file
string fileName;
fileName.resize(unpack<size_t>(buffer));  // extrae 8 bytes → 7, redimensiona el string
unpackv(buffer, (char*)fileName.data(), fileName.size()); // extrae 7 bytes → "prueba"
```

### Ejemplo: respuesta a `list_files`

**Servidor empaqueta:**
```cpp
vector<string> files = fm.listFiles();   // ej: ["a.txt", "b.txt"]
vector<unsigned char> response;
pack(response, files.size());            // pack(2) → 8 bytes
for (const string& f : files) {
    pack(response, f.size());            // pack(5) → 8 bytes para "a.txt"
    packv(response, f.data(), f.size()); // packv("a.txt") → 5 bytes
    // repite para "b.txt"
}
sendMSG<unsigned char>(clientID, response);
```

**Representación del buffer:**
```
┌───────────┬──────────┬─────────┬──────────┬─────────┐
│ nFich (8B)│len (8B)  │"a.txt"  │len (8B)  │"b.txt"  │
│     2     │    5     │ 5 bytes │    5     │ 5 bytes │
└───────────┴──────────┴─────────┴──────────┴─────────┘
```

**Cliente desempaqueta:**
```cpp
vector<unsigned char> response;
recvMSG<unsigned char>(conn.serverId, response);

size_t numFiles = unpack<size_t>(response);   // → 2
for (size_t i = 0; i < numFiles; i++) {
    string name;
    name.resize(unpack<size_t>(response));    // → 5
    unpackv(response, (char*)name.data(), name.size()); // → "a.txt"
    cout << name << endl;
}
```

---

## 5. EL PROTOCOLO DE COMANDOS

### Enum Command (FileManager — compartido entre server, client y proxy)

```cpp
enum Command {
    list_files = 0,   // lls en el terminal → llama a fm.listFiles()
    read_file  = 1,   // download → llama a fm.readFile()
    write_file = 2    // upload → llama a fm.writeFile()
};
```

El valor que viaja por la red es el entero (0, 1, 2), no el nombre. Si server y client tienen el enum en distinto orden, el servidor interpretará mal el comando → **bug silencioso**.

### Tabla completa de mensajes FileManager

| Operación | El cliente envía | El servidor responde |
|---|---|---|
| `list_files` | Command (4B) | nFicheros (8B) + para cada uno: len(8B) + nombre |
| `read_file` | Command (4B) + len_nombre (8B) + nombre | nBytes (8B) + bytes del fichero |
| `write_file` | Command (4B) + len_nombre (8B) + nombre + len_datos (8B) + datos | bool confirmación (1B) |

### Enum BrokerCommand (Broker — separado del anterior)

```cpp
enum BrokerCommand {
    register_server = 0,  // server → broker: "soy un servidor, apúntame"
    request_server  = 1,  // proxy → broker: "dame un servidor disponible"
    release_server  = 2   // proxy → broker: "ya acabé, decrementa el contador"
};
```

### Tabla de mensajes del Broker

| Mensaje | Quién envía → a quién | El emisor manda | El broker responde |
|---|---|---|---|
| `register_server` | server → broker | BrokerCommand (4B) + puerto (4B) | bool ACK (1B) |
| `request_server` | proxy → broker | BrokerCommand (4B) | bool ok + len_ip (8B) + ip + puerto (4B) |
| `release_server` | proxy → broker | BrokerCommand (4B) + len_ip (8B) + ip + puerto (4B) | (nada) |

---

## 6. CÓDIGO DETALLADO POR COMPONENTE

### 6.1 server.cpp — Servidor de objetos

**Variables globales:**
```cpp
FileManager fm("FileManagerDir");  // instancia real, compartida entre todos los hilos
mutex fm_mutex;                    // protege fm de accesos concurrentes
```

**Secuencia del main():**
```
1. Lee puerto de argv[1]
2. initClient(broker) → register_server + puerto → espera ACK
3. initServer(puerto) → arranca como servidor
4. Bucle: checkClient() → getLastClientID() → thread(atenderCliente, id)
```

**Función atenderCliente(int clientID) — bucle infinito:**
```
while(true):
  recvMSG → buffer
  si buffer vacío → cliente desconectado → return
  unpack<Command>(buffer) → cmd
  switch(cmd):
    case list_files:
      fm_mutex.lock()
      files = fm.listFiles()
      fm_mutex.unlock()
      empaquetar: pack(nFiles) + para cada uno: pack(len) + packv(nombre)
      sendMSG(response)

    case read_file:
      unpack el nombre del archivo del buffer
      fm_mutex.lock()
      fm.readFile(nombre, fileData)
      fm_mutex.unlock()
      empaquetar: pack(fileData.size()) + packv(fileData)
      sendMSG(response)

    case write_file:
      unpack nombre del buffer
      unpack datos del buffer
      fm_mutex.lock()
      fm.writeFile(nombre, datos)
      fm_mutex.unlock()
      empaquetar: pack(true)   ← confirmación
      sendMSG(response)
```

**Gestión de concurrencia:**
- Cada cliente se atiende en su propio hilo (`thread::detach()` → corre independientemente).
- El objeto `fm` es compartido → el `fm_mutex` evita que dos hilos lo llamen a la vez (condición de carrera).

### 6.2 filemanager_proxy.cpp — El proxy (patrón Proxy remoto)

**Concepto clave:** El proxy implementa la **misma interfaz** que el FileManager real. El `main_fm.cpp` crea un `FileManager("ruta")` y llama sus métodos sin saber si es el objeto real o el proxy.

```cpp
class FileManager {  // mismo nombre que la clase real
private:
    connection_t conn;  // socket abierto al servidor (dura toda la sesión)
    string ip;          // IP del servidor asignado (para release_server)
    int port;           // puerto del servidor asignado
public:
    FileManager(string path);    // ignora path, se conecta al broker y al servidor
    ~FileManager();
    vector<string> listFiles();
    void readFile(string fileName, vector<unsigned char> &data);
    void writeFile(string fileName, vector<unsigned char> &data);
};
```

**Constructor — secuencia completa:**
```
1. initClient(broker, 5000)
2. pack(request_server) → sendMSG(broker)
3. recvMSG(broker) → unpack bool, unpack len_ip, unpackv ip, unpack port
4. closeConnection(broker)         ← ya no necesitamos al broker
5. this->ip = ip_recibida
6. this->port = port_recibido
7. this->conn = initClient(ip, port)   ← conexión al servidor asignado
```

**Destructor — secuencia completa:**
```
1. initClient(broker, 5000)
2. pack(release_server) + pack(ip.size()) + packv(ip) + pack(port) → sendMSG
3. closeConnection(broker)
4. closeConnection(this->conn.serverId)   ← cierra la conexión al servidor
```

**listFiles() — flujo:**
```
1. pack(list_files) → sendMSG(conn.serverId)
2. recvMSG(conn.serverId) → response
3. unpack<size_t>(response) → numFiles
4. bucle numFiles veces:
     name.resize(unpack<size_t>(response))
     unpackv(response, name.data(), name.size())
     files.push_back(name)
5. return files
```

**readFile(fileName, data) — flujo:**
```
1. pack(read_file) + pack(fileName.size()) + packv(fileName) → sendMSG
2. recvMSG → response
3. fileData.resize(unpack<size_t>(response))
4. unpackv(response, fileData.data(), fileData.size())
5. data = fileData   ← rellena el parámetro por referencia
```

**writeFile(fileName, data) — flujo:**
```
1. pack(write_file) + pack(fileName.size()) + packv(fileName)
   + pack(data.size()) + packv(data) → sendMSG
2. recvMSG → response   (bool confirmación, se puede ignorar)
```

### 6.3 broker.cpp — El intermediario

**Estructura de datos:**
```cpp
struct Servidores_t {
    string ip;      // IP del servidor
    int port;       // puerto del servidor
    int active = 0; // número de conexiones activas en este momento
};
vector<Servidores_t> ServerList;   // lista global de servidores registrados
mutex ServerList_mutex;             // protege ServerList de accesos concurrentes
```

**Cómo obtiene la IP del servidor que se registra:**
El servidor solo manda su puerto. La IP se obtiene del socket:

```cpp
struct sockaddr_in addr;
socklen_t addrlen = sizeof(addr);
int sock = clientList[clientID].socket;
getpeername(sock, (struct sockaddr*)&addr, &addrlen);
string serverIp = inet_ntoa(addr.sin_addr);
```

Por qué se hace así: el servidor no sabe su propia IP "hacia fuera" (en AWS puede tener IP privada distinta a la pública). El socket TCP sí tiene la IP real del extremo remoto.

**Algoritmo de balanceo B2 (mínima carga):**
```cpp
Servidores_t* minServer = &ServerList[0];  // puntero al primer servidor
for (auto& server : ServerList) {          // referencia: no copia
    if (server.active < minServer->active) {
        minServer = &server;               // redirigimos el puntero al nuevo mínimo
    }
}
ServerList_mutex.lock();
minServer->active++;     // incrementamos el contador del servidor elegido
ServerList_mutex.unlock();
// luego enviamos minServer->ip y minServer->port al proxy
```

**Explicación de puntero vs referencia en este contexto:**

| Símbolo | Qué hace en este código |
|---|---|
| `Servidores_t* minServer` | Puntero: puede apuntar a distintos elementos del vector |
| `&ServerList[0]` | Operador dirección-de: inicializa el puntero con la dirección del primer elemento |
| `auto& server` | Referencia en el for: no copia, permite modificar el elemento real |
| `minServer->active` | Accede al campo `active` a través del puntero |
| `minServer = &server` | Redirige el puntero al servidor con menos carga |

### 6.4 client.cpp — Cliente de prueba (terminal interactivo)

Este archivo es un cliente de referencia con terminal interactivo. NO usa el proxy; llama directamente a los primitivos de red. Sirve para probar el servidor de forma manual.

**Comandos disponibles:**
```
lls           → list_files por red → muestra ficheros del servidor
download <f>  → read_file por red → descarga el fichero y lo guarda en local
upload <f>    → write_file por red → lee el fichero local y lo sube al servidor
ls            → lista ficheros del directorio LOCAL (no va por red)
exit()        → cierra la conexión y termina
```

**Ciclo de vida de la conexión:**
```
initClient("127.0.0.1", 1234) → conn
[bucle de comandos]
exit() → closeConnection(conn.serverId)
```

---

## 7. CICLO DE VIDA DE UNA CONEXIÓN TCP

```
LADO CLIENTE                              LADO SERVIDOR
────────────                              ─────────────
connection_t conn = initClient(ip, p)     initServer(port)
                                          while(checkClient()):
                                            clientID = getLastClientID()
                                            thread(atenderCliente, clientID)

sendMSG(conn.serverId, buf)    ──────►    recvMSG(clientID, buf)
recvMSG(conn.serverId, resp)   ◄──────    sendMSG(clientID, resp)

[se pueden hacer N peticiones sobre la misma conexión]

closeConnection(conn.serverId)            recvMSG devuelve buffer vacío
                                          → el hilo detecta desconexión y termina
```

**conn.serverId** no es un puerto ni una IP. Es el índice interno que usa `utils` para identificar con quién hablar en sus tablas internas.

---

## 8. CONCURRENCIA: HILOS Y MUTEX

### Por qué un hilo por cliente

Sin hilos, el servidor atiende a un cliente a la vez: mientras lee un fichero grande, los demás clientes esperan. Con hilos:

```
cliente 1 ──► hilo 1 ──► fm.readFile("grande.mp4") ...
cliente 2 ──► hilo 2 ──► fm.listFiles()   ← se ejecuta en paralelo
cliente 3 ──► hilo 3 ──► fm.writeFile(...) ← también en paralelo
```

### El problema de la condición de carrera

El objeto `fm` (FileManager) es **compartido** entre todos los hilos. Si dos hilos llaman a `fm.readFile()` a la vez, pueden interferir en los datos internos de `fm` → resultado corrupto.

### La solución: mutex

```cpp
fm_mutex.lock();     // solo un hilo puede pasar aquí a la vez
fm.readFile(...)     // sección crítica: acceso exclusivo al fm
fm_mutex.unlock();   // libera el candado → siguiente hilo puede entrar
```

**En el broker** también hay un mutex para `ServerList`:
```cpp
ServerList_mutex.lock();
ServerList.push_back(Server);  // o minServer->active++
ServerList_mutex.unlock();
```

### Creación y desconexión de hilos

```cpp
thread* t = new thread(atenderCliente, clientID);
t->detach();   // el hilo vive independientemente del puntero t
               // cuando atenderCliente() termina, el hilo se destruye solo
```

`detach()` significa: "no necesito unirme a este hilo después, que viva por su cuenta". Sin `detach()`, habría que hacer `join()` para esperar a que el hilo termine, lo que bloquearía el bucle principal.

---

## 9. BUILD SYSTEM: CMakeLists.txt

### Cómo funciona la compilación

```
cmake ..    → lee CMakeLists.txt → genera Makefiles
make        → compila todo
```

### Cuatro ejecutables que se generan

| Ejecutable | Fuentes | Bibliotecas extra |
|---|---|---|
| `server` | utils.cpp + server.cpp | pthread, libFileManager.a, stdc++fs |
| `client` | utils.cpp + client.cpp | pthread, stdc++fs |
| `fileManager` | utils.cpp + filemanager_proxy.cpp + main_fm.cpp | pthread, stdc++fs |
| `broker` | utils.cpp + broker.cpp | pthread, stdc++fs |

**Por qué `server` enlaza libFileManager.a y `fileManager` (proxy) no:**
- El servidor necesita ejecutar el FileManager real → necesita su implementación (.a)
- El proxy solo serializa llamadas por red → no necesita la implementación real

**`stdc++fs`**: biblioteca de sistema de ficheros de C++17 (para `experimental::filesystem` usado en `client.cpp`)

**`pthread`**: biblioteca de hilos POSIX (para `std::thread` en Linux)

### Importar una biblioteca estática precompilada

```cmake
add_library(FileManager STATIC IMPORTED)
set_target_properties(FileManager PROPERTIES
  IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/libFileManager.a"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/"
)
```

`STATIC IMPORTED` le dice a CMake que esta biblioteca ya está compilada, solo hay que enlazarla.

---

## 10. PATRONES DE DISEÑO USADOS

### Patrón Proxy Remoto

El `FileManager` proxy tiene exactamente la misma interfaz que el `FileManager` real. `main_fm.cpp` usa `FileManager("ruta")` y llama `fm.listFiles()`, `fm.readFile()`, `fm.writeFile()` sin saber que en realidad esas llamadas viajan por red.

```
main_fm.cpp:
  FileManager fm("ruta");    ← construye el proxy, no el real
  fm.listFiles();            ← serializa y manda por TCP, el cliente no lo sabe
```

Este patrón se llama **transparencia de ubicación** — el llamador no sabe si el objeto es local o remoto.

### Patrón Servidor de Objetos

El servidor mantiene una única instancia del objeto real y gestiona múltiples clientes que acceden a él de forma concurrente. Cada cliente tiene su propio hilo, pero comparten el mismo objeto.

### Patrón Broker / Service Locator

El broker actúa como directorio de servicios. Los servidores se registran en él, y los clientes lo consultan para encontrar dónde conectarse. Además hace balanceo de carga (selecciona el servidor con menos clientes activos).

---

## 11. DIAGRAMA DE FLUJO: UNA LLAMADA COMPLETA (listFiles con broker)

```
main_fm.cpp                proxy                broker               server
    │                        │                     │                    │
    │ fm.listFiles()          │                     │                    │
    │────────────────────────►│                     │                    │
    │                         │                     │                    │
    │                         │──initClient(5000)──►│                    │
    │                         │──request_server────►│                    │
    │                         │◄── ip + port ───────│                    │
    │                         │──closeConn(broker)  │                    │
    │                         │                     │                    │
    │                         │──initClient(ip,port)────────────────────►│
    │                         │                     │                    │ (ya conectado en ctor)
    │                         │                     │                    │
    │                         │──list_files────────────────────────────►│
    │                         │                     │                    │ fm.listFiles()
    │                         │                     │                    │ ["a.txt","b.txt"]
    │                         │◄── 2+"a.txt"+"b.txt"────────────────────│
    │                         │                     │                    │
    │◄────────────────────────│                     │                    │
    │ ["a.txt", "b.txt"]      │                     │                    │
```

---

## 12. CONCEPTOS CLAVE PARA EL EXAMEN

### ¿Por qué pack la longitud antes del dato?

TCP no conoce el concepto de "mensaje" — es un flujo de bytes. Sin longitud previa, el receptor no sabe cuándo termina un dato y empieza otro. Al mandar la longitud primero, el receptor sabe exactamente cuántos bytes leer.

### ¿Por qué getpeername() en el broker?

El servidor que se registra no sabe su propia IP "hacia fuera" (en AWS tiene IP privada distinta a la pública). En cambio, el socket TCP que llega al broker sí contiene la IP real del extremo remoto. `getpeername()` extrae esa información del socket.

### .h vs .a — la diferencia entre contrato e implementación

- `.h` (header): **contrato** — declara qué métodos existen y con qué tipos. Se programa contra él.
- `.a` (static library): **implementación compilada** — contiene el código máquina. Solo se enlaza cuando se necesita ejecutar el objeto real.
- El proxy programa contra el `.h` (conoce la interfaz) pero no enlaza la `.a` (no ejecuta el objeto real).

### Transparencia del proxy

`main_fm.cpp` no cambió ni una línea. La misma llamada `fm.listFiles()` funciona localmente (con la `.a`) o por red (con el proxy). Esto es el objetivo de la práctica: **distribución transparente**.

### Balanceo de carga B2

El broker elige siempre el servidor con menos conexiones activas (`active`). Incrementa el contador al asignar (`request_server`) y lo decrementa al liberar (`release_server`). Así, si hay N servidores, las peticiones se distribuyen al que menos carga tiene en ese momento.

### Hilo por cliente vs hilo único

| Modelo | Ventaja | Desventaja |
|---|---|---|
| Hilo único | Sencillo, sin condiciones de carrera | Un cliente bloquea a todos |
| Hilo por cliente | Atiende varios a la vez | Necesita mutex para recursos compartidos |

En esta práctica se usa hilo por cliente, con mutex sobre el FileManager compartido.

---

## 13. ERRORES COMUNES Y CÓMO EVITARLOS

| Error | Consecuencia | Solución |
|---|---|---|
| Enum en distinto orden en server y client | El servidor interpreta mal el comando (bug silencioso) | Definir el enum una sola vez o verificar que coinciden |
| No hacer lock del mutex antes de usar `fm` | Condición de carrera: resultado corrupto o crash | Siempre lock/unlock alrededor de fm |
| Unpack en orden distinto al pack | Se extraen datos incorrectos | Mantener el mismo orden estricto |
| No enviar la longitud antes del string | El receptor no sabe cuántos bytes leer | Siempre pack(str.size()) antes de packv(str) |
| `t->detach()` olvidado | El hilo no corre independientemente | Siempre detach() tras crear el hilo de atención |
| No cerrar la conexión al broker tras request_server | Fuga de conexiones | closeConnection(brokerConn.serverId) tras recibir IP:puerto |

---

## 14. GLOSARIO TÉCNICO

| Término | Definición en contexto |
|---|---|
| **TCP** | Protocolo de transporte orientado a conexión. Garantiza entrega en orden, sin pérdidas. Es un flujo de bytes, no de mensajes. |
| **Socket** | Extremo de una conexión TCP. Identificado por IP:puerto. |
| **Serialización** | Convertir datos (string, int, vector) a bytes para transmitir por red. |
| **Deserialización** | Reconstruir los datos a partir de los bytes recibidos. |
| **Framing** | Técnica de delimitar mensajes en un flujo de bytes (aquí: longitud + datos). |
| **Mutex** | Mecanismo de exclusión mutua. Solo un hilo puede tener el candado a la vez. |
| **Condición de carrera** | Bug donde el resultado depende del orden de ejecución de los hilos. |
| **Proxy** | Objeto que implementa la misma interfaz que el real pero redirige las llamadas (aquí, por red). |
| **Broker** | Intermediario que conecta consumidores con proveedores de servicios. |
| **Balanceo de carga** | Distribuir las peticiones entre varios servidores para no saturar ninguno. |
| **initServer / initClient** | Funciones de utils que abstraen la creación de sockets TCP. |
| **pack / unpack** | Funciones de utils para serializar datos en un buffer de bytes. |
| **connection_t** | Tipo que encapsula una conexión: socket + serverId (índice interno de utils). |
| **serverId** | Índice interno de utils para identificar una conexión activa. No es un puerto. |
| **getpeername()** | Llamada del SO que devuelve la IP y puerto del extremo remoto de un socket. |
| **detach()** | Desconecta el hilo de su handle → vive de forma independiente. |
| **libFileManager.a** | Biblioteca estática: archivo con código objeto (.o) enlazado en tiempo de compilación. |
| **CMake** | Sistema de configuración de compilación multiplataforma. Genera Makefiles. |
