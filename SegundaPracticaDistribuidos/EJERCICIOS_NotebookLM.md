# Práctica 2 — Ejercicios de estudio activo
### Docker + Kubernetes en AWS · Para usar con Google NotebookLM

> Este documento contiene ejercicios de **recuerdo activo**. Para cada ejercicio: intenta responder de memoria, luego contrasta con el documento de teoría `NotebookLM_EstudioP2.md`.
>
> Formato de uso recomendado en NotebookLM: pide al modelo que te haga los ejercicios uno a uno, que corrija tu respuesta y te explique los errores antes de pasar al siguiente.

---

## BLOQUE 1 — Dockerfiles

### Ejercicio 1.1 — Dockerfile del broker desde cero

Escribe de memoria el Dockerfile completo de `d1n0s/p2-broker:latest`. Justifica cada línea.

<details>
<summary>Solución</summary>

```dockerfile
FROM ubuntu:20.04

RUN apt-get update && apt-get install -y software-properties-common

EXPOSE 32002

COPY brokerFileManager /
RUN chmod +x /brokerFileManager

CMD ["/brokerFileManager"]
```

Justificación línea a línea:
- `FROM ubuntu:20.04`: los binarios del profesor requieren glibc >= 2.14 y GLIBCXX_3.4.22. Alpine usa musl (incompatible). Ubuntu 20.04 tiene las versiones correctas.
- `apt-get update...`: actualiza índices de paquetes para instalar dependencias del sistema.
- `EXPOSE 32002`: documenta el puerto que usa el broker (no abre el puerto — eso lo hace el Service de K8s).
- `COPY brokerFileManager /`: copia el binario al raíz del contenedor.
- `chmod +x`: da permisos de ejecución (puede ser necesario dependiendo de cómo se copió).
- `CMD ["/brokerFileManager"]`: comando por defecto al arrancar el contenedor.

</details>

---

### Ejercicio 1.2 — Dockerfile del servidor desde cero

Escribe el Dockerfile completo de `d1n0s/p2-server:latest`. Incluye todas las particularidades necesarias para que el binario funcione.

<details>
<summary>Solución</summary>

```dockerfile
FROM ubuntu:20.04

RUN apt-get update
RUN apt-get install -y software-properties-common
RUN apt-get install -y curl

EXPOSE 32001

COPY serverFileManager /
RUN chmod +x /serverFileManager

RUN mkdir FileManagerDir

COPY resolv.conf /

CMD cp resolv.conf /etc/resolv.conf && \
    /serverFileManager 10.111.182.40 32002 $MY_NODE_IP 32001
```

Particularidades clave:
- `curl`: necesario para que el servidor descubra su IP pública (en la versión original del profesor). En la versión con Downward API no es necesario, pero el Dockerfile base lo tiene.
- `RUN mkdir FileManagerDir`: el binario busca esta carpeta junto al ejecutable. Sin ella, `lls/upload/download` fallan.
- `COPY resolv.conf /`: parche de DNS para que `curl api.ipify.org` funcione dentro del contenedor. Con `nameserver 8.8.8.8`.
- `$MY_NODE_IP`: variable de entorno inyectada por la Downward API de Kubernetes para que el pod registre la IP de su nodo.

</details>

---

### Ejercicio 1.3 — ¿Por qué ubuntu:20.04 y no alpine?

<details>
<summary>Solución</summary>

Los binarios del profesor (`brokerFileManager`, `serverFileManager`, `clientFileManager`) están compilados dinámicamente y enlazados contra `glibc`. Específicamente requieren:
- `glibc >= 2.14`
- `GLIBCXX_3.4.22` (de `libstdc++`)

Alpine Linux usa `musl libc`, que es una implementación alternativa de la librería C estándar, **no compatible** con glibc. Si se intenta ejecutar el binario en un contenedor Alpine, falla con "not found" o "exec format error" porque el enlazador dinámico no encuentra las librerías que necesita.

Ubuntu 20.04 tiene las versiones correctas de glibc y libstdc++ para estos binarios.

</details>

---

### Ejercicio 1.4 — Diferencia entre imagen y contenedor

Explica con tus propias palabras la diferencia entre imagen Docker y contenedor Docker, y da un ejemplo de cada uno de la práctica.

<details>
<summary>Solución</summary>

**Imagen**: la plantilla inmutable. Es el "molde" — contiene el sistema de archivos base, los binarios instalados, las variables de entorno y el comando de arranque, pero no está ejecutándose. Se almacena en disco.

Ejemplo de la práctica: `d1n0s/p2-broker:latest` — la imagen subida a Docker Hub con el binario `brokerFileManager`.

