include $(TOPDIR)/rules.mk

APP_TITLE:=GK BACnet MQTT
APP_NAME:=$(notdir ${CURDIR})
APP_DEPENDS:=+gk-bacnet-mqtt
CLOSED_GPL_INSTALL:=y

PKG_VERSION:=1.0.0
PKG_RELEASE:=2

define Package/$(APP_NAME)/description
	GK BACnet MQTT configuration, runtime status and log UI.
endef

include ../app.mk
