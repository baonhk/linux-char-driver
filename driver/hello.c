/*
 * hello.c - A complete Linux character device driver example
 *
 * Features:
 *   - Dynamic major/minor allocation (alloc_chrdev_region)
 *   - Automatic /dev node creation via class + device_create (udev)
 *   - Standard file operations: open, release, read, write
 *   - ioctl() interface: LED_ON / LED_OFF / GET_STATUS / RESET_BUF
 *   - Kernel logging via pr_info()/pr_err() (visible with dmesg)
 *   - Mutex to protect the shared internal buffer
 *   - Wait queue so read() blocks until data is available
 *   - poll()/select() support for non-blocking I/O multiplexing
 *
 * Build:  see driver/Makefile
 * Load:   sudo insmod hello.ko
 * Node:   /dev/mydevice (created automatically by udev)
 * Unload: sudo rmmod hello
 *
 * Author: (your name)
 * License: GPL
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/sched.h>  /* current */
#include <linux/err.h>    /* IS_ERR / PTR_ERR */

#include "mydevice_ioctl.h"

#define DEVICE_NAME   "mydevice"
#define CLASS_NAME    "mydevice_class"
#define BUF_SIZE      1024

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Educational Linux character device driver (chardev + ioctl + mutex + waitqueue + poll)");
MODULE_VERSION("1.0");

/* ---------- Driver private state ---------- */

static dev_t            dev_num;          /* major:minor, dynamically allocated */
static struct cdev      my_cdev;
static struct class    *my_class  = NULL;
static struct device   *my_device = NULL;

static char             msg_buf[BUF_SIZE];
static size_t           msg_len   = 0;     /* bytes currently stored in msg_buf */

static DEFINE_MUTEX(dev_mutex);            /* protects msg_buf / msg_len / led_status */
static DECLARE_WAIT_QUEUE_HEAD(read_wq);   /* readers block here until data arrives */

static int led_status = 0;                 /* 0 = OFF, 1 = ON (simulated) */

/* ---------- file_operations implementation ---------- */

static int mydevice_open(struct inode *inode, struct file *file)
{
	pr_info(DEVICE_NAME ": device opened (pid=%d)\n", current->pid);
	return 0;
}

static int mydevice_release(struct inode *inode, struct file *file)
{
	pr_info(DEVICE_NAME ": device closed (pid=%d)\n", current->pid);
	return 0;
}

/*
 * read() - copy data from the kernel buffer to userspace.
 * Blocks (unless O_NONBLOCK) until msg_len > 0, demonstrating a wait queue.
 */
static ssize_t mydevice_read(struct file *file, char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	ssize_t ret;

	if (mutex_lock_interruptible(&dev_mutex))
		return -ERESTARTSYS;

	/* Wait until there is something to read */
	while (msg_len == 0) {
		mutex_unlock(&dev_mutex);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		pr_info(DEVICE_NAME ": read() blocking, waiting for data\n");
		if (wait_event_interruptible(read_wq, msg_len > 0))
			return -ERESTARTSYS; /* interrupted by a signal */

		if (mutex_lock_interruptible(&dev_mutex))
			return -ERESTARTSYS;
	}

	if (count > msg_len)
		count = msg_len;

	if (copy_to_user(ubuf, msg_buf, count)) {
		mutex_unlock(&dev_mutex);
		return -EFAULT;
	}

	/* Simple model: each read() drains the whole buffer */
	memmove(msg_buf, msg_buf + count, msg_len - count);
	msg_len -= count;
	ret = count;

	pr_info(DEVICE_NAME ": read() returned %zd bytes\n", ret);

	mutex_unlock(&dev_mutex);
	return ret;
}

/*
 * write() - copy data from userspace into the kernel buffer and log it.
 * Wakes up any blocked readers.
 */
static ssize_t mydevice_write(struct file *file, const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	if (mutex_lock_interruptible(&dev_mutex))
		return -ERESTARTSYS;

	if (count > BUF_SIZE) {
		mutex_unlock(&dev_mutex);
		pr_err(DEVICE_NAME ": write() rejected, %zu bytes exceeds buffer size\n", count);
		return -EINVAL;
	}

	if (copy_from_user(msg_buf, ubuf, count)) {
		mutex_unlock(&dev_mutex);
		return -EFAULT;
	}

	msg_len = count;

	pr_info(DEVICE_NAME ": write() received %zu bytes: \"%.*s\"\n",
		count, (int)count, msg_buf);

	mutex_unlock(&dev_mutex);

	/* Notify any process blocked in read() or poll() */
	wake_up_interruptible(&read_wq);

	return count;
}

