##################################################################################
# CGminer 4.11.1 GekkoScience Compac & 2pac BM1384 #
##################################################################################

This is a cgminer 4.11.1 with support fot GekkoScience Compac & 2pac BM1384 Support.

This software is forked from cgminer 4.11.1 original from ckolivas.

(you can refer to original documentation to docs/README)

## GekkoScience Usb miner ##

![](https://raw.githubusercontent.com/wareck/cgminer-gekko/master/docs/gekko.jpg)

This software use a slighty moddified driver from novak's gekko driver.

I allows working with icarus miner and gekko on same rig.

to build this specific code on linux:

	sudo apt-get update
	sudo apt-get install build-essential autoconf automake libtool pkg-config libcurl4-openssl-dev libudev-dev \
	libjansson-dev libncurses5-dev	libudev-dev libjansson-dev 
	CFLAGS="-O2 -march=native" ./autogen.sh
	./configure --enable-gekko
	make
	make install

### Option Summary ###

```
  --gekko-compac-freq <clock>   Chip clock speed (MHz) default is 200 Mhz
  --gekko-2pac-freq <clock> Chip clok speed (Mhz) default is 150 Mhz 
  --suggest-diff <value> Limit diff for starting mine default is 32
```

### Command line ###

```
 ./cgminer -o pool_url -u username -p password --gekko-compac-freq 200 --gekko-2pac-freq 150 
```

For windows users, you can donwload the release zip file

Inside you can find a cgminer_run.bat file and you can adjust you settings.

## Nicehash extranonce support ##

You can use your miner with last extranonce support for nicehash by adding #xnsub at the address end, like this:

	./cgminer -o stratum+tcp://scrypt.eu.nicehash.com:3333#xnsub -u my_btc_address -p x --gekko-compac-freq 200 --gekko-2pac-freq 150
	
