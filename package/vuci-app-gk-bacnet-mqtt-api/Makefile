include $(TOPDIR)/rules.mk

PKG_NAME:=vuci-app-gk-bacnet-mqtt-api
PKG_VERSION:=1.0.0
PKG_RELEASE:=4

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

# RutOS Package Manager installs third-party payload below /usr/local and may
# invoke postinst with IPKG_INSTROOT=/usr/local. Treat that as a live PM install.
# Only skip true firmware/image staging roots.
define Package/$(PKG_NAME)/postinst
#!/bin/sh
ROOT="$${IPKG_INSTROOT:-}"
if [ -n "$$ROOT" ] && [ "$$ROOT" != "/usr/local" ]; then
	exit 0
fi

copy_if_present() {
	src="$$1"
	dst="$$2"
	if [ -f "$$src" ]; then
		mkdir -p "$${dst%/*}"
		cp -f "$$src" "$$dst"
		return 0
	fi
	return 1
}

# API Lua services. With Package Manager the payload is under /usr/local/usr/...
# With direct opkg the canonical files already exist under /usr/lib/....
copy_if_present /usr/local/usr/lib/lua/api/services/gk_bacnet_mqtt_config.lua \
	/usr/lib/lua/api/services/gk_bacnet_mqtt_config.lua || true
copy_if_present /usr/local/usr/lib/lua/api/services/gk_bacnet_mqtt_status.lua \
	/usr/lib/lua/api/services/gk_bacnet_mqtt_status.lua || true

# Route and ACL metadata must be visible to the system VuCI/rpcd loaders.
for base in /usr/local/usr/share /usr/local/share /usr/share; do
	if [ -f "$$base/vuci/path.d/gk-bacnet-mqtt.json" ]; then
		mkdir -p /usr/share/vuci/path.d
		if [ "$$base/vuci/path.d/gk-bacnet-mqtt.json" != "/usr/share/vuci/path.d/gk-bacnet-mqtt.json" ]; then
			cp -f "$$base/vuci/path.d/gk-bacnet-mqtt.json" /usr/share/vuci/path.d/gk-bacnet-mqtt.json
		fi
		break
	fi
done

for base in /usr/local/usr/share /usr/local/share /usr/share; do
	if [ -f "$$base/rpcd/acl.d/gk-bacnet-mqtt.json" ]; then
		mkdir -p /usr/share/rpcd/acl.d
		if [ "$$base/rpcd/acl.d/gk-bacnet-mqtt.json" != "/usr/share/rpcd/acl.d/gk-bacnet-mqtt.json" ]; then
			cp -f "$$base/rpcd/acl.d/gk-bacnet-mqtt.json" /usr/share/rpcd/acl.d/gk-bacnet-mqtt.json
		fi
		break
	fi
done

/etc/init.d/rpcd restart >/dev/null 2>&1 || true
exit 0
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