/*
 * unlocked_ioctl() - handle LED_ON / LED_OFF / GET_STATUS / RESET_BUF
 */
static long mydevice_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int status;

	if (_IOC_TYPE(cmd) != MYDEV_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > MYDEV_IOC_MAXNR)
		return -ENOTTY;

	if (mutex_lock_interruptible(&dev_mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case MYDEV_IOC_LED_ON:
		led_status = 1;
		pr_info(DEVICE_NAME ": ioctl LED_ON -> LED is now ON\n");
		break;

	case MYDEV_IOC_LED_OFF:
		led_status = 0;
		pr_info(DEVICE_NAME ": ioctl LED_OFF -> LED is now OFF\n");
		break;

	case MYDEV_IOC_GET_STATUS:
		status = led_status;
		mutex_unlock(&dev_mutex);
		if (copy_to_user((int __user *)arg, &status, sizeof(status)))
			return -EFAULT;
		pr_info(DEVICE_NAME ": ioctl GET_STATUS -> %d\n", status);
		return 0;

	case MYDEV_IOC_RESET_BUF:
		msg_len = 0;
		pr_info(DEVICE_NAME ": ioctl RESET_BUF -> buffer cleared\n");
		break;

	default:
		mutex_unlock(&dev_mutex);
		return -ENOTTY;
	}

	mutex_unlock(&dev_mutex);
	return 0;
}

/*
 * poll() - let userspace use select()/poll()/epoll() on this device.
 * Readable when msg_len > 0; always writable in this simple model.
 */
static __poll_t mydevice_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &read_wq, wait);

	mutex_lock(&dev_mutex);
	if (msg_len > 0)
		mask |= POLLIN | POLLRDNORM;
	mutex_unlock(&dev_mutex);

	mask |= POLLOUT | POLLWRNORM; /* writes never block in this demo */

	return mask;
}

static const struct file_operations fops = {
	.owner          = THIS_MODULE,
	.open           = mydevice_open,
	.release        = mydevice_release,
	.read           = mydevice_read,
	.write          = mydevice_write,
	.unlocked_ioctl = mydevice_ioctl,
	.poll           = mydevice_poll,
};

/* ---------- Module init / exit ---------- */

static int __init hello_init(void)
{
	int ret;

	/* 1. Dynamically allocate a major/minor number */
	ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err(DEVICE_NAME ": failed to allocate char device region\n");
		return ret;
	}
	pr_info(DEVICE_NAME ": allocated major=%d minor=%d\n",
		MAJOR(dev_num), MINOR(dev_num));

	/* 2. Initialize and add the cdev to the kernel */
	cdev_init(&my_cdev, &fops);
	my_cdev.owner = THIS_MODULE;
	ret = cdev_add(&my_cdev, dev_num, 1);
	if (ret < 0) {
		pr_err(DEVICE_NAME ": cdev_add failed\n");
		goto err_unregister;
	}

	/* 3. Create device class (visible in /sys/class) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	my_class = class_create(CLASS_NAME);
#else
	my_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
	if (IS_ERR(my_class)) {
		pr_err(DEVICE_NAME ": failed to create device class\n");
		ret = PTR_ERR(my_class);
		goto err_cdev;
	}

	/* 4. Create the /dev/mydevice node automatically via udev */
	my_device = device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME);
	if (IS_ERR(my_device)) {
		pr_err(DEVICE_NAME ": failed to create device node\n");
		ret = PTR_ERR(my_device);
		goto err_class;
	}

	pr_info(DEVICE_NAME ": module loaded, /dev/%s is ready\n", DEVICE_NAME);
	return 0;

err_class:
	class_destroy(my_class);
err_cdev:
	cdev_del(&my_cdev);
err_unregister:
	unregister_chrdev_region(dev_num, 1);
	return ret;
}

static void __exit hello_exit(void)
{
	device_destroy(my_class, dev_num);
	class_destroy(my_class);
	cdev_del(&my_cdev);
	unregister_chrdev_region(dev_num, 1);

	pr_info(DEVICE_NAME ": module unloaded\n");
}

module_init(hello_init);
module_exit(hello_exit);
