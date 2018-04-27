MULTI_REMOTE := multi-remote

CFLAGS := -Wall -g -O2
LDFLAGS :=

SRCS := multi-remote.c \
	util.c

OBJS := $(SRCS:.c=.o)

$(MULTI_REMOTE): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

install: $(MULTI_REMOTE)
	install -D -m 755 $< $(DESTDIR)$(prefix)/bin/$<

clean:
	rm -f $(MULTI_REMOTE) $(OBJS)
