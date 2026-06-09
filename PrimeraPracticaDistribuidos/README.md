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
- [ ] Fase 5 — Broker (OBLIGATORIO para el examen, aunque sea opcional en la entrega)
  - [x] Broker: estructura base compilando (BrokerCommand, Servidores_t, ServerList, bucle de aceptación, switch vacío)
  - [ ] Broker: implementar register_server (añadir a ServerList + ACK)
  - [ ] Broker: implementar request_server (devolver IP:puerto con menos carga)
  - [ ] Broker: implementar release_server (decrementar contador)
  - [ ] Server: registrarse en el broker al arrancar (actúa también de cliente)
  - [ ] Proxy: pedir servidor al broker antes de conectarse + release en destructor
- [ ] Fase 6 — Entrega

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
- **Entrega:** ZIP con los archivos generados/editados, cada uno con el nombre del alumno
  en un comentario al inicio. Nombre obligatorio del ZIP:
  `PR1SISDIS_Alumno1_Apellido1.zip`. La carpeta `build/` no se entrega.

## Recordatorio para Git

```bash
# .gitignore recomendado
build/
*.o
```
