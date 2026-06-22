# Guía de estudio — Práctica 2: Docker + Kubernetes en AWS
### Programación de Sistemas Distribuidos · Alumno: D1n0sXIX

> Documento creado para Google NotebookLM. Contiene toda la teoría, arquitectura, decisiones de diseño y comandos de la práctica, con analogías del mundo real para facilitar la comprensión y el estudio.

---

## 1. El problema que resuelve esta práctica

Imagina que tienes tres empleados en una empresa:
- **El recepcionista** (brokerFileManager): sabe dónde está cada trabajador y te da su dirección.
- **Los trabajadores** (serverFileManager): hacen el trabajo real (guardar y servir ficheros).
- **El cliente** (clientFileManager): llega a la empresa, pregunta al recepcionista por un trabajador, y luego va directamente a hablar con él.

El reto de la práctica es **desplegar esta empresa en la nube** (AWS), de forma que:
1. Todo funcione dentro de contenedores Docker.
2. Kubernetes gestione automáticamente cuántos trabajadores hay y dónde están.
3. Si un trabajador "cae", Kubernetes lo levanta solo.
4. Los ficheros que guardan los trabajadores no se pierden aunque el contenedor se reinicie.

---

## 2. Docker — El contenedor de transporte

### ¿Qué es Docker?

Piensa en Docker como los **contenedores de transporte marítimo** (los rectangulares de los barcos). Antes de que existieran, mover mercancías entre barcos, camiones y trenes era un caos porque cada vehículo tenía dimensiones distintas. El contenedor estandarizó todo: da igual el barco, el camión o el país — el contenedor siempre encaja.

Docker hace lo mismo con el software:
- **Sin Docker**: "en mi máquina funciona, pero en el servidor no" — porque las versiones de librerías, el sistema operativo y las configuraciones son distintas.
- **Con Docker**: el programa viaja dentro de un contenedor con TODO lo que necesita (librerías, sistema de archivos, configuración). Funciona igual en cualquier máquina que tenga Docker instalado.

### Imagen vs Contenedor

- **Imagen Docker** = la plantilla, el molde. Como el plano de un apartamento. No ocupa espacio en uso, solo en disco.
- **Contenedor** = la instancia en ejecución de esa imagen. Como el apartamento construido y habitado. Puedes tener 10 contenedores del mismo apartamento corriendo a la vez.

### Los Dockerfiles de la práctica

Se crearon dos imágenes:

**Imagen del broker** (`d1n0s/p2-broker:latest`):
- Base: Ubuntu 20.04 (necesario por las dependencias de glibc del binario).
- Copia el ejecutable `brokerFileManager`.
- Expone el puerto 32002.
- Al arrancar, ejecuta `./brokerFileManager` y se queda esperando conexiones.

**Imagen del server** (`d1n0s/p2-server:latest`):
- Base: Ubuntu 20.04.
- Instala `curl` (para descubrir la IP pública, aunque en K8s se usa Downward API).
- Copia el ejecutable `serverFileManager`.
- Crea la carpeta `FileManagerDir` (donde se guardan los ficheros).
- Expone el puerto 32001.

> **Por qué Ubuntu 20.04 y no Alpine Linux**: los binarios del profesor están compilados dinámicamente y requieren `glibc >= 2.14` y `GLIBCXX_3.4.22`. Alpine usa `musl libc`, que es incompatible. Ubuntu 20.04 tiene la versión correcta de glibc.

### Distribución de imágenes (Docker Hub)

Para que Kubernetes pueda descargar las imágenes en cualquier nodo del clúster, se suben a **Docker Hub** (como el App Store de las imágenes Docker). Cada nodo hace `docker pull` automáticamente cuando el pod se crea.

```
Arch Linux (build) → docker push → Docker Hub → Kubernetes pull en cada nodo
```

---

## 3. Kubernetes — El director de orquesta

### ¿Qué es Kubernetes?

Si Docker es el contenedor de transporte, **Kubernetes es el puerto marítimo completo**: gestiona dónde va cada contenedor, cuántos hay, los reemplaza si se dañan, y equilibra la carga de trabajo.

