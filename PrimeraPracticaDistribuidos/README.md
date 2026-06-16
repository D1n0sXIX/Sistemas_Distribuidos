# Práctica 1 — Objetos remotos (Programación de Sistemas Distribuidos)

Distribuir la clase `FileManager` mediante sockets TCP, de forma **transparente** para el
programa `main_fm.cpp` suministrado. Se implementa un **servidor de objetos** (tiene el
`FileManager` real) y un **cliente** (hace las llamadas por red). Como extra opcional, un
**broker** de descubrimiento y balanceo.

---

## Estado del proyecto

- [x] Fase 0 — Zona de trabajo lista
- [x] Fase 1 — Compilación base (`fileManager`) funciona
- [x] Fase 2 — Capa de red (`utils`) entendida
- [x] Fase 3 — Servidor + cliente en localhost
  - [x] Servidor: arranque, bucle de clientes, hilo por cliente
  - [x] Servidor: `list_files` (empaquetar lista de nombres)
  - [x] Servidor: `read_file` (leer fichero y devolver bytes)
  - [x] Servidor: `write_file` (recibir nombre + bytes y escribir)
  - [x] Cliente: conexión + envío de comandos + recepción
  - [x] Prueba de punta a punta en localhost
- [x] Fase 4 — Cliente transparente (`main_fm.cpp` reutilizable)
- [x] Fase 5 — Broker (probado de punta a punta)
  - [x] Broker: `register_server` — añade a ServerList + ACK
  - [x] Broker: `request_server` — devuelve IP:puerto con menos carga (balanceo B2)
  - [x] Broker: `release_server` — decrementa contador activos
  - [x] Server: se registra en el broker al arrancar (actúa como cliente)
  - [x] Proxy: pide servidor al broker en el constructor + release en destructor

---

## Estructura de la carpeta

```
practica1/
├── CMakeLists.txt          # Receta de compilación
├── filemanager.h           # SUMINISTRADO  · declaración de la clase (no tocar)
├── libFileManager.a        # SUMINISTRADO  · implementación real compilada (no tocar)
├── main_fm.cpp             # SUMINISTRADO  · programa de prueba original (no tocar)
├── utils.h                 # SUMINISTRADO  · capa de red: sockets, pack/unpack, send/recv (no tocar)
├── utils.cpp               # SUMINISTRADO  · implementación de la capa de red (no tocar)
├── server.cpp              # PROPIO        · servidor de objetos FileManager
├── client.cpp              # PROPIO        · cliente que llama por red
├── FileManagerDir/         # carpeta de ficheros DEL SERVIDOR (ficheros de prueba)
└── build/                  # se genera al compilar · NO se entrega
```

## Qué es cada archivo

| Archivo | Rol | ¿Se modifica? |
|---|---|---|
| `filemanager.h` | Declara `FileManager`: `listFiles()`, `readFile()`, `writeFile()` | No |
| `libFileManager.a` | Librería estática con la implementación real (toca el disco) | No |
| `main_fm.cpp` | Terminal de prueba: comandos `ls`, `lls`, `upload`, `download`, `exit()` | No |
| `utils.h` / `utils.cpp` | Sockets TCP + serialización (`pack`/`unpack`) + `sendMSG`/`recvMSG` | No (salvo broker) |
| `server.cpp` | Recibe peticiones, ejecuta el `FileManager` real, responde | Sí (lo escribo yo) |
| `client.cpp` | Envía peticiones al servidor y procesa la respuesta | Sí (lo escribo yo) |

> El servidor **enlaza** `libFileManager.a` (tiene el objeto real). El cliente **no** la
> enlaza: solo habla por red.

---

## Cómo compilar y probar

Requisitos (Ubuntu/Debian):

```bash
sudo apt update
sudo apt install build-essential cmake
```

Preparación (una sola vez):

```bash
# El archivo debe llamarse EXACTAMENTE CMakeLists.txt
mkdir -p FileManagerDir
echo "hola mundo" > FileManagerDir/prueba.txt
```

Compilar y ejecutar:

```bash
mkdir build && cd build
cmake ..                 # lee CMakeLists.txt y prepara la compilación
make                     # compila todo
# o por objetivos:  make fileManager / make server / make client
```

