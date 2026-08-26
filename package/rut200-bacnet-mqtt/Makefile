include $(TOPDIR)/rules.mk

PKG_NAME:=rut200-bacnet-mqtt
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/rut200-bacnet-mqtt
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=RUT200 BACnet to MQTT Gateway
  DEPENDS:=+libbacnet-teltonika +libmosquitto-ssl +libatomic
endef

define Package/rut200-bacnet-mqtt/description
 Native BACnet/IP to MQTT gateway for Teltonika RUT200.
endef

define Build/Compile
endef

define Package/rut200-bacnet-mqtt/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/rut200-bacnet-mqtt $(1)/usr/sbin/rut200-bacnet-mqtt
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/rut200-bacnet-mqtt.config $(1)/etc/config/rut200-bacnet-mqtt
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/rut200-bacnet-mqtt.init $(1)/etc/init.d/rut200-bacnet-mqtt
endef

$(eval $(call BuildPackage,rut200-bacnet-mqtt))
