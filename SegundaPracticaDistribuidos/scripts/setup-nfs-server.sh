#!/usr/bin/env bash
# ============================================================
# setup-nfs-server.sh
# Alumno: D1n0sXIX
#
# Configura una EC2 Ubuntu como servidor NFS dedicado para la
# Práctica 2. Exporta /data/filemanager a toda la VPC de AWS
# (172.31.0.0/16) para que los pods del server puedan montarlo.
#
# Uso: sudo bash setup-nfs-server.sh
# ============================================================

set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

NFS_DIR="/data/filemanager"
VPC_CIDR="172.31.0.0/16"

# ── Comprobación de root ──────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
  echo "[ERROR] Ejecuta el script con sudo: sudo bash setup-nfs-server.sh"
  exit 1
fi

echo ""
echo "============================================================"
echo "  setup-nfs-server.sh — Servidor NFS para Práctica 2"
echo "============================================================"
echo ""

# ── 1. Instalar nfs-kernel-server ─────────────────────────────
echo "==> [1/4] Instalando nfs-kernel-server..."
apt-get update -y -q
apt-get install -y -q nfs-kernel-server

# ── 2. Crear directorio compartido ────────────────────────────
echo "==> [2/4] Creando directorio compartido: ${NFS_DIR}..."
mkdir -p "${NFS_DIR}"
# nobody:nogroup permite que los contenedores (que no tienen usuario fijo)
# puedan leer y escribir sin problemas de permisos
chown nobody:nogroup "${NFS_DIR}"
chmod 777 "${NFS_DIR}"

# ── 3. Configurar /etc/exports ────────────────────────────────
echo "==> [3/4] Configurando /etc/exports..."
# rw          → lectura y escritura
# sync        → escribe en disco antes de confirmar (más seguro)
# no_subtree_check → evita errores si el directorio se mueve
# no_root_squash   → el root del cliente conserva permisos de root en NFS
if grep -q "${NFS_DIR}" /etc/exports; then
  echo "     (entrada ya existente en /etc/exports, se sobreescribe)"
  sed -i "\|${NFS_DIR}|d" /etc/exports
fi
echo "${NFS_DIR} ${VPC_CIDR}(rw,sync,no_subtree_check,no_root_squash)" >> /etc/exports

# ── 4. Arrancar y habilitar el servicio ───────────────────────
echo "==> [4/4] Arrancando servidor NFS..."
exportfs -ra
systemctl restart nfs-kernel-server
systemctl enable nfs-kernel-server

# ── Verificación rápida ───────────────────────────────────────
echo ""
echo "  Exportaciones activas:"
exportfs -v

# ── Listo ─────────────────────────────────────────────────────
NFS_IP=$(hostname -I | awk '{print $1}')
echo ""
echo "============================================================"
echo "  Servidor NFS listo."
echo ""
echo "  IP privada de esta máquina: ${NFS_IP}"
echo "  Directorio exportado:       ${NFS_DIR}"
echo ""
echo "  Anota la IP privada — la necesitarás en el"
echo "  PersistentVolume de Kubernetes (k8s/nfs-pv.yaml)."
echo ""
echo "  Para verificar desde otro nodo del VPC:"
echo "    showmount -e ${NFS_IP}"
echo "============================================================"
