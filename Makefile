TARGET = server

server: server.c 
	gcc server.c -o server `pkg-config --cflags --libs gstreamer-interfaces-0.10 gtk+-2.0 gstreamer-pbutils-0.10 gstreamer-0.10`

send:
	./server --sync

clean:
	rm -rf *o server