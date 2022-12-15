FROM docker.io/ubuntu:20.04

LABEL maintainer="Ray Burgemeestre <ray@cppse.nl>"

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install --no-install-recommends -y v4l2loopback-dkms make sudo libreadline8 libavfilter7 libxcb-shape0 ffmpeg && \
    apt-get clean && \
    apt-get autoremove && \
    rm -rf /var/lib/apt/lists/*

COPY virtual-bg-1.0 /release

ENTRYPOINT ["/bin/sh", "-c", "cd /release; ./main"]
