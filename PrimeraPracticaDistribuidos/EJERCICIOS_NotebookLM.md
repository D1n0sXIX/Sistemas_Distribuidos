# Práctica 1 — Ejercicios de estudio activo
### Programación de Sistemas Distribuidos · Para usar con Google NotebookLM

> Este documento contiene ejercicios de **recuerdo activo**. Para cada ejercicio: intenta responder de memoria, luego contrasta con el documento de teoría `ESTUDIO_NotebookLM.md`.
>
> Formato de uso recomendado en NotebookLM: pide al modelo que te haga los ejercicios uno a uno, que corrija tu respuesta y te explique los errores antes de pasar al siguiente.

---

## BLOQUE 1 — Serialización pack/unpack

### Ejercicio 1.1 — list_files: cliente envía

Escribe el código C++ completo del **cliente/proxy** para empaquetar y enviar la petición `list_files`. Solo hay que mandar el comando (no lleva argumentos).

Restricciones: usa `vector<unsigned char>`, `pack`, `sendMSG`. No copies código — hazlo de memoria.

<details>
<summary>Solución</summary>

```cpp
vector<unsigned char> buffer;
pack(buffer, list_files);                        // 4 bytes: el enum Command
sendMSG<unsigned char>(conn.serverId, buffer);   // envía
```

</details>

---

### Ejercicio 1.2 — list_files: servidor responde

El servidor ha recibido un `list_files`. Tiene el resultado en `vector<string> files`. Escribe el código que empaqueta la respuesta y la envía.

<details>
<summary>Solución</summary>

```cpp
vector<unsigned char> response;
pack(response, files.size());                          // 8 bytes: número de ficheros (size_t)
for (const string& f : files) {
    pack(response, f.size());                          // 8 bytes: longitud del nombre
    packv(response, f.data(), f.size());               // N bytes: el nombre
}
sendMSG<unsigned char>(clientID, response);
```

</details>

---

### Ejercicio 1.3 — list_files: cliente desempaqueta la respuesta

El cliente acaba de hacer `recvMSG` y tiene la respuesta en `vector<unsigned char> response`. Escribe el código que extrae la lista de nombres de ficheros y la imprime.

<details>
<summary>Solución</summary>

```cpp
size_t numFiles = unpack<size_t>(response);
for (size_t i = 0; i < numFiles; i++) {
    string name;
    name.resize(unpack<size_t>(response));
    unpackv(response, (char*)name.data(), name.size());
    cout << name << endl;
}
```

</details>

---

### Ejercicio 1.4 — read_file: cliente envía

Escribe el código que empaqueta la petición `read_file` con el nombre de fichero `fileName` (tipo `string`).

<details>
<summary>Solución</summary>

```cpp
vector<unsigned char> buffer;
pack(buffer, read_file);                              // comando (4B)
pack(buffer, fileName.size());                        // longitud del nombre (8B)
packv(buffer, fileName.data(), fileName.size());      // bytes del nombre
sendMSG<unsigned char>(conn.serverId, buffer);
```

</details>

---

### Ejercicio 1.5 — read_file: servidor desempaqueta + responde

El servidor acaba de recibir un mensaje con `read_file`. Ya extrajo el `Command`. Escribe el código que:
1. Extrae el nombre del fichero del buffer.
2. Llama a `fm.readFile(nombre, datos)`.
3. Empaqueta y envía los datos del fichero.

(El mutex ya está gestionado: asume que tienes que hacer lock/unlock alrededor de la llamada a `fm`.)

<details>
<summary>Solución</summary>

```cpp
// 1. Extraer nombre del fichero
string fileName;
fileName.resize(unpack<size_t>(buffer));
unpackv(buffer, (char*)fileName.data(), fileName.size());

// 2. Llamar a fm con mutex
vector<unsigned char> fileData;
fm_mutex.lock();
fm.readFile(fileName, fileData);
fm_mutex.unlock();

// 3. Empaquetar y enviar
vector<unsigned char> response;
pack(response, fileData.size());                       // 8 bytes: tamaño del fichero
packv(response, fileData.data(), fileData.size());     // bytes del fichero
sendMSG<unsigned char>(clientID, response);
```

