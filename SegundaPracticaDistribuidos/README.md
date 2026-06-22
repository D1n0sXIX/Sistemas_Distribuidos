# Práctica 2 — Despliegue con Docker + Kubernetes en AWS
--- Link a AWS - https://awsacademy.instructure.com/login/canvas
Despliegue de una aplicación distribuida (broker + servidores de ficheros + cliente) sobre un clúster de **Kubernetes (kubeadm)** montado en instancias **EC2**, empaquetando los programas servidor en **imágenes Docker**.

> Asignatura: Programación de Sistemas Distribuidos · Alumno: **D1n0sXIX** (poner nombre/apellidos reales en la entrega)
> Estación de trabajo: **Arch Linux** · Infraestructura: **AWS EC2**
> El contexto técnico completo está en [`CLAUDE.md`](./CLAUDE.md).

---

## Arquitectura de la app

```
                 (1) registra IP/Puerto
   serverFileManager ───────────────────────▶  brokerFileManager
        ▲  (escucha 32001)                          ▲ (escucha 32002)
        │                                            │
        │ (3) conexión DIRECTA cliente↔server        │ (2) pide datos
        │     usando la IP/Puerto registrados        │     de conexión
        └────────────────── clientFileManager ───────┘
```

| Programa | Invocación | Puerto |
|---|---|---|
| `brokerFileManager` | `./brokerFileManager` (arranca el 1º) | 32002 |
| `serverFileManager` | `./serverFileManager <BROKER_IP> <BROKER_PORT> <PUBLIC_IP> <PUBLIC_PORT>` | 32001 |
| `clientFileManager` | `./clientFileManager <BROKER_IP> <BROKER_PORT>` | — |

**Reto central:** el server se autorregistra en el broker con `<PUBLIC_IP>:<PUBLIC_PORT>` y el cliente se conecta a esa dirección literal → debe ser **alcanzable** desde donde corra el cliente. Ver §6 de `CLAUDE.md`.

---

## Configuración básica

Un nodo worker con un pod de broker y un pod de server. El cliente se conecta al broker vía NodePort, recibe la dirección del server y se conecta directamente a él.

```
                        CLÚSTER KUBERNETES
  ┌─────────────────────────────────────────────────────────────────┐
  │  EC2 control-plane (172.31.85.97)                               │
  │  Solo gestión del clúster — kubectl habla con él                │
  └─────────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────────┐
  │  EC2 worker-1 (172.31.81.126)                                   │
  │                                                                 │
  │  ┌──────────────────────┐   ┌──────────────────────┐           │
  │  │ POD broker           │   │ POD server           │           │
  │  │  brokerFileManager   │   │  serverFileManager   │           │
  │  │  puerto 32002        │   │  puerto 32001        │           │
  │  └──────────────────────┘   └──────────────────────┘           │
  │        ▲ NodePort:32002            ▲ NodePort:32001             │
  └────────┼────────────────────────── ┼───────────────────────────┘
           │                           │
           │ (1) conecta al broker     │ (3) conecta al server
           │ 172.31.81.126:32002       │ 172.31.81.126:32001
           │                           │
           │    (2) broker devuelve    │
           │    "server en            ─┘
           │    172.31.81.126:32001"
           │
  ┌────────┴──────────────┐
  │  EC2 p2-cliente       │
  │  clientFileManager    │
  │  ls/lls/upload/download│
  └───────────────────────┘
```

---

## Configuración avanzada 1 — hostPath (réplicas en un nodo)

Varias réplicas del server en el **mismo nodo**, compartiendo una carpeta del nodo (`hostPath`). El NodePort balancea entre los pods. Los ficheros persisten aunque los pods se reinicien.

```
                        CLÚSTER KUBERNETES
  ┌─────────────────────────────────────────────────────────────────┐
  │  EC2 worker-1 (172.31.81.126)                                   │
  │                                                                 │
  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
  │  │ POD broker   │  │ POD server-1 │  │ POD server-2 │          │
  │  │ broker:32002 │  │ server:32001 │  │ server:32001 │          │
  │  └──────────────┘  └──────┬───────┘  └──────┬───────┘          │
  │                           │                  │                  │
  │                           └────────┬─────────┘                  │
  │                                    │ montan la misma carpeta    │
  │                           ┌────────▼─────────┐                  │
  │                           │  /data/filemanager│  (hostPath)     │
  │                           │  del nodo físico  │                 │
  │                           └──────────────────┘                  │
  │        ▲ NodePort:32002         ▲ NodePort:32001                │
  └────────┼────────────────────────┼────────────────────────────── ┘
           │                        │ balancea entre server-1 y server-2
  ┌────────┴────────────────────────┴──────┐
  │  EC2 p2-cliente — clientFileManager    │
  │  upload → va a uno de los dos pods     │
  │  lls    → ve los mismos ficheros       │
  └────────────────────────────────────────┘
```

