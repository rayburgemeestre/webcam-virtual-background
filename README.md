<img src="https://cppse.nl/webcam-virtual-background.png">

# Linux Virtual Background Webcam

This project is not very polished, as it was originally a sandbox just for myself. I created this about a year ago, but
funnily enough there doesn't really seem to be any Linux software that I'm aware of at least that provides background
blurring, so perhaps this will be useful to others.. but please don't judge me on the quality of the code in this repo
:)

The following project is awesome, it taught me how background segregation works, and that "Tensorflow Lite" was a
thing (never knew), the code is here: https://github.com/Volcomix/virtual-background. Don't forget to check the project
live in the browser: https://volcomix.github.io/virtual-background/. Obviously, I took a lot of inspiration from the
code when I implemented the same thing using `ffmpeg`, but also tried to add some unique features myself. The initial
goal of this project was to achieve the same results with a virtual video device "system wide".

## Features

Will add an overview of features and gifs here later.

## Quick start

* Deploy using "Deploy using Docker", or "Deploy from binaries".
* Run the program, follow usage instructions below. Typically:
    ```
    rayb@ideapad ~> cam
    cam> set-device /dev/video0
    cam> set-mode blur
    cam> start
    ```
* Use the virtual webcam labeled `Virtual 640x480 420P TFlite Camera` in your video conferencing software.

For detailed usage, see: [Usage.md](Usage.md).

## Deploy using Docker

In case `docker` is present on your system, then you can create a simple `cam` wrapper script that starts the program using a container.
However, there is an additional pre-requisite:

    apt install -y v4l2loopback-dkms  # other distros might have a different package name

We can create a script named `cam` that will create two virtual devices and launch the program using a container from docker.io.

```bash
cat << EOF > cam 
# recreate virtual devices
sudo modprobe -r v4l2loopback;
sudo modprobe v4l2loopback video_nr=8,9 exclusive_caps=0,1 card_label="Virtual Temp Camera Input","Virtual 640x480 420P TFlite Camera";
xhost +
# the rest runs inside docker.
docker run --privileged -it -e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix docker.io/rayburgemeestre/virtual-bg:1.0
EOF
chmod +x cam
sudo cp -prv cam /usr/local/bin/  # optionally
```

* If you don't like the `--privileged` flag, you can also discard it and use `--device /dev/video0 --device /dev/video8 --device /dev/video9` (or whatever devices are appropriate in your case).
* The `-e DISPLAY=$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix` flags are also optional, and only needed if you wish to use the `preview` command, to show a window of the virtual camera output.
* The `xhost +` is also needed only for the `preview` command, to allow the container to access the X11 server.
* The `exclusive_caps=0,1` parameter for the virtual devices is important: the `1` makes it available for chrome to use. We don't set the "Virtual Temp Camera Input" device to `1` as it's just an intermediate device to be used by this project only.

## Deploy from binaries

This should work at least on Ubuntu 20.04 and 22.04:

    apt-get update
    apt-get install -y v4l2loopback-dkms make sudo libreadline8 libavfilter7 libxcb-shape0 ffmpeg

    wget https://cppse.nl/release.tar.gz
    tar -xvf release.tar.gz
    cd release
    ./cam


## Compiling from source

Check `make`:

    rayb@ideapad:~/projects/webcam-virtual-background[master]> make
    clean                clean project
    compile              compile project
    configure            configure ffmpeg, tensorflow and mediapipe dependencies
    debug                run project in debug
    device               setup two devices /dev/video8 and /dev/video9
    export               create files in the lib dir
    link                 link /dev/video0 to /dev/video8 with 30fps, YUV420p pixel format and 640x480 resolution
    probe                probe devices
    run2                 run project
    run                  run project

Not all targets are documented as of now, but building on Ubuntu 20.04 (might also work on other distros) should be done as follows.

    make configure    # optional, see below

This is by far the most time consuming step, and likely the most error prone. It downloads and compiles a lot of
dependencies. If you don't want to do this, you can skip this step and try the libraries committed to git in `lib`.
These were compiled on Ubuntu 20.04 by me, and were only 24 MiB anyway.

Then, download https://cppse.nl/spaceship.tar.gz and extract it in the `backgrounds` folder. If you don't care about
the `animated` spaceship background, you can skip this step. (Please note that if you try to use that particular
animated background, the program might crash in that case). Next:

    make compile      # produces `main` binary
    make release      # optionally copy all related binaries and files to `release` directory
    make docker       # optionally create an ubuntu docker image with the `release` directory binaries

The `Dockerfile` can be viewed to see the dependencies. The installed dependencies happen to be exactly the same on
Ubuntu 22.04.

## The models

These originate from this excellent project: https://github.com/Volcomix/virtual-background

You can google the filenames and end up in various discussions online as well. The TL;DR is, they are all in the public
domain. They originated from Google Meet IIRC.

## Links

Background segregation stuff code that helped me with TFlite

* https://github.com/floe/backscrub/issues/28#issuecomment-771445099
* https://github.com/floe/backscrub/blob/master/deepseg.cc#L252-L258

Shout out to:

* https://github.com/spacejake/tflite-cpp - for showing how to use TFlite
* https://gist.github.com/bfraboni/946d9456b15cac3170514307cf032a27 - for the blur code
* https://www.youtube.com/watch?v=1kDQggH30Lg - free virtual background

## Some design decisions

* Reason for the resizing is larger resolutions were a bit heavy on the CPU, at least on the laptop I was using at the time. Code optimizations code improve this.
* Reason for the YUV420p conversion is that I could make the code simpler, I did spend many days at the time to use a higher level ffmpeg API to achieve support for all ffmpeg supported pixel formats. The point would be that different webcams could be hooked up directly, and potentially we don't need `/dev/video8` at all, but didn't manage to do it quickly enough.
