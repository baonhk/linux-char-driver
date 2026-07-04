/*
 * test.c - Userspace test application for /dev/mydevice
 *
 * Demonstrates:
 *   - open()/close() on a character device
 *   - write() to send data into the kernel driver
 *   - read() to receive data back from the driver
 *   - ioctl() to control a simulated LED and query its status
 *   - poll() to wait for readable data without blocking on read()
 *
 * Build:
 *   gcc -Wall -o test test.c
 *   (mydevice_ioctl.h is included from ../driver/)
 *
 * Run:
 *   sudo ./test              # basic write/read/ioctl demo
 *   sudo ./test --poll       # also demonstrate poll()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>

#include "../driver/mydevice_ioctl.h"

#define DEVICE_PATH "/dev/mydevice"

static void die(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

static void demo_write_read(int fd)
{
	const char *msg = "Hello from userspace!";
	char buf[256];
	ssize_t n;

	printf("[app] write(\"%s\")\n", msg);
	n = write(fd, msg, strlen(msg));
	if (n < 0)
		die("write");
	printf("[app] wrote %zd bytes\n", n);

	memset(buf, 0, sizeof(buf));
	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0)
		die("read");
	printf("[app] read back %zd bytes: \"%s\"\n", n, buf);
}

static void demo_ioctl(int fd)
{
	int status = -1;

	printf("[app] ioctl(MYDEV_IOC_LED_ON)\n");
	if (ioctl(fd, MYDEV_IOC_LED_ON) < 0)
		die("ioctl LED_ON");

	if (ioctl(fd, MYDEV_IOC_GET_STATUS, &status) < 0)
		die("ioctl GET_STATUS");
	printf("[app] LED status = %d (expected 1)\n", status);

	printf("[app] ioctl(MYDEV_IOC_LED_OFF)\n");
	if (ioctl(fd, MYDEV_IOC_LED_OFF) < 0)
		die("ioctl LED_OFF");

	if (ioctl(fd, MYDEV_IOC_GET_STATUS, &status) < 0)
		die("ioctl GET_STATUS");
	printf("[app] LED status = %d (expected 0)\n", status);
}

static void demo_poll(int fd)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int ret;

	printf("[app] polling for up to 3s (write to /dev/mydevice from another\n"
	       "      shell with: echo -n hi | sudo tee %s)\n", DEVICE_PATH);

	ret = poll(&pfd, 1, 3000);
	if (ret < 0) {
		die("poll");
	} else if (ret == 0) {
		printf("[app] poll timed out, no data available\n");
	} else if (pfd.revents & POLLIN) {
		char buf[256] = {0};
		ssize_t n = read(fd, buf, sizeof(buf) - 1);
		printf("[app] poll woke up, read %zd bytes: \"%s\"\n", n, buf);
	}
}

int main(int argc, char *argv[])
{
	int fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0)
		die("open " DEVICE_PATH " (did you `sudo insmod hello.ko`?)");

	printf("[app] opened %s (fd=%d)\n", DEVICE_PATH, fd);

	demo_write_read(fd);
	demo_ioctl(fd);

	if (argc > 1 && strcmp(argv[1], "--poll") == 0)
		demo_poll(fd);

	close(fd);
	printf("[app] closed device\n");
	return 0;
}
