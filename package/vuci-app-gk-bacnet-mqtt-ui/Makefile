include $(TOPDIR)/rules.mk

APP_TITLE:=GK BACnet MQTT
APP_DEPENDS:=+gk-bacnet-mqtt

ifneq (,$(filter m,$(CONFIG_PACKAGE_vuci-app-gk-bacnet-mqtt-ui))$(DUMP))
	APP_TLT_NAME:=GK BACnet MQTT
	APP_ROUTER:=$(TLT_PLATFORM_NAME)
endif

include ../app.mk

define Package/vuci-app-gk-bacnet-mqtt-ui/description
	GK BACnet MQTT configuration, runtime status and log UI.
endef

# call BuildPackage - OpenWrt buildroot signature
