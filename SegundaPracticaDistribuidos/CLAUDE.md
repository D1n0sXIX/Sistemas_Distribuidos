# CLAUDE.md — Práctica 2: Despliegue con Docker + Kubernetes en AWS

> Archivo de contexto para agentes de Claude (Claude Code / Cowork).
> Asignatura: Programación de Sistemas Distribuidos. Alumno: **D1n0sXIX** (rellenar nombre/apellidos reales en la entrega).
> Entorno de desarrollo del alumno: **Arch Linux**. Infraestructura: **AWS EC2** + **Kubernetes (kubeadm)**.

---

## 0. Reglas de colaboración (LEER PRIMERO)

- **El alumno aprende haciendo.** El rol de Claude es **explicar conceptos, dar pistas, señalar errores y orientar** — NO entregar soluciones completas (manifiestos YAML, Dockerfiles o comandos de despliegue listos para copiar) salvo que se pidan de forma explícita. Antes de dar código, preguntar o proponer el enfoque.
- **Los binarios NO se modifican.** `brokerFileManager`, `serverFileManager` y `clientFileManager` vienen precompilados por el profesor y se usan tal cual. El trabajo del alumno es **empaquetar, distribuir y desplegar**, no programar C++.
- Mantener documentación viva: este `CLAUDE.md` y un `README.md` con seguimiento por fases (estilo Práctica 1).
- Ante una duda de diseño, primero razonar el "por qué" (red, alcanzabilidad, almacenamiento) y luego el "cómo".

---

## 1. Objetivo de la práctica

Desplegar una aplicación distribuida (broker + servidores de ficheros + cliente) sobre un **clúster de Kubernetes** montado en instancias **EC2**, usando **imágenes Docker** para los programas servidor. Puntos obligatorios para aprobar:

1. Creación de imágenes Docker (broker y server).
2. Replicación de la imagen a lo largo del clúster.
3. Uso de Kubernetes para gestionar réplicas y balanceo de carga.

Configuraciones avanzadas (para optar al 10, ver §9):
- Avanzada 1: varias réplicas en **un solo nodo** esclavo → carpeta compartida con `hostPath`.
- Avanzada 2: varios **nodos** esclavos → carpeta compartida en red con **NFS**.

---

## 1.b Estructura del proyecto (orientación para el agente)

```
.
├── CLAUDE.md                 # Este archivo (contexto)
├── README.md                 # Seguimiento por fases
├── docker/
│   ├── Dockerfile.broker     # imagen del broker (32002)
│   ├── Dockerfile.server     # imagen del server (32001 + FileManagerDir)
│   └── resolv.conf           # solo si se usa curl api.ipify.org
├── k8s/                      # manifiestos (deployments/services/PV/PVC)
└── bin/                      # ⚠️ binarios del profesor — NO MODIFICAR
    ├── brokerFileManager
    ├── serverFileManager
    └── clientFileManager
```

Reglas para el agente al operar en el repo:
- **Nunca** editar ni recompilar nada en `bin/`. Son artefactos precompilados intocables.
- El trabajo editable son `docker/`, `k8s/`, scripts y la documentación.
- Antes de generar un YAML/Dockerfile completo, proponer el enfoque y dejar que el alumno lo escriba (ver §0).

---

## 2. Arquitectura del sistema (cómo se hablan los 3 programas)

```
                 (1) registra IP/Puerto
   serverFileManager ───────────────────────▶  brokerFileManager
        ▲  (escucha 32001)                          ▲ (escucha 32002)
        │                                            │
        │ (3) conexión DIRECTA cliente↔server        │ (2) pide datos
        │     usando la IP/Puerto registrados        │     de conexión
        │                                            │
        └────────────────── clientFileManager ───────┘
```

1. **Arranca primero el broker.** Se queda esperando conexiones en el puerto **32002**.
2. Cada **server** arranca pasándole por `argv` la IP del broker; se conecta y le comunica **su propia IP/Puerto de conexión**. Luego espera clientes en **32001**.
3. El **cliente** se conecta al broker (32002), recibe los datos de conexión de los servidores y **se conecta directamente** a ellos para `ls/lls/upload/download`.

> ⚠️ **La pieza crítica:** el server **se autorregistra** en el broker con una dirección (IP/Puerto). El cliente intentará conectarse **exactamente a esa dirección**. Por tanto, *lo que el server registre debe ser alcanzable por el cliente*. Todo el diseño de red de la práctica gira en torno a esto (ver §6).

