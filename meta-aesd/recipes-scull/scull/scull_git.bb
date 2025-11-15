# Recipe created by recipetool
# This is the basis of a recipe and may need further editing in order to be fully functional.
# (Feel free to remove these comments when editing.)

# WARNING: the following LICENSE and LIC_FILES_CHKSUM values are best guesses - it is
# your responsibility to verify that the values are complete and correct.
#
# The following license files were not able to be identified and are
# represented as "Unknown" below, you will need to check them yourself:
#   LICENSE
LICENSE = "Unknown"
LIC_FILES_CHKSUM = "file://LICENSE;md5=f098732a73b5f6f3430472f5b094ffdb"

SRC_URI = "git://github.com/cu-ecen-aeld/ldd3.git;protocol=https;branch=master"
SRC_URI += "file://0001-Build-only-scull-and-misc-modules.patch"
SRC_URI += "file://scull-init"
# Modify these as desired
PV = "1.0+git${SRCPV}"
SRCREV = "5c3cae6ddc96b8645dfa6f6bc4ddbba08aae8789"

S = "${WORKDIR}/git"

inherit module

EXTRA_OEMAKE:append:task-install = " -C ${STAGING_KERNEL_DIR} M=${S}/scull"
EXTRA_OEMAKE += "KERNELDIR=${STAGING_KERNEL_DIR}"


inherit update-rc.d

INITSCRIPT_NAME = "scull"
INITSCRIPT_PARAMS = "defaults 97"

do_install() {
    install -d ${D}${base_libdir}/modules/${KERNEL_VERSION}/extra
    install -m 0644 ${S}/scull/*.ko ${D}${base_libdir}/modules/${KERNEL_VERSION}/extra/
}
do_install:append() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/scull-init ${D}${sysconfdir}/init.d/scull
}
FILES:${PN} += "${sysconfdir}/init.d/scull"
