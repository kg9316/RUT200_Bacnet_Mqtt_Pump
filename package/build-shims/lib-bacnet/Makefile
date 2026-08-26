include $(TOPDIR)/rules.mk

PKG_NAME:=lib-bacnet
PKG_VERSION:=1
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/lib-bacnet
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=Teltonika lib-bacnet runtime dependency shim
endef

define Package/lib-bacnet/description
 Build-time metadata shim only. The real lib-bacnet package is supplied by
 Teltonika RutOS Package Manager for the target device/firmware.
endef

define Build/Compile
endef

define Package/lib-bacnet/install
endef

$(eval $(call BuildPackage,lib-bacnet))
