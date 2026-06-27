/*
 * gpio-sensor.c — simulated GPIO sensor character device
 *
 * /dev/gpio-sensor: each read() returns one ADC sample as decimal text.
 * ioctl() supports RESET, SET_MODE, and GET_COUNT (see gpio-sensor.h).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include "gpio-sensor.h"

#define DEVICE_NAME "gpio-sensor"
#define CLASS_NAME  "gpio_sensor"
#define BUF_SIZE    32

static dev_t   dev_num;
static struct cdev   gpio_cdev;
static struct class *gpio_class;
static struct device *gpio_dev;

/* Simulated sensor state -------------------------------------------------- */

static u32 sensor_seed    = 0x12345678;
static u32 sensor_counter = 0;
static unsigned int sensor_mode = GPIO_SENSOR_MODE_NORMAL;

/*
 * LCG step — same constants as many C stdlib rand() implementations.
 * We discard low bits (poor randomness) and mask to the range implied
 * by the current mode.
 */
static u32 next_reading(void)
{
	sensor_seed = sensor_seed * 1664525u + 1013904223u;
	switch (sensor_mode) {
	case GPIO_SENSOR_MODE_FAST:
		return (sensor_seed >> 24) & 0xFF;   /* 8-bit  [0, 255]  */
	case GPIO_SENSOR_MODE_SLOW:
		return (sensor_seed >> 20) & 0xFFF;  /* 12-bit [0, 4095] */
	default:
		return (sensor_seed >> 22) & 0x3FF;  /* 10-bit [0, 1023] */
	}
}

/* file_operations callbacks ----------------------------------------------- */

static int gpio_open(struct inode *inode, struct file *file)
{
	pr_info(DEVICE_NAME ": open\n");
	return 0;
}

static int gpio_release(struct inode *inode, struct file *file)
{
	pr_info(DEVICE_NAME ": release\n");
	return 0;
}

static ssize_t gpio_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	char tmp[BUF_SIZE];
	int  len;
	u32  val;

	if (*ppos > 0)
		return 0;

	val = next_reading();
	sensor_counter++;

	len = snprintf(tmp, sizeof(tmp), "%u\n", val);
	if (count < (size_t)len)
		return -EINVAL;
	if (copy_to_user(buf, tmp, len))
		return -EFAULT;

	*ppos = len;
	return len;
}

static ssize_t gpio_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	pr_info(DEVICE_NAME ": write (%zu bytes ignored)\n", count);
	return count;
}

/*
 * ioctl handler.
 *
 * unlocked_ioctl is the modern interface (no Big Kernel Lock).
 * cmd encodes direction + size + magic + number; arg is either a value
 * or a userspace pointer depending on the direction bits.
 *
 * get_user / put_user copy a single scalar across the user/kernel boundary
 * — cheaper than copy_to/from_user for a single word, and they check
 * alignment and address validity automatically.
 */
static long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int val;

	/* Reject commands with the wrong magic number */
	if (_IOC_TYPE(cmd) != GPIO_SENSOR_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case GPIO_SENSOR_RESET:
		sensor_seed    = 0x12345678;
		sensor_counter = 0;
		pr_info(DEVICE_NAME ": reset\n");
		return 0;

	case GPIO_SENSOR_SET_MODE:
		/*
		 * _IOW means userspace wrote a value into arg.
		 * We cast arg to a pointer and use get_user() to fetch
		 * the unsigned int safely from userspace memory.
		 */
		if (get_user(val, (unsigned int __user *)arg))
			return -EFAULT;
		if (val > GPIO_SENSOR_MODE_SLOW)
			return -EINVAL;
		sensor_mode = val;
		pr_info(DEVICE_NAME ": mode → %u\n", sensor_mode);
		return 0;

	case GPIO_SENSOR_GET_COUNT:
		/*
		 * _IOR means we're returning a value to userspace.
		 * put_user() writes the scalar to the userspace address.
		 */
		val = sensor_counter;
		if (put_user(val, (unsigned int __user *)arg))
			return -EFAULT;
		return 0;

	default:
		/*
		 * -ENOTTY ("not a typewriter") is the POSIX-mandated return
		 * for an unrecognised ioctl — do not return -EINVAL here.
		 */
		return -ENOTTY;
	}
}

static const struct file_operations gpio_fops = {
	.owner          = THIS_MODULE,
	.open           = gpio_open,
	.release        = gpio_release,
	.read           = gpio_read,
	.write          = gpio_write,
	.unlocked_ioctl = gpio_ioctl,
};

/* Module init / exit ------------------------------------------------------- */

static int __init gpio_sensor_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err(DEVICE_NAME ": alloc_chrdev_region failed (%d)\n", ret);
		return ret;
	}

	cdev_init(&gpio_cdev, &gpio_fops);
	gpio_cdev.owner = THIS_MODULE;

	ret = cdev_add(&gpio_cdev, dev_num, 1);
	if (ret < 0) {
		pr_err(DEVICE_NAME ": cdev_add failed (%d)\n", ret);
		goto err_cdev;
	}

	gpio_class = class_create(CLASS_NAME);
	if (IS_ERR(gpio_class)) {
		ret = PTR_ERR(gpio_class);
		pr_err(DEVICE_NAME ": class_create failed (%d)\n", ret);
		goto err_class;
	}

	gpio_dev = device_create(gpio_class, NULL, dev_num, NULL, DEVICE_NAME);
	if (IS_ERR(gpio_dev)) {
		ret = PTR_ERR(gpio_dev);
		pr_err(DEVICE_NAME ": device_create failed (%d)\n", ret);
		goto err_device;
	}

	pr_info(DEVICE_NAME ": loaded  major=%d minor=%d\n",
		MAJOR(dev_num), MINOR(dev_num));
	return 0;

err_device:
	class_destroy(gpio_class);
err_class:
	cdev_del(&gpio_cdev);
err_cdev:
	unregister_chrdev_region(dev_num, 1);
	return ret;
}

static void __exit gpio_sensor_exit(void)
{
	device_destroy(gpio_class, dev_num);
	class_destroy(gpio_class);
	cdev_del(&gpio_cdev);
	unregister_chrdev_region(dev_num, 1);
	pr_info(DEVICE_NAME ": unloaded\n");
}

module_init(gpio_sensor_init);
module_exit(gpio_sensor_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simulated GPIO sensor char device with ioctl");
MODULE_AUTHOR("Custom Yocto Layer");
