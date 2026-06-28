/*
 * gpio-sensor-app.cpp
 *
 * Exercises the full gpio-sensor driver stack:
 *   /dev/gpio-sensor          — char device read
 *   ioctl GET_COUNT           — query read counter
 *   ioctl SET_MODE            — change ADC resolution
 *   ioctl RESET               — reset seed + counter
 *   /sys/.../mode  (write)    — change mode via sysfs
 *   /sys/.../count (read)     — verify counter via sysfs
 *
 * Load the module before running:  modprobe gpio-sensor
 */

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <gpio-sensor.h>

static const char *DEV_PATH    = "/dev/gpio-sensor";
static const char *SYSFS_MODE  = "/sys/class/gpio_sensor/gpio-sensor/mode";
static const char *SYSFS_COUNT = "/sys/class/gpio_sensor/gpio-sensor/count";

/* ── sysfs helpers ────────────────────────────────────────────────────────── */

static std::string sysfs_read(const char *path)
{
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error(std::string("sysfs_read: cannot open ") + path);
    std::string val;
    std::getline(f, val);
    return val;
}

static void sysfs_write(const char *path, const std::string &val)
{
    std::ofstream f(path);
    if (!f)
        throw std::runtime_error(std::string("sysfs_write: cannot open ") + path);
    f << val << "\n";
    if (!f)
        throw std::runtime_error(std::string("sysfs_write: write failed on ") + path);
}

/* ── device helper ────────────────────────────────────────────────────────── */

/*
 * Take one fresh ADC sample from the char device.
 *
 * The driver sets *ppos = len after the first read(), so a second read()
 * on the same open fd would return EOF.  lseek(fd, 0, SEEK_SET) resets
 * f_pos to 0, making the driver think the file was just opened — so each
 * call to dev_read() produces a new value without reopening the device.
 */
static std::string dev_read(int fd)
{
    char buf[32];
    if (lseek(fd, 0, SEEK_SET) < 0)
        throw std::runtime_error(std::string("lseek: ") + strerror(errno));
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0)
        throw std::runtime_error(std::string("read: ") + strerror(errno));
    buf[n] = '\0';
    if (n > 0 && buf[n - 1] == '\n')
        buf[n - 1] = '\0';
    return std::string(buf);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main()
{
    std::cout << "=== gpio-sensor userspace demo ===\n\n";

    /* 1 ── open device ────────────────────────────────────────────────────── */
    int fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) {
        std::cerr << "open " << DEV_PATH << ": " << strerror(errno) << "\n";
        return 1;
    }
    std::cout << "[dev]   opened " << DEV_PATH << "  fd=" << fd << "\n";

    /* 2 ── sysfs: read initial state ──────────────────────────────────────── */
    std::cout << "[sysfs] mode  = " << sysfs_read(SYSFS_MODE)
              << "  (0=NORMAL)\n";
    std::cout << "[sysfs] count = " << sysfs_read(SYSFS_COUNT) << "\n\n";

    /* 3 ── three reads in NORMAL mode (10-bit ADC, 0–1023) ────────────────── */
    std::cout << "--- NORMAL mode (10-bit, range 0-1023) ---\n";
    for (int i = 0; i < 3; ++i)
        std::cout << "  sample[" << i << "] = " << dev_read(fd) << "\n";

    /* 4 ── ioctl: GET_COUNT ────────────────────────────────────────────────── */
    unsigned int hw_count = 0;
    if (ioctl(fd, GPIO_SENSOR_GET_COUNT, &hw_count) < 0) {
        std::cerr << "ioctl GET_COUNT: " << strerror(errno) << "\n";
        close(fd); return 1;
    }
    std::cout << "\n[ioctl] GET_COUNT  = " << hw_count << "\n";
    std::cout << "[sysfs] count      = " << sysfs_read(SYSFS_COUNT)
              << "  (ioctl and sysfs agree)\n\n";

    /* 5 ── sysfs write: switch to FAST mode (8-bit, 0–255) ─────────────────── */
    std::cout << "--- sysfs write: FAST mode (8-bit, range 0-255) ---\n";
    sysfs_write(SYSFS_MODE, std::to_string(GPIO_SENSOR_MODE_FAST));
    std::cout << "[sysfs] mode now = " << sysfs_read(SYSFS_MODE)
              << "  (1=FAST)\n";
    std::cout << "  sample         = " << dev_read(fd)
              << "  (should be ≤ 255)\n\n";

    /* 6 ── ioctl: SET_MODE → SLOW (12-bit, 0–4095) ─────────────────────────── */
    std::cout << "--- ioctl SET_MODE: SLOW mode (12-bit, range 0-4095) ---\n";
    unsigned int slow = GPIO_SENSOR_MODE_SLOW;
    if (ioctl(fd, GPIO_SENSOR_SET_MODE, &slow) < 0) {
        std::cerr << "ioctl SET_MODE: " << strerror(errno) << "\n";
        close(fd); return 1;
    }
    std::cout << "[sysfs] mode now = " << sysfs_read(SYSFS_MODE)
              << "  (2=SLOW)\n";
    std::cout << "  sample         = " << dev_read(fd)
              << "  (can be up to 4095)\n\n";

    /* 7 ── ioctl: RESET ────────────────────────────────────────────────────── */
    std::cout << "--- ioctl RESET ---\n";
    if (ioctl(fd, GPIO_SENSOR_RESET) < 0) {
        std::cerr << "ioctl RESET: " << strerror(errno) << "\n";
        close(fd); return 1;
    }
    std::cout << "[sysfs] count after reset = " << sysfs_read(SYSFS_COUNT)
              << "  (expect 0)\n";

    /* 8 ── restore NORMAL mode via sysfs ──────────────────────────────────── */
    sysfs_write(SYSFS_MODE, std::to_string(GPIO_SENSOR_MODE_NORMAL));
    std::cout << "[sysfs] mode restored to  " << sysfs_read(SYSFS_MODE)
              << "  (0=NORMAL)\n\n";

    std::cout << "=== demo complete ===\n";
    close(fd);
    return 0;
}