</details>

---

### Ejercicio 1.6 — write_file: flujo completo

Es el comando más complejo porque lleva nombre + datos. Escribe en dos partes separadas:

**Parte A:** El proxy empaqueta y envía la petición `write_file` con `fileName` y `data` (ambos como variables ya disponibles).

**Parte B:** El servidor desempaqueta el mensaje completo (ya se extrajo el Command), llama a `fm.writeFile` y envía la confirmación.

<details>
<summary>Solución Parte A — proxy</summary>

```cpp
vector<unsigned char> buffer;
pack(buffer, write_file);
pack(buffer, fileName.size());
packv(buffer, fileName.data(), fileName.size());
pack(buffer, data.size());
packv(buffer, data.data(), data.size());
sendMSG<unsigned char>(conn.serverId, buffer);
```

</details>

<details>
<summary>Solución Parte B — servidor</summary>

```cpp
// Extraer nombre
string fileName;
fileName.resize(unpack<size_t>(buffer));
unpackv(buffer, (char*)fileName.data(), fileName.size());

// Extraer datos
vector<unsigned char> fileData;
fileData.resize(unpack<size_t>(buffer));
unpackv(buffer, fileData.data(), fileData.size());

// Escribir con mutex
fm_mutex.lock();
fm.writeFile(fileName, fileData);
fm_mutex.unlock();

// Confirmación
vector<unsigned char> response;
pack(response, true);
sendMSG<unsigned char>(clientID, response);
```

</details>

---

### Ejercicio 1.7 — Diagrama de buffer

Dibuja en ASCII el layout en memoria del buffer que el proxy envía para `write_file("hola.txt", <4 bytes de datos>)`.
Indica qué función produce cada segmento y cuántos bytes ocupa.

<details>
<summary>Solución</summary>

```
┌──────────────┬──────────────────┬──────────┬─────────────────────┬────────────┐
│ Command (4B) │ len nombre (8B)  │"hola.txt"│   len datos (8B)    │ datos (4B) │
│  write_file  │       8          │ 8 bytes  │         4           │  4 bytes   │
└──────────────┴──────────────────┴──────────┴─────────────────────┴────────────┘
  pack(cmd)      pack(name.size())  packv(...)  pack(data.size())    packv(...)
```

</details>

---

## BLOQUE 2 — Protocolo de comandos y enums

### Ejercicio 2.1 — Escribe los dos enums de memoria

Escribe el enum `Command` (para FileManager) y el enum `BrokerCommand` (para el broker), con sus valores numéricos explícitos.

<details>
<summary>Solución</summary>

```cpp
enum Command {
    list_files = 0,
    read_file  = 1,
    write_file = 2
};

enum BrokerCommand {
    register_server = 0,
    request_server  = 1,
    release_server  = 2
};
```

</details>

---

### Ejercicio 2.2 — Bug silencioso de enum

En el servidor tienes:
```cpp
enum Command { list_files=0, write_file=1, read_file=2 };
```
En el cliente tienes:
```cpp
enum Command { list_files=0, read_file=1, write_file=2 };
```

El cliente hace `upload prueba.txt` (que usa `write_file`). ¿Qué recibirá el servidor? ¿Qué síntoma verías en ejecución?

<details>
<summary>Solución</summary>

El cliente envía el entero `2` (porque `write_file=2` en su enum). El servidor recibe `2` y lo interpreta como `read_file` (porque en su enum `read_file=2`). El servidor intentará extraer un nombre de fichero del buffer, pero el buffer tiene el formato de `write_file` (nombre + datos), así que la desempaquetación será incorrecta.

El síntoma visible: el servidor puede crashear, devolver datos basura, o devolver un error al cliente, pero el comando ejecutado no es el esperado. No hay ningún mensaje de error claro — es un **bug silencioso**.

