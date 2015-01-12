all:
	g++ -fPIC -o libcyusb.o -c libcyusb.c
	g++ -shared -Wl,-soname,libcyusb.so -o libcyusb.so.1 libcyusb.o -l usb-1.0 -l rt
	ln -sf libcyusb.so.1 libcyusb.so
	g++ -o fwload_fx3 fwload_fx3.c -L . -l cyusb
clean:
	rm -f libcyusb.so libcyusb.so.1 libcyusb.o
	rm -f fwload_fx3
install:
	@echo 'run ../install.sh'
