#!/usr/bin/env bash
# ============================================================
# setup-worker.sh
# Alumno: D1n0sXIX
#
# Prepara un nodo Ubuntu (EC2) para unirlo al clúster Kubernetes
# de la Práctica 2 como worker.
# Instala: containerd, kubeadm, kubelet, kubectl v1.29.15
#
# Uso: sudo bash setup-worker.sh
# ============================================================

set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

KUBE_VERSION="1.29.15-1.1"

# ── Comprobación de root ──────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
  echo "[ERROR] Ejecuta el script con sudo: sudo bash setup-worker.sh"
  exit 1
fi

echo ""
echo "============================================================"
echo "  setup-worker.sh — Kubernetes v1.29.15 worker node setup"
echo "============================================================"
echo ""

# ── 1. Módulos del kernel ─────────────────────────────────────
echo "==> [1/6] Cargando módulos del kernel (overlay, br_netfilter)..."
cat <<EOF > /etc/modules-load.d/k8s.conf
overlay
br_netfilter
EOF
modprobe overlay
modprobe br_netfilter

# ── 2. Parámetros sysctl ──────────────────────────────────────
echo "==> [2/6] Configurando sysctl para Kubernetes..."
cat <<EOF > /etc/sysctl.d/k8s.conf
net.bridge.bridge-nf-call-iptables  = 1
net.bridge.bridge-nf-call-ip6tables = 1
net.ipv4.ip_forward                 = 1
EOF
sysctl --system > /dev/null

# ── 3. Containerd ─────────────────────────────────────────────
apt-get update -y -q
apt-get install -y -q ca-certificates curl gnupg apt-transport-https

install -m 0755 -d /etc/apt/keyrings

if command -v containerd &>/dev/null; then
  echo "==> [3/6] containerd ya instalado ($(containerd --version | awk '{print $3}')), omitiendo instalacion..."
else
  echo "==> [3/6] Instalando containerd..."
  install -m 0755 -d /etc/apt/keyrings
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
    | gpg --dearmor -o /etc/apt/keyrings/docker.gpg
  chmod a+r /etc/apt/keyrings/docker.gpg

  echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] \
https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "$VERSION_CODENAME") stable" \
    > /etc/apt/sources.list.d/docker.list

  apt-get update -y -q
  apt-get install -y -q containerd.io
fi

# ── 4. Configurar containerd con SystemdCgroup ────────────────
echo "==> [4/6] Configurando containerd (SystemdCgroup=true)..."
containerd config default > /etc/containerd/config.toml
sed -i 's/SystemdCgroup = false/SystemdCgroup = true/' /etc/containerd/config.toml
systemctl restart containerd
systemctl enable containerd

# ── 5. Kubeadm / Kubelet / Kubectl ───────────────────────────
echo "==> [5/6] Instalando kubeadm, kubelet, kubectl v${KUBE_VERSION}..."
curl -fsSL https://pkgs.k8s.io/core:/stable:/v1.29/deb/Release.key \
  | gpg --dearmor -o /etc/apt/keyrings/kubernetes-apt-keyring.gpg

echo "deb [signed-by=/etc/apt/keyrings/kubernetes-apt-keyring.gpg] \
https://pkgs.k8s.io/core:/stable:/v1.29/deb/ /" \
  > /etc/apt/sources.list.d/kubernetes.list

apt-get update -y -q
apt-get install -y -q \
  kubelet="${KUBE_VERSION}" \
  kubeadm="${KUBE_VERSION}" \
  kubectl="${KUBE_VERSION}"

# Fijar versiones para que apt no las actualice solo
apt-mark hold kubelet kubeadm kubectl

systemctl enable kubelet

# ── 6. Desactivar swap ────────────────────────────────────────
echo "==> [6/6] Desactivando swap (requisito de Kubernetes)..."
swapoff -a
# Eliminar entradas de swap en fstab para que no vuelva tras reinicio
sed -i '/\bswap\b/d' /etc/fstab

# ── Listo ─────────────────────────────────────────────────────
echo ""
echo "============================================================"
echo "  Instalacion completada."
echo ""
echo "  Para unir este nodo al cluster:"
echo ""
echo "  1. En el control-plane (172.31.85.97) ejecuta:"
echo "       kubeadm token create --print-join-command"
echo ""
echo "  2. Copia el comando que devuelve y ejecutalo aqui con sudo."
echo "     Tendra esta pinta:"
echo "       sudo kubeadm join 172.31.85.97:6443 --token <token> \\"
echo "         --discovery-token-ca-cert-hash sha256:<hash>"
echo ""
echo "  3. Verifica desde Arch que el nodo esta Ready:"
echo "       kubectl get nodes -o wide"
echo "============================================================"