Solución: definir los enums una sola vez (en un header compartido) o verificar manualmente que coinciden byte a byte.

</details>

---

### Ejercicio 2.3 — Tabla de mensajes

Completa esta tabla de memoria:

| Operación | El cliente envía | El servidor responde |
|---|---|---|
| `list_files` | ??? | ??? |
| `read_file` | ??? | ??? |
| `write_file` | ??? | ??? |

<details>
<summary>Solución</summary>

| Operación | El cliente envía | El servidor responde |
|---|---|---|
| `list_files` | Command (4B) | nFicheros(8B) + para cada uno: len(8B) + nombre |
| `read_file` | Command(4B) + len_nombre(8B) + nombre | nBytes(8B) + bytes del fichero |
| `write_file` | Command(4B) + len_nombre(8B) + nombre + len_datos(8B) + datos | bool confirmación (1B) |

</details>

---

## BLOQUE 3 — Servidor: server.cpp

### Ejercicio 3.1 — Secuencia de arranque del servidor

Describe en orden los pasos del `main()` del servidor, desde que arranca hasta que está listo para atender clientes. Menciona las funciones de `utils` que se usan.

<details>
<summary>Solución</summary>

```
1. Lee el puerto de argv[1]
2. initClient(broker_ip, 5000)        ← se conecta al broker como cliente
3. pack(register_server) + pack(puerto)
   sendMSG(broker)                    ← le dice al broker: "existo, en este puerto"
4. recvMSG(broker) → unpack<bool>     ← espera ACK del broker
5. closeConnection(broker)            ← ya no necesita la conexión con el broker
6. initServer(puerto)                 ← ahora sí arranca como servidor
7. Bucle: checkClient() → getLastClientID() → thread(atenderCliente, id)
```

</details>

---

### Ejercicio 3.2 — Función atenderCliente

Escribe el esqueleto de la función `atenderCliente(int clientID)`. Incluye: detección de desconexión, el switch de comandos y la gestión del mutex. No tienes que escribir el código completo de cada caso, solo la estructura.

<details>
<summary>Solución</summary>

```cpp
void atenderCliente(int clientID) {
    while (true) {
        vector<unsigned char> buffer;
        recvMSG<unsigned char>(clientID, buffer);

        if (buffer.empty()) return;   // cliente desconectado

        Command cmd = unpack<Command>(buffer);

        switch (cmd) {
            case list_files: {
                fm_mutex.lock();
                vector<string> files = fm.listFiles();
                fm_mutex.unlock();
                // empaquetar y sendMSG...
                break;
            }
            case read_file: {
                // unpack nombre del buffer
                fm_mutex.lock();
                fm.readFile(nombre, datos);
                fm_mutex.unlock();
                // empaquetar y sendMSG...
                break;
            }
            case write_file: {
                // unpack nombre y datos del buffer
                fm_mutex.lock();
                fm.writeFile(nombre, datos);
                fm_mutex.unlock();
                // pack(true) y sendMSG...
                break;
            }
        }
    }
}
```

</details>

---

### Ejercicio 3.3 — Por qué detach()

¿Qué es `thread::detach()` y qué pasaría si no lo llamaras al crear el hilo de cada cliente?

<details>
<summary>Solución</summary>

`detach()` desconecta el hilo del objeto `thread` que lo creó. El hilo queda "libre" y vive de forma independiente hasta que su función termina.

Sin `detach()`, habría que llamar a `join()` para esperar a que el hilo termine. Si el bucle principal hace `join()` en el hilo del cliente 1, se bloquea hasta que ese cliente se desconecte — nadie más puede conectarse mientras tanto. La práctica exige atender múltiples clientes en paralelo, así que `detach()` es obligatorio aquí.

</details>

---

### Ejercicio 3.4 — Variables globales del servidor

¿Qué variables globales tiene `server.cpp` y por qué son globales (y no locales al `main`)?

<details>
<summary>Solución</summary>