---

## 3. Contratos de los binarios (VERIFICADO con `strings`/Dockerfile de ejemplo)

| Programa | Invocación | Puerto que abre | Notas |
|---|---|---|---|
| `brokerFileManager` | `./brokerFileManager` (arranca el 1º; usa por defecto **32002**; confirmar si admite puerto por `argv` en la 1ª prueba) | **32002** | Solo espera y reparte datos de conexión. No usa `FileManagerDir`. |
| `serverFileManager` | `./serverFileManager <BROKER_IP> <BROKER_PORT> <PUBLIC_IP> <PUBLIC_PORT>` | **32001** | Necesita una carpeta `FileManagerDir` **junto al ejecutable**. `<PUBLIC_IP> <PUBLIC_PORT>` = la dirección que registra en el broker y a la que el cliente se conectará. |
| `clientFileManager` | `./clientFileManager <BROKER_IP> <BROKER_PORT>` | — | Comandos: `ls`, `lls`, `upload <fichero>`, `download <fichero>`, `exit()`. |

Cadenas de uso reales extraídas de los binarios:
- `Usage: serverFileManager <BROKER IP> <BROKER PORT> <PUBLIC IP> <PUBLIC PORT>`
- `Usage: clientFileManager <BROKER IP> <BROKER PORT>`

**Comandos del cliente** (lado usuario):
- `ls` — lista ficheros **locales** al cliente.
- `lls` — lista ficheros del **servidor** (carpeta `FileManagerDir`).
- `upload <fichero_local>` — copia un fichero del cliente al servidor.
- `download <fichero_remoto>` — copia un fichero del servidor al cliente.
- `exit()` — cierra el cliente.

**Dependencias de los binarios** (importantes para elegir imagen base):
- Dinámicamente enlazados: `libpthread`, `libstdc++.so.6`, `libgcc_s`, `libc`.
- Requieren `GLIBC ≥ 2.14` y `GLIBCXX_3.4.22` → **`ubuntu:20.04` funciona** (es la base del Dockerfile de ejemplo). Evitar bases demasiado nuevas/antiguas o `alpine` (musl, no glibc).

---

## 4. Dockerfile de ejemplo del profesor (analizado)

