include $(TOPDIR)/rules.mk

APP_NAME:=vuci-app-gk-bacnet-mqtt-ui
PKG_NAME:=$(APP_NAME)
APP_TITLE:=VuCI UI Support for GK BACnet MQTT
APP_APP_NAME:=gk-bacnet-mqtt
APP_DEPENDS:=+gk-bacnet-mqtt +vuci-app-gk-bacnet-mqtt-api
CLOSED_GPL_INSTALL:=y

ifeq (m,$(CONFIG_PACKAGE_vuci-app-gk-bacnet-mqtt-ui))
	APP_TLT_NAME:=GK BACnet MQTT
	APP_ROUTER:=$(TLT_PLATFORM_NAME)
endif

define Package/$(PKG_NAME)/description
	GK BACnet MQTT configuration, runtime status and log UI
endef

include ../app.mk
