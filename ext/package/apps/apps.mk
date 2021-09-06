################################################################################
#
# thesis apps
#
################################################################################

APPS_VERSION = 1.0
APPS_SOURCE = src
APPS_SITE = $(BR2_EXTERNAL_thesis_PATH)/../fsw
APPS_SITE_METHOD = local
APPS_DEPENDENCIES = zlib

define APPS_BUILD_CMDS
    env $(TARGET_CONFIGURE_OPTS) scons -C $(@D) build/app build/S80app
endef

define APPS_INSTALL_TARGET_CMDS
    env $(TARGET_CONFIGURE_OPTS) scons -C $(@D) install --prefix=$(TARGET_DIR)
endef

$(eval $(generic-package))
