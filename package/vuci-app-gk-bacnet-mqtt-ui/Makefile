include $(TOPDIR)/rules.mk

APP_TITLE:=GK BACnet MQTT
APP_NAME:=$(notdir ${CURDIR})
APP_DEPENDS:=+gk-bacnet-mqtt
CLOSED_GPL_INSTALL:=y

PKG_VERSION:=1.0.0
PKG_RELEASE:=3

# Expose the application as a Teltonika third-party Package Manager app.
# These fields are intentionally only present on the UI package.
ifneq (,$(filter m,$(CONFIG_PACKAGE_$(APP_NAME)))$(DUMP))
	APP_TLT_NAME:=GK BACnet MQTT
	APP_ROUTER:=$(TLT_PLATFORM_NAME)
endif

define Package/$(APP_NAME)/description
	GK BACnet MQTT configuration, runtime status and log UI.
endef

include ../app.mk
