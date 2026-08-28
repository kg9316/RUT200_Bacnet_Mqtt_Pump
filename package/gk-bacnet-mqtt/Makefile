include $(TOPDIR)/rules.mk

PKG_NAME:=gk-bacnet-mqtt
PKG_VERSION:=1.0.0
PKG_RELEASE:=7

include $(INCLUDE_DIR)/package.mk

define Package/gk-bacnet-mqtt
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=GK BACnet to MQTT Gateway
  DEPENDS:=+libmosquitto-ssl +libatomic +lib-bacnet
endef

define Package/gk-bacnet-mqtt/description
 Native BACnet/IP to MQTT gateway for Teltonika RUT2M RutOS devices.
 Uses the Teltonika lib-bacnet runtime package matching RUT2M_R_00.07.24.2.
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
endef

# RutOS Package Manager installs third-party bundle payloads below /usr/local.
# Expose the daemon, init script and UCI config at the normal OpenWrt paths so
# procd, uci and VuCI ConfigService can use the package normally. Direct opkg
# installs already land at the normal paths and therefore need no links.
define Package/gk-bacnet-mqtt/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0

if [ -x /usr/local/usr/sbin/gk-bacnet-mqtt ]; then
	mkdir -p /usr/sbin /etc/init.d /etc/config
	ln -sf /usr/local/usr/sbin/gk-bacnet-mqtt /usr/sbin/gk-bacnet-mqtt
	ln -sf /usr/local/etc/init.d/gk-bacnet-mqtt /etc/init.d/gk-bacnet-mqtt

	# Preserve an existing real UCI config on upgrades. Otherwise expose the
	# Package Manager config from /usr/local at the standard UCI location.
	if [ ! -e /etc/config/gk_bacnet_mqtt ]; then
		ln -s /usr/local/etc/config/gk_bacnet_mqtt /etc/config/gk_bacnet_mqtt
	fi
fi

if [ -x /etc/init.d/gk-bacnet-mqtt ]; then
	/etc/init.d/gk-bacnet-mqtt enable >/dev/null 2>&1 || true
	/etc/init.d/gk-bacnet-mqtt restart >/dev/null 2>&1 || true
fi

exit 0
endef

define Package/gk-bacnet-mqtt/prerm
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0

if [ -x /etc/init.d/gk-bacnet-mqtt ]; then
	/etc/init.d/gk-bacnet-mqtt stop >/dev/null 2>&1 || true
	/etc/init.d/gk-bacnet-mqtt disable >/dev/null 2>&1 || true
fi

[ -L /usr/sbin/gk-bacnet-mqtt ] && rm -f /usr/sbin/gk-bacnet-mqtt
[ -L /etc/init.d/gk-bacnet-mqtt ] && rm -f /etc/init.d/gk-bacnet-mqtt
[ -L /etc/config/gk_bacnet_mqtt ] && rm -f /etc/config/gk_bacnet_mqtt

exit 0
endef

$(eval $(call BuildPackage,gk-bacnet-mqtt))