**Contenedor**: una instancia en ejecución de una imagen. Tiene su propio proceso, su propia red y su propio sistema de archivos (basado en la imagen, más una capa de escritura efímera). Puede haber N contenedores del mismo image corriendo a la vez.

Ejemplo de la práctica: el pod del broker corriendo en `worker-1` — es un contenedor instanciado de `d1n0s/p2-broker:latest`.

</details>

---

## BLOQUE 2 — Manifiestos Kubernetes: Deployment

### Ejercicio 2.1 — Deployment del broker

Escribe el YAML completo del Deployment del broker: 1 réplica, imagen correcta, puerto 32002.

<details>
<summary>Solución</summary>

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: broker-deployment
spec:
  replicas: 1
  selector:
    matchLabels:
      app: broker
  template:
    metadata:
      labels:
        app: broker
    spec:
      containers:
        - name: broker
          image: d1n0s/p2-broker:latest
          ports:
            - containerPort: 32002
```

</details>

---

### Ejercicio 2.2 — Deployment del servidor (configuración básica)

Escribe el Deployment del servidor con 1 réplica, usando `hostPath` para `FileManagerDir` y la Downward API para inyectar la IP del nodo.

<details>
<summary>Solución</summary>

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: server-deployment
spec:
  replicas: 1
  strategy:
    type: Recreate
  selector:
    matchLabels:
      app: server
  template:
    metadata:
      labels:
        app: server
    spec:
      volumes:
        - name: filemanagerdir
          hostPath:
            path: /data/filemanager
            type: DirectoryOrCreate
      containers:
        - name: server
          image: d1n0s/p2-server:latest
          volumeMounts:
            - name: filemanagerdir
              mountPath: /FileManagerDir
          env:
            - name: MY_NODE_IP
              valueFrom:
                fieldRef:
                  fieldPath: status.hostIP
          command: ["/bin/sh", "-c"]
          args: ["/serverFileManager 10.111.182.40 32002 $MY_NODE_IP 32001"]
          ports:
            - containerPort: 32001
```

</details>

---

### Ejercicio 2.3 — Deployment del servidor (Avanzada 2: NFS + 2 réplicas)

Escribe el Deployment del servidor con 2 réplicas, usando PVC de NFS en vez de hostPath. El resto es igual que en 2.2.

<details>
<summary>Solución</summary>

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: server-deployment
spec:
  replicas: 2
  strategy:
    type: Recreate
  selector:
    matchLabels:
      app: server
  template:
    metadata:
      labels:
        app: server
    spec:
      volumes:
        - name: filemanagerdir
          persistentVolumeClaim:
            claimName: nfs-pvc          # referencia al PVC (que apunta al NFS)
      containers:
        - name: server
          image: d1n0s/p2-server:latest
          volumeMounts:
            - name: filemanagerdir
              mountPath: /FileManagerDir
          env:
            - name: MY_NODE_IP
              valueFrom:
                fieldRef:
                  fieldPath: status.hostIP
          command: ["/bin/sh", "-c"]
          args: ["/serverFileManager 10.111.182.40 32002 $MY_NODE_IP 32001"]
          ports:
            - containerPort: 32001
```

Diferencia clave respecto a 2.2: el volumen usa `persistentVolumeClaim.claimName: nfs-pvc` en vez de `hostPath`. Esto permite compartir datos entre pods en nodos distintos.

</details>

---

### Ejercicio 2.4 — ¿Por qué strategy: Recreate?

<details>
<summary>Solución</summary>

La estrategia por defecto de Kubernetes es `RollingUpdate`: crea pods nuevos antes de eliminar los viejos para lograr actualizaciones sin downtime.

El problema en esta práctica: si hay dos pods del server en el mismo nodo intentando escuchar en el puerto `32001`, hay un **conflicto de puerto**. El segundo pod no puede arrancar porque el puerto ya está ocupado.

`Recreate` mata todos los pods primero y luego crea los nuevos, garantizando que no hay dos instancias del mismo pod corriendo a la vez en el mismo nodo. El trade-off es que hay un breve momento sin servicio durante la actualización.

</details>

---

### Ejercicio 2.5 — Campos clave de un Deployment: para qué sirve cada uno

Explica el propósito de estos campos:
- `replicas`
- `selector.matchLabels`
- `template.metadata.labels`
- `volumeMounts.mountPath`
- `fieldRef.fieldPath: status.hostIP`

<details>
<summary>Solución</summary>

- **`replicas`**: cuántas copias del pod quiere Kubernetes que estén corriendo en todo momento. Si un pod muere, el Deployment crea uno nuevo hasta llegar a este número.

- **`selector.matchLabels`**: el filtro que usa el Deployment para saber qué pods gestiona. Solo los pods con estas etiquetas son "suyos". Si coincide con pods de otro Deployment, habría conflictos.

- **`template.metadata.labels`**: las etiquetas que se ponen a cada pod que se crea. Deben coincidir con `selector.matchLabels`, si no el Deployment nunca encuentra sus propios pods.

- **`volumeMounts.mountPath`**: la ruta dentro del contenedor donde se monta el volumen. Es aquí donde el binario buscará `FileManagerDir`. Si el binario espera `/FileManagerDir` y se monta en `/data`, no encontrará los ficheros.

- **`fieldRef.fieldPath: status.hostIP`**: Downward API — inyecta como variable de entorno la IP del nodo físico donde corre el pod. Cada pod obtiene la IP de su propio nodo, no la del pod ni la del clúster.

</details>

---

## BLOQUE 3 — Manifiestos Kubernetes: Service

### Ejercicio 3.1 — Service NodePort para el broker

Escribe el YAML completo del Service del broker como NodePort en el puerto 32002.

<details>
<summary>Solución</summary>

```yaml
apiVersion: v1
kind: Service
metadata:
  name: broker-service
