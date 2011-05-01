/*
 *  
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <linux/videodev2.h>

#include "isp_user.h"

#define V4L2_MT9P031_GREEN1_GAIN		(V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_MT9P031_BLUE_GAIN			(V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_MT9P031_RED_GAIN			(V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_MT9P031_GREEN2_GAIN		(V4L2_CID_PRIVATE_BASE + 3)

int nbins = 256;
int nframes = 4;
unsigned long gain = 0x20;
int current_exposure = 10000;
double target_intensity = 25.0;
int verbose;
int shutdown_time;

struct hist_summary {
	unsigned int median_bin[4];
	double avg;
};

static int msleep(int milliseconds)
{
        struct timespec ts;

        if (milliseconds < 1) {
                return -2;
        }

        ts.tv_sec = milliseconds / 1000;
        ts.tv_nsec = 1000000 * (milliseconds % 1000);

        return nanosleep(&ts, NULL);
}

static int xioctl(int fd, int request, void *arg)
{
	int r;

	do {
		r = ioctl(fd, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

static int get_exposure(int fd)
{
	struct v4l2_control control;

	memset(&control, 0, sizeof (control));
	control.id = V4L2_CID_EXPOSURE;

	if (-1 == ioctl(fd, VIDIOC_G_CTRL, &control)) {
	        perror("VIDIOC_G_CTRL");
		return -1;
	}

	current_exposure = control.value;

	return 0;
}

static int set_exposure(int fd, int exposure)
{
	struct v4l2_control control;

	memset(&control, 0, sizeof (control));
	control.id = V4L2_CID_EXPOSURE;
	control.value = exposure;

	if (-1 == ioctl(fd, VIDIOC_S_CTRL, &control)) {
	        perror("VIDIOC_S_CTRL");
		return -1;
	}

	return 0;
}

static void set_gain(int fd, int control_id, int gain)
{
	struct v4l2_queryctrl queryctrl;
	struct v4l2_control control;

	memset(&queryctrl, 0, sizeof (queryctrl));
	queryctrl.id = control_id;

	if (-1 == ioctl(fd, VIDIOC_QUERYCTRL, &queryctrl)) {
		if (errno != EINVAL) {
		        perror("VIDIOC_QUERYCTRL");
		} 
		else {
		        printf("Control is not supported\n");
		}
	} 
	else if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
		printf("Control is not supported\n");
	} 
	else {
		memset(&control, 0, sizeof (control));
		control.id = control_id;
		control.value = gain;

		if (-1 == ioctl(fd, VIDIOC_S_CTRL, &control))
		        perror("VIDIOC_S_CTRL");
	}
}

static int enable_histogram(int fd)
{
	struct isp_hist_config cfg;
	unsigned int x_start, x_end;
	unsigned int y_start, y_end;
	
	memset(&cfg, 0, sizeof(cfg));

	cfg.hist_source = 0; 		// CCDC is the source
	cfg.input_bit_width = 10;

	cfg.hist_frames = nframes;

	switch (nbins) {
	case 32:
		cfg.hist_bins = BINS_32;
		break;
	case 64:
		cfg.hist_bins = BINS_64;
		break;

	case 128:
		cfg.hist_bins = BINS_128;
		break;

	case 256:
		cfg.hist_bins = BINS_256;
		break;

	default:
		printf("Invalid number of bins %d\n", nbins);
		return -1;
	}

	/* 
	fixed-point 8-bit values 3Q5, 
	0x10 = 0.5 
	0x20 = 1.0 gain, 
	0x40 = 2.0
	*/
	cfg.wb_gain_R = gain;
	cfg.wb_gain_RG = gain;
	cfg.wb_gain_B = gain;
	cfg.wb_gain_BG = gain;

	/* 0 = reg0, 1 = reg0 and reg1, ..., 3 = reg0-reg3 */
	cfg.num_regions = 0;

	/* packed start [29:16] and end [13:0] pixel positions */ 
	/* choose a 600x600 pixel region in the center for stats */
	x_start = 980;
	x_end = 1579;
	y_start = 860;
	y_end = 1259;
	cfg.reg0_hor = (x_start << 16) | x_end;
	cfg.reg0_ver = (y_start << 16) | y_end;

	if (-1 == xioctl(fd, VIDIOC_PRIVATE_ISP_HIST_CFG, &cfg)) {
		perror("VIDIOC_PRIVATE_ISP_HIST_CFG");
		return -1;
	}
	
	return 0;
}

static void get_hist_summary(unsigned int *d, struct hist_summary *hs)
{
	int i, j;
	unsigned int half, sum;
	unsigned int *p;

	hs->avg = 0.0;

	for (j = 0; j < 4; j++) {
		p = &d[j * nbins];

		sum = 0;
		half = 0;

		for (i = 0; i < nbins; i++) {
			sum += p[i];
		}

		for (i = 0; i < nbins; i++) {
			half += p[i];
		
			if (half > sum / 2)
				break;	
		}

		hs->median_bin[j] = i;
		hs->avg += i;
	}

	hs->avg /= 4.0;
}

static int read_histogram(int fd, struct isp_hist_data *isp_hist, struct hist_summary *hs)
{	
	int result, i;

	if (enable_histogram(fd) < 0)
		return -1;

	msleep(500 * nframes);

	for (i = 0; i < 10; i++) {
		result = ioctl(fd, VIDIOC_PRIVATE_ISP_HIST_REQ, isp_hist);

		if (!result)
			break;

		if (errno != EBUSY)
			break;

		msleep(500);
	}

	if (result)
		perror("VIDIOC_PRIVATE_ISP_HIST_REQ");
	else
		get_hist_summary(isp_hist->hist_statistics_buf, hs);

	return result;
}