Prueba de la base (programa original, sin red):

```bash
./fileManager
# escribe: lls   -> debe listar prueba.txt
# escribe: exit()
```

Prueba distribuida (dos terminales, localhost):

```bash
# Terminal 1
./server
# Terminal 2
./client            # se conecta a 127.0.0.1
```

---

## Mapa rápido de la capa de red (`utils`)

Funciones que se usan desde `server.cpp` / `client.cpp` (no hace falta entender el resto):

- Servidor: `initServer(puerto)` arranca y acepta clientes en un hilo. En el bucle:
  `checkClient()` para ver si hay alguien esperando, `getLastClientID()` para su ID.
- Cliente: `initClient("127.0.0.1", puerto)` devuelve un `connection_t`; el identificador
  para enviar/recibir es su campo `.serverId`.
- Mensajes: `sendMSG(id, buffer)` / `recvMSG(id, buffer)`.
- Serializar: `pack`/`packv` para meter datos; `unpack`/`unpackv` para sacarlos,
  **en el mismo orden** en que se metieron.

Patrón de un `string` por red: primero su longitud, luego sus caracteres.

## Mapeo de comandos a mensajes

| Comando (cliente) | Método de FileManager | ¿Pasa por la red? |
|---|---|---|
| `ls` | — (lista la carpeta local del cliente) | No |
| `lls` | `listFiles()` | Sí |
| `download <f>` | `readFile()` | Sí |
| `upload <f>` | `writeFile()` | Sí |
| `exit()` | — (cierra) | No |

---

## Notas

- **Local vs AWS:** P1 se hace en local (una VM con Linux vale; servidor y cliente por
  `127.0.0.1`). AWS solo es necesario para el broker multi-máquina y para la Práctica 2
  (Kubernetes).

## Recordatorio para Git

```bash
# .gitignore recomendado
build/
*.o
```

---

## Guía de estudio — Patrones clave para el examen

### Diagrama completo del sistema (con broker)

```
                        ┌─────────────────┐
                        │     BROKER      │  puerto 5000 (fijo)
                        │  ServerList[]   │  lista de servers registrados
                        │  activos: N     │  contador de conexiones activas
                        └────────┬────────┘
                                 │
               ┌─────────────────┴──────────────────┐
               │  register_server                    │  request_server / release_server
               │  (server → broker al arrancar)      │  (proxy → broker en ctor/dtor)
               │                                     │
    ┌──────────▼──────────┐               ┌──────────▼──────────┐
    │       SERVER        │               │        PROXY         │
    │  puerto argv[1]     │  ←── TCP ───► │  filemanager_proxy   │
    │  FileManager real   │  puerto dado  │  (dentro de main_fm) │
    │  libFileManager.a   │  por broker   └─────────────────────┘
    └─────────────────────┘

Flujo de arranque:
  1. broker arranca → escucha en 5000
  2. server arranca → initClient(broker) → register_server + puerto → recibe ACK → initServer(puerto)
  3. proxy (constructor) → initClient(broker) → request_server → recibe IP:puerto → initClient(server)
  4. proxy (destructor) → initClient(broker) → release_server + IP:puerto → closeConnection(broker)
                        → closeConnection(server)
```

---

### Patrón pack/unpack (el más repetido)

TCP es un flujo de bytes sin separadores. Para mandar un string o un bloque de bytes hay que decir primero cuántos vienen — eso es el "framing".

**Empaquetar y enviar:**
```
vector<unsigned char> buf;

pack(buf, comando);           // tipo enum/int/bool/size_t  → mete sizeof(T) bytes
pack(buf, nombre.size());     // longitud del string (size_t = 8 bytes en 64 bits)
packv(buf, nombre.data(), nombre.size());  // los bytes del string en sí

sendMSG<unsigned char>(id, buf);   // envía todo el buffer de golpe
```

**Recibir y desempaquetar:**
```
vector<unsigned char> resp;
recvMSG<unsigned char>(id, resp);  // recibe el mensaje completo

T valor = unpack<T>(resp);         // consume sizeof(T) bytes del frente
string nombre;
nombre.resize(unpack<size_t>(resp));         // primero la longitud
unpackv(resp, (char*)nombre.data(), nombre.size());  // luego los datos
```

