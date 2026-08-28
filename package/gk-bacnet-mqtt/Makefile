include $(TOPDIR)/rules.mk

PKG_NAME:=gk-bacnet-mqtt
PKG_VERSION:=1.0.0
PKG_RELEASE:=4

include $(INCLUDE_DIR)/package.mk

define Package/gk-bacnet-mqtt
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=GK BACnet to MQTT Gateway
  DEPENDS:=+libmosquitto-ssl +libatomic
endef

define Package/gk-bacnet-mqtt/description
 Native BACnet/IP to MQTT gateway for Teltonika RUT2M RutOS devices.
 The gateway binary statically includes its own BACnet Stack runtime and
 does not depend on Teltonika lib-bacnet or the BACnet Router package.
endef

define Build/Compile
endef

define Package/gk-bacnet-mqtt/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/gk-bacnet-mqtt $(1)/usr/sbin/gk-bacnet-mqtt
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/gk-bacnet-mqtt.config $(1)/etc/config/gk-bacnet-mqtt
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/gk-bacnet-mqtt.init $(1)/etc/init.d/gk-bacnet-mqtt
endef

$(eval $(call BuildPackage,gk-bacnet-mqtt))