spec:
  type: NodePort
  selector:
    app: broker
  ports:
    - port: 32002         # puerto del Service dentro del clúster (ClusterIP)
      targetPort: 32002   # puerto del contenedor al que llega el tráfico
      nodePort: 32002     # puerto abierto en el nodo físico
```

</details>

---

### Ejercicio 3.2 — Las tres IPs de un Service

Para el `broker-service`, explica la diferencia entre estas tres direcciones y quién usa cada una:
- `10.111.182.40:32002`
- `172.31.81.126:32002`
- `10.244.1.3:32002`

<details>
<summary>Solución</summary>

- **`10.244.1.3:32002`** → IP del **Pod**. Asignada por Flannel (CNI). Solo accesible dentro del clúster, cambia cada vez que el pod se recrea. Nadie debería usarla directamente.

- **`10.111.182.40:32002`** → **ClusterIP** del Service. Dirección estable dentro del clúster. Los pods que necesitan conectarse al broker desde dentro del clúster usan esta IP. En la práctica, los pods del server usan esta IP para hablar con el broker (en los `args` del Deployment del server: `10.111.182.40 32002`). No es accesible desde fuera del clúster.

- **`172.31.81.126:32002`** → **IP del nodo worker + NodePort**. Accesible desde fuera del clúster (desde el VPC). El cliente externo (`clientFileManager`) usa esta dirección para conectarse al broker.

</details>

---

### Ejercicio 3.3 — ¿Por qué usar IP privada y no pública para el cliente?

El cliente se conecta desde `p2-cliente` (dentro del VPC). ¿Por qué usa `172.31.81.126:32002` y no la IP pública del worker?

<details>
<summary>Solución</summary>

AWS no hace **hairpinning** entre instancias del mismo VPC: cuando una instancia EC2 intenta conectarse a la IP pública de otra instancia del mismo VPC, el tráfico no se redirige correctamente — la conexión falla o no llega.

Las instancias dentro del VPC deben comunicarse siempre usando las **IPs privadas** (`172.31.x.x`). Solo los clientes externos al VPC necesitan la IP pública.

`p2-cliente` es una EC2 dentro del mismo VPC (`172.31.0.0/16`), así que siempre usa IPs privadas para conectarse a los workers.

</details>

---

## BLOQUE 4 — Almacenamiento: PV y PVC

### Ejercicio 4.1 — PersistentVolume para NFS

Escribe el YAML del PersistentVolume que apunta al servidor NFS en `172.31.85.178:/data/filemanager`.

<details>
<summary>Solución</summary>

```yaml
apiVersion: v1
kind: PersistentVolume
metadata:
  name: nfs-pv
spec:
  capacity:
    storage: 1Gi
  accessModes:
    - ReadWriteMany
  persistentVolumeReclaimPolicy: Retain
  nfs:
    server: 172.31.85.178
    path: /data/filemanager
```

</details>

---

### Ejercicio 4.2 — PersistentVolumeClaim

Escribe el YAML del PVC que solicita almacenamiento para montar en los pods del server.

<details>
<summary>Solución</summary>

```yaml
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: nfs-pvc
spec:
  accessModes:
    - ReadWriteMany
  resources:
    requests:
      storage: 1Gi
