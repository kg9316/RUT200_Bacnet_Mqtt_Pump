include $(TOPDIR)/rules.mk

PKG_NAME:=vuci-app-gk-bacnet-mqtt-api
PKG_VERSION:=1.0.0
PKG_RELEASE:=3

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

# RutOS Package Manager relocates third-party payload below /usr/local.
# The API loader, however, loads Lua services from /usr/lib/lua/api/services.
# Install canonical runtime copies on the live device while keeping the normal
# package layout intact for direct opkg installs.
define Package/$(PKG_NAME)/postinst
#!/bin/sh
[ -n "$$IPKG_INSTROOT" ] && exit 0

copy_if_present() {
	src="$$1"
	dst="$$2"
	if [ -f "$$src" ]; then
		mkdir -p "$${dst%/*}"
		cp -f "$$src" "$$dst"
	fi
}

# API Lua services: api-core uses the canonical /usr/lib path.
copy_if_present /usr/local/usr/lib/lua/api/services/gk_bacnet_mqtt_config.lua \
	/usr/lib/lua/api/services/gk_bacnet_mqtt_config.lua
copy_if_present /usr/local/usr/lib/lua/api/services/gk_bacnet_mqtt_status.lua \
	/usr/lib/lua/api/services/gk_bacnet_mqtt_status.lua

# Route/ACL metadata can be relocated differently by Package Manager versions.
# Mirror it to both standard and RutOS local lookup locations.
for base in /usr/local/usr/share /usr/local/share /usr/share; do
	if [ -f "$$base/vuci/path.d/gk-bacnet-mqtt.json" ]; then
		mkdir -p /usr/share/vuci/path.d /usr/local/share/vuci/path.d
		cp -f "$$base/vuci/path.d/gk-bacnet-mqtt.json" /usr/share/vuci/path.d/gk-bacnet-mqtt.json
		cp -f "$$base/vuci/path.d/gk-bacnet-mqtt.json" /usr/local/share/vuci/path.d/gk-bacnet-mqtt.json
		break
	fi
done

for base in /usr/local/usr/share /usr/local/share /usr/share; do
	if [ -f "$$base/rpcd/acl.d/gk-bacnet-mqtt.json" ]; then
		mkdir -p /usr/share/rpcd/acl.d /usr/local/share/rpcd/acl.d
		cp -f "$$base/rpcd/acl.d/gk-bacnet-mqtt.json" /usr/share/rpcd/acl.d/gk-bacnet-mqtt.json
		cp -f "$$base/rpcd/acl.d/gk-bacnet-mqtt.json" /usr/local/share/rpcd/acl.d/gk-bacnet-mqtt.json
		break
	fi
done

/etc/init.d/rpcd restart >/dev/null 2>&1 || true
exit 0
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
