include $(TOPDIR)/rules.mk

APP_TITLE:=GK BACnet MQTT
APP_NAME:=$(notdir ${CURDIR})
# This VuCI app is the single physical runtime package. Keep the package name
# as vuci-app-...-ui so Teltonika Package Manager treats it as a native VuCI app.
APP_DEPENDS:=+libmosquitto-ssl +libatomic +api-core
CLOSED_GPL_INSTALL:=y

PKG_VERSION:=1.0.0
PKG_RELEASE:=36

ifneq (,$(filter m,$(CONFIG_PACKAGE_$(APP_NAME)))$(DUMP))
	APP_TLT_NAME:=GK BACnet MQTT
	APP_ROUTER:=$(TLT_PLATFORM_NAME)
endif

define Package/$(APP_NAME)/description
	GK BACnet to MQTT gateway. Single RutOS package containing daemon, UCI config, API and VuCI UI.
endef

# Without this, opkg has no way to know /etc/config/gk_bacnet_mqtt should
# survive a reinstall/upgrade, and silently overwrites any user-edited
# values back to the packaged defaults on every install. Fast Build injects
# this same conffiles declaration directly into the shipped .ipk's control
# data (see .github/workflows/build-gk-fast.yml) since it doesn't re-run
# the SDK package graph this Makefile normally feeds.
define Package/$(APP_NAME)/conffiles
/etc/config/gk_bacnet_mqtt
endef

include ../app.mk