El zip `DockerfileCurl` trae un Dockerfile de referencia **para el server**:

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
    /serverFileManager 172.31.31.163 32002 $(curl -s https://api.ipify.org) 32001
```

Lecciones de este Dockerfile:
- `172.31.31.163` → IP **privada de VPC** del broker (rango por defecto de AWS `172.31.0.0/16`). Las instancias del mismo VPC se ven por IP privada.
- `$(curl -s https://api.ipify.org)` → el server descubre su **IP pública de internet** y la registra. Esto sirve en *Docker sobre EC2 con puerto mapeado 1:1*, pero **en Kubernetes el pod no tiene esa IP** → hay que repensarlo (ver §6).
- `resolv.conf` con `nameserver 8.8.8.8` es un parche de DNS para que `curl` resuelva dentro del contenedor.
- `EXPOSE 32001` es solo documentativo; el puerto real se decide en el `Service`/red del pod.

> El broker necesita su **propio Dockerfile** (análogo, sin `FileManagerDir`, exponiendo **32002**). Es trabajo del alumno escribirlo.

---

## 5. Entorno Arch Linux — herramientas y para qué (AWS + K8s)

Instalación de referencia (revisar nombres de paquete antes de ejecutar; algunos están en AUR):

```bash
# --- Acceso a AWS ---
sudo pacman -S openssh                 # SSH a las instancias EC2 (clave .pem)
yay -S aws-cli-v2                       # AWS CLI v2 (AUR). Gestionar EC2/VPC desde terminal
yay -S aws-session-manager-plugin       # (opcional) SSM Session Manager, conectar sin abrir SSH

# --- Docker (construir imágenes localmente) ---
sudo pacman -S docker docker-buildx
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"         # re-loguear después

# --- Kubernetes (cliente local) ---
sudo pacman -S kubectl                   # kubectl para hablar con el clúster
yay -S helm                              # (opcional) si se usan charts
```

Notas Arch:
- `aws-cli-v2` y `session-manager-plugin` viven en **AUR** → usar `yay`/`paru`.
- `kubeadm`, `kubelet` y el runtime de contenedores se instalan **en las instancias EC2** (que serán Ubuntu/Amazon Linux), **no** en Arch. Arch es la estación de trabajo: construir imágenes, `kubectl`, SSH.
- Configurar credenciales: `aws configure` (access key, secret, región, p.ej. `eu-west-1`/Madrid sería `eu-south-2`).

---

## 6. EL RETO CENTRAL: alcanzabilidad de la dirección registrada

Recordatorio: el server registra `<PUBLIC_IP> <PUBLIC_PORT>` en el broker, y el cliente se conecta **a esa dirección literal**. En Kubernetes esto NO es trivial porque:
- Cada pod tiene una IP **interna del clúster** (`10.x` / CNI), no alcanzable desde fuera.
- Los `Service` exponen pods, pero introducen NAT/puertos distintos.

Decisión de diseño que el alumno debe tomar y razonar (no asumir una sin pensarla):

| Opción | Idea | Pros | Contras |
|---|---|---|---|
| **A. `hostNetwork: true` en el pod server** | El pod usa la red del nodo; el server descubre la IP del nodo y registra `IP_nodo:32001` | Lo más cercano al Dockerfile de ejemplo; la dirección registrada es directamente alcanzable | Choca si hay varias réplicas en el mismo nodo (puerto 32001 colisiona) |
| **B. `NodePort` + registrar `IP_nodo:NodePort`** | El server bindea 32001 dentro; se publica vía NodePort (30000–32767); registra el NodePort | Soporta varias réplicas, balanceo de Service | Hay que **inyectar** el NodePort y la IP del nodo como `PUBLIC_PORT/PUBLIC_IP` (Downward API / env / initContainer) |
| **C. Cliente dentro del clúster** | El cliente corre como pod/nodo y usa IPs internas/Service DNS | Evita exposición externa | El enunciado sugiere cliente desde el equipo del alumno o una EC2 |

Preguntas guía para elegir:
1. ¿Desde **dónde** corre el cliente? (tu Arch local fuera del VPC / una EC2 dentro del VPC / un pod). Eso decide qué IP debe registrar el server: pública de internet, privada de VPC, o de clúster.
2. ¿Cuántas **réplicas** del server por nodo? (define si el puerto puede ser fijo 32001 o necesitas NodePort).
3. ¿El broker debe ser alcanzable por el cliente externo? Sí → necesita exposición (NodePort/LoadBalancer); además debe ser alcanzable por los servers (Service ClusterIP interno vale para server→broker).

> Sugerencia de arranque (config básica, 1 réplica): empezar por la **Opción A** (`hostNetwork`) porque reproduce el comportamiento del Dockerfile de ejemplo y aísla el problema de red. Al pasar a réplicas múltiples (§9) reconsiderar A→B.

---

## 7. Puertos y Security Groups en AWS (causa nº1 de "no conecta")

El clúster kubeadm y la app necesitan que los **Security Groups** del VPC permitan, **entre nodos** y desde el cliente cuando aplique:

- App: **32001** (server), **32002** (broker).
- Rango **NodePort**: `30000–32767` (TCP) si se usa la opción B.
- Kubernetes control-plane / kubelet (entre nodos): `6443` (API), `2379–2380` (etcd), `10250` (kubelet), `10257`, `10259`.
- CNI (según plugin): Flannel VXLAN `8472/UDP`; Calico `179/TCP` (BGP) + IP-in-IP.
- SSH `22` (solo desde tu IP).

> Regla práctica: para tráfico entre nodos del clúster, permitir todo el tráfico **dentro del propio Security Group** (source = el mismo SG). Exponer al exterior solo lo justo (SSH desde tu IP, y el/los puerto(s) que el cliente externo necesite).

---

## 8. Fases del despliegue (hitos, estilo Práctica 1)

- [ ] **Fase 0 — Familiarización (sin K8s).** Probar los 3 binarios "a pelo" entre **2 instancias EC2** (o local): arrancar broker, luego server (pasándole IP broker + su IP/puerto), luego cliente. Verificar `lls/upload/download`. Esto valida los contratos y la red antes de meter Docker/K8s.
- [ ] **Fase 1 — Imágenes Docker.** Escribir Dockerfile del **server** (base `ubuntu:20.04`, copiar binario, `mkdir FileManagerDir`, exponer 32001) y del **broker** (exponer 32002). Construir y probar los contenedores en una EC2.
- [ ] **Fase 2 — Distribución de imagen.** Publicar en un registry (Docker Hub o registry local) para que todos los nodos puedan hacer `pull` (ver §10).
- [ ] **Fase 3 — Clúster kubeadm.** Maquina1 = control-plane; ≥1 nodo esclavo (+ idealmente nodo broker). Instalar runtime + kubeadm/kubelet/kubectl en las EC2, `kubeadm init`, CNI, `kubeadm join`.
- [ ] **Fase 4 — Deployments + Services.** Deployment del broker (+ Service para que servers y cliente lo alcancen) y Deployment del server (+ Service/exposición según §6). Resolver el autorregistro de IP/puerto.
- [ ] **Fase 5 — Demo end-to-end.** Cliente → broker → server(s). Subir/bajar varios ficheros, varias conexiones. Probar **varias veces**.
- [ ] **Fase 6 — Avanzadas (opcional/examen).** `hostPath` (mismo nodo) y/o NFS (varios nodos) para `FileManagerDir` compartido. Ver §9.

---

## 9. Configuraciones avanzadas (almacenamiento compartido)

El `FileManagerDir` de cada réplica del server debe **verse igual** desde cualquier pod (no volátil), si no, cada cliente vería ficheros distintos según a qué réplica le toque.

- **Avanzada 1 — varias réplicas en UN nodo:** montar `FileManagerDir` desde un **`hostPath`** del nodo. Todas las réplicas del mismo nodo comparten esa carpeta. (Recordar el problema de puerto de §6 al replicar.)
- **Avanzada 2 — varios NODOS esclavos:** `hostPath` ya no sirve (cada nodo es distinto). Montar un **volumen NFS** (PersistentVolume/PVC con NFS) accesible desde todos los nodos. Guía de referencia citada por el profesor: instalación/config de volúmenes NFS en Kubernetes (jorgedelacruz.es). Probar que un `upload` en una réplica se ve con `lls` desde otra.

---

## 10. Distribución de la imagen por el clúster

"Replicar la imagen a lo largo del clúster" → cada nodo debe poder obtener la imagen. Opciones:
- **Docker Hub** (más simple): `docker tag` + `docker push usuario/imagen:tag`; en los manifiestos usar esa referencia; Kubernetes hace `pull` en cada nodo.
- **Registry privado / local** (`registry:2`) si no se quiere repo público.
- **`docker save` + `docker load`** en cada nodo (manual, no escalable, válido para demo pequeña).
- Si se construye **en cada nodo**, usar `imagePullPolicy: IfNotPresent` o `Never` y asegurar que el runtime ve la imagen local.

---

## 11. Entrega (según enunciado)

- ZIP con los archivos generados/editados por el alumno (Dockerfiles, manifiestos YAML, scripts, README). Estructura libre.
- **Cada archivo** debe incluir, en comentarios al inicio, el **nombre del alumno**.
- Nombre del ZIP **obligatorio**: `PR2SISDIS_Alumno1_Apellido1.zip`.
- Realizar el ejercicio de forma individual. (Fecha de entrega/examen según indique el profesor en el enunciado vigente.)

---

## 12. Cheatsheet rápido

```bash
# Conectar a EC2
ssh -i clave.pem ubuntu@<IP_PUBLICA_EC2>

# Construir y publicar imagen (desde Arch)
docker build -t <usuario>/p2-server:latest -f Dockerfile.server .
docker push <usuario>/p2-server:latest

# Clúster
kubectl get nodes -o wide
kubectl get pods -o wide
kubectl get svc
kubectl logs <pod>
kubectl describe pod <pod>
kubectl exec -it <pod> -- bash

# Probar binarios sin K8s (familiarización)
./brokerFileManager                                   # nodo broker (32002)
./serverFileManager <IP_BROKER> 32002 <MI_IP> 32001   # nodo server (necesita FileManagerDir/)
./clientFileManager <IP_BROKER> 32002                 # cliente
```

---

## 13. Cosas a NO olvidar / errores típicos

- `FileManagerDir` debe existir **junto al binario del server**; si falta, `lls/upload/download` fallan.
- La IP/Puerto que el server registra **debe ser alcanzable por el cliente** (es el bug nº1). Verificar con `kubectl logs` qué dirección registró.
- Security Groups: si "conecta el broker pero no el server", casi siempre es el SG/NodePort.
- Base de imagen con **glibc** (no Alpine) por las dependencias de los binarios.
- DNS dentro del contenedor (el `resolv.conf` con 8.8.8.8) solo hace falta si usas `curl api.ipify.org`; con otra estrategia de IP puede sobrar.
- No subir las claves `.pem` ni credenciales AWS al ZIP/GitHub.
