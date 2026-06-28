SUMMARY = "Userspace demo application for the gpio-sensor driver"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Pull gpio-sensor.h into the cross-sysroot so the compiler can find it
# via #include <gpio-sensor.h>.  The header is installed by the driver
# recipe's do_install:append into ${STAGING_INCDIR}.
DEPENDS = "gpio-sensor"

SRC_URI = "file://gpio-sensor-app.cpp"

S = "${WORKDIR}"

do_compile() {
    ${CXX} ${CXXFLAGS} ${LDFLAGS} gpio-sensor-app.cpp -o gpio-sensor-app
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 gpio-sensor-app ${D}${bindir}/gpio-sensor-app
}
