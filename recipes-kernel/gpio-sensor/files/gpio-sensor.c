/*
 * gpio-sensor.c — simulated GPIO sensor character device
 *
 * /dev/gpio-sensor : each read() returns one ADC sample as decimal text.
 * ioctl()          : RESET, SET_MODE, GET_COUNT  (see gpio-sensor.h).
 * sysfs            : /sys/class/gpio_sensor/gpio-sensor/{mode,count}
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

static u32 next_reading(void)
{
	sensor_seed = sensor_seed * 1664525u + 1013904223u;
	switch (sensor_mode) {
	case GPIO_SENSOR_MODE_FAST:
		return (sensor_seed >> 24) & 0xFF;
	case GPIO_SENSOR_MODE_SLOW:
		return (sensor_seed >> 20) & 0xFFF;
	default:
		return (sensor_seed >> 22) & 0x3FF;
	}
}

/* sysfs attributes --------------------------------------------------------
 *
 * DEVICE_ATTR_RW(mode) expands to:
 *   - mode_show()  called on  cat /sys/.../mode
 *   - mode_store() called on  echo N > /sys/.../mode
 *   - struct device_attribute dev_attr_mode
 *
 * DEVICE_ATTR_RO(count) expands to count_show() + dev_attr_count (no store).
 *
 * sysfs_emit() is the modern replacement for scnprintf(buf, PAGE_SIZE, ...)
 * in show functions — it checks bounds and appends a newline if missing.
 *
 * kstrtouint() parses an ASCII integer from userspace safely, returning
 * -EINVAL / -ERANGE on bad input so the caller can return it directly.
 */

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "%u\n", sensor_mode);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);  /* base 0: auto-detect 0x/0/decimal */
	if (ret)
		return ret;
	if (val > GPIO_SENSOR_MODE_SLOW)
		return -EINVAL;

	sensor_mode = val;
	pr_info(DEVICE_NAME ": sysfs mode → %u\n", sensor_mode);
	return count;
}
static DEVICE_ATTR_RW(mode);

static ssize_t count_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	return sysfs_emit(buf, "%u\n", sensor_counter);
}
static DEVICE_ATTR_RO(count);

/*
 * Collect our attrs into a group.  ATTRIBUTE_GROUPS(gpio_sensor) expands to:
 *   struct attribute_group gpio_sensor_group       = { .attrs = gpio_sensor_attrs }
 *   const struct attribute_group *gpio_sensor_groups[] = { &gpio_sensor_group, NULL }
 *
 * Passing gpio_sensor_groups to device_create_with_groups() makes the kernel
 * register and unregister them together with the device — no manual
 * device_create_file / device_remove_file calls needed.
 */
static struct attribute *gpio_sensor_attrs[] = {
	&dev_attr_mode.attr,
	&dev_attr_count.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gpio_sensor);

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

static long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int val;

	if (_IOC_TYPE(cmd) != GPIO_SENSOR_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case GPIO_SENSOR_RESET:
		sensor_seed    = 0x12345678;
		sensor_counter = 0;
		pr_info(DEVICE_NAME ": reset\n");
		return 0;

	case GPIO_SENSOR_SET_MODE:
		if (get_user(val, (unsigned int __user *)arg))
			return -EFAULT;
		if (val > GPIO_SENSOR_MODE_SLOW)
			return -EINVAL;
		sensor_mode = val;
		pr_info(DEVICE_NAME ": ioctl mode → %u\n", sensor_mode);
		return 0;

	case GPIO_SENSOR_GET_COUNT:
		val = sensor_counter;
		if (put_user(val, (unsigned int __user *)arg))
			return -EFAULT;
		return 0;

	default:
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
	/*
	 * Without an explicit .llseek, vfs_llseek() falls back to no_llseek
	 * which returns -ESPIPE on any lseek() call.  default_llseek simply
	 * updates file->f_pos, which our read() uses via *ppos to give one
	 * fresh sample per read() without reopening the fd.
	 */
	.llseek         = default_llseek,
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

	/*
	 * device_create_with_groups() is like device_create() but also
	 * registers our sysfs attribute group.  The kernel tears the attrs
	 * down automatically when device_destroy() is called, so init and
	 * exit stay symmetric with no extra file-management code.
	 */
	gpio_dev = device_create_with_groups(gpio_class, NULL, dev_num, NULL,
					     gpio_sensor_groups, DEVICE_NAME);
	if (IS_ERR(gpio_dev)) {
		ret = PTR_ERR(gpio_dev);
		pr_err(DEVICE_NAME ": device_create_with_groups failed (%d)\n", ret);
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
	device_destroy(gpio_class, dev_num);   /* also removes sysfs attrs */
	class_destroy(gpio_class);
	cdev_del(&gpio_cdev);
	unregister_chrdev_region(dev_num, 1);
	pr_info(DEVICE_NAME ": unloaded\n");
}

module_init(gpio_sensor_init);
module_exit(gpio_sensor_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simulated GPIO sensor char device with ioctl + sysfs");
MODULE_AUTHOR("Custom Yocto Layer");
