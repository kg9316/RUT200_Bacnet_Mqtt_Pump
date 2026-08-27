include $(TOPDIR)/rules.mk

PKG_NAME:=lib-bacnet
PKG_VERSION:=1
PKG_RELEASE:=2

include $(INCLUDE_DIR)/package.mk

define Package/lib-bacnet
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=Teltonika lib-bacnet runtime dependency shim
endef

define Package/lib-bacnet/description
 Build-time shim for the GPL SDK. It exposes Teltonika's existing
 libbacnet.so to OpenWrt's package dependency scanner. The resulting shim
 package is not published by the GK workflow; RutOS Package Manager supplies
 the real lib-bacnet package on the target device.
endef

define Build/Compile
endef

define Package/lib-bacnet/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(TOPDIR)/package/teltonika/libs/lib-bacnet/ipkg-install/usr/lib/libbacnet.so $(1)/usr/lib/libbacnet.so
endef

$(eval $(call BuildPackage,lib-bacnet))