Sin Kubernetes tendrías que:
- Arrancar cada contenedor a mano en cada máquina.
- Vigilar si alguno cae y levantarlo manualmente.
- Repartir el tráfico a mano entre contenedores.

Con Kubernetes declaras el **estado deseado** ("quiero 2 servidores siempre corriendo") y él se encarga de que sea así, pase lo que pase.

### Clúster de Kubernetes

Un clúster es un conjunto de máquinas (nodos) que Kubernetes gestiona como si fueran una sola:

```
CLÚSTER
├── Nodo control-plane (el cerebro)
│   ├── API Server: recibe los comandos de kubectl
│   ├── Scheduler: decide en qué nodo va cada pod
│   └── etcd: base de datos del estado del clúster
└── Nodos worker (los músculos)
    ├── kubelet: agente que ejecuta los pods en ese nodo
    ├── kube-proxy: gestiona las reglas de red (NodePort, etc.)
    └── containerd: runtime que ejecuta los contenedores
```

En la práctica:
- `p2-control-plane` (`172.31.85.97`): el cerebro. Nunca ejecuta pods de la app.
- `p2-worker` (`172.31.81.126`): nodo esclavo 1. Aquí corre el broker y un server.
- `p2-worker-2` (`172.31.85.248`): nodo esclavo 2. Aquí corre el segundo server (Avanzada 2).

### Los objetos principales de Kubernetes

#### Pod
La unidad mínima de Kubernetes. Un pod contiene uno o más contenedores que comparten red y almacenamiento. Es efímero — si muere, Kubernetes crea uno nuevo (con IP diferente).

Analogía: un pod es como una **habitación de hotel**. Tiene su propia dirección interna, pero si el hotel la reasigna (el pod muere y se recrea), la habitación puede cambiar de número.

#### Deployment
Le dice a Kubernetes: "quiero que siempre haya N pods de este tipo corriendo". Si un pod cae, el Deployment lo recrea automáticamente.

Analogía: el Deployment es el **contrato con el hotel** que garantiza que siempre tendrás una habitación disponible, aunque tengan que cambiarte de habitación si la tuya tiene problemas.

Campos clave:
- `replicas`: cuántas copias del pod deben estar corriendo.
- `strategy: Recreate`: mata todos los pods antes de crear los nuevos. Útil cuando los pods no pueden coexistir (por conflicto de puertos o recursos).
- `selector.matchLabels`: cómo encuentra Kubernetes los pods que gestiona.
- `template`: la plantilla del pod (imagen, variables de entorno, volúmenes...).

#### Service
Los pods tienen IPs internas que cambian cada vez que se recrean. Un **Service** proporciona una dirección estable para acceder a un grupo de pods.

Analogía: el Service es como la **recepción de un hotel**. Da igual en qué habitación esté el huésped — siempre llamas a recepción (misma IP/puerto) y te conectan con él.

Tipos de Service usados:
- **ClusterIP** (por defecto): solo accesible dentro del clúster. Los pods lo usan para hablar entre sí.
- **NodePort**: abre un puerto en TODOS los nodos del clúster. Accesible desde fuera. El cliente usa este tipo.

#### Las tres IPs de un Service (muy importante para el examen)

Para el broker, hay tres IPs distintas con propósitos distintos:

| Tipo | Ejemplo | Quién la usa |
|---|---|---|
| IP del Pod | `10.244.1.3` | Solo dentro del clúster, cambia al recrear el pod |
| ClusterIP del Service | `10.111.182.40` | Los pods internos (el server la usa para conectar al broker) |
| IP del Nodo + NodePort | `172.31.81.126:32002` | El cliente externo |

> El cliente usa siempre la IP del **nodo físico** más el **NodePort**. Nunca la IP del pod ni la ClusterIP.

---

## 4. Los tres programas y cómo se hablan

### El flujo de comunicación