---

## Configuración avanzada 2 — NFS (réplicas en varios nodos)

Los pods del server se distribuyen en **dos nodos distintos**. La carpeta compartida es un volumen NFS montado en todos los pods vía PersistentVolume/PersistentVolumeClaim. Cada pod registra en el broker la IP de su propio nodo (Downward API).

```
                        CLÚSTER KUBERNETES
  ┌──────────────────────────────────────────────────────────────────────┐
  │  EC2 worker-1 (172.31.81.126)         EC2 worker-2 (172.31.85.248)  │
  │                                                                      │
  │  ┌─────────────┐ ┌─────────────┐      ┌─────────────┐              │
  │  │ POD broker  │ │ POD server  │      │ POD server  │              │
  │  │ :32002      │ │ :32001      │      │ :32001      │              │
  │  └─────────────┘ └──────┬──────┘      └──────┬──────┘              │
  │   ▲ NodePort:32002       │                    │                     │
  │                ▲ NodePort:32001     ▲ NodePort:32001                │
  └────────────────┼────────────────────┼──────────────────────────────┘
                   │                    │
                   └────────┬───────────┘
                            │ montan vía NFS (PV/PVC)
                   ┌────────▼──────────────────┐
                   │  EC2 nfs-server            │
                   │  172.31.85.178             │
                   │  exporta /data/filemanager │
                   └────────────────────────────┘

  Flujo de conexión:

  EC2 p2-cliente
       │
       │ (1) 172.31.81.126:32002  →  NodePort  →  Pod broker
       │         broker devuelve: "server-A en 172.31.81.126:32001"
       │                          "server-B en 172.31.85.248:32001"
       │
       │ (2) conecta a 172.31.81.126:32001 ó 172.31.85.248:32001
       │         NodePort  →  Pod server (en cualquier nodo)
       │
       │ (3) upload/lls/download
       │         todos los pods leen y escriben en el mismo NFS
       ▼
    ficheros visibles desde cualquier réplica en cualquier nodo
```

> **Clave:** cada pod server usa la Downward API (`status.hostIP`) para registrar en el broker la IP del nodo donde está corriendo — no la IP interna del pod, que el cliente no puede alcanzar.

---

## Seguimiento por fases

### Fase 0 — Familiarización (sin K8s) ✅
- [x] Probar los 3 binarios "a pelo" en local (3 terminales: broker, server, cliente)
- [x] Verificar `ls`, `lls`, `upload`, `download`, `exit()` end-to-end
- [x] Confirmar que `brokerFileManager` usa 32002 fijo
- [x] Confirmar que `serverFileManager` necesita `FileManagerDir/` junto al ejecutable

### Fase 1 — Imágenes Docker ✅
- [x] `broker/Dockerfile` escrito — ubuntu:20.04, brokerFileManager, EXPOSE 32002
- [x] `server/Dockerfile` escrito — ubuntu:20.04, curl, serverFileManager, FileManagerDir, resolv.conf, EXPOSE 32001
- [x] Binarios copiados a `broker/` y `server/` para el COPY

### Fase 2 — Distribución de imagen ✅
- [x] `docker build` + `docker push` de `d1n0s/p2-broker:latest` y `d1n0s/p2-server:latest`
- [x] Imágenes disponibles en Docker Hub — Kubernetes las descarga automáticamente al desplegar

### Fase 3 — Clúster kubeadm ✅
- [x] containerd instalado y configurado en control-plane y worker
- [x] kubeadm/kubelet/kubectl v1.29.15 instalados en ambos nodos
- [x] `kubeadm init --pod-network-cidr=10.244.0.0/16` en control-plane
- [x] Flannel CNI instalado
- [x] Worker unido con `kubeadm join`
- [x] `kubectl get nodes` → ambos nodos en `Ready`

  | Nodo | IP pública | IP privada |
  |---|---|---|
  | control-plane | `18.233.154.106` | `172.31.85.97` |
  | worker | `3.84.165.31` | `172.31.81.126` |
  | cliente | `98.93.149.89` | — |

  **kubectl desde Arch:** kubeconfig copiado con `scp`, IP cambiada a pública, `insecure-skip-tls-verify: true` (certificado no incluye IP pública).