```

</details>

---

### Ejercicio 4.3 — Relación PV → PVC → Pod

Dibuja o describe en texto la cadena de vinculación entre el PV, el PVC y el Pod del server.

<details>
<summary>Solución</summary>

```
nfs-pv                     nfs-pvc                    Pod server
(el almacenamiento real)   (la reserva/ticket)        (quien usa el almacenamiento)

  nfs:                  ←vinculado→  claimName:     ←montado en→  mountPath:
  server: 172.31.85.178              nfs-pvc                      /FileManagerDir
  path: /data/filemanager
```

Kubernetes vincula automáticamente el PVC con el PV si:
1. El `accessMode` coincide (ambos `ReadWriteMany`).
2. El PV tiene suficiente capacidad (PV ofrece `1Gi`, PVC pide `1Gi`).

Una vez vinculados (`Bound`), el Deployment puede referenciar el PVC en su sección `volumes`, y el PVC redirige transparentemente al NFS real.

</details>

---

### Ejercicio 4.4 — Tabla: hostPath vs NFS

Completa la tabla comparativa:

| | hostPath | NFS |
|---|---|---|
| Dónde viven los datos | ??? | ??? |
| ¿Compartido entre pods del mismo nodo? | ??? | ??? |
| ¿Compartido entre pods de nodos distintos? | ??? | ??? |
| ¿Persiste si el pod cae? | ??? | ??? |
| ¿Persiste si el nodo cae? | ??? | ??? |
| Objeto K8s necesario | ??? | ??? |

<details>
<summary>Solución</summary>

| | hostPath | NFS |
|---|---|---|
| Dónde viven los datos | Disco local del nodo | Servidor NFS externo (`172.31.85.178`) |
| ¿Compartido entre pods del mismo nodo? | Sí | Sí |
| ¿Compartido entre pods de nodos distintos? | No | Sí |
| ¿Persiste si el pod cae? | Sí | Sí |
| ¿Persiste si el nodo cae? | No (datos en ese nodo) | Sí (datos en el NFS server) |
| Objeto K8s necesario | Solo `hostPath` en el volumen | PV + PVC |

</details>

---

### Ejercicio 4.5 — AccessModes: ¿qué pasa si se usa ReadWriteOnce?

Si el PV y el PVC tienen `ReadWriteOnce` en vez de `ReadWriteMany`, ¿qué ocurre con los pods en múltiples nodos?

<details>
<summary>Solución</summary>

`ReadWriteOnce` significa que solo **un nodo** puede montar el volumen con permisos de lectura y escritura a la vez.

Si hay pods del server en `worker-1` y `worker-2`, el primero que monte el PVC lo hará correctamente. El segundo pod intentará montarlo en su nodo, pero Kubernetes lo denegará (o el montaje fallará) porque el volumen ya está montado en modo escritura en otro nodo.

El pod en el segundo nodo se quedaría en estado `ContainerCreating` o `Pending` indefinidamente. Con NFS hay que usar `ReadWriteMany` para que varios nodos puedan montar el mismo volumen simultáneamente.

</details>

---

## BLOQUE 5 — Red y alcanzabilidad

### Ejercicio 5.1 — El reto central de la práctica

Explica el "reto central" de la práctica con tus propias palabras: ¿por qué la IP que el server registra en el broker es tan crítica?

<details>
<summary>Solución</summary>

El flujo de conexión es:
1. El server se registra en el broker con su IP y puerto.
2. El cliente pregunta al broker qué server hay disponible.
3. El broker le da la IP y puerto del server.
4. **El cliente se conecta directamente a esa dirección literal.**

Si el server registra su IP interna de pod (`10.244.x.x`, asignada por Flannel), el cliente intentará conectarse a esa IP desde fuera del clúster. Esa IP no es accesible desde fuera — el paquete no llegaría nunca. La conexión falla.

La solución es que el server registre la **IP del nodo worker** (`172.31.81.126`) y el **NodePort** (`32001`), porque esa dirección sí es accesible desde dentro del VPC. El NodePort service en el worker redirige ese tráfico al pod correcto.

</details>

---

### Ejercicio 5.2 — Downward API

¿Qué es la Downward API de Kubernetes y cómo se usa en esta práctica? Escribe el fragmento de YAML que la implementa.

<details>
<summary>Solución</summary>

La **Downward API** es un mecanismo de Kubernetes que permite que un pod conozca información sobre sí mismo o sobre el nodo donde está corriendo, sin necesidad de llamadas externas a la API de Kubernetes.

En la práctica se usa para que cada pod sepa la IP del nodo donde está corriendo (`status.hostIP`) y la use como argumento al registrarse en el broker. Así, el pod en `worker-1` registra `172.31.81.126` y el pod en `worker-2` registra `172.31.85.248`.

```yaml
env:
  - name: MY_NODE_IP
    valueFrom:
      fieldRef:
        fieldPath: status.hostIP    # IP del nodo físico donde corre el pod
