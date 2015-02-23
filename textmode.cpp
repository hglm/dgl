#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <sys/ioctl.h>

int main(int arc, char *argv[]) {
	int kd_fd = open("/dev/tty0", O_RDWR);
        if (kd_fd < 0) {
		printf("Cannot open /dev/tty0. Are you root?\n");
		exit(1);
	}
	int r = ioctl(kd_fd, KDSETMODE, KD_TEXT);
	if (r != 0) {
		printf("Set textmode ioctl failed.\n");
		exit(1);
	}
	printf("Succesfully set console text mode.\n");
	exit(0);
}

