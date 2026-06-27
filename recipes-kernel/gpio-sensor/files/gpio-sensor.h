/*
 * gpio-sensor.h — shared ioctl interface between kernel driver and userspace.
 *
 * Include guards and a __KERNEL__ conditional let the same file compile
 * cleanly in both contexts:
 *   kernel : #include "gpio-sensor.h"   (picks up linux/ioctl.h)
 *   userspace : #include <gpio-sensor.h> (picks up sys/ioctl.h)
 */

#ifndef GPIO_SENSOR_H
#define GPIO_SENSOR_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

/*
 * Magic number — must be unique per driver to avoid collisions.
 * 'G' (0x47) is used here; a production driver would claim a number
 * from Documentation/userspace-api/ioctl/ioctl-number.rst.
 */
#define GPIO_SENSOR_MAGIC  'G'

/*
 * _IO(type, nr)           — no argument (just a command signal)
 * _IOW(type, nr, T)       — write T from userspace into kernel
 * _IOR(type, nr, T)       — read T from kernel into userspace
 *
 * The kernel encodes direction + size into the top bits of the cmd word;
 * the driver checks this via _IOC_DIR/_IOC_SIZE macros.
 */

/* Reset LCG seed and read counter to initial state */
#define GPIO_SENSOR_RESET     _IO(GPIO_SENSOR_MAGIC,  0)

/* Set sampling mode — pass one of the GPIO_SENSOR_MODE_* constants */
#define GPIO_SENSOR_SET_MODE  _IOW(GPIO_SENSOR_MAGIC, 1, unsigned int)

/* Get total number of read() calls since last reset */
#define GPIO_SENSOR_GET_COUNT _IOR(GPIO_SENSOR_MAGIC, 2, unsigned int)

/* Sampling mode constants */
#define GPIO_SENSOR_MODE_NORMAL  0   /* 10-bit ADC range [0, 1023] */
#define GPIO_SENSOR_MODE_FAST    1   /*  8-bit range [0, 255]  — low-res */
#define GPIO_SENSOR_MODE_SLOW    2   /* 12-bit range [0, 4095] — high-res */

#endif /* GPIO_SENSOR_H */
