include $(TOPDIR)/rules.mk

PKG_NAME:=gk-bacnet-mqtt
PKG_VERSION:=1.0.0
PKG_RELEASE:=12

include $(INCLUDE_DIR)/package.mk

define Package/gk-bacnet-mqtt
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=GK BACnet to MQTT Gateway
  DEPENDS:=+libmosquitto-ssl +libatomic +api-core
endef

define Package/gk-bacnet-mqtt/description
 Native BACnet/IP to MQTT gateway for Teltonika RUT2M RutOS devices.
 Includes the gateway daemon, UCI configuration, VuCI API and compiled VuCI UI.
 Statically links bacnet-stack 1.3.8 (BACDL_BIP) instead of a Teltonika
 runtime dependency.
endef

define Build/Compile
endef

define Package/gk-bacnet-mqtt/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/gk-bacnet-mqtt $(1)/usr/sbin/gk-bacnet-mqtt
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/gk-bacnet-mqtt.config $(1)/etc/config/gk_bacnet_mqtt
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/gk-bacnet-mqtt.init $(1)/etc/init.d/gk-bacnet-mqtt

	# The cached VuCI package graph still traverses this legacy core target.
	# Runtime API/UI data is optional here because the final single runtime package is VuCI.
	if [ -d ./runtime/api/services ]; then \
		$(INSTALL_DIR) $(1)/usr/lib/lua/api/services; \
		$(CP) ./runtime/api/services/*.lua $(1)/usr/lib/lua/api/services/; \
	fi
	if [ -d ./runtime/vuci/path.d ]; then \
		$(INSTALL_DIR) $(1)/usr/share/vuci/path.d; \
		$(CP) ./runtime/vuci/path.d/*.json $(1)/usr/share/vuci/path.d/; \
	fi
	if [ -d ./runtime/rpcd/acl.d ]; then \
		$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d; \
		$(CP) ./runtime/rpcd/acl.d/*.json $(1)/usr/share/rpcd/acl.d/; \
	fi
	if [ -d ./runtime/vuci/menu.d ]; then \
		$(INSTALL_DIR) $(1)/usr/share/vuci/menu.d; \
		$(CP) ./runtime/vuci/menu.d/*.json $(1)/usr/share/vuci/menu.d/; \
	fi
	if [ -d ./runtime/www ]; then \
		$(INSTALL_DIR) $(1)/www; \
		$(CP) ./runtime/www/* $(1)/www/; \
	fi
endef

define Package/gk-bacnet-mqtt/postinst
#!/bin/sh
ROOT="$${IPKG_INSTROOT:-}"
if [ -z "$$ROOT" ] || [ "$$ROOT" = "/usr/local" ]; then
	/etc/init.d/rpcd restart >/dev/null 2>&1 || true
fi
exit 0
endef

$(eval $(call BuildPackage,gk-bacnet-mqtt))
