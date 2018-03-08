# Customise these as appropriate
MODNAME = mod_event_kafka.so
MODOBJ = mod_event_kafka.o #file.o file2.o file3.o
MODCFLAGS = -Wall -Werror 
MODLDFLAGS = -lssl 

CXX = g++
CXXFLAGS = -fPIC -g -ggdb -I/usr/include  `pkg-config --cflags freeswitch` $(MODCFLAGS) -std=c++0x
LDFLAGS = `pkg-config --libs freeswitch` -lrdkafka++ $(MODLDFLAGS) 

.PHONY: all
all: $(MODNAME)

$(MODNAME): $(MODOBJ)
	@$(CXX) -shared -o $@ $(MODOBJ) $(LDFLAGS)

.cpp.o: $<
	@$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	rm -f $(MODNAME) $(MODOBJ)

.PHONY: install
install: $(MODNAME)
	install -d $(DESTDIR)/usr/lib/freeswitch/mod
	install $(MODNAME) $(DESTDIR)/usr/lib/freeswitch/mod
	install -d $(DESTDIR)/etc/freeswitch/autoload_configs
	install event_kafka.conf.xml $(DESTDIR)/etc/freeswitch/autoload_configs/