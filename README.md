# mod_event_kafka
Freeswitch Kafka Plugin

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