> `unpack` va consumiendo el buffer de izquierda a derecha en el mismo orden en que se empaquetó. No hay índice manual — cada llamada avanza el cursor interno.

**Representación del buffer para `read_file`:**
```
┌─────────────┬──────────────────┬──────────────────────────────┐
│ Command (4B)│  len nombre (8B) │  nombre (len bytes)          │
└─────────────┴──────────────────┴──────────────────────────────┘
  pack(cmd)     pack(name.size())   packv(name.data(), name.size())
```

---

### Por qué `pack` la longitud antes que el dato

Sin longitud, el receptor no sabe cuántos bytes leer:
```
"hola" "mundo"   →   686f6c61 6d756e64 6f   (flujo continuo, sin separación)
```
Con longitud previa:
```
[4]["hola"][5]["mundo"]  →  el receptor lee 4, extrae "hola", lee 5, extrae "mundo"
```

---

### `getpeername()` — obtener la IP del cliente conectado

Cuando el broker acepta una conexión de un server, solo tiene el `clientID`. Para saber la IP real del server que se conectó, usa `getpeername()`:

```cpp
struct sockaddr_in addr;        // estructura que almacenará la dirección
socklen_t addrlen = sizeof(addr);
int sock = clientList[clientID].socket;   // socket del cliente (dado por utils)

getpeername(sock, (struct sockaddr*)&addr, &addrlen);
// addr.sin_addr ahora tiene la IP del extremo remoto
string ip = inet_ntoa(addr.sin_addr);   // convierte la IP a string "x.x.x.x"
```

> **Por qué no manda el server su propia IP:** el server no sabe qué IP tiene "hacia fuera" (especialmente en AWS). El broker la obtiene del socket, que es la fuente fiable.

---

### Puntero `*` vs referencia `&` — búsqueda del servidor con menos carga

```cpp
Servidores_t* minServer = &ServerList[0];  // (1) puntero al primer elemento
for (auto& server : ServerList) {          // (2) referencia: no copia, permite modificar
    if (server.active < minServer->active) {  // (3) -> porque minServer es puntero
        minServer = &server;               // (4) redirigimos el puntero al nuevo mínimo
    }
}
ServerList_mutex.lock();
minServer->active++;                       // (5) modifica el elemento real del vector
ServerList_mutex.unlock();
```

| Símbolo | Qué hace | Cuándo |
|---|---|---|
| `T*` | Puntero: guarda una dirección de memoria | Cuando quieres redirigir a distintos objetos |
| `T&` | Referencia: alias del objeto original | Cuando solo quieres acceder/modificar sin copiar |
| `&var` | Operador "dirección de": obtiene la dirección de `var` | Al inicializar un puntero |
| `ptr->campo` | Accede al campo a través de un puntero | Equivale a `(*ptr).campo` |

**Por qué no usar un índice `int`:** el puntero permite modificar directamente `minServer->active++` sin hacer `ServerList[idx].active++` — ambos son válidos pero el puntero es más directo y es el patrón habitual en C++.

---

### Ciclo de vida de una conexión

```
Lado cliente:                          Lado servidor:
connection_t conn = initClient(ip, p)  initServer(port)
                                       while(checkClient()) → getLastClientID()
                                       thread(atenderCliente, id)

sendMSG(conn.serverId, buf)    →       recvMSG(clientID, buf)
recvMSG(conn.serverId, resp)   ←       sendMSG(clientID, resp)

closeConnection(conn.serverId)         // buffer vacío → cliente desconectado
```

> `conn.serverId` es el ID interno que asigna `utils` a cada conexión. No es un puerto ni una IP — es simplemente el índice para decirle a `utils` con quién hablar.

---

### Enum como protocolo compartido

El `enum Command` debe declararse **en el mismo orden** en server y en client/proxy. El valor que viaja por la red es el entero subyacente (0, 1, 2…), no el nombre.

```cpp
enum Command { list_files=0, read_file=1, write_file=2 };
```

Si un lado tiene un enum distinto, `unpack<Command>` devolverá el entero correcto pero lo interpretará como otro caso del switch → bug silencioso difícil de detectar.
