include $(TOPDIR)/rules.mk

APP_NAME:=vuci-app-gk-bacnet-mqtt-api
PKG_NAME:=$(APP_NAME)
APP_TITLE:=VuCI API Support for GK BACnet MQTT
APP_DEPENDS:=+gk-bacnet-mqtt
CLOSED_GPL_INSTALL:=y

ifeq (m,$(CONFIG_PACKAGE_vuci-app-gk-bacnet-mqtt-api))
	APP_TLT_NAME:=GK BACnet MQTT API
	APP_APP_NAME:=gk-bacnet-mqtt
	APP_ROUTER:=$(TLT_PLATFORM_NAME)
endif

include ../api.mk
