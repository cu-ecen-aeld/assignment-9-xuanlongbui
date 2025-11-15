# NOTE: multiple licenses have been detected; they have been separated with &
# in the LICENSE value for now since it is a reasonable assumption that all
# of the licenses apply. If instead there is a choice between the multiple
# licenses then you should change the value to separate the licenses with |
# instead of &. If there is any doubt, check the accompanying documentation
# to determine which situation is applicable.
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = ""

SRC_URI = "gitsm://github.com/cu-ecen-aeld/assignments-3-and-later-xuanlongbui.git;protocol=https;branch=master"
SRC_URI += "file://aesdchar"
# Modify these as desired
PV = "1.0+git${SRCPV}"
SRCREV = "f7a14d3473d517c8f71c7499d537ddca6cdffc68"

S = "${WORKDIR}/git/aesd-char-driver"

inherit module
inherit update-rc.d

INITSCRIPT_NAME = "aesdchar"
INITSCRIPT_PARAMS = "defaults 96"

EXTRA_OEMAKE += " -C ${STAGING_KERNEL_DIR} M=${S} KERNELDIR=${STAGING_KERNEL_DIR}"

do_install() {
    install -d ${D}${base_libdir}/modules/${KERNEL_VERSION}/extra
    install -m 0644 ${S}/*.ko ${D}${base_libdir}/modules/${KERNEL_VERSION}/extra/
    install -m 0755 ${S}/aesdchar_load ${D}${base_libdir}/modules/${KERNEL_VERSION}/extra/
    install -m 0755 ${S}/aesdchar_unload ${D}${base_libdir}/modules/${KERNEL_VERSION}/extra/
}

do_install:append() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/aesdchar ${D}${sysconfdir}/init.d/${INITSCRIPT_NAME}
}
FILES:${PN} += " \
    ${base_libdir}/modules/${KERNEL_VERSION}/extra/aesdchar_load \
    ${base_libdir}/modules/${KERNEL_VERSION}/extra/aesdchar_unload \
    ${sysconfdir}/init.d/aesdchar \
"
# pkg_postinst:${PN} () {
#     if [ "x$D" != "x" ]; then
#         exit 0
#     fi
#     echo "Running depmod on target..."
#     depmod -a
#     update-rc.d ${INITSCRIPT_NAME} defaults 96
# }
    