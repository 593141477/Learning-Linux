#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <libv4lconvert.h>

const int scr_width = 800, scr_height = 480, scr_bpp = 32;

const char *cam_name = "/dev/video3";
const char *scr_name = "/dev/fb0";

struct v4l2_capability cap;
struct v4l2_format fmt, dst_fmt;
struct v4l2_input input;
struct v4l2_fmtdesc fmtdesc;
int fd_cam, fd_scr;

void enum_fmt()
{
	int i = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	for(;; ++i) {
		char pixel[5];

		fmtdesc.index = i;
		if(ioctl(fd_cam, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
			if(errno != EINVAL) {
				perror("VIDIOC_ENUM_FMT");
				exit(1);
			}
			break;
		}

		*(int*)pixel = fmtdesc.pixelformat;
		pixel[4] = '\0';

		printf("format %d: flags=%u, pixel=%s, description=%s\n", 
			fmtdesc.index, fmtdesc.flags, pixel, fmtdesc.description);
	}
	putchar('\n');
}
void get_input()
{
	int idx;
	if(ioctl(fd_cam, VIDIOC_G_INPUT, &idx) < 0) {
		perror("VIDIOC_G_INPUT");
		exit(1);
	}

	input.index = idx;
	if(ioctl(fd_cam, VIDIOC_ENUMINPUT, &input) < 0) {
		perror("VIDIOC_ENUMINPUT");
		exit(1);
	}
	printf("current input: %s\n", input.name);
}
void get_format()
{
	char pixel[5];

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ioctl(fd_cam, VIDIOC_G_FMT, &fmt) < 0) {
		perror("VIDIOC_G_FMT");
		exit(1);
	}

	*(int*)pixel = fmt.fmt.pix.pixelformat;
	pixel[4] = '\0';

	printf("current format: %s %dx%d %d\n", pixel, fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.sizeimage);
}
void init_screen()
{
	if(ioctl(fd_scr, FBIOBLANK, FB_BLANK_UNBLANK) < 0) {
		perror("FBIOBLANK");
		exit(1);
	}
	system("echo 0 >/sys/class/graphics/fbcon/cursor_blink");
}
void capture()
{
	unsigned int *scr_buf;
	unsigned char *cam_buf, *dst_buf;
	int bitmap_size = fmt.fmt.pix.width*fmt.fmt.pix.height*3;
	int src_size, i, j;

	struct v4lconvert_data *lib;

	lib = v4lconvert_create(fd_cam);
	if(!lib) {
		perror("v4lconvert_create");
		exit(1);
	}

	cam_buf = malloc(fmt.fmt.pix.sizeimage);
	dst_buf = malloc(bitmap_size);
	if(!cam_buf || !dst_buf){
		perror("malloc");
		exit(1);
	}

	scr_buf = mmap(0, scr_width*scr_height*scr_bpp/8, PROT_READ|PROT_WRITE, MAP_SHARED, fd_scr, 0);
	if(scr_buf <= 0) {
		perror("mmap");
		exit(1);
	}

	dst_fmt = fmt;
	dst_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;

	for(;;) {
		src_size = read(fd_cam, cam_buf, fmt.fmt.pix.sizeimage);
		if(src_size <= 0){
			perror("read");
			exit(1);
		}

		v4lconvert_convert(lib, &fmt, &dst_fmt, cam_buf, src_size, dst_buf, bitmap_size);

		for(i = 0; i < fmt.fmt.pix.height; i++)
			for(j = 0; j < fmt.fmt.pix.width; j++){
				int k = i*fmt.fmt.pix.width + j;
				k *= 3; //3 bytes per pixel
				scr_buf[i*scr_width + j] = (dst_buf[k + 2] << 16)|(dst_buf[k + 1] << 8)|dst_buf[k];
			}
	}

	munmap(scr_buf, scr_width*scr_height*scr_bpp/8);
	free(cam_buf);
	free(dst_buf);

	v4lconvert_destroy(lib);
}
int main()
{
	fd_cam = open(cam_name, O_RDWR, 0);
	fd_scr = open(scr_name, O_RDWR);
	if(fd_cam < 0 || fd_scr < 0 || errno){
		perror("open device");
		exit(1);
	}

	// if(ioctl(fd_cam, VIDIOC_QUERYCAP, &cap) < 0){
	// 	perror("VIDIOC_QUERYCAP");
	// 	exit(1);
	// }

	enum_fmt();
	get_input();
	get_format();

	init_screen();

	capture();

	close(fd_scr);
	close(fd_cam);
}