```cpp
FileManager fm("FileManagerDir");  // instancia real del FileManager
mutex fm_mutex;                    // protege fm
```

Son globales porque la función `atenderCliente` se ejecuta en un hilo separado. Si `fm` fuera local a `main`, los hilos no podrían acceder a ella. Al ser global, todos los hilos acceden al mismo objeto, y el mutex garantiza que solo uno lo use a la vez.

</details>

---

## BLOQUE 4 — Proxy: filemanager_proxy.cpp

### Ejercicio 4.1 — Constructor del proxy

Escribe en pseudocódigo o código C++ la secuencia completa del constructor `FileManager::FileManager(string path)`.

<details>
<summary>Solución</summary>

```cpp
FileManager::FileManager(string path) {
    // 1. Conectar al broker
    connection_t brokerConn = initClient("127.0.0.1", 5000);

    // 2. Pedir un servidor
    vector<unsigned char> buf;
    pack(buf, request_server);
    sendMSG<unsigned char>(brokerConn.serverId, buf);

    // 3. Recibir IP y puerto del servidor asignado
    vector<unsigned char> resp;
    recvMSG<unsigned char>(brokerConn.serverId, resp);

    bool ok = unpack<bool>(resp);
    string serverIp;
    serverIp.resize(unpack<size_t>(resp));
    unpackv(resp, (char*)serverIp.data(), serverIp.size());
    int serverPort = unpack<int>(resp);

    // 4. Cerrar conexión con el broker
    closeConnection(brokerConn.serverId);

    // 5. Guardar y conectar al servidor asignado
    this->ip = serverIp;
    this->port = serverPort;
    this->conn = initClient(serverIp, serverPort);
}
```

</details>

---

### Ejercicio 4.2 — Destructor del proxy

Escribe la secuencia del destructor `FileManager::~FileManager()`.

<details>
<summary>Solución</summary>

```cpp
FileManager::~FileManager() {
    // 1. Conectar al broker para liberar el servidor
    connection_t brokerConn = initClient("127.0.0.1", 5000);

    // 2. Enviar release_server con la IP y puerto del servidor que usamos
    vector<unsigned char> buf;
    pack(buf, release_server);
    pack(buf, this->ip.size());
    packv(buf, this->ip.data(), this->ip.size());
    pack(buf, this->port);
    sendMSG<unsigned char>(brokerConn.serverId, buf);

    // 3. Cerrar conexión con el broker
    closeConnection(brokerConn.serverId);

    // 4. Cerrar conexión con el servidor
    closeConnection(this->conn.serverId);
}
```

</details>

---

### Ejercicio 4.3 — readFile en el proxy

Escribe el método `FileManager::readFile(string fileName, vector<unsigned char> &data)` del proxy.

<details>
<summary>Solución</summary>

```cpp
void FileManager::readFile(string fileName, vector<unsigned char> &data) {
    // Enviar petición
    vector<unsigned char> buf;
    pack(buf, read_file);
    pack(buf, fileName.size());
    packv(buf, fileName.data(), fileName.size());
    sendMSG<unsigned char>(conn.serverId, buf);

    // Recibir respuesta
    vector<unsigned char> resp;
    recvMSG<unsigned char>(conn.serverId, resp);

    data.resize(unpack<size_t>(resp));
    unpackv(resp, data.data(), data.size());
}
```

</details>

---

## BLOQUE 5 — Broker: broker.cpp

### Ejercicio 5.1 — getpeername()

El broker acaba de aceptar una conexión de un nuevo servidor. Solo tiene el `clientID`. Escribe el código que obtiene la IP real del servidor que se conectó.

<details>
<summary>Solución</summary>

```cpp
struct sockaddr_in addr;
socklen_t addrlen = sizeof(addr);
int sock = clientList[clientID].socket;
getpeername(sock, (struct sockaddr*)&addr, &addrlen);
string serverIp = inet_ntoa(addr.sin_addr);
```

</details>

---