### Fase 4 — Deployments + Services ✅
- [x] `k8s/broker-deployment.yaml` — 1 réplica, puerto 32002
- [x] `k8s/broker-service.yaml` — NodePort 32002 (accesible desde `p2-cliente` dentro del VPC)
- [x] `k8s/server-deployment.yaml` — `strategy: Recreate`, `hostPath` volume en `/data/filemanager`
- [x] `k8s/server-service.yaml` — NodePort 32001 (enruta tráfico del nodo a los pods del server)
- [x] `kubectl apply -f k8s/` — todos los pods en `Running`

### Fase 5 — Demo end-to-end ✅
- [x] Cliente (`p2-cliente`) → broker → server: conexión establecida
- [x] `upload test.txt` → fichero aparece en `lls` ✅
- [x] `download test.txt` → fichero descargado en cliente ✅

### Fase 6 — Configuraciones avanzadas (examen / 10 puntos)
- [x] **Avanzada 1:** 2 réplicas del server en el worker + `FileManagerDir` compartido por `hostPath` ✅
  - Ambos pods montan `/data/filemanager` del nodo worker
  - El NodePort service balancea entre los dos pods
  - `upload` en una réplica → `lls` muestra el fichero independientemente de a qué réplica llega la petición
- [x] **Avanzada 2:** 2 workers en nodos distintos + `FileManagerDir` compartido por **NFS** (PV/PVC) ✅
  - `k8s/nfs-pv.yaml` — PersistentVolume apuntando a `172.31.85.178:/data/filemanager`
  - `k8s/nfs-pvc.yaml` — PersistentVolumeClaim `ReadWriteMany 1Gi`
  - Pods distribuidos en `worker-1` y `worker-2` automáticamente por el scheduler
  - Downward API (`status.hostIP`) inyecta la IP del nodo para que cada pod registre su propia dirección en el broker
  - `upload` en una réplica → `lls` devuelve el fichero desde cualquier pod/nodo

---

## Estructura del repositorio

```
.
├── CLAUDE.md                 # Contexto para agentes Claude
├── README.md                 # Este archivo
├── bin/                      # ⚠️ binarios del profesor (NO modificar)
│   ├── brokerFileManager
│   ├── serverFileManager
│   └── clientFileManager
├── broker/
│   └── Dockerfile
├── server/
│   └── Dockerfile
├── scripts/
│   ├── setup-worker.sh        # instala K8s en un nodo worker nuevo
│   └── setup-nfs-server.sh    # configura un servidor NFS dedicado
└── k8s/
    ├── broker-deployment.yaml
    ├── broker-service.yaml    # NodePort 32002
    ├── server-deployment.yaml # 2 réplicas, NFS PVC, Downward API
    ├── server-service.yaml    # NodePort 32001
    ├── nfs-pv.yaml            # PersistentVolume → 172.31.85.178:/data/filemanager
    └── nfs-pvc.yaml           # PersistentVolumeClaim ReadWriteMany 1Gi
```

---

## Entorno local (Arch Linux)

```bash
sudo pacman -S openssh docker docker-buildx kubectl
yay -S aws-cli-v2 aws-session-manager-plugin
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"   # re-loguear después
aws configure                      # credenciales + región
```

---

## Comandos kubectl útiles

```bash
# ── Ver estado general ────────────────────────────────────────────────
kubectl get nodes                      # nodos del clúster y su estado
kubectl get nodes -o wide              # añade IPs y versión del SO
kubectl get pods                       # pods en el namespace default
kubectl get pods -o wide               # añade IP del pod y nodo donde corre
kubectl get svc                        # services y sus puertos
kubectl get pv,pvc                     # PersistentVolumes y Claims (NFS)
kubectl get all                        # todo a la vez (pods, svc, deployments...)

# ── Inspeccionar un objeto ────────────────────────────────────────────
kubectl describe pod <nombre-pod>      # detalle completo: eventos, errores, volúmenes
kubectl describe svc broker-service    # detalle del service (endpoints, NodePort)
kubectl describe pv nfs-pv             # detalle del PV (estado Bound/Available)

# ── Logs y depuración ─────────────────────────────────────────────────
kubectl logs <nombre-pod>              # stdout del contenedor (qué imprimió el binario)
kubectl logs <nombre-pod> --previous   # logs del contenedor anterior si crasheó
kubectl exec -it <nombre-pod> -- bash  # entrar al contenedor interactivamente

# ── Aplicar y eliminar manifiestos ───────────────────────────────────
kubectl apply -f k8s/archivo.yaml      # crea o actualiza el objeto
kubectl apply -f k8s/                  # aplica todos los YAMLs de la carpeta
kubectl delete -f k8s/archivo.yaml     # elimina el objeto
kubectl delete pod <nombre-pod>        # fuerza recreación (Deployment lo levanta solo)

# ── Ver IPs concretas ────────────────────────────────────────────────
kubectl get pods -o wide               # IP pod (10.244.x.x) y nodo donde corre
kubectl get svc                        # ClusterIP del service y NodePort
# → La IP que usa el cliente SIEMPRE es: IP_privada_nodo_worker + NodePort
#   Ejemplo: 172.31.81.126:32002 (broker)  172.31.81.126:32001 (server)
```