#define MIN_EXPOSURE 63
#define MAX_EXPOSURE 120000
static int adjust_exposure(int fd, double avg)
{
	int new_exposure;
	double diff;

	diff = target_intensity - avg;

	if (verbose > 1)
		printf("raw diff: %3.2lf\n", diff);

	if (abs(diff) < 1.0)
		return 0;

	if (verbose > 1)
		printf("current exposure: %d\n", current_exposure);

	new_exposure = (int)((double)current_exposure * (target_intensity / avg));
	
	if (verbose > 1)
		printf("new_exposure: %d\n", new_exposure);

	if (new_exposure < MIN_EXPOSURE || new_exposure > MAX_EXPOSURE)
		return 0;
	
	if (verbose)	
		printf("Adjusting exposure %d to %d\n", current_exposure, new_exposure);

	if (!set_exposure(fd, new_exposure))
		current_exposure = new_exposure;

	return 0;	
}

static int open_device(const char *dev_name)
{
	int fd;
	struct stat st; 

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n",
			dev_name, errno, strerror (errno));
		exit(1);
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no device\n", dev_name);
		exit(1);
	}

	fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);

	if (-1 == fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n",
			dev_name, errno, strerror (errno));
		exit(1);
	}

	return fd;
}

static int do_recover_ops(const char *dev_name)
{
	int fd;

	if (!verbose)
		verbose = 1;

	printf("Going into recovery mode avg and old_avg > 254\n");
	close(fd);
	msleep(30000);

	fd = open(dev_name,  O_RDWR | O_NONBLOCK, 0);

	if (fd < 0) {
		perror("open");
		return fd;
	}

	set_exposure(fd, 10000);
	msleep(1000);

	return fd;
}

static void main_loop(const char *dev_name)
{
	int fd;
	struct isp_hist_data isp_hist;
	struct hist_summary hs;
	double old_avg;

	isp_hist.hist_statistics_buf = malloc(4096);

	if (!isp_hist.hist_statistics_buf) {
		printf("memory alloc fail in read_histogram\n");
		return;
	}

	fd = open_device(dev_name);

	if (get_exposure(fd) < 0)
		goto main_loop_end;

	old_avg = 0.0;
		
	while (!shutdown_time) {
		memset(isp_hist.hist_statistics_buf, 0, 4096);

		read_histogram(fd, &isp_hist, &hs);

		if (verbose)
			printf("summary: median-bins: %3u  %3u  %3u  %3u    avg: %3.2lf\n", 
				hs.median_bin[0], hs.median_bin[1], hs.median_bin[2], 
				hs.median_bin[3], hs.avg);

		
		adjust_exposure(fd, hs.avg);

		if (old_avg > 254.0 && hs.avg > 254.0) {
			close(fd);
			old_avg = 0.0;
			fd = do_recover_ops(dev_name);
			if (fd < 0)				
				break;
		}
		else {
			old_avg = hs.avg;
			msleep(1000);		
		}
	}

main_loop_end:

	close(fd);
	free(isp_hist.hist_statistics_buf);
}

static void sig_handler(int sig)
{
	if (sig == SIGINT || sig == SIGTERM || sig == SIGHUP)
		shutdown_time = 1;
}
 
static void install_signal_handlers()
{
	struct sigaction sia;

	bzero(&sia, sizeof sia);
	sia.sa_handler = sig_handler;

	if (sigaction(SIGINT, &sia, NULL) < 0) {
		perror("sigaction(SIGINT)");
		exit(1);
	} 
}

static void usage(FILE *fp, char **argv)
{
	fprintf (fp,
		"Usage: %s [options]\n\n"
		"Options:\n"
		"-t<n>  target intensity 0-255\n"
		"-b<n>  num histogram bins n=32,64,128 or 256 (default = 128)\n"
		"-f<n>  num frames to collect (default = 1)\n"
		"-g<n>  gain in fixed-point 3Q5 format, (default 0x20 = gain of 1.0)\n"
		"-v	verbose output\n"
		"-h     print this message\n"
		"\n",
		argv[0]);
}

int main(int argc, char **argv)
{
	int opt;
	char *endp;
	char dev_name[] = "/dev/video0";

	while ((opt = getopt(argc, argv, "t:b:f:g:vh")) != -1) {
		switch (opt) {
		case 't':
			target_intensity = atof(optarg);

			if (target_intensity < 0.0 || target_intensity > 255.0) {
				printf("Invalid target intensity: %.0lf\n", 
					target_intensity);

				usage(stderr, argv);
				exit(1);
			}

			break;

		case 'b':
			nbins = atoi(optarg);				
			break;

		case 'f':
			nframes = atoi(optarg);
			break;

		case 'g':
			gain = strtoul(optarg, &endp, 0);

			if (gain < 1 || gain > 255) {
				printf("Invalid gain %lu  Valid range 0-255\n", 
					gain);
				usage(stderr, argv);
				exit(1);
			}

			break;

		case 'v':
			verbose++;
			break;

		case 'h':
			usage(stdout, argv);
			exit(0);

		default:
			usage(stderr, argv);
			exit(1);
		}
	}

	install_signal_handlers();

	main_loop(dev_name);

	return 0;
}

