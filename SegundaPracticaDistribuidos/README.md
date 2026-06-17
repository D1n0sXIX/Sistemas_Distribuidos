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

## Arquitectura en Kubernetes

El **control-plane no interviene en el flujo de la app** — solo gestiona el clúster (lo usas tú con `kubectl`). La app corre entera en el nodo worker.

```
┌─────────────────────── CLÚSTER KUBERNETES ──────────────────────────┐
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │  NODO 1 — EC2 (control-plane)                                │    │
│  │  Cerebro de K8s (API, scheduler...). Solo gestión.           │    │
│  │  Tu kubectl habla con él. La app NO corre aquí.              │    │
│  └──────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │  NODO 2 — EC2 (worker)                                       │    │
│  │                                                              │    │
│  │   ┌─────────────────────┐   ┌─────────────────────┐         │    │
│  │   │  POD (broker)       │   │  POD (server)       │         │    │
│  │   │  ┌───────────────┐  │   │  ┌───────────────┐  │         │    │
│  │   │  │ contenedor    │  │   │  │ contenedor    │  │         │    │
│  │   │  │ brokerFile-   │  │   │  │ serverFile-   │  │         │    │
│  │   │  │ Manager       │  │   │  │ Manager       │  │         │    │
│  │   │  └───────────────┘  │   │  └───────────────┘  │         │    │
│  │   └─────────────────────┘   └─────────────────────┘         │    │
│  └──────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────────┘
```

### Flujo de una conexión cliente → servidor

```
Tu Arch Linux (clientFileManager)
     │
     │ 1. conecta al BROKER (puerto 32002)
     ▼
EC2 worker — Pod broker
     │
     │ 2. "el server está en IP:puerto X"  (solo un directorio, no redirige tráfico)
     │
     ◀───
     │
     │ 3. conecta DIRECTAMENTE al SERVER (puerto 32001)
     ▼
EC2 worker — Pod server
     │
     │ 4. lls / upload / download
```

> El broker **no actúa de intermediario** en las transferencias — solo es un directorio de conexión. Una vez que el cliente recibe la IP:puerto del server, habla directamente con él.

---

## Seguimiento por fases

### Fase 0 — Familiarización (sin K8s) ✅
- [x] Probar los 3 binarios "a pelo" en local (3 terminales: broker, server, cliente)
- [x] Verificar `ls`, `lls`, `upload`, `download`, `exit()` end-to-end
- [x] Confirmar que `brokerFileManager` usa 32002 fijo
- [x] Confirmar que `serverFileManager` necesita `FileManagerDir/` junto al ejecutable
- [ ] Repetir en EC2 para verificar conectividad real entre instancias

### Fase 1 — Imágenes Docker
- [ ] `Dockerfile.server` (base `ubuntu:20.04`, copia binario, `mkdir FileManagerDir`, EXPOSE 32001)
- [ ] `Dockerfile.broker` (análogo, EXPOSE 32002, sin `FileManagerDir`)
- [ ] Construir y probar ambos contenedores en una EC2

### Fase 2 — Distribución de imagen
- [ ] Elegir estrategia de registry (Docker Hub / registry local / save+load)
- [ ] Publicar imágenes y verificar `pull` desde otro nodo

### Fase 3 — Clúster kubeadm
- [ ] Maquina1 = control-plane (`kubeadm init`)
- [ ] Instalar CNI (Flannel/Calico)
- [ ] ≥1 nodo esclavo unido (`kubeadm join`)
- [ ] (idealmente) nodo dedicado para el broker
- [ ] Security Groups: 32001, 32002, NodePort 30000–32767, puertos K8s, SSH
- [ ] `kubectl get nodes -o wide` todos en `Ready`

### Fase 4 — Deployments + Services
- [ ] Deployment + Service del broker (alcanzable por servers y cliente)
- [ ] Deployment + Service del server
- [ ] Resolver el autorregistro de IP/puerto (decidir hostNetwork vs NodePort — ver §6 `CLAUDE.md`)
- [ ] Verificar en `kubectl logs` la dirección que registra el server

### Fase 5 — Demo end-to-end
- [ ] Cliente → broker → server
- [ ] `upload` y `download` de varios ficheros
- [ ] Varias conexiones simultáneas
- [ ] Repetir la demo varias veces (robustez)

### Fase 6 — Configuraciones avanzadas (examen / 10 puntos)
- [ ] **Avanzada 1:** varias réplicas en un nodo + `FileManagerDir` compartido por `hostPath`
- [ ] **Avanzada 2:** varios nodos esclavos + `FileManagerDir` compartido por **NFS** (PV/PVC)
- [ ] Verificar que un `upload` en una réplica se ve con `lls` desde otra

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
└── k8s/
    ├── broker-deployment.yaml
    ├── broker-service.yaml
    ├── server-deployment.yaml
    ├── server-service.yaml
    └── nfs/                   # (avanzada 2) PV/PVC NFS
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

## Entrega

- ZIP con los archivos generados/editados (Dockerfiles, YAMLs, scripts, README).
- Cada archivo con el **nombre del alumno** en comentarios al inicio.
- Nombre obligatorio del ZIP: `PR2SISDIS_Alumno1_Apellido1.zip`.
- Trabajo individual. No incluir claves `.pem` ni credenciales AWS.