---

## Referencia de los manifiestos YAML

### Deployment

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: server-deployment       # nombre del objeto en Kubernetes
spec:
  replicas: 2                   # cuántas copias del pod quiere mantener vivas
  strategy:
    type: Recreate              # mata todos los pods antes de crear los nuevos
                                # (necesario si hay conflicto de puertos/recursos)
  selector:
    matchLabels:
      app: server               # encuentra los pods que gestiona por esta etiqueta
  template:                     # plantilla del pod que crea
    metadata:
      labels:
        app: server             # etiqueta del pod (debe coincidir con selector)
    spec:
      volumes:
        - name: filemanagerdir
          persistentVolumeClaim:
            claimName: nfs-pvc  # monta el PVC (que apunta al NFS)
      containers:
        - name: server
          image: d1n0s/p2-server:latest
          volumeMounts:
            - name: filemanagerdir
              mountPath: /FileManagerDir   # ruta dentro del contenedor
          env:
            - name: MY_NODE_IP
              valueFrom:
                fieldRef:
                  fieldPath: status.hostIP # Downward API: IP del nodo donde corre el pod
          command: ["/bin/sh", "-c"]       # usa shell para poder expandir variables
          args: ["/serverFileManager 10.111.182.40 32002 $MY_NODE_IP 32001"]
          ports:
            - containerPort: 32001
```

### Service (NodePort)

```yaml
apiVersion: v1
kind: Service
metadata:
  name: broker-service
spec:
  type: NodePort          # expone el puerto en TODOS los nodos del clúster
  selector:
    app: broker           # redirige tráfico a los pods con esta etiqueta
  ports:
    - port: 32002         # puerto del Service dentro del clúster (ClusterIP)
      targetPort: 32002   # puerto del contenedor al que llega el tráfico
      nodePort: 32002     # puerto abierto en el nodo físico (el que usa el cliente)
```

> **Tres IPs distintas de un Service:**
> - `ClusterIP` (ej. `10.111.182.40`) — solo accesible dentro del clúster, la usan los pods entre sí
> - `NodePort` (ej. `172.31.81.126:32002`) — accesible desde fuera, la usa el cliente
> - `PodIP` (ej. `10.244.1.3`) — IP directa del pod, solo dentro del clúster

### PersistentVolume (NFS)

```yaml
apiVersion: v1
kind: PersistentVolume
metadata:
  name: nfs-pv
spec:
  capacity:
    storage: 1Gi                  # tamaño nominal (NFS no lo aplica realmente)
  accessModes:
    - ReadWriteMany               # varios pods en varios nodos leen y escriben
  persistentVolumeReclaimPolicy: Retain   # al borrar el PVC, los datos NO se eliminan
  nfs:
    server: 172.31.85.178         # IP del servidor NFS
    path: /data/filemanager       # carpeta exportada por el servidor NFS
```

### PersistentVolumeClaim

```yaml
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: nfs-pvc               # nombre que referencia el Deployment en claimName
spec:
  accessModes:
    - ReadWriteMany            # debe coincidir con el PV para que se vinculen
  resources:
    requests:
      storage: 1Gi             # Kubernetes busca un PV con al menos este tamaño
```

> **Relación PV → PVC → Pod:**
> `nfs-pv` (el almacenamiento real) ←vinculado→ `nfs-pvc` (la reserva) ←montado en→ Pod server

---

## Entrega

- ZIP con los archivos generados/editados (Dockerfiles, YAMLs, scripts, README).
- Cada archivo con el **nombre del alumno** en comentarios al inicio.
- Nombre obligatorio del ZIP: `PR2SISDIS_Alumno1_Apellido1.zip`.
- Trabajo individual. No incluir claves `.pem` ni credenciales AWS.
