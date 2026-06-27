SUMMARY = "Simulated GPIO sensor character device driver with ioctl interface"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = "file://gpio-sensor.c \
           file://gpio-sensor.h \
           file://Makefile"

S = "${WORKDIR}"

# Install the shared header so the userspace app recipe can find it
# via DEPENDS = "gpio-sensor" and include <gpio-sensor.h>.
do_install:append() {
    install -d ${D}${includedir}
    install -m 0644 ${S}/gpio-sensor.h ${D}${includedir}/gpio-sensor.h
}

FILES:${PN}-dev += "${includedir}/gpio-sensor.h"

RPROVIDES:${PN} += "kernel-module-gpio-sensor"