```
PASO 1 — El broker arranca y espera en :32002

PASO 2 — El server arranca y se registra en el broker:
  serverFileManager <IP_BROKER> <PUERTO_BROKER> <MI_IP_PUBLICA> <MI_PUERTO>
  "Hola broker, soy un server. Conéctense a mí en 172.31.81.126:32001"

PASO 3 — El cliente arranca y pregunta al broker:
  clientFileManager <IP_BROKER> <PUERTO_BROKER>
  "Broker, dame la dirección de un server"
  Broker responde: "Ve a 172.31.81.126:32001"

PASO 4 — El cliente se conecta DIRECTAMENTE al server:
  Comandos disponibles: ls, lls, upload, download, exit()
```

### El reto central: la dirección que el server registra debe ser alcanzable por el cliente

Este es el problema de diseño más importante de toda la práctica.

Cuando el server arranca, le dice al broker su IP y puerto. El cliente luego intentará conectarse **exactamente a esa dirección**. Si el server registra una IP interna del clúster (`10.244.x.x`), el cliente externo no podrá conectarse.

Solución usada: el server registra la **IP privada del nodo worker** (`172.31.81.126`) y el **NodePort** (`32001`). El NodePort service redirige ese tráfico al pod correcto.

### Los comandos del cliente

| Comando | Qué hace |
|---|---|
| `ls` | Lista ficheros **locales** al cliente |
| `lls` | Lista ficheros del servidor (carpeta `FileManagerDir` junto al binario) |
| `upload <fichero>` | Copia un fichero del cliente al servidor |
| `download <fichero>` | Copia un fichero del servidor al cliente |
| `exit()` | Cierra el cliente |

---

## 5. La infraestructura AWS

### Instancias EC2

EC2 (Elastic Compute Cloud) son máquinas virtuales en la nube de Amazon. En la práctica se usan 5:

| Nombre | Tipo | IP privada | Rol |
|---|---|---|---|
| `p2-control-plane` | t2.medium | `172.31.85.97` | Cerebro del clúster K8s |
| `p2-worker` | t2.medium | `172.31.81.126` | Nodo esclavo 1 (broker + server) |
| `p2-worker-2` | t2.medium | `172.31.85.248` | Nodo esclavo 2 (server, Avanzada 2) |
| `p2-nfs-server` | t2.micro | `172.31.85.178` | Servidor NFS (Avanzada 2) |
| `p2-cliente` | t2.micro | `172.31.94.41` | Ejecuta clientFileManager |

> **IPs privadas vs públicas**: las IPs públicas cambian cada vez que se para y arranca una instancia. Las IPs privadas (`172.31.x.x`) son estables mientras la instancia existe. Siempre usar IPs privadas para comunicación dentro del VPC.

### VPC y Security Groups

El VPC (Virtual Private Cloud) es una red privada virtual en AWS. Todas las instancias de la práctica están en el rango `172.31.0.0/16`.

Los Security Groups son como el **portero de un edificio**: controlan qué tráfico entra y sale de cada instancia. Se configuró un Security Group que permite todo el tráfico entre las instancias del clúster (self-referencing rule) y expone los puertos 32001 y 32002 para el cliente.

> **Hairpinning en AWS**: dentro del VPC, las instancias no pueden conectarse entre sí usando sus IPs públicas. Siempre hay que usar las IPs privadas para comunicación interna.

### Flannel CNI

CNI (Container Network Interface) es el plugin de red de Kubernetes que asigna IPs a los pods. En la práctica se usa **Flannel**, que crea una red virtual `10.244.0.0/16` sobre la red del VPC. Cada pod recibe una IP de este rango.

---

## 6. Configuración básica — Un worker, un server

### Arquitectura

```
EC2 cliente (172.31.94.41)
      │
      │ ./clientFileManager 172.31.81.126 32002
      ▼
EC2 worker-1 (172.31.81.126)
      │
      ├── NodePort :32002 → Pod broker (10.244.1.3)
      │       broker dice: "server en 172.31.81.126:32001"
      │
      └── NodePort :32001 → Pod server (10.244.1.4)
              lee/escribe en FileManagerDir
```

