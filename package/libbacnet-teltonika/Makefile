include $(TOPDIR)/rules.mk

PKG_NAME:=libbacnet-teltonika
PKG_VERSION:=1.3.8
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/libbacnet-teltonika
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=Teltonika BACnet Stack Library
endef

define Package/libbacnet-teltonika/description
 Teltonika supplied BACnet Stack 1.3.8 shared library.
endef

define Build/Compile
endef

define Package/libbacnet-teltonika/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_BIN) $(TOPDIR)/package/teltonika/libs/lib-bacnet/ipkg-install/usr/lib/libbacnet.so $(1)/usr/lib/libbacnet.so
endef

$(eval $(call BuildPackage,libbacnet-teltonika))
