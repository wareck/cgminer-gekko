### ############################################################
##  CGminer 4.11.1 GekkoScience Compac, 2pac & Newpack BM1384 #
### ############################################################

This is a cgminer 4.11.1 with support fot GekkoScience Compac, 2pac & Newpack BM1384 Support.

This software is forked from cgminer 4.11.1 original from ckolivas.

(you can refer to original documentation to docs/README)

### GekkoScience compac Usb miner ##

![](https://raw.githubusercontent.com/wareck/cgminer-gekko/master/docs/gekko.jpg)

### GekkoScience 2pac Usb miner ##

![](https://raw.githubusercontent.com/wareck/cgminer-gekko/master/docs/2pac.jpg)

### GekkoScience Newpac Usb miner ##

![](https://raw.githubusercontent.com/wareck/cgminer-gekko/master/docs/newpac.jpg)

### GekkoScience Terminus r606 ##

![](https://raw.githubusercontent.com/wareck/cgminer-gekko/master/docs/terminus.jpg)

This software use a slighty moddified driver from novak's gekko driver.

I allows working with icarus miner and gekko on same rig.

to build this specific code on linux:

	sudo apt-get update -y
	sudo apt-get install build-essential autoconf automake libtool pkg-config libcurl4-openssl-dev libudev-dev \
	libjansson-dev libncurses5-dev -y
	git clone https://github.com/wareck/cgminer-gekko.git
	cd cgminer-gekko
	sudo usermod -a -G dialout,plugdev $USER
	sudo cp 01-cgminer.rules /etc/udev/rules.d/
	CFLAGS="-O2 -march=native" ./autogen.sh
	./configure
	make
	make install

### Option Summary ###

```
  --gekko-compac-freq <clock>   Chip clock speed (MHz) default is 200 Mhz
  --gekko-2pac-freq <clock> Chip clock speed (Mhz) default is 150 Mhz 
  --gekko-newpac-freq <clock> Chip clock speed (Mhz) default is 150 Mhz
  --suggest-diff <value> Limit diff for starting mine default is 32
```

### Command line ###

```
 ./cgminer -o pool_url -u username -p password --gekko-compac-freq 200 --gekko-2pac-freq 150 --gekko-newpac-freq 150
```

For windows users, you can donwload the release zip file

Inside you can find a cgminer_run.bat file and you can adjust you settings.

### Nicehash extranonce support ##

You can use your miner with last extranonce support for nicehash by adding #xnsub at the address end, like this:

	./cgminer -o stratum+tcp://sha256.eu.nicehash.com:3334#xnsub -u my_btc_address -p x --gekko-compac-freq 200 --gekko-2pac-freq 150 --gekko-newpac-freq 150
	
## Credits
```
ckolivas : https://github.com/ckolivas/cgminer.git (original cgminer code)
vthoang  : https://github.com/vthoang/cgminer.git (gekko driver)
nicehash : https://github.com/nicehash/cgminer-ckolivas.git (nicheash extranonce)
prmam : coinbase flags fix and default configuration, udev permissions fix
chipjarred : macos compilation fix, memory leak fix, and Fixing change of sign warnings when referring to buffers