command: ["/bin/sh", "-c"]
args: ["/serverFileManager 10.111.182.40 32002 $MY_NODE_IP 32001"]
```

El shell (`/bin/sh -c`) expande `$MY_NODE_IP` antes de ejecutar el binario.

</details>

---

### Ejercicio 5.3 — Diagrama de flujo: sesión completa del cliente

Dibuja o describe el flujo completo desde que el cliente arranca hasta que hace `upload test.txt` exitosamente, con los NodePorts y pods involucrados (configuración Avanzada 2, 2 nodos).

<details>
<summary>Solución</summary>

```
p2-cliente (172.31.94.41)
    │
    │ (1) ./clientFileManager 172.31.81.126 32002
    ▼
172.31.81.126:32002  →  NodePort broker-service  →  Pod broker (10.244.1.3:32002)
    │
    │ Broker devuelve: "172.31.81.126:32001" y "172.31.85.248:32001"
    ▼
(2) El cliente elige uno: conecta a 172.31.81.126:32001
    →  NodePort server-service en worker-1  →  Pod server en worker-1 (10.244.1.5:32001)

(3) Cliente hace: upload test.txt
    →  Pod server recibe el fichero
    →  Lo escribe en /FileManagerDir  →  montado desde NFS (172.31.85.178:/data/filemanager)

(4) Si luego el cliente conecta al server en worker-2 (172.31.85.248:32001)
    y hace lls
    →  Pod server en worker-2 también tiene /FileManagerDir montado desde el mismo NFS
    →  Ve test.txt ✅
