include $(TOPDIR)/rules.mk

PKG_NAME:=vuci-app-gk-bacnet-mqtt-ui
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
  SECTION:=vuci
  CATEGORY:=VuCI
  SUBMENU:=Applications/UI
  TITLE:=GK BACnet MQTT UI
  DEPENDS:=+gk-bacnet-mqtt +vuci-app-gk-bacnet-mqtt-api
endef

define Package/$(PKG_NAME)/description
  GK BACnet MQTT configuration, runtime status and log UI.
endef

define Build/Compile
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)
	$(CP) ./files/* $(1)/
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
