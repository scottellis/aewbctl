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

int nbins;
int nframes;
unsigned long gain;
int current_exposure;
double target_intensity;
int timing;
int verbose;
int dry_run;
int dump_bins;
int shutdown_time;

#define NUM_COLOR_COMPONENTS 4

struct hist_summary {
	unsigned int median_bin[NUM_COLOR_COMPONENTS];
	unsigned int count[NUM_COLOR_COMPONENTS];
	double avg[NUM_COLOR_COMPONENTS];
	double overall_avg;
};

int msleep(int milliseconds)
{
        struct timespec ts;

        if (milliseconds < 1) {
                return -2;
        }

        ts.tv_sec = milliseconds / 1000;
        ts.tv_nsec = 1000000 * (milliseconds % 1000);

        return nanosleep(&ts, NULL);
}

int xioctl(int fd, int request, void *arg)
{
	int r;

	do {
		r = ioctl(fd, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

int get_exposure(int fd)
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

int set_exposure(int fd, int exposure)
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

void set_gain(int fd, int control_id, int gain)
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

struct region {
	unsigned int left;
	unsigned int right;
	unsigned int top;
	unsigned int bottom;
};

struct region regions[4] = {
	{ 680, 1880, 360, 1560 },
	{ 280, 480, 760, 1160 },
	{ 2280, 2480, 760, 1160 },
	{ 1080, 1480, 1170, 1270 },
};

int enable_histogram(int fd)
{
	struct isp_hist_config cfg;
	
	memset(&cfg, 0, sizeof(cfg));

	cfg.hist_source = 0; 		// CCDC is the source
	cfg.input_bit_width = 10;

	cfg.hist_frames = nframes;
	cfg.num_regions = 1;

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
	}

	cfg.reg0_hor = (regions[0].left << 16) | regions[0].right;
	cfg.reg0_ver = (regions[0].top << 16) | regions[0].bottom;
	/*
	cfg.reg1_hor = (regions[1].left << 16) | regions[1].right;
	cfg.reg1_ver = (regions[1].top << 16) | regions[1].bottom;
	cfg.reg2_hor = (regions[2].left << 16) | regions[2].right;
	cfg.reg2_ver = (regions[2].top << 16) | regions[2].bottom;
	cfg.reg3_hor = (regions[3].left << 16) | regions[3].right;
	cfg.reg3_ver = (regions[3].top << 16) | regions[3].bottom;
	*/

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

	if (-1 == xioctl(fd, VIDIOC_PRIVATE_ISP_HIST_CFG, &cfg)) {
		perror("VIDIOC_PRIVATE_ISP_HIST_CFG");
		return -1;
	}
	
	return 0;
}

void get_hist_summary(unsigned int *d, struct hist_summary *hs)
{
	int i, j;
	unsigned int *p;
	double bin_size;
	double half;

	hs->overall_avg = 0.0;

	bin_size = 256.0 / nbins;

	for (j = 0; j < NUM_COLOR_COMPONENTS; j++) {
		p = &d[j * nbins];

		hs->avg[j] = 0.0;
		hs->count[j] = 0;
		half = 0.0;

		for (i = 0; i < nbins; i++) {
			hs->avg[j] += (p[i] * i * bin_size) + (p[i] * (bin_size / 2));
			hs->count[j] += p[i];
		}

		for (i = 0; i < nbins; i++) {
			half += (p[i] * i) + (p[i] * (bin_size / 2));
		
			if (half > hs->avg[j] / 2)
				break;	
		}

		hs->median_bin[j] = i;

		if (hs->count[j] > 0)
			hs->avg[j] /= hs->count[j];

		hs->overall_avg += hs->avg[j];
	}

	hs->overall_avg /= NUM_COLOR_COMPONENTS;
}

void dump_histogram_bins(unsigned int *d, struct hist_summary *hs)
{
	int i, j;
	unsigned int *p;

	for (j = 0; j < NUM_COLOR_COMPONENTS; j++) {
		printf("\nComponent[%d]: Avg: %0.2lf  Median Bin: %d  Count: %u", 
				j, hs->avg[j], hs->median_bin[j], hs->count[j]);

		if (dump_bins) {
			p = &d[j * nbins];

			for (i = 0; i < nbins; i++) {
				if ((i % 16) == 0)
					printf("\n   %6u", p[i]);
				else 
					printf("   %6u", p[i]);
			}
		
			printf("\n");
		}
	}

	if (dump_bins)
		printf("\n\n");
	else
		printf("\n");
}

int read_histogram(int fd, struct isp_hist_data *isp_hist, struct hist_summary *hs)
{	
	int result, i;

	if (enable_histogram(fd) < 0)
		return -1;

	msleep(500 * nframes);

	for (i = 0; i < 10; i++) {
		result = ioctl(fd, VIDIOC_PRIVATE_ISP_HIST_REQ, isp_hist);

		if (!result)
			break;

		if (errno != EBUSY && errno != EINVAL)
			break;

		msleep(200);
	}

	if (result) {
		perror("VIDIOC_PRIVATE_ISP_HIST_REQ");
	}
	else {
		get_hist_summary(isp_hist->hist_statistics_buf, hs);

		if (verbose)
			dump_histogram_bins(isp_hist->hist_statistics_buf, hs);
	}

	return result;
}

#define MIN_EXPOSURE 63
#define MAX_EXPOSURE 140000
int adjust_exposure(int fd, double avg)
{
	int new_exposure;
	double diff;

	diff = target_intensity - avg;

	if (verbose > 1)
		printf("raw diff: %3.2lf\n", diff);

	if (abs(diff) < 1.0)
		return 0;

	new_exposure = (int)((double)current_exposure * (target_intensity / avg));

	if (new_exposure < MIN_EXPOSURE)
		new_exposure = MIN_EXPOSURE;
	else if (new_exposure > MAX_EXPOSURE)
		new_exposure = MAX_EXPOSURE;
	
	if (new_exposure == current_exposure) {
		if (verbose)
			printf("New exposure same as old: %d\n", new_exposure);

		return 0;
	}

	if (verbose)
		printf("Adjusting %d to %d\n", current_exposure, new_exposure);

	if (dry_run) {
		/* during testing often changing externally, so need to update */
		get_exposure(fd);
	} 
	else {
		if (!set_exposure(fd, new_exposure))
			current_exposure = new_exposure;
	}

	return 0;	
}

int open_device(const char *dev_name)
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

void main_loop(const char *dev_name)
{
	int fd;
	struct isp_hist_data isp_hist;
	struct hist_summary hs;

	isp_hist.hist_statistics_buf = malloc(4096);

	if (!isp_hist.hist_statistics_buf) {
		printf("memory alloc fail in read_histogram\n");
		return;
	}

	fd = open_device(dev_name);

	if (get_exposure(fd) < 0)
		goto main_loop_end;
	
	while (!shutdown_time) {
		memset(isp_hist.hist_statistics_buf, 0xff, 4096);

		read_histogram(fd, &isp_hist, &hs);

		if (verbose)
			printf("Target: %3.2lf  Current: %3.2lf\n", 
				target_intensity, hs.overall_avg);

		adjust_exposure(fd, hs.overall_avg);
		msleep(timing * 1000);		
	}

main_loop_end:

	close(fd);
	free(isp_hist.hist_statistics_buf);
}

void sig_handler(int sig)
{
	if (sig == SIGINT)
		shutdown_time = 1;
}
 
void install_signal_handlers()
{
	struct sigaction sia;

	bzero(&sia, sizeof sia);
	sia.sa_handler = sig_handler;

	if (sigaction(SIGINT, &sia, NULL) < 0) {
		perror("sigaction(SIGINT)");
		exit(1);
	} 
}

void summarize_start()
{
	printf("\nStart Conditions:\n");
	printf("  nbins     = %d\n", nbins);
	printf("  nframes   = %d\n", nframes);
	printf("  gain      = 0x%04lX\n", gain);
	printf("  target    = %3.2lf\n", target_intensity);
	printf("  timing    = %d\n", timing);
	printf("  verbose   = %d\n", verbose);
	printf("  dry_run   = %d\n", dry_run);
	printf("  dump_bins = %d\n\n", dump_bins);	
}

void usage(FILE *fp, char **argv)
{
	fprintf (fp,
		"Usage: %s [options]\n\n"
		"Options:\n"
		"-i<n>  target intensity 0-255, default is 50\n"
                "-t<n>  adjustment frequency in seconds (range 1-30, default is 1)\n"
		"-b<n>  num histogram bins n=32,64,128 or 256 (default = 32)\n"
		"-f<n>  num frames to collect (default = 1)\n"
		"-g<n>  gain in fixed-point 3Q5 format, (default 0x40 = gain of 2.0)\n"
		"-v     verbose output, can be used more then once to get more info\n"
		"-n     dry run, no adjustments made, implies at least one -v\n"
		"-d     dump bins\n"
		"-h     print this message\n"
		"\n",
		argv[0]);
}

int main(int argc, char **argv)
{
	int opt, n;
	char *endp;
	char dev_name[] = "/dev/video0";

	nbins = 32;
	nframes = 1;
	gain = 0x20;	
	target_intensity = 50.0;
	timing = 1;

	while ((opt = getopt(argc, argv, "i:t:b:f:g:vndh")) != -1) {
		switch (opt) {
		case 'i':
			target_intensity = atof(optarg);

			if (target_intensity < 0.0 || target_intensity > 255.0) {
				printf("Invalid target intensity: %.0lf\n", 
					target_intensity);

				usage(stderr, argv);
				exit(1);
			}

			break;

		case 't':
			timing = atoi(optarg);

			if (timing < 0 || timing > 30) {
				printf("Invalid adjustment frequency: %d\n", 
					timing);

				usage(stderr, argv);
				exit(1);
			}

			break;

		case 'b':
			nbins = atoi(optarg);

			if (nbins != 32 && nbins != 64 && nbins != 128 && nbins != 256) {
				printf("Invalid bins: %d\n", n);
				usage(stderr, argv);
				exit(1);
			}
	
			break;

		case 'f':
			nframes = atoi(optarg);
			break;

		case 'g':
			gain = strtoul(optarg, &endp, 0);

			if (gain < 1 || gain > 255) {
				printf("Invalid gain %lu\n", gain);
				usage(stderr, argv);
				exit(1);
			}

			break;

		case 'v':
			verbose++;
			break;

		case 'n':
			dry_run = 1;
			break;

		case 'd':
			dump_bins = 1;
			break;

		case 'h':
			usage(stdout, argv);
			exit(0);

		default:
			usage(stderr, argv);
			exit(1);
		}
	}

	if (dry_run && !verbose)
		verbose = 1;

	install_signal_handlers();

	summarize_start();

	main_loop(dev_name);

	return 0;
}

