################################################################################
#
# thesis apps
#
################################################################################

APPS_VERSION = 1.0
APPS_SOURCE = src
APPS_SITE = $(APPS_PKGDIR)/src
APPS_SITE_METHOD = local

define APPS_BUILD_CMDS
    $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) app
endef

define APPS_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/app $(TARGET_DIR)/usr/bin
    $(INSTALL) -D -m 0755 $(@D)/S80app $(TARGET_DIR)/etc/init.d
endef

$(eval $(generic-package))
