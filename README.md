Make sure you have : v4l2loopback-dkms installed.

Full list of dependencies, for both Ubuntu 20.04 and 22.04:

apt install v4l2loopback-dkms make sudo libreadline8 libavfilter7 libxcb-shape0

Tested inside the ubuntu:20.04 and ubuntu:22.04 docker images, after the above dependencies
all libraries (check with: ldd main) should be found.

--

See Code & Makefile for details.

https://github.com/floe/backscrub/issues/28#issuecomment-771445099
https://github.com/floe/backscrub/blob/master/deepseg.cc#L252-L258

Shout out to:

( for showing how to use TFlite )
https://github.com/spacejake/tflite-cpp

Thanks for the blur:

https://gist.github.com/bfraboni/946d9456b15cac3170514307cf032a27

Example free virtual background:

https://www.youtube.com/watch?v=1kDQggH30Lg

