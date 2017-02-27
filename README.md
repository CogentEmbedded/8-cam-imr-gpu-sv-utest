# IMR + GPU surround view utest application with mirror camera replacement

Demo application demonstrates surround view with pre-defined viewpoints on Renesas boards
with 4 additional cameras for mirror replacement purposes. First additional camera works
as driver monitor.

## Build
Project uses cmake tool to build an application.
Example for H3

```
. /opt/poky/2.0.2/environment-setup-aarch64-poky-linux 
mkdir build
cd build
cmake ../
make

```
## Run
To run application 
```
usage: utest-imr-sv [options]
Options and arguments:
-d  : Debug log level (default: 1)
-v  : Paths to 4 VIN camera devices(default: /dev/video0,/dev/video1,/dev/video2,/dev/video3 )
-r  : Paths to 5 imr devices (default: /dev/video8,/dev/video9,/dev/video10,/dev/video11,/dev/video12)
-f  : Video format input (available options: uyvy,yuyv,nv12,nv16
-o  :  Desired Weston display output number 0, 1,.., N
-w  : VIN camera capture width (default: 1280)
-h  : VIN camera capture height (default: 800)
-W  : VSP width render output (default: 1280)
-H  : VSP height render output (default: 720)
-X  : Car png file width
-Y  : Car png file height
-n  : Number of buffers for VIN
-s  : Number of steps for model positions (default: 8:32:8)
-m  : Model PNG picture prefix path (default: ./data/model)
-M  : Mesh file path
-S  : Car shadow rectangle
-g  : Sphere gain
-b  : Background color
```
Example of usage:

```
./sc -W 1920 -H 1080 -m ./data/model -M meshFull.obj -X 1920 -Y 1080 -g 1.0 -b 0x000000 -c config.txt -S -0.20:-0.1:0.20:0.1 -s 8:32:8
```

Example of generation png files with car:

```
./gen -w <width> -h <height> -c <color> -o <path to store> -s <positions> -m <car object> \
-l <car length> -S <shadow rectangle> -d <debug>
./gen -w 1920 -h 1080  -c 0x404040FF -o ./data/model -s 8:32:8 -m Car.obj -l 1.0  -S -0.2:-0.10:0.2:0.10

```


# Controling

To control application with SpaceNav start SpaceNav daemon: spacenavd command.
There is a 5 widgets on the main screen: main 3d SurroundView screen, DriverMonitor camera,
right, left and rear mirror replacement cameras.

To change focus on widget press right button on SpaceNav.
To change zoom and view on mirror replacement cameras rotate SpaceNav joystick, when widget is in focus.
To hide Driver Monitor camera and mirror cameras press and hold left button on joystick, when widget is in focus.
To switch to IMR demo press left button on SpaceNav joystick, when main 3d SurroundView in focus. 
To get back from IMR demo press left button again.
To rotate view in IMR demo use joystick or touchscreen.

# Calibration and mesh saving

See the https://github.com/CogentEmbedded/sv-utest and
https://github.com/CogentEmbedded/sv-utest/blob/master/docs/cogente_sv_manual_public.pdf
for the instructions how to calibrate and save mesh for IMR demo.
