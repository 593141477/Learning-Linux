#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;

int main(int argc, char **argv)
{
	int fd, bpp, width, height, size;
	int i, j, k;
	unsigned int *buf;

	fd = open("/dev/fb0", O_RDWR);
	if(fd < 0 || errno) {
		perror("open");
		exit(1);
	}

	if(ioctl(fd, FBIOGET_FSCREENINFO, &finfo)
		|| ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) || errno) {
		perror("ioctl");
		exit(1);
	}

	if(ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK) || errno) {
		perror("ioctl");
		exit(1);
	}

	bpp = vinfo.bits_per_pixel;
	//bpp = vinfo.red.length + vinfo.green.length + vinfo.blue.length + vinfo.transp.length;
	width = vinfo.xres;
	height = vinfo.yres;

	printf("w: %d, h: %d, bpp: %d\n", width, height, bpp);

	size = width*height*bpp/8;

	buf = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

	if(buf <= 0 || errno) {
		perror("mmap");
		exit(1);
	}

	memset(buf, 0, size);

	void draw(uint8_t r, uint8_t g, uint8_t b) {
		int i, j, tmp;
		tmp = (r << 16)|(g << 8)|b;
		printf("color: %x\n", tmp);
		for(i=0; i<height; i++) {
			for(j=0; j<width; j++) {
				int k = i*width + j;
				buf[k] = tmp;
			}
		}
	}
	
	for(i=0; i<256; i+=16)
		for(j=0; j<256; j+=16)
			for(k=0; k<256; k+=16) {
				draw((uint8_t)i, (uint8_t)j, (uint8_t)k);
				usleep(100000);
			}

	munmap(buf, size);

	close(fd);

	puts("OK");
	return 0;
}