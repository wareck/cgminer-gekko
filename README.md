### #########################################################################
##  CGminer 4.12.1 GekkoScience Compac, 2pac & Newpack & CompacF & terminus #
### #########################################################################

This is cgminer 4.12.1 with support for GekkoScience Compac, CompacF, 2pac.

This software is forked from cgminer 4.10.0 original from ckolivas.

Then i added v.thoang gekko drivers and kanoi upgrade, avalon updates , xtranonce

(you can refer to original documentation to docs/README)

### GekkoScience compac & 2pac Usb miner ##
<p align="center">
<img src="https://raw.githubusercontent.com/wareck/cgminer-gekko/master/docs/gekko.jpg">
<img src="https://raw.githubusercontent.com/wareck/cgminer-gekko/master/docs/2pac.jpg">
</p>

### GekkoScience Newpac & CompacF Usb miner ##
<p align="center">
<img src="https://raw.githubusercontent.com/wareck/cgminer-gekko/master/docs/newpac.jpg">
<img src="https://raw.githubusercontent.com/wareck/cgminer-gekko/master/docs/compacf.png">
</p>

### GekkoScience Terminus r606 / R909 ##
<p align="center">
<img src="https://raw.githubusercontent.com/wareck/cgminer-gekko/master/docs/terminus.jpg">
</p>

This software use a slighty moddified driver from novak's gekko driver.

I allows working with icarus miner and gekko on same rig.

to build this specific code on linux:

	sudo apt-get update -y
	sudo apt-get install build-essential autoconf automake libtool pkg-config libcurl4-openssl-dev libudev-dev \
	libjansson-dev libncurses5-dev libusb-1.0-0-dev zlib1g-dev git -y

	git clone https://github.com/wareck/cgminer-gekko.git

	cd cgminer-gekko
	sudo usermod -a -G dialout,plugdev $USER
	sudo cp 01-cgminer.rules /etc/udev/rules.d/
	CFLAGS="-O2 -march=native" ./autogen.sh
	./configure --enable-gekko
	make
	sudo make install
	sudo reboot
	
### Option Summary ###

```
  --gekko-compac-freq <clock>   Chip clock speed (MHz) default is 200 Mhz
  --gekko-2pac-freq <clock> Chip clock speed (Mhz) default is 150 Mhz 
  --gekko-newpac-freq <clock> Chip clock speed (Mhz) default is 150 Mhz
  --gekko-r606-freq <clock> Set GekkoScience Terminus R606 frequency in MHz, range 50-900 (default: 550)
  --gekko-r909-freq <clock> Set GekkoScience Terminus R606 frequency in MHz, range 50-900 (default: 550
  --gekko-terminus-detect Detect GekkoScience Terminus BM1384
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

### For Raspberry Users : ###

You may need to use "legacy" version of raspberry OS (32 or 64 bit no matter).

## Credits
```
Kanoi    : https://github.com/kanoi/cgminer.git (code update to 4.12.0 and gekko driver improvement)
ckolivas : https://github.com/ckolivas/cgminer.git (original cgminer code)
vthoang  : https://github.com/vthoang/cgminer.git (gekko driver)
nicehash : https://github.com/nicehash/cgminer-ckolivas.git (nicheash extranonce)
prmam : coinbase flags fix and default configuration, udev permissions fix
chipjarred : macos compilation fix, memory leak fix, and Fixing change of sign warnings when referring to buffers
