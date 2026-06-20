# Version de la maquina
FROM ubuntu:20.04
# Ejecutamos update para evitar errores
RUN apt-get update
# Exponemos el puerto del BROKER
EXPOSE 32002
# Copiamos el binario del broker y le damos permisos
COPY brokerFileManager /
RUN chmod +x /brokerFileManager
# Ejecutamos el binario
CMD ["/brokerFileManager"]