### Ejercicio 5.2 — ¿Por qué el broker usa getpeername en vez de que el servidor mande su IP?

<details>
<summary>Solución</summary>

En un entorno como AWS, una máquina puede tener varias interfaces de red (privada, pública, Docker bridge…). El servidor no sabe con certeza cuál es su IP "hacia fuera" — la IP que el broker y los clientes pueden alcanzar.

En cambio, el socket TCP que llega al broker tiene en su cabecera la IP real del extremo remoto (la del servidor). `getpeername()` la extrae directamente del socket, que es la fuente fiable. No se puede falsificar ni confundir con otras interfaces.

</details>

---

### Ejercicio 5.3 — Algoritmo de balanceo B2

Escribe el código del broker que selecciona el servidor con menos conexiones activas de `ServerList`. Usa punteros, no índices.

<details>
<summary>Solución</summary>

```cpp
Servidores_t* minServer = &ServerList[0];
for (auto& server : ServerList) {
    if (server.active < minServer->active) {
        minServer = &server;
    }
}
ServerList_mutex.lock();
minServer->active++;
ServerList_mutex.unlock();
// usar minServer->ip y minServer->port para responder al proxy
```

</details>

---

### Ejercicio 5.4 — Punteros vs referencias: 4 preguntas

Para el código del ejercicio anterior responde:

1. ¿Por qué `minServer` es un puntero (`*`) y no una referencia (`&`)?
2. ¿Por qué el bucle usa `auto& server` con referencia?
3. ¿Qué hace `minServer = &server`?
4. ¿Por qué `minServer->active` en vez de `minServer.active`?

<details>
<summary>Solución</summary>

1. Porque necesita **redirigirse** a distintos elementos del vector según cuál tenga menor carga. Una referencia no se puede redirigir — siempre apunta al mismo objeto original.
2. Para no copiar el elemento. Con `auto server` (sin `&`), `server` sería una copia y `minServer = &server` apuntaría a esa copia, no al elemento real del vector.
3. Redirige el puntero para que apunte al servidor actual (que tiene menos carga que el anterior mínimo). Equivale a "el nuevo mínimo es este servidor".
4. Porque `minServer` es un puntero. Para acceder a los campos de un objeto a través de un puntero se usa `->`. Es equivalente a `(*minServer).active`.

</details>

---

### Ejercicio 5.5 — Estructura del broker

Escribe la estructura `Servidores_t` y las variables globales del broker.

<details>
<summary>Solución</summary>

```cpp
struct Servidores_t {
    string ip;
    int port;
    int active = 0;
};

vector<Servidores_t> ServerList;
mutex ServerList_mutex;
```

</details>

---

## BLOQUE 6 — Ciclo de vida y arquitectura

### Ejercicio 6.1 — Ciclo de vida de una conexión TCP

Completa el siguiente diagrama con las funciones correctas de `utils`:

```
LADO CLIENTE                              LADO SERVIDOR
────────────                              ─────────────
??? = ???(ip, puerto)                     ???(puerto)
                                          while(???):
                                            id = ???
                                            thread(???, id)

???(conn.serverId, buf)    ──────►        ???(id, buf)
???(conn.serverId, resp)   ◄──────        ???(id, resp)

???(conn.serverId)                        ??? devuelve buffer vacío → hilo termina
```

<details>
<summary>Solución</summary>

```
LADO CLIENTE                              LADO SERVIDOR
────────────                              ─────────────
conn = initClient(ip, puerto)             initServer(puerto)
                                          while(checkClient()):
                                            id = getLastClientID()
                                            thread(atenderCliente, id)

sendMSG(conn.serverId, buf)   ──────►    recvMSG(id, buf)
recvMSG(conn.serverId, resp)  ◄──────    sendMSG(id, resp)

closeConnection(conn.serverId)           recvMSG devuelve buffer vacío → hilo termina
```

</details>

---

### Ejercicio 6.2 — Flujo de arranque del sistema completo (con broker)