### Manifiestos Kubernetes usados

- `broker-deployment.yaml`: 1 réplica del broker.
- `broker-service.yaml`: NodePort en puerto 32002.
- `server-deployment.yaml`: 1 réplica del server.
- `server-service.yaml`: NodePort en puerto 32001.

### El problema de la IP en el server

El server necesita registrarse en el broker con una dirección alcanzable por el cliente. En Kubernetes, sin `hostNetwork`, el pod tiene una IP interna (`10.244.x.x`). La solución: usar la **Downward API** de Kubernetes para inyectar la IP del nodo como variable de entorno:

```yaml
env:
  - name: MY_NODE_IP
    valueFrom:
      fieldRef:
        fieldPath: status.hostIP   # IP del nodo donde corre el pod
command: ["/bin/sh", "-c"]
args: ["/serverFileManager 10.111.182.40 32002 $MY_NODE_IP 32001"]
```

El shell expande `$MY_NODE_IP` antes de lanzar el binario. Así cada pod registra la IP del nodo donde está corriendo.

---

## 7. Configuración avanzada 1 — hostPath (réplicas en un nodo)

### El problema a resolver

Si hay 2 pods del server en el mismo nodo, cada uno tiene su propia `FileManagerDir` dentro del contenedor. Si subes un fichero y el NodePort te manda al pod A, pero luego haces `lls` y te manda al pod B, no verás el fichero. Cada pod ve su propia carpeta.

### La solución: hostPath

`hostPath` monta una carpeta del **nodo físico** dentro del contenedor. Todos los pods del mismo nodo que monten el mismo `hostPath` comparten esa carpeta.

Analogía: es como si varios inquilinos de un edificio tuvieran llave del mismo trastero en el sótano. Da igual a qué inquilino llames — todos acceden al mismo trastero.

```yaml
volumes:
  - name: filemanagerdir
    hostPath:
      path: /data/filemanager    # carpeta en el nodo físico
      type: DirectoryOrCreate    # la crea si no existe
```

### Qué se consigue

- 2 pods del server en `worker-1` comparten `/data/filemanager` del nodo.
- El NodePort balancea peticiones entre los dos pods.
- `upload` va a un pod → el fichero queda en `/data/filemanager` del nodo → `lls` desde cualquier pod devuelve el mismo fichero.
- Si un pod se reinicia, los ficheros siguen ahí (están en el nodo, no en el contenedor).

### Limitación

Solo funciona si todos los pods están en el **mismo nodo**. Si hubiera pods en nodos distintos, cada nodo tendría su propia carpeta `/data/filemanager` y los ficheros no se compartirían.

---

## 8. Configuración avanzada 2 — NFS (réplicas en varios nodos)

### El problema a resolver

Con pods en nodos distintos (`worker-1` y `worker-2`), `hostPath` no sirve porque cada nodo tiene su propio disco. Necesitamos un almacenamiento **compartido en red** que todos los nodos puedan montar.

### La solución: NFS

NFS (Network File System) es un protocolo para compartir carpetas por red. Una máquina exporta una carpeta y otras la montan como si fuera un disco local.

Analogía: NFS es como un **disco duro NAS en casa** (esos aparatos de almacenamiento conectados al router). Todos los ordenadores de la casa pueden acceder a los mismos ficheros aunque estén en habitaciones distintas.

En la práctica:
- `p2-nfs-server` (`172.31.85.178`) exporta `/data/filemanager` a toda la VPC (`172.31.0.0/16`).
- Ambos workers montan esa carpeta en los pods del server.

### PersistentVolume (PV) y PersistentVolumeClaim (PVC)

Kubernetes abstrae el almacenamiento en dos objetos:

**PV (PersistentVolume)** = la plaza de parking del garaje.
- Existe independientemente de quién la use.
- El administrador la crea: "hay una plaza NFS en esta dirección".

**PVC (PersistentVolumeClaim)** = el ticket/abono de parking.
- Lo solicita el desarrollador: "necesito una plaza con estas características".
- Kubernetes vincula automáticamente el PVC con un PV que encaje.

