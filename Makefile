CC      = gcc
CFLAGS  = -std=c99 -O2 -Wall -Wextra -fPIC -Isrc \
          -Wno-stringop-truncation -Wno-format-truncation
LDFLAGS = -shared -ldl

TARGET  = cbmwcx.so
SRCS    = src/cbmwcx.c src/cbm.c src/ini.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

src/%.o: src/%.c src/wcxhead.h src/cbm.h src/ini.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	@mkdir -p ~/.config/doublecmd/plugins/wcx/cbmwcx
	cp $(TARGET) ~/.config/doublecmd/plugins/wcx/cbmwcx/
	@if [ ! -f ~/.config/doublecmd/plugins/wcx/cbmwcx/cbmwcx.ini ]; then \
	    cp cbmwcx.ini ~/.config/doublecmd/plugins/wcx/cbmwcx/; \
	fi
	@echo "Installed to ~/.config/doublecmd/plugins/wcx/cbmwcx/"
	@echo "Register in Double Commander:"
	@echo "  Options → Plugins → WCX plugins → Add"
	@echo "  Plugin: ~/.config/doublecmd/plugins/wcx/cbmwcx/cbmwcx.so"
	@echo "  Extensions: d64 d71 d80 d81 d82 t64"

clean:
	rm -f $(OBJS) $(TARGET)
