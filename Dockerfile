# syntax=docker/dockerfile:1
FROM ubuntu:22.04

# install app dependencies
RUN apt-get update && apt-get install -y libcrypto++8 libcrypto++-utils libcrypto++-dev g++ make

# install app
COPY src /src
COPY Makefile /
COPY benchmark.sh /
COPY docker_script.sh /

RUN mkdir build
RUN make
RUN chmod 755 benchmark.sh

ENTRYPOINT ["./docker_script.sh"]