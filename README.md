  aewbctl
=======


Attempt to control the exposure and white balance for an OMAP3 ISP connected
camera using the ISP histogram stats and the exposure/gain controls of the
camera. All done through the V4L2 interface.

Only exposure control is implemented so far.

There are mutiple flags you can pass in, but the important one is -t for the
target intensity you want. A value between 0-255.

Here is an example run on a cloudy, windy day, outside light was changing
rapidly. It's debug so lots of output.


	root@beagleboard:~# aewbctl -t28
	summary: median-bins:  10   16   16   11    avg: 13.25
	Adjusting exposure 3338 to 4813
	summary: median-bins:  17   28   28   19    avg: 23.00
	Adjusting exposure 4813 to 5313
	summary: median-bins:  19   31   31   21    avg: 25.50
	Adjusting exposure 5313 to 5563
	summary: median-bins:  23   36   37   25    avg: 30.25
	Adjusting exposure 5563 to 5338
	summary: median-bins:  28   43   44   29    avg: 36.00
	Adjusting exposure 5338 to 4538
	summary: median-bins:  29   39   39   29    avg: 34.00
	Adjusting exposure 4538 to 3938
	summary: median-bins:  28   42   43   28    avg: 35.25
	Adjusting exposure 3938 to 3213
	summary: median-bins:  22   30   30   23    avg: 26.25
	Adjusting exposure 3213 to 3388
	summary: median-bins:  22   31   31   22    avg: 26.50
	Adjusting exposure 3388 to 3538
	summary: median-bins:  29   38   39   29    avg: 33.75
	Adjusting exposure 3538 to 2963
	summary: median-bins:  23   32   32   23    avg: 27.50
	summary: median-bins:  24   32   33   24    avg: 28.25
	summary: median-bins:  27   37   38   26    avg: 32.00
	Adjusting exposure 2963 to 2563
	summary: median-bins:  22   34   34   22    avg: 28.00
	summary: median-bins:  23   35   35   22    avg: 28.75
	summary: median-bins:  23   35   35   23    avg: 29.00
	summary: median-bins:  24   34   34   24    avg: 29.00
	summary: median-bins:  20   31   31   20    avg: 25.50
	Adjusting exposure 2563 to 2813
	summary: median-bins:  15   25   25   16    avg: 20.25
	Adjusting exposure 2813 to 3588
	summary: median-bins:  11   19   19   12    avg: 15.25
	Adjusting exposure 3588 to 4863
	...
	summary: median-bins:  21   34   34   23    avg: 28.00
	summary: median-bins:  20   32   33   22    avg: 26.75
	Adjusting exposure 6963 to 7088
	summary: median-bins:  21   34   34   23    avg: 28.00
	summary: median-bins:  20   33   33   23    avg: 27.25
	summary: median-bins:  21   34   34   23    avg: 28.00
	summary: median-bins:  20   33   33   23    avg: 27.25
	summary: median-bins:  22   32   32   24    avg: 27.50
	summary: median-bins:  22   31   31   24    avg: 27.00
	summary: median-bins:  21   34   34   23    avg: 28.00
	summary: median-bins:  22   31   31   24    avg: 27.00
	summary: median-bins:  22   32   32   24    avg: 27.50
	summary: median-bins:  22   31   31   24    avg: 27.00
	summary: median-bins:  21   34   34   23    avg: 28.00
	summary: median-bins:  20   33   33   22    avg: 27.00
	summary: median-bins:  21   34   34   23    avg: 28.00
	summary: median-bins:  22   31   31   24    avg: 27.00
	summary: median-bins:  21   34   35   23    avg: 28.25
	summary: median-bins:  21   33   34   23    avg: 27.75
	summary: median-bins:  22   32   33   24    avg: 27.75