```

</details>

---

### Ejercicio 5.4 — Diagnóstico de conectividad

Escenario: el cliente conecta al broker (step 1 OK), recibe `172.31.81.126:32001`, intenta conectarse al server y falla ("Connection refused"). Nombra las 3 causas más probables y cómo verificar cada una.

<details>
<summary>Solución</summary>

1. **El pod del server no está corriendo.**
   Verificación: `kubectl get pods -o wide` → ver si el pod está en `Running` o en `CrashLoopBackOff`/`Pending`. Si crashea, `kubectl logs <pod>` para ver el error.

2. **El Security Group no permite tráfico en el puerto 32001 desde la IP del cliente.**
   Verificación: en la consola de AWS, comprobar el Security Group del worker. Debe tener una regla que permita TCP:32001 desde `172.31.0.0/16` (o desde la IP de `p2-cliente`). Si falta, añadirla.

3. **El Service NodePort no está correctamente configurado (selector equivocado o puerto incorrecto).**
   Verificación: `kubectl get svc server-service` → ver el NodePort asignado. `kubectl describe svc server-service` → ver los `Endpoints`: si están vacíos, el selector no coincide con ningún pod. Comparar el `selector` del Service con las `labels` del pod.

4. (Extra) **El server registró una IP incorrecta en el broker** (la IP del pod en vez de la del nodo).
   Verificación: `kubectl logs <pod-server>` → buscar el mensaje de registro que muestra con qué IP/Puerto se conectó al broker.

</details>

---

## BLOQUE 6 — Arquitectura y conceptos de Kubernetes

### Ejercicio 6.1 — Componentes del clúster

¿Cuál es el rol de cada uno de estos componentes?

- API Server
- Scheduler
- etcd
- kubelet
- kube-proxy
- containerd

<details>
<summary>Solución</summary>

- **API Server**: el punto de entrada del clúster. Recibe todos los comandos de `kubectl` y de otros componentes. Es la única forma de interactuar con el estado del clúster.

- **Scheduler**: decide en qué nodo se ejecuta cada pod nuevo, basándose en recursos disponibles, afinidades y restricciones.

- **etcd**: base de datos distribuida y consistente donde se almacena todo el estado del clúster (pods, services, configuraciones...). Es la fuente de verdad.

- **kubelet**: agente que corre en cada nodo worker. Recibe órdenes del API Server y se asegura de que los contenedores definidos en los pods estén corriendo.

- **kube-proxy**: gestiona las reglas de red en cada nodo. Implementa los Services (incluyendo NodePort) creando reglas iptables/ipvs para redirigir el tráfico.

- **containerd**: el runtime de contenedores. Es quien realmente descarga las imágenes de Docker Hub y ejecuta los contenedores.

</details>

---

### Ejercicio 6.2 — ¿Qué hace kubectl apply vs kubectl delete?

<details>
<summary>Solución</summary>

- **`kubectl apply -f archivo.yaml`**: aplica el manifiesto al clúster. Si el objeto no existe, lo crea. Si ya existe, lo actualiza para que coincida con el YAML (solo cambia los campos que difieren). Es **declarativo** — describes el estado deseado.

- **`kubectl delete -f archivo.yaml`**: elimina el objeto descrito en el YAML del clúster. Si era un Deployment, también elimina todos los pods que gestionaba. No se puede deshacer.

- **`kubectl delete pod <nombre>`**: elimina un pod concreto. Si ese pod pertenece a un Deployment, Kubernetes lo recrea automáticamente (el Deployment sigue queriendo N réplicas). Es la forma de "forzar un reinicio" de un pod.

</details>

---

### Ejercicio 6.3 — Comandos kubectl: ¿cuál usas para cada tarea?

Para cada tarea, di el comando exacto:

1. Ver todos los pods con su nodo y su IP de pod.
2. Ver los logs del último contenedor que crasheó en un pod.
3. Entrar a una shell interactiva dentro de un pod.
4. Ver todos los Services con sus puertos y NodePorts.
5. Ver el estado de los PV y PVC.
6. Ver los eventos y errores de un pod que no arranca.

<details>
<summary>Solución</summary>

1. `kubectl get pods -o wide`
2. `kubectl logs <nombre-pod> --previous`
3. `kubectl exec -it <nombre-pod> -- bash`
4. `kubectl get svc`
5. `kubectl get pv,pvc`
6. `kubectl describe pod <nombre-pod>`

</details>

---

### Ejercicio 6.4 — Pod efímero vs Service estable

¿Por qué los pods son "efímeros" y qué problema resuelve el Service?

<details>
<summary>Solución</summary>

Un pod es **efímero** porque Kubernetes puede recrearlo en cualquier momento (por un fallo, una actualización, un reschedule). Cada vez que un pod se recrea, recibe una nueva IP interna (`10.244.x.x`) — diferente a la anterior.

Si el server intentara conectarse al broker usando directamente la IP del pod del broker, la conexión fallaría cada vez que el broker se recrée (porque la IP cambia).

El **Service** proporciona una dirección estable: la ClusterIP del Service no cambia aunque los pods se recreen. Kubernetes actualiza internamente a qué pods redirige el tráfico del Service (mediante `Endpoints`). Así el server siempre usa la misma IP (`10.111.182.40`) para hablar con el broker, independientemente de cuántas veces se haya recreado el pod del broker.

</details>

---

## BLOQUE 7 — AWS y red

### Ejercicio 7.1 — Infraestructura EC2 de la práctica

De memoria, di el nombre, tipo de instancia e IP privada de cada máquina EC2 de la práctica.

<details>
<summary>Solución</summary>

| Nombre | Tipo | IP privada | Rol |
|---|---|---|---|
| `p2-control-plane` | t2.medium | `172.31.85.97` | Cerebro del clúster K8s |
| `p2-worker` | t2.medium | `172.31.81.126` | Nodo esclavo 1 |
| `p2-worker-2` | t2.medium | `172.31.85.248` | Nodo esclavo 2 (Avanzada 2) |
| `p2-nfs-server` | t2.micro | `172.31.85.178` | Servidor NFS (Avanzada 2) |
| `p2-cliente` | t2.micro | `172.31.94.41` | Ejecuta clientFileManager |

Todas en la red `172.31.0.0/16` (el VPC de AWS Academy).

</details>

---

### Ejercicio 7.2 — Security Groups: ¿qué puertos hay que abrir?

Para que la práctica funcione completamente (Avanzada 2), enumera los puertos que deben estar abiertos en los Security Groups y entre qué máquinas.

<details>
<summary>Solución</summary>

- **Entre todos los nodos del clúster** (self-referencing rule en el SG): todo el tráfico. Esto cubre:
  - `6443/TCP`: API Server de Kubernetes
  - `2379-2380/TCP`: etcd
  - `10250/TCP`: kubelet
  - `8472/UDP`: Flannel VXLAN (red de pods)
  - Puertos de la app: `32001`, `32002`

- **Desde `p2-cliente`** hacia los workers:
  - `32001/TCP` (server NodePort)
  - `32002/TCP` (broker NodePort)

- **SSH** (`22/TCP`): desde la IP del alumno hacia todas las instancias.

- **Desde los workers hacia el NFS server** (`p2-nfs-server`):
  - `2049/TCP` (NFS) y puertos relacionados.

</details>

---

### Ejercicio 7.3 — ¿Qué es Flannel y para qué sirve?

<details>
<summary>Solución</summary>

**Flannel** es un plugin de red CNI (Container Network Interface) para Kubernetes. Su función es asignar IPs a los pods y crear la red virtual que permite que pods de distintos nodos se comuniquen entre sí.

Flannel crea una red superpuesta (overlay) usando VXLAN: encapsula los paquetes entre nodos en tramas UDP. Desde dentro de un pod, la comunicación parece directa — no saben que hay una encapsulación.

En la práctica, Flannel asigna IPs del rango `10.244.0.0/16` a los pods:
- Pods en `worker-1` reciben IPs de `10.244.1.x`
- Pods en `worker-2` reciben IPs de `10.244.2.x`

Sin Flannel, los pods de distintos nodos no podrían comunicarse (el broker en worker-1 no podría hablar con el control-plane, etc.).

</details>

---

## BLOQUE 8 — Escenarios de diagnóstico

### Ejercicio 8.1 — Pod en CrashLoopBackOff

El pod del server arranca, escribe un mensaje de error y se cierra. Kubernetes intenta recrearlo repetidamente. ¿Qué comandos usas para diagnosticar el problema y qué causas buscarías primero?

<details>
<summary>Solución</summary>

Comandos:
```bash
kubectl logs <nombre-pod>           # ver stdout/stderr del binario
kubectl logs <nombre-pod> --previous  # ver logs del contenedor anterior (si crasheó)
kubectl describe pod <nombre-pod>   # ver eventos: OOMKilled, ImagePullBackOff, etc.
```

Causas más comunes en esta práctica:
1. **`FileManagerDir` no existe dentro del contenedor.** El binario no puede arrancar sin ella. Solución: verificar que el `RUN mkdir FileManagerDir` está en el Dockerfile o que el volumen se monta correctamente.
2. **El broker no es alcanzable.** El server intenta conectarse a la IP/puerto del broker y falla. Verificar que el broker-service existe y que la ClusterIP usada en los args del server es la correcta (`kubectl get svc broker-service`).
3. **Variable de entorno `MY_NODE_IP` vacía.** Si la Downward API no se configuró bien, el servidor recibe una cadena vacía como IP pública y falla. Ver los logs para el mensaje de error concreto.
4. **Imagen no encontrada.** `ImagePullBackOff` en `kubectl describe` indica que la imagen no está en Docker Hub o el nombre es incorrecto.

</details>

---

### Ejercicio 8.2 — PVC en Pending

`kubectl get pvc` muestra `nfs-pvc` en estado `Pending`. ¿Qué puede estar mal?

<details>
<summary>Solución</summary>

`Pending` significa que Kubernetes no encontró ningún PV que satisfaga los requisitos del PVC. Causas posibles:

1. **El PV no existe.** `kubectl get pv` → si `nfs-pv` no aparece, no se aplicó el YAML del PV. Solución: `kubectl apply -f k8s/nfs-pv.yaml`.

2. **El `accessMode` no coincide.** El PVC pide `ReadWriteMany` pero el PV tiene `ReadWriteOnce`. Kubernetes no los vincula si los modos no coinciden.

3. **El PV ya está vinculado a otro PVC.** Un PV solo puede estar en `Bound` con un PVC a la vez. Si se borró el PVC pero no el PV, el PV puede quedar en estado `Released` (con la política `Retain`). Hay que eliminar el PV antiguo y recrearlo.

4. **Capacidad insuficiente.** El PVC pide `2Gi` pero el PV solo ofrece `1Gi`. Kubernetes no los vincula.

</details>

---

### Ejercicio 8.3 — upload funciona pero lls no ve el fichero desde el otro pod

Se hace `upload test.txt` y el servidor confirma éxito. Luego se hace `lls` y el fichero no aparece. Diagnóstico.

<details>
<summary>Solución</summary>

El síntoma indica que el `upload` fue a un pod (p. ej., server-A) y el `lls` fue a otro pod (server-B) que tiene su propio `FileManagerDir` separado. Causas:

1. **No se configuró el almacenamiento compartido.** Cada pod tiene su propia capa de escritura efímera. Si no se montó `hostPath` o NFS, cada pod escribe en su propio contenedor. Solución: añadir el volumen compartido al Deployment.

2. **Configuración avanzada 1 (hostPath)**: verificar que el `mountPath` en el contenedor (`/FileManagerDir`) coincide exactamente con donde el binario busca los ficheros. `kubectl exec -it <pod> -- ls /FileManagerDir` en ambos pods para confirmar que ven los mismos ficheros.

3. **Configuración avanzada 2 (NFS)**: verificar que el PVC está en `Bound` y que `nfs-common` está instalado en los workers (`apt list --installed | grep nfs-common`). Sin `nfs-common`, el montaje NFS falla silenciosamente y el pod usa su sistema de archivos local.

</details>

---

## BLOQUE 9 — Preguntas de comprensión global

### Ejercicio 9.1 — ¿Qué hace el broker exactamente?

Describe el rol exacto del broker en el sistema. ¿Redirige el tráfico? ¿Lo procesa?

<details>
<summary>Solución</summary>

El broker es solo un **directorio de conexión** — no redirige ni procesa el tráfico de datos.

Su único trabajo:
1. Mantener una lista de servers disponibles (IP + Puerto) que se registraron.
2. Cuando un cliente pregunta, devolverle la dirección de un server disponible.

Después de que el cliente recibe la dirección del server, **se conecta directamente al server sin pasar por el broker**. Todo el tráfico de `lls/upload/download` va directamente entre cliente y server.

Esto es diferente a un proxy o un load balancer que redirige cada petición. El broker solo hace el "matchmaking" inicial.

</details>

---

### Ejercicio 9.2 — Diferencia entre configuración básica, Avanzada 1 y Avanzada 2

Explica en qué se diferencian las tres configuraciones de la práctica en cuanto a: número de réplicas, nodos usados, y cómo se comparte el almacenamiento.

<details>
<summary>Solución</summary>

| | Básica | Avanzada 1 | Avanzada 2 |
|---|---|---|---|
| Réplicas del server | 1 | 2 | 2 |
| Nodos worker | 1 (`worker-1`) | 1 (`worker-1`) | 2 (`worker-1` + `worker-2`) |
| Almacenamiento | Dentro del contenedor (efímero) | `hostPath` en el nodo | NFS vía PV/PVC |
| Ficheros compartidos entre pods | No aplica (1 pod) | Sí (mismo nodo) | Sí (distintos nodos) |
| Objeto K8s para almacenamiento | Ninguno | `hostPath` en volumes | PV + PVC |

</details>

---

### Ejercicio 9.3 — ¿Por qué el serverFileManager necesita IP como argumento y no hostname?

<details>
<summary>Solución</summary>

Porque el binario `serverFileManager` fue compilado por el profesor con una implementación de socket que no resuelve hostnames — solo acepta IPs en formato `x.x.x.x`.

Si se le pasa `broker-service` (el nombre DNS del Service dentro del clúster), el binario intenta conectarse a ese string como si fuera una IP, falla la resolución y la conexión no se establece.

Por eso en los `args` del Deployment siempre se usa la ClusterIP numérica del broker-service (`10.111.182.40`), que se obtiene con `kubectl get svc broker-service`.

Este es uno de los errores típicos de la práctica: usar el nombre DNS del Service en vez de la ClusterIP.

</details>

---

### Ejercicio 9.4 — ¿Qué pasaría si el serverFileManager se registra con la IP del pod?

Describe el escenario completo: qué registra, qué devuelve el broker al cliente y qué pasa cuando el cliente intenta conectarse.

<details>
<summary>Solución</summary>

1. El server arranca. Sin Downward API, usa su IP de pod (ej. `10.244.1.5`). Envía `register_server` al broker con IP `10.244.1.5` y puerto `32001`.

2. El broker guarda `{ip: "10.244.1.5", port: 32001}` en su lista.

3. El cliente pregunta al broker por un server. El broker devuelve `10.244.1.5:32001`.

4. El cliente intenta `connect("10.244.1.5", 32001)`. Esta IP es una dirección de la red interna de Flannel (`10.244.0.0/16`), solo enrutable dentro del clúster. `p2-cliente` es una EC2 fuera del clúster — no tiene rutas hacia `10.244.x.x`.

5. La conexión falla inmediatamente con "Network unreachable" o timeout. El cliente no puede usar el server.

Solución: usar la Downward API (`status.hostIP`) para registrar la IP del nodo worker, no la IP del pod.

</details>

---

### Ejercicio 9.5 — ¿Por qué se necesita nfs-common en los workers?

<details>
<summary>Solución</summary>

NFS es un protocolo de red para compartir sistemas de archivos. Para **montar** un volumen NFS, el sistema operativo del nodo necesita un cliente NFS que entienda el protocolo.

En Linux, ese cliente está en el paquete `nfs-common`. Sin él, cuando Kubernetes intenta montar el PVC NFS en el pod, el nodo lanza el comando de montaje (`mount -t nfs ...`), que falla porque el kernel no tiene el módulo NFS cargado ni las herramientas necesarias.

El resultado: el pod se queda en `ContainerCreating` indefinidamente. `kubectl describe pod` mostrará un evento del tipo `MountVolume.SetUp failed`.

El **servidor NFS** (`p2-nfs-server`) usa `nfs-kernel-server` para **exportar** la carpeta. Los **clientes NFS** (los workers) usan `nfs-common` para **montarla**. Son dos roles distintos del protocolo.

</details>