Describe los 4 pasos del arranque del sistema completo: broker, server, proxy (constructor), proxy (destructor). Para cada paso indica quién actúa como cliente y quién como servidor.

<details>
<summary>Solución</summary>

```
Paso 1: broker arranca
  → initServer(5000)
  → queda esperando conexiones

Paso 2: server arranca
  → actúa como CLIENTE del broker: initClient(broker, 5000)
  → envía register_server + puerto
  → recibe ACK
  → ahora actúa como SERVIDOR: initServer(su_puerto)

Paso 3: proxy se construye
  → actúa como CLIENTE del broker: initClient(broker, 5000)
  → envía request_server
  → recibe ip + puerto del servidor con menos carga
  → closeConnection(broker)
  → actúa como CLIENTE del servidor: initClient(ip, puerto)
  → esta conexión dura toda la sesión

Paso 4: proxy se destruye
  → actúa como CLIENTE del broker: initClient(broker, 5000)
  → envía release_server + ip + puerto (para decrementar el contador)
  → closeConnection(broker)
  → closeConnection(servidor)
```

</details>

---

### Ejercicio 6.3 — Diagrama del sistema completo

Dibuja de memoria el diagrama del sistema con broker, indicando qué mensajes van de quién a quién y en qué fase.

<details>
<summary>Solución</summary>

```
                         ┌──────────────────────┐
                         │        BROKER         │
                         │   puerto 5000         │
                         │   ServerList[]        │
                         └──────────┬────────────┘
                                    │
              ┌─────────────────────┴────────────────────────┐
              │ register_server (server → broker al arrancar) │ request_server / release_server
              │                                               │ (proxy → broker en ctor/dtor)
              │                                               │
  ┌───────────▼──────────┐                    ┌──────────────▼──────────┐
  │        SERVER         │◄───── TCP ────────►│    PROXY                │
  │  puerto: argv[1]      │  puerto dado        │  (dentro de main_fm)   │
  │  FileManager real     │  por el broker      │  misma interfaz que fm │
  │  libFileManager.a     │                     └─────────────────────────┘
  └──────────────────────┘
```

</details>

---

## BLOQUE 7 — Concurrencia

### Ejercicio 7.1 — ¿Qué es una condición de carrera?

En el contexto del servidor, explica qué es una condición de carrera, cómo puede ocurrir con el objeto `fm`, y cómo se previene.

<details>
<summary>Solución</summary>

Una **condición de carrera** ocurre cuando dos o más hilos acceden a un recurso compartido al mismo tiempo y al menos uno de ellos escribe, produciendo un resultado que depende del orden de ejecución — y ese orden no está garantizado.

En el servidor, el objeto `fm` (FileManager) es compartido entre todos los hilos de atención. Si el hilo del cliente A está en medio de `fm.readFile()` (que puede implicar leer un fichero en varias etapas) y el hilo del cliente B llama a `fm.writeFile()` sobre el mismo fichero, los datos internos de `fm` quedan en un estado inconsistente.

Se previene con un `mutex`: antes de cualquier llamada a `fm`, el hilo hace `fm_mutex.lock()`. Solo un hilo puede tener el candado a la vez — los demás esperan. Al terminar, `fm_mutex.unlock()` libera el candado.

</details>

---

### Ejercicio 7.2 — ¿Por qué hacer lock/unlock alrededor de fm y no de todo el bucle?

<details>
<summary>Solución</summary>

El lock debe proteger solo la **sección crítica**: el acceso al objeto `fm`. Si se hiciera lock al inicio del bucle y unlock al final, un hilo bloquearía a todos los demás durante el `recvMSG` (que puede tardar mucho si el cliente es lento o envía datos grandes). Esto eliminaría la ventaja de tener múltiples hilos — degeneraría en un servidor de un solo cliente a la vez.

Con lock/unlock solo alrededor de `fm.readFile/writeFile/listFiles`, los hilos pueden recibir mensajes, deserializar y enviar respuestas en paralelo. Solo se sincronizan en el instante de tocar el FileManager.