**El Deployment** = el coche que aparca.
- No sabe dónde está el garaje físicamente.
- Solo dice "aparco usando el abono `nfs-pvc`".

```
nfs-pv (la plaza)  ←vinculado→  nfs-pvc (el abono)  ←montado en→  Pod server
      ↕
NFS server 172.31.85.178:/data/filemanager
```

### AccessModes

| Modo | Significado | Uso |
|---|---|---|
| `ReadWriteOnce` | Un solo nodo puede leer y escribir | Disco local, EBS de AWS |
| `ReadOnlyMany` | Muchos nodos pueden leer, ninguno escribir | Datos estáticos |
| `ReadWriteMany` | Muchos nodos pueden leer Y escribir | NFS, sistemas de ficheros en red |

En la práctica se usa `ReadWriteMany` porque varios pods en varios nodos necesitan escribir ficheros.

### El reto de la IP con múltiples nodos

Con pods en nodos distintos, cada pod tiene que registrar en el broker la IP de **su propio nodo**, no la del otro. La Downward API resuelve esto:

- Pod en `worker-1` → `status.hostIP` = `172.31.81.126` → registra `172.31.81.126:32001`
- Pod en `worker-2` → `status.hostIP` = `172.31.85.248` → registra `172.31.85.248:32001`

El cliente recibe del broker las dos direcciones y puede conectarse a cualquiera. El NodePort de cada nodo redirige al pod local.

---

## 9. Conceptos clave para el examen

### ¿Por qué el cliente no usa la IP del pod directamente?

Los pods tienen IPs internas (`10.244.x.x`) asignadas por Flannel, solo accesibles dentro del clúster. El cliente está en otra máquina (fuera del clúster) y no puede rutear a esas IPs. Por eso se usan NodePort Services que abren puertos en las IPs de los nodos (que sí son accesibles dentro del VPC).

### ¿Qué hace el broker exactamente?

El broker es solo un **directorio de conexión**. No redirige tráfico entre cliente y server — solo almacena la lista de servers disponibles (IP:Puerto) y se la da al cliente cuando este la pide. Luego el cliente se conecta **directamente** al server, sin pasar por el broker.

### ¿Por qué strategy: Recreate?

Rolling Update (la estrategia por defecto) crea pods nuevos antes de eliminar los viejos. Si dos pods del server intentan usar el mismo puerto en el mismo nodo, hay conflicto. `Recreate` elimina todos primero y luego crea los nuevos, evitando el conflicto.

### ¿Qué es la Downward API?

Un mecanismo de Kubernetes que permite que un pod conozca información sobre sí mismo o sobre el nodo donde está corriendo, sin necesidad de llamadas externas. Se inyecta como variable de entorno o fichero. En la práctica se usa `status.hostIP` para que cada pod conozca la IP del nodo donde está.

### ¿Por qué nfs-common en los workers?

El kernel de Linux necesita el paquete `nfs-common` para poder montar volúmenes NFS. Sin él, cuando Kubernetes intenta montar el PVC en el pod, el nodo no sabe cómo hablar el protocolo NFS y el pod se queda en `ContainerCreating`.

### Diferencia entre hostPath y NFS

| | hostPath | NFS |
|---|---|---|
| Dónde viven los datos | Disco local del nodo | Servidor NFS externo |
| Compartido entre pods | Solo en el mismo nodo | Entre todos los nodos |
| Persiste si el pod cae | Sí (disco del nodo) | Sí (servidor NFS) |
| Persiste si el nodo cae | No | Sí |
| Configuración K8s | `hostPath` en volumes | PV + PVC |

---

## 10. Flujo completo de una sesión de trabajo

