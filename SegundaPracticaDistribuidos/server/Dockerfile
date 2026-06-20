#Version de la maquina
FROM ubuntu:20.04
# Instalamos servicios necesarios
RUN apt-get update
RUN apt-get install -y software-properties-common
RUN apt-get install -y curl
# Exponemos el puerto
EXPOSE 32001
# Copiamos el binario y le damos permisos
COPY serverFileManager /
RUN chmod +x /serverFileManager
# Creamos la carpeta donde se alojara el servicio de ficheros
RUN mkdir FileManagerDir

# Copiamos el archivo de configuracion del DNS + ejecutamos el binario conn el peurto del broker
COPY resolv.conf /
CMD cp resolv.conf /etc/resolv.conf && /serverFileManager 172.31.81.126 32002 $(curl -s https://api.ipify.org) 32001