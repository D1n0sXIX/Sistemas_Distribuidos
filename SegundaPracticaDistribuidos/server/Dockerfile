FROM ubuntu:20.04
RUN apt-get update
RUN apt-get install -y software-properties-common 
RUN apt-get install -y curl
EXPOSE 32001
COPY serverFileManager /
RUN chmod +x /serverFileManager
RUN mkdir FileManagerDir
COPY resolv.conf /
CMD cp resolv.conf /etc/resolv.conf && /serverFileManager 172.31.31.163 32002 $(curl -s https://api.ipify.org) 32001
