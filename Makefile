CFLAGS = -std=c++11 -o

all: recorder server client

recorder: recorder.cpp
	g++ recorder.cpp $(CFLAGS) recorder `pkg-config --cflags --libs gstreamer-interfaces-0.10 gtk+-2.0 gstreamer-pbutils-0.10 gstreamer-0.10` -lpthread

server: server.cpp 
	g++ server.cpp $(CFLAGS) server `pkg-config --cflags --libs gstreamer-interfaces-0.10 gtk+-2.0 gstreamer-pbutils-0.10 gstreamer-0.10` -lpthread

client: client.cpp 
	g++ client.cpp $(CFLAGS) client `pkg-config --cflags --libs gstreamer-interfaces-0.10 gtk+-2.0 gstreamer-pbutils-0.10 gstreamer-0.10` -lpthread

send:
	./server hello.mkv

record:
	./recorder hello

receive:
	./client --sync

commit:
	git add recorder.cpp 
	git add server.cpp
	git add client.cpp  
	git add Makefile
	git add README.md 
	git commit -m "cs414_mp2"
	git push -u origin master

clean:
	rm -rf *o server recorder client