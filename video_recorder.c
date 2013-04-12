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

#define NUM_BUF 7
#define NUM_FRAME 100

const char *cam_name = "/dev/video3";
const char *out_name = "cam.yuv";
int page_size;

struct v4l2_capability cap;
struct v4l2_format fmt, dst_fmt;
struct v4l2_input input;
struct v4l2_fmtdesc fmtdesc;
struct v4l2_requestbuffers reqbuf;
struct v4l2_buffer buffers[NUM_BUF];
int fd_cam, fd_out, buffersize;
int *buf_pointer[NUM_BUF];

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
int calc_size(int size)
{
	return (size + page_size - 1) & ~(page_size - 1);
}
void buf_alloc_mmap()
{
	int i;

	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuf.memory = V4L2_MEMORY_MMAP;
	reqbuf.count = NUM_BUF;
	if(ioctl(fd_cam, VIDIOC_REQBUFS, &reqbuf) < 0) {
		perror("VIDIOC_REQBUFS");
		exit(1);
	}
	printf("buffers: %d\n", reqbuf.count);

	for(i=0; i<reqbuf.count; i++) {
		
		buffers[i].index = i;
		buffers[i].type = reqbuf.type;
		buffers[i].memory = V4L2_MEMORY_MMAP;

		if(ioctl(fd_cam, VIDIOC_QUERYBUF, &buffers[i]) < 0) {
			perror("VIDIOC_QUERYBUF");
			exit(1);
		}

		if(MAP_FAILED == (buf_pointer[i] = mmap(0, buffers[i].length, PROT_READ|PROT_WRITE, MAP_SHARED, fd_cam, buffers[i].m.offset))) {
			perror("mmap");
			exit(1);
		}
		
		if(ioctl(fd_cam, VIDIOC_QBUF, &buffers[i]) < 0) {
			perror("VIDIOC_QBUF");
			exit(1);
		}
	}
}
void free_buf_mmap()
{
	int i;
	for(i=0; i<reqbuf.count; i++)
		munmap(buf_pointer[i], buffers[i].length);
}
void buf_alloc_user_ptr()
{
	int i;

	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuf.memory = V4L2_MEMORY_USERPTR;
	reqbuf.count = NUM_BUF;
	if(ioctl(fd_cam, VIDIOC_REQBUFS, &reqbuf) < 0) {
		perror("VIDIOC_REQBUFS");
		exit(1);
	}
	printf("buffers: %d\n", reqbuf.count);

	for(i=0; i<reqbuf.count; i++) {
		
		if(!(buffers[i].m.userptr = (unsigned long)memalign(page_size, buffersize))) {
			perror("memalign");
			exit(1);
		}

		buffers[i].index = i;
		buffers[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffers[i].memory = V4L2_MEMORY_USERPTR;
		buffers[i].length = buffersize;
		if(ioctl(fd_cam, VIDIOC_QBUF, &buffers[i]) < 0) {
			perror("VIDIOC_QBUF");
			exit(1);
		}

		buf_pointer[i] = (void*)buffers[i].m.userptr;
	}
}
void free_buf_user_ptr()
{
	int i;
	for(i=0; i<reqbuf.count; i++)
		free((void*)buffers[i].m.userptr);
}
void capture()
{
	unsigned char *yuv420_buf;
	int yuv420_size = fmt.fmt.pix.width*fmt.fmt.pix.height*3/2;
	int src_size, i, j, k, nframe;

	struct v4lconvert_data *lib;

	buffersize = calc_size(fmt.fmt.pix.sizeimage);

	buf_alloc_mmap();

	lib = v4lconvert_create(fd_cam);
	if(!lib) {
		perror("v4lconvert_create");
		exit(1);
	}

	yuv420_buf = malloc(yuv420_size);
	if(!yuv420_buf){
		perror("malloc");
		exit(1);
	}

	if(ioctl(fd_cam, VIDIOC_STREAMON, &reqbuf.type) < 0) {
		perror("VIDIOC_STREAMON");
		exit(1);
	}

	dst_fmt = fmt;
	dst_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;

	if(!v4lconvert_supported_dst_format(dst_fmt.fmt.pix.pixelformat)){
		puts("v4lconvert_supported_dst_format");
		exit(1);
	}

	for(errno = 0, nframe = 0; nframe < NUM_FRAME; nframe++) {
		struct v4l2_buffer cam_buf = {0};

		cam_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cam_buf.memory = reqbuf.memory;

		if(ioctl(fd_cam, VIDIOC_DQBUF, &cam_buf) < 0) {
			perror("VIDIOC_DQBUF");
			exit(1);
		}

		printf("DQBUF: index=%d, seq=%d, time=%d.%06d\n", cam_buf.index, cam_buf.sequence, cam_buf.timestamp.tv_sec, cam_buf.timestamp.tv_usec);

		src_size = cam_buf.length;

		if(v4lconvert_convert(lib, &fmt, &dst_fmt, (void*)buf_pointer[cam_buf.index], src_size, yuv420_buf, yuv420_size) <= 0){
			perror("v4lconvert_convert");
			exit(1);
		}

		cam_buf.length = buffersize;
		if(ioctl(fd_cam, VIDIOC_QBUF, &cam_buf) < 0) {
			perror("VIDIOC_QBUF");
			exit(1);
		}

		write(fd_out, yuv420_buf, yuv420_size);
	}

	if(ioctl(fd_cam, VIDIOC_STREAMOFF, &reqbuf.type) < 0) {
		perror("VIDIOC_STREAMOFF");
		exit(1);
	}

	free(yuv420_buf);

	v4lconvert_destroy(lib);

	free_buf_mmap();
}
int main()
{
	page_size = sysconf(_SC_PAGESIZE);

	fd_cam = open(cam_name, O_RDWR, 0);
	fd_out = open(out_name, O_RDWR|O_CREAT);

	if(fd_cam < 0 || fd_out < 0 || errno){
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

	capture();

	close(fd_cam);
	close(fd_out);
}
