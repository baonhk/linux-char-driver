/*
 * mydevice_ioctl.h
 *
 * Shared ioctl definitions between the kernel driver (hello.c) and the
 * userspace test application (app/test.c).
 *
 * This header must be included by BOTH sides so that the ioctl "magic
 * number" and command numbers always match.
 */

#ifndef MYDEVICE_IOCTL_H
#define MYDEVICE_IOCTL_H

#include <linux/ioctl.h>

/* Unique "magic" number for this driver's ioctl family.
 * Must not collide with other drivers on the system (see
 * Documentation/userspace-api/ioctl/ioctl-number.rst in kernel source). */
#define MYDEV_IOC_MAGIC   'k'

/* Turn the (virtual) LED on */
#define MYDEV_IOC_LED_ON     _IO(MYDEV_IOC_MAGIC, 1)

/* Turn the (virtual) LED off */
#define MYDEV_IOC_LED_OFF    _IO(MYDEV_IOC_MAGIC, 2)

/* Read back current LED status (0 = off, 1 = on) */
#define MYDEV_IOC_GET_STATUS _IOR(MYDEV_IOC_MAGIC, 3, int)

/* Reset internal message buffer */
#define MYDEV_IOC_RESET_BUF  _IO(MYDEV_IOC_MAGIC, 4)

#define MYDEV_IOC_MAXNR 4

#endif /* MYDEVICE_IOCTL_H */