```
1. kubectl apply -f k8s/
   → K8s crea los pods en los workers
   → Kubernetes descarga las imágenes de Docker Hub en cada nodo
   → Los pods arrancan y los servers se registran en el broker

2. ssh ubuntu@p2-cliente
   ./clientFileManager 172.31.81.126 32002

3. El cliente conecta a 172.31.81.126:32002
   → NodePort del broker-service recibe la conexión
   → La redirige al pod broker (10.244.1.3:32002)
   → El broker devuelve: "server en 172.31.81.126:32001 y 172.31.85.248:32001"

4. El cliente conecta a una de esas direcciones (ej: 172.31.81.126:32001)
   → NodePort del server-service recibe la conexión
   → La redirige a uno de los pods server

5. El usuario ejecuta comandos:
   lls      → lista /FileManagerDir del pod server (montado desde NFS)
   upload X → copia X al NFS a través del pod server
   lls      → ahora aparece X (visible desde todos los pods de todos los nodos)
   download X → descarga X del NFS al cliente
   exit()   → cierra la conexión
```

---

## 11. Preguntas frecuentes de examen

**P: ¿Qué IP le pasas al clientFileManager y de dónde viene?**
R: La IP privada del nodo worker donde está el pod del broker (`172.31.81.126`) más el puerto NodePort (`32002`). No es la IP del pod. Se obtiene con `kubectl get nodes -o wide` (nodo) y `kubectl get svc` (puerto).

**P: ¿Por qué el server necesita `FileManagerDir` junto al ejecutable?**
R: El binario `serverFileManager` está programado para buscar esa carpeta en su directorio de trabajo. Sin ella, `lls`, `upload` y `download` fallan. En Docker se crea con `RUN mkdir FileManagerDir` en el Dockerfile.

**P: ¿Qué pasaría si el server registra su IP de pod en el broker?**
R: El broker guardaría `10.244.x.x:32001`. Cuando el cliente intente conectarse a esa IP, no llegaría — es una IP interna del clúster no accesible desde fuera. La conexión fallaría.

**P: ¿Para qué sirve `apt-mark hold kubelet kubeadm kubectl`?**
R: Para que apt no actualice automáticamente esos paquetes. Si kubelet se actualiza a una versión incompatible con el control-plane, el nodo dejaría de funcionar.

**P: ¿Por qué se necesita `nfs-common` en los workers pero no en el NFS server?**
R: El NFS server exporta la carpeta con `nfs-kernel-server`. Los workers necesitan `nfs-common` para poder **montar** esa carpeta exportada. Son dos roles distintos del protocolo NFS: exportador y cliente.

**P: ¿Qué significa `Bound` en `kubectl get pv,pvc`?**
R: Que el PV y el PVC están vinculados entre sí. Kubernetes encontró un PV que satisface los requisitos del PVC (tamaño, accessMode) y los enlazó. Un PVC en `Pending` significa que no hay ningún PV disponible que encaje.

**P: ¿Qué hace `strategy: Recreate` vs `RollingUpdate`?**
R: `Recreate` mata todos los pods antes de crear los nuevos — hay un momento de downtime pero no hay conflictos. `RollingUpdate` (por defecto) crea pods nuevos antes de eliminar los viejos — sin downtime, pero puede haber conflictos si dos pods no pueden coexistir.

**P: ¿Qué es la Downward API y para qué se usa en la práctica?**
R: Es un mecanismo de Kubernetes para inyectar metadatos del pod/nodo como variables de entorno. En la práctica se usa `status.hostIP` para que cada pod server conozca la IP del nodo donde está corriendo y la use al registrarse en el broker.

---

## 12. Comandos de referencia rápida

```bash
# Estado del clúster
kubectl get nodes -o wide
kubectl get pods -o wide
kubectl get svc
kubectl get pv,pvc

# Depuración
kubectl describe pod <nombre>
kubectl logs <nombre-pod>
kubectl exec -it <nombre-pod> -- bash

# Aplicar cambios
kubectl apply -f k8s/archivo.yaml
kubectl delete pod <nombre>    # K8s lo recrea solo

# Conectar a EC2
ssh -i labsuser.pem ubuntu@<IP_PUBLICA>

# Usar el cliente (desde p2-cliente)
./clientFileManager 172.31.81.126 32002
```
