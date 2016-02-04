
all: usr_neoproxy	

ifdef CROSS
CXX = arm-linux-gnueabihf-g++
endif

usr_neoproxy: usr_neoproxy.cpp
	$(CXX) -std=gnu++11  -g -o usr_neoproxy usr_neoproxy.cpp -lev

clean:
	rm -f *~ usr_neoproxy
