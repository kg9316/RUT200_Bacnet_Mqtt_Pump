include $(TOPDIR)/rules.mk

APP_TITLE:=VuCI UI Support for GK BACnet MQTT
APP_NAME:=$(notdir ${CURDIR})
CLOSED_GPL_INSTALL:=y

ifneq (,$(filter m,$(CONFIG_PACKAGE_$(APP_NAME)))$(DUMP))
	APP_TLT_NAME:=GK BACnet MQTT
	APP_ROUTER:=$(TLT_PLATFORM_NAME)
endif

define Package/$(APP_NAME)/description
	GK BACnet MQTT configuration, runtime status and log UI
endef

include ../app.mk
