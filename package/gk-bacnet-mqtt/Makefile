include $(TOPDIR)/rules.mk

PKG_NAME:=gk-bacnet-mqtt
PKG_VERSION:=1.0.0
PKG_RELEASE:=6

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

$(eval $(call BuildPackage,gk-bacnet-mqtt))
