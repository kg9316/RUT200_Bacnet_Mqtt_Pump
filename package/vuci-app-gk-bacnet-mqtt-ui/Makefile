include $(TOPDIR)/rules.mk

APP_TITLE:=GK BACnet MQTT
APP_NAME:=$(notdir ${CURDIR})
# This VuCI app is the single physical runtime package. Keep the package name
# as vuci-app-...-ui so Teltonika Package Manager treats it as a native VuCI app.
APP_DEPENDS:=+libmosquitto-ssl +libatomic +lib-bacnet +api-core
CLOSED_GPL_INSTALL:=y

PKG_VERSION:=1.0.0
PKG_RELEASE:=8

ifneq (,$(filter m,$(CONFIG_PACKAGE_$(APP_NAME)))$(DUMP))
	APP_TLT_NAME:=GK BACnet MQTT
	APP_ROUTER:=$(TLT_PLATFORM_NAME)
endif

define Package/$(APP_NAME)/description
	GK BACnet to MQTT gateway. Single RutOS package containing daemon, UCI config, API and VuCI UI.
endef

include ../app.mk
