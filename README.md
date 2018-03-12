# mod_event_kafka
Freeswitch Kafka Plugin

### Install Dependencies
```bash
sudo apt-get install libfreeswitch-dev
sudo apt-get install build-essential pkg-config 

echo "deb http://ftp.de.debian.org/debian stretch main" > /etc/apt/sources.list.d/strech.list
sudo apt-get install librdkafka-dev 
```

### Build

```
make
make install
```