</details>

---

## BLOQUE 8 — Build system

### Ejercicio 8.1 — ¿Qué ejecutables genera CMake y qué fuentes usa cada uno?

Completa la tabla:

| Ejecutable | Archivos fuente | ¿Enlaza libFileManager.a? |
|---|---|---|
| `server` | ??? | ??? |
| `client` | ??? | ??? |
| `fileManager` | ??? | ??? |
| `broker` | ??? | ??? |

<details>
<summary>Solución</summary>

| Ejecutable | Archivos fuente | ¿Enlaza libFileManager.a? |
|---|---|---|
| `server` | utils.cpp + server.cpp | Sí |
| `client` | utils.cpp + client.cpp | No |
| `fileManager` | utils.cpp + filemanager_proxy.cpp + main_fm.cpp | No |
| `broker` | utils.cpp + broker.cpp | No |

Solo el servidor enlaza `libFileManager.a` porque es el único que tiene el objeto real. El proxy solo habla por red — no necesita la implementación.

</details>

---

### Ejercicio 8.2 — .h vs .a

Explica la diferencia entre `filemanager.h` y `libFileManager.a` y para qué sirve cada uno.

<details>
<summary>Solución</summary>

- **`filemanager.h`** (header): el **contrato**. Declara qué métodos existen (`listFiles`, `readFile`, `writeFile`) y con qué tipos. Cualquier archivo que incluya este header puede *llamar* a esos métodos sin importar cómo están implementados. El compilador lo necesita para verificar tipos en tiempo de compilación.

- **`libFileManager.a`** (static library): la **implementación compilada**. Contiene el código máquina que realmente lee y escribe en disco. Solo se enlaza cuando el ejecutable necesita **ejecutar** el objeto real (el servidor). El proxy incluye el `.h` (conoce la interfaz) pero no enlaza la `.a` (no ejecuta el objeto real, lo llama por red).

</details>

---

## BLOQUE 9 — Preguntas de comprensión global

### Ejercicio 9.1 — Transparencia del proxy

¿Qué significa que la distribución es "transparente"? ¿Qué cambio concreto se hizo en `main_fm.cpp` para que funcionara con el proxy?

<details>
<summary>Solución</summary>

"Transparente" significa que `main_fm.cpp` no sabe si está usando el FileManager real o el proxy. El código del programa de prueba no cambió ni una sola línea. Sigue haciendo `FileManager fm("ruta"); fm.listFiles();` igual que antes.

Lo que cambió fue la compilación: en vez de enlazar `libFileManager.a` (el objeto real), se enlaza `filemanager_proxy.cpp` (el proxy). Como ambos implementan exactamente la misma interfaz declarada en `filemanager.h`, el compilador acepta el cambio sin modificar el código que los usa.

</details>

---

### Ejercicio 9.2 — conn.serverId

¿Qué es `conn.serverId`? ¿Es un puerto? ¿Una IP? ¿Un socket?

<details>
<summary>Solución</summary>

`conn.serverId` es un **índice interno** de la capa `utils`. Cuando se llama a `initClient()`, `utils` añade la nueva conexión a una tabla interna y devuelve su índice. Ese índice es el `serverId`.

No es un puerto (que es un número del protocolo TCP de 0 a 65535), no es una IP, y no es directamente el descriptor de socket Unix (aunque internamente `utils` lo usa para acceder al socket real). Es simplemente el identificador que hay que pasarle a `sendMSG`/`recvMSG`/`closeConnection` para decirle a `utils` con qué conexión operar.

</details>

---

### Ejercicio 9.3 — ¿Por qué el servidor necesita FileManagerDir/ pero el cliente no?

<details>
<summary>Solución</summary>

Porque el FileManager real (en `libFileManager.a`) opera sobre el disco local: lee y escribe ficheros en la carpeta `FileManagerDir/` junto al ejecutable. Es el servidor quien tiene la instancia real (`FileManager fm("FileManagerDir")`).

