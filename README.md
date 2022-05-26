# mod_event_kafka
Freeswitch Kafka Plugin 

[![Build Status](https://github.com/voiceip/mod_event_kafka/actions/workflows/main.yml/badge.svg?branch=master)](https://github.com/voiceip/mod_event_kafka/actions/workflows/main.yml)

Install this plugin to publish all of the freeswitch generated events to Kafka reliably from the freeswitch server. To enable just configure the `event_kafka.conf.xml` 

```xml
<configuration name="event_kafka.conf" description="Kafka Event Configuration">
	<settings>
		<param name="bootstrap-servers" value="localhost:9092"/>
		<param name="topic" value="kafa-topic-name" /> 
		<param name="username" value="" />
		<param name="password" value="" />
		<param name="buffer-size" value="256" /> 
		<param name="compression" value="snappy"/>
		<param name="event-filter" value=""/> 
	</settings>
 </configuration>
```
and enable autoloading of the module by adding the following entry in `modules.conf.xml`

```xml
 <load module="mod_event_kafka"/>
```



# Building

## IDE Based Build

We use vscode + docker, to enable easy building on any platform with the use of [remote-container](https://code.visualstudio.com/docs/remote/containers#_getting-started) feature of `Visual Studio Code`. If you are new to this, follow the [getting started guide](https://code.visualstudio.com/docs/remote/containers#_getting-started) 

Open the project in `Visual Studio Code` and just Run Task `Release`.


## Manually Building

### Install Dependencies
```bash
sudo apt-get install libfreeswitch-dev
sudo apt-get install build-essential pkg-config 
sudo apt-get install librdkafka-dev libz-dev libssl-dev
```

### Build

```
make
make install
```


