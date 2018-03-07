#!/bin/bash
set -x

BUILD_ROOT=$(mktemp -d)
VERSION=$(date +%s)
cp -r deb/* $BUILD_ROOT/

mkdir -p $BUILD_ROOT/usr/local/lib/
mkdir -p $BUILD_ROOT/usr/lib/freeswitch/mod
mkdir -p $BUILD_ROOT/etc/freeswitch/autoload_configs/

cp /etc/freeswitch/autoload_configs/event_kafka.conf.xml  $BUILD_ROOT/etc/freeswitch/autoload_configs/event_kafka.conf.xml
cp /usr/lib/freeswitch/mod/mod_event_kafka.so  $BUILD_ROOT/usr/lib/freeswitch/mod/mod_event_kafka.so
cp /usr/local/lib/librdkafka++.* $BUILD_ROOT/usr/local/lib/

sed -i "s/_VERSION_/$VERSION/g" $BUILD_ROOT/DEBIAN/control
dpkg-deb --build $BUILD_ROOT mod_event_kafka.deb

rm -rf $BUILD_ROOT