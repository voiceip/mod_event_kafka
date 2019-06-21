#-------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License. See https://go.microsoft.com/fwlink/?linkid=2090316 for license information.
#-------------------------------------------------------------------------------------------------------------

FROM debian:9



# Install git, process tools
RUN apt-get update && apt-get -y install git procps wget 

# Install C++ tools
RUN apt-get -y install build-essential cmake cppcheck valgrind pkg-config lsb-release

# Install Freeswitch dev
RUN echo "deb [trusted=yes] http://files.freeswitch.org/repo/deb/freeswitch-1.8/ stretch main" > /etc/apt/sources.list.d/freeswitch.list
RUN wget -O - https://files.freeswitch.org/repo/deb/freeswitch-1.8/fsstretch-archive-keyring.asc | apt-key add -

RUN apt-get update && apt-get -y install libfreeswitch-dev librdkafka-dev libz-dev libssl-dev

# Clean up
RUN apt-get autoremove -y \
    && apt-get clean -y 
#   && rm -rf /var/lib/apt/lists/*

# Set the default shell to bash instead of sh
ENV SHELL /bin/bash