SHELL:=/bin/bash

.PHONY: help
help:
	@grep -E '^[0-9a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-20s\033[0m %s\n", $$1, $$2}'

configure:  ## configure ffmpeg, tensorflow and mediapipe dependencies
	make ffmpeg
	make tf
	make mediapipe

ffmpeg:
	mkdir -p build
	pushd build && \
		tar -xvf ../deps/ffmpeg-4.4.tar.bz2 && \
		sudo apt-get install -y yasm && \
		sudo apt-get install -y nasm && \
		pushd ffmpeg-4.4 && \
			CFLAGS=-fPIC ./configure --prefix=$$PWD/../../ffmpeg --enable-shared --disable-static --disable-libvpx --disable-vaapi --enable-libfreetype && \
			make -j$$(nproc) -s V=0 && \
			make install && \
		popd && \
	popd

tf:
	pushd build && \
		git clone https://github.com/tensorflow/tensorflow -b v2.3.0 && \
			pushd tensorflow && \
			./tensorflow/lite/tools/make/download_dependencies.sh && \
			bazel build //tensorflow/lite:libtensorflowlite.so

mediapipe:
	pushd build && \
		git clone https://github.com/google/mediapipe -b v0.8.7.1
			# no need to compile this one

compile:  ## compile project
	LD_LIBRARY_PATH=$$LD_LIBRARY_PATH:$$PWD/ffmpeg/lib:$$PWD/build/tensorflow/bazel-bin/tensorflow/lite \
	PKG_CONFIG_PATH=$$PWD/ffmpeg/lib/pkgconfig c++ -O0 -g --std=c++11 -I$$PWD/ffmpeg/include -I$$PWD/build/tensorflow/ -I$$PWD/build/tensorflow/third_party/ \
	-I$$PWD/build/mediapipe \
	-I$$PWD/build/tensorflow/tensorflow/lite/tools/make/downloads/flatbuffers/include \
	-L$$PWD/ffmpeg/lib \
	-L$$PWD/build/tensorflow/bazel-bin/tensorflow/lite \
	src/remuxing.cpp src/transpose_conv_bias.cc src/blur_float.cpp \
	-lavdevice -lavformat -lavcodec -lavutil -ltensorflowlite -lswscale \
	-o main

device:  ## setup two devices /dev/video8 and /dev/video9
	sudo modprobe -r v4l2loopback
	# sudo modprobe v4l2loopback video_nr=8,9 exclusive_caps=1,1 card_label="Virtual YUV420P Camera","Virtual 640x480 420P TFlite Camera"
	sudo modprobe v4l2loopback video_nr=7,8,9 exclusive_caps=1,1,1 card_label="Dummy Camera","Virtual YUV420P Camera","Virtual 640x480 420P TFlite Camera"

link:  ## link /dev/video0 to /dev/video8 with 30fps, YUV420p pixel format and 640x480 resolution
	ffmpeg -i /dev/video6 -f v4l2 -input_format mjpeg -framerate 10 -video_size 1024x680 -vf scale=640:480:force_original_aspect_ratio=increase,crop=640:480 -pix_fmt yuv420p -f v4l2 /dev/video8

run:  ## run project
	LD_LIBRARY_PATH=$$LD_LIBRARY_PATH:$$PWD/ffmpeg/lib:$$PWD/build/tensorflow/bazel-bin/tensorflow/lite \
		./main /dev/video2 /dev/video9 2 1

debug:  ## run project in debug
	LD_LIBRARY_PATH=$$LD_LIBRARY_PATH:$$PWD/ffmpeg/lib:$$PWD/build/tensorflow/bazel-bin/tensorflow/lite \
		gdb --args ./main /dev/video2 /dev/video9 3 1

clean:  ## clean project
	rm -rf build
	rm -rf ffmpeg
	# rm -rf ~/.cache/bazel

format:
	clang-format -i src/remuxing.cpp