El cliente (proxy) no tiene ningún objeto FileManager real. Solo serializa las llamadas y las manda por red. No toca el disco en absoluto — lo hace el servidor en su máquina. Por eso el cliente puede no tener `FileManagerDir/` ni la librería `.a`.

</details>

---

### Ejercicio 9.4 — Balanceo B2 en el broker

El broker tiene tres servidores registrados con cargas `{active: 3}, {active: 1}, {active: 4}`. Llega un `request_server`. ¿A qué servidor se asigna y cómo queda el contador después?

<details>
<summary>Solución</summary>

El broker selecciona el servidor con menos carga activa: el segundo, con `active = 1`.

Después de asignarlo, incrementa su contador: `minServer->active++` → queda `{active: 2}`.

El estado final de la lista es: `{active: 3}, {active: 2}, {active: 4}`.

</details>

---

## BLOQUE 10 — Errores típicos y diagnóstico

### Ejercicio 10.1 — Identifica el error

```cpp
// Servidor recibe list_files:
vector<unsigned char> response;
pack(response, files.size());          // A
for (const string& f : files) {
    packv(response, f.data(), f.size()); // B — sin pack de longitud
}
sendMSG<unsigned char>(clientID, response);
```

¿Qué error hay? ¿Qué síntoma produce en el cliente?

<details>
<summary>Solución</summary>

Falta el `pack(response, f.size())` antes de cada `packv`. El servidor envía el número total de ficheros y luego los bytes de cada nombre concatenados, sin indicar dónde termina uno y empieza el otro.

El cliente intentará `unpack<size_t>` para obtener la longitud del primer nombre — leerá los primeros 8 bytes del nombre como si fueran un número (que será un valor enorme y sin sentido), luego intentará `unpackv` de esa cantidad de bytes y probablemente leerá basura o crasheará.

</details>

---

### Ejercicio 10.2 — Identifica el error de orden

```cpp
// Proxy: listFiles()
vector<unsigned char> buf;
pack(buf, fileName.size());    // primero la longitud
pack(buf, list_files);         // luego el comando
packv(buf, fileName.data(), fileName.size());
sendMSG<unsigned char>(conn.serverId, buf);
```

¿Qué error hay?

<details>
<summary>Solución</summary>

`list_files` no lleva argumentos (no hay `fileName`). El código es del todo incorrecto en estructura: `list_files` solo necesita `pack(buf, list_files)` y nada más.

Además, aunque fuera `read_file`, el orden está mal: primero va el `Command` y luego la longitud + datos. El servidor hace `unpack<Command>` como primer paso — si en el buffer el primer elemento es `fileName.size()` (un `size_t`), lo interpretará como un número de comando incorrecto.

</details>

---

### Ejercicio 10.3 — Diagnóstico: el cliente se conecta pero no recibe datos

Describe al menos 3 causas posibles y cómo verificarías cada una.

<details>
<summary>Solución</summary>

1. **El servidor no llama a `sendMSG` después de procesar el comando.** Verificación: añadir prints antes y después del `sendMSG` en el servidor para confirmar que llega hasta ahí.

2. **El orden de pack/unpack está invertido.** El servidor desempaqueta en un orden distinto al que empaquetó el cliente. El `unpack` puede colgar esperando más bytes o extraer datos incorrectos. Verificación: comparar línea a línea el pack del cliente con el unpack del servidor.

3. **El comando enviado no coincide con ningún `case` del switch.** Si el enum está desincronizado, el servidor recibe un entero que no es 0, 1 ni 2 y ningún case se activa — no envía respuesta. El cliente se queda bloqueado en `recvMSG`. Verificación: imprimir el valor entero de `cmd` recibido en el servidor.

4. **El servidor crashea antes de responder.** El buffer tiene un tamaño incorrecto (p. ej., intentó `resize` con un número enorme). Verificación: ejecutar el servidor con `valgrind` o simplemente ver si el proceso sigue vivo.

</details>

