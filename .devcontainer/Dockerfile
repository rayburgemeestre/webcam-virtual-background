FROM mcr.microsoft.com/devcontainers/cpp:0-debian-11

ARG REINSTALL_CMAKE_VERSION_FROM_SOURCE="3.22.2"
FROM ubuntu:latest

RUN apt-get update && apt-get install -y python3-pip 
RUN apt-get install curl -y 
RUN curl -sSL https://deb.nodesource.com/setup_18.x | bash -
RUN apt-get install aptitude -y
RUN aptitude install -y npm 
RUN apt-get install nodejs
RUN python3 -m pip install numpy
RUN npm install -g @bazel/bazelisk 
RUN apt-get install -y yasm 
RUN apt-get install -y nasm 
RUN apt-get install -y libreadline-dev 
# Tensorflow
WORKDIR /home
RUN apt-get install gdb -y
RUN apt-get install cmake -y
RUN apt-get install git -y
RUN git clone https://github.com/tensorflow/tensorflow 
WORKDIR /home/tensorflow 
RUN bazel build //tensorflow/lite:libtensorflowlite.so 
RUN mkdir /home/tensorflow/build
WORKDIR /home/tensorflow/build
RUN cmake ../tensorflow/lite
RUN make install
WORKDIR /home
#pipepyer
RUN git clone https://github.com/google/mediapipe -b v0.8.7.1 
# cpp-readline
RUN git clone https://github.com/Svalorzen/cpp-readline || true 
WORKDIR /home/cpp-readline
RUN mkdir -p build 
WORKDIR /home/cpp-readline/build
RUN cmake .. 
RUN make -j 8 
WORKDIR /home
## Tiny-Process-Library
RUN git clone https://gitlab.com/eidheim/tiny-process-library || true 
WORKDIR /home/tiny-process-library
RUN mkdir build
WORKDIR /home/tiny-process-library/build
RUN	cmake .. 
RUN	make -j 8
## ffmpeg
WORKDIR /home
RUN curl -LO https://ffmpeg.org/releases/ffmpeg-4.4.tar.bz2
RUN tar -xvf ffmpeg-4.4.tar.bz2
WORKDIR /home/ffmpeg-4.4
RUN apt-get install libfreetype6-dev libfontconfig1-dev xclip -y
RUN CFLAGS=-fPIC ./configure --prefix=$$PWD/../../ffmpeg --enable-shared --disable-static --disable-libvpx --disable-vaapi --enable-libfreetype 
RUN make  
RUN make install 
RUN apt-get install ffmpeg -y 




# Optionally install the cmake for vcpkg
COPY ./reinstall-cmake.sh /tmp/

RUN if [ "${REINSTALL_CMAKE_VERSION_FROM_SOURCE}" != "none" ]; then \
    chmod +x /tmp/reinstall-cmake.sh && /tmp/reinstall-cmake.sh ${REINSTALL_CMAKE_VERSION_FROM_SOURCE}; \
    fi \
    && rm -f /tmp/reinstall-cmake.sh

# [Optional] Uncomment this section to install additional vcpkg ports.
# RUN su vscode -c "${VCPKG_ROOT}/vcpkg install <your-port-name-here>"

# [Optional] Uncomment this section to install additional packages.
# RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
#     && apt-get -y install --no-install-recommends <your-package-list-here>
