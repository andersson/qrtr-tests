.PHONY: all
.PHONY: ramdisk

all:

TESTS := qrtr-multi-remote \
	 qrtr-recv-no-drops \
	 qrtr-resume-tx-indefinite \
	 qrtr-confirm-rx-usage \
	 qrtr-service-announcement \

CFLAGS := -Wall -g -O2
LDFLAGS :=

all-tests :=
all-install :=
all-ramdiisk :=

RAMDISK_TEMPLATE := no-RAMDISK_TEMPLATE-specified
RAMDISK_OVERLAY := .ramdisk-overlay

define add-test
$1: $1.o qrtr-test.o util.o
	@$$(CC) -o $$@ $$^ $$(LDFLAGS)

all-tests += $1

$(DESTDIR)$(prefix)/bin/$1: $1
	@echo "INSTALL $$<"
	@install -D -m 755 $$< $$@

all-install += $(DESTDIR)$(prefix)/bin/$1

$(RAMDISK_OVERLAY)/usr/bin/$1: $1
	@echo "INSTALL $$<"
	@install -D -m 755 $$< $$@

all-ramdisk += $(RAMDISK_OVERLAY)/usr/bin/$1
endef

$(foreach t,${TESTS},$(eval $(call add-test,$t)))

ramdisk.cpio: CC := aarch64-linux-gnu-gcc
ramdisk.cpio: $(all-ramdisk) $(RAMDISK_TEMPLATE)
	cp $(RAMDISK_TEMPLATE) $@.gz
	gunzip -fd $@.gz
	(cd $(RAMDISK_OVERLAY) ; find | cpio -oA -H newc -F ../$@)

ramdisk.lz4: ramdisk.cpio
	lz4 -f -l $< $@

all: $(all-tests)

install: $(all-install)

ramdisk: ramdisk.lz4

clean:
	rm -f $(all-tests) *.o ramdisk.cpio ramdisk.lz4
