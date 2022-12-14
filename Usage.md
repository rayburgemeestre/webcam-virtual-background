## Usage in more detail

It can take any `/dev/videoX` device, resizes it to 640x480 (currently hard-coded), convert it to YUV420p, feed that to
a virtual device `/dev/video8`. Then this project's C++ code with tensorflow lite will read that device, do all the
background segregation and blurring, and feed the end-result to `/dev/video9`. This is the device that can be used in
chrome/teams/google meet, etc.

Assuming it has been installed on your machine, you can run it like this, it presents you with a simple shell specific
to the program:


    rayb@ideapad:~> cam

    Art by Marcin Glinski           _
                                   / \
                                  / .'_
                                 / __| \
                 `.             | / (-' |
               `.  \_..._       :  (_,-/
             `-. `,'     `-.   /`-.__,'
                `/ __       \ /     /
                /`/  \       :'    /
              _,\o\_o/       /    /
             (_) ___.--.    /    /
              `-. -._.i \.      :
                 `.\  ( |:.     |
                ,' )`-' |:..   / \
       __     ,'   |    `.:.      `.
      (_ `---:     )      \:.       \
       ,'     `. .'\       \:.       )
     ,' ,'     ,'  \\ o    |:.      /
    (_,'  ,7  /     \`.__.':..     /,,,
      (_,'(_,'   _gdMbp,,dp,,,,,,dMMMMMbp,,
              ,dMMMMMMMMMMMMMMMMMMMMMMMMMMMb,
           .dMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMb,  fsc
         .dMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM,
        ,MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
       dMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM.
     .dMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMb
     V I R T U A L    B A C K G R O U N D   W E B C A M

    Available commands are:
        preview
        stop
        set-mode
        set-cam
        list-cams
        start
        exit
        quit
        set-model
        run
        help
    cam> 

At startup the available commands are printed. The above output can be seen at any point by typing `help`.

You can select the camera to use with `list-cams`:

    cam> list-cams
    Current camera: /dev/video0
    Found: /dev/video0 -     Stream #0:0: Video: rawvideo (YUY2 / 0x32595559), yuyv422, 1920x1080, 165888 kb/s, 5 fps, 5 tbr, 1000k tbn, 1000k tbc
    Found: /dev/video2 -     Stream #0:0: Video: rawvideo (Y800 / 0x30303859), gray, 640x360, 27648 kb/s, 15 fps, 15 tbr, 1000k tbn, 1000k tbc
    cam>

Please note that if your camera is already being used in another program (e.g., Zoom, Teams, etc.), it might not properly show up here, and simply not work. Make sure you quit those programs first.

Then to select the one that you want, my laptop camera is the YUYV422 1920x1080 device (the following command is useless as `/dev/video0` was already selected by default):

    cam> set-cam /dev/video0
    Selected camera: /dev/video0
    cam>

(You can always preview any device when in doubt with `ffplay /dev/video0`.)

Then w/r/t the effect to apply, for this the `set-mode` command is used and can also be invoked later to switch modes.
(The default pre-selected one is `animated`, which is the animated spaceship background.)

    cam> set-mode
    Usage: set-mode < mode >
    Valid modes:
    - normal
    - white
    - black
    - blur
    - virtual
    - animated
    - snowflakes
    - snowflakesblur
    Received error code 1
    cam!>

To change it to `snowflakes` because it's almost Christmas (at the time of writing):

    cam> set-mode snowflakes

Now that we're all set, we can type `start` and this will look as follows:

    cam> start
    cam> Loaded model
    Input #0, video4linux2,v4l2, from '':
      Duration: N/A, start: 208072.435191, bitrate: 110592 kb/s
      Stream #0:0: Video: rawvideo (I420 / 0x30323449), yuv420p, 640x480, 110592 kb/s, 30 fps, 30 tbr, 1000k tbn, 1000k tbc
    Output #0, video4linux2,v4l2, to '':
    The codec pixfmt: 0
    Output #0, video4linux2,v4l2, to '/dev/video9':
      Stream #0:0: Video: rawvideo (I420 / 0x30323449), yuv420p, 640x480, q=2-31, 110592 kb/s

If the output looks similar to above, then what happened under the hood is:
* The camera is being read from `/dev/video0` and fed to `/dev/video8` as 640x480 YUV420p. This is handled by a system call to `ffmpeg`.
* The `/dev/video8` device is being read by this project (in a different thread from the shell), processed and fed to `/dev/video9`, also  as 640x480 YUV420p.

Choosing `stop` should terminate both the ffmpeg program and the background thread.

The shell is still responsive, and available for commands (just press return to see the `cam>` prompt again if it is not visible).

Anyway, it should just work, and if you want to see how it looks, without hopping onto Zoom, you can `ffplay /dev/video9` on your system, or use the `preview` command, which does just that via a system call.

    cam> preview

Do not forget to close the window, it might interfere with the virtual camera being used in other software (keeping the device "already in use" for e.g. Zoom)

    cam> Preview window exited: 0

`stop` and/or `exit` to terminate the program. Use an extra Ctrl+C if needed.

The device should be recognized on the system via the following label:

* `Virtual Temp Camera Input` this is /dev/video8 (don't use this one!)
* `Virtual 640x480 420P TFlite Camera` this is /dev/video9
