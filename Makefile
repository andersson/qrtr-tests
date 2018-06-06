.PHONY: all

all:

TESTS := qrtr-multi-remote \
	 qrtr-recv-no-drops \
	 qrtr-resume-tx-indefinite \
	 qrtr-confirm-rx-usage \
	 qrtr-service-announcement \


CFLAGS := -Wall -g -O2
LDFLAGS :=

all-tests :=

define add-test
$1: $1.o qrtr-test.o util.o
	@echo "CC $$<"
	@$(CC) -o $$@ $$^ $$(LDFLAGS)

all-tests += $1

$(DESTDIR)$(prefix)/bin/$1: $1
	@echo "INSTALL $$<"
	@install -D -m 755 $$< $$@

all-install += $(DESTDIR)$(prefix)/bin/$1
endef

$(foreach t,${TESTS},$(eval $(call add-test,$t)))

all: $(all-tests)

install: $(all-install)

clean:
	rm -f $(all-tests) *.o
