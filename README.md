# mod_event_kafka
Freeswitch Kafka Plugin

```bash
echo "deb http://ftp.de.debian.org/debian stretch main" > /etc/apt/sources.list.d/kafka.list
sudo apt-get install freeswitch-dev
sudo apt-get install build-essential pkg-config
sudo apt-get install librdkafka-dev libssl-dev
make
```