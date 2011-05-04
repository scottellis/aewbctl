  aewbctl
=======


Attempt to control the exposure and white balance for an OMAP3 ISP connected
camera using the ISP histogram stats and the exposure/gain controls of the
camera. All done through the V4L2 interface.

Only exposure control is implemented so far.

There are mutiple flags you can pass in, but the important one is -t for the
target intensity you want. A value between 0-255.

	root@beagleboard:~# aewbctl -h
	Usage: aewbctl [options]

	Options:
	-t<n>  target intensity 0-255
	-b<n>  num histogram bins n=32,64,128 or 256 (default = 256)
	-f<n>  num frames to collect (default = 4)
	-g<n>  gain in fixed-point 3Q5 format, (default 0x20 = gain of 1.0)
	-v	verbose output, can be used more then once
	-h     print this message


