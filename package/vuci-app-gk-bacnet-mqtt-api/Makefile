include $(TOPDIR)/rules.mk

PKG_NAME:=vuci-app-gk-bacnet-mqtt-api
PKG_VERSION:=1.0.0
PKG_RELEASE:=5

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
  SECTION:=vuci
  CATEGORY:=VuCI
  SUBMENU:=Applications/API
  TITLE:=GK BACnet MQTT API
  DEPENDS:=+gk-bacnet-mqtt +api-core
endef

define Package/$(PKG_NAME)/description
  Runtime status, configuration and log API for GK BACnet MQTT.
endef

define Build/Compile
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)
	$(CP) ./files/* $(1)/
endef

# RutOS 7.14+ Package Manager owns the /usr/local overlay. Do not copy API
# payload into read-only /usr paths; rpcd/VuCI load package files via the overlay.
define Package/$(PKG_NAME)/postinst
#!/bin/sh
ROOT="$${IPKG_INSTROOT:-}"
if [ -n "$$ROOT" ] && [ "$$ROOT" != "/usr/local" ]; then
	exit 0
fi
/etc/init.d/rpcd restart >/dev/null 2>&1 || true
exit 0
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
