<?php
#
# v42.14
# Copyright 2011-2018 Kano
# Licensed under the GNU General Public License version 3,
#  or (at your option) any later version.
#
session_start();
#
global $ect;
$ect = 'ECTABLE';
#
global $doctype, $title, $miner, $port, $readonly, $notify;
global $rigport, $rigs, $rignames, $rigbuttons;
global $mcast, $mcastexpect, $mcastaddr, $mcastport, $mcastcode;
global $mcastlistport, $mcasttimeout, $mcastretries, $allowgen;
global $rigipsecurity, $rigtotals, $forcerigtotals;
global $socksndtimeoutsec, $sockrcvtimeoutsec;
global $checklastshare, $poolinputs, $hidefields;
global $ignorerefresh, $changerefresh, $autorefresh;
global $allowcustompages, $customsummarypages, $user_pages;
global $miner_font_family, $miner_font_size;
global $bad_font_family, $bad_font_size, $add_css_names;
global $colouroverride, $placebuttons, $userlist, $mmcmd;
global $avatmaxmed, $avatmaxhi, $avatempmed, $avatemphi, $avaghsmmmin;
global $s9tempmed, $s9temphi, $s9chainmin, $s9acn;
#
$doctype = "<!DOCTYPE html>\n";
#
# See API-README for more details of these variables and how
# to configure miner.php
#
# Web page title
$title = 'Mine';
#
# Set $readonly to true to force miner.php to be readonly
# Set $readonly to false then it will check cgminer 'privileged'
$readonly = false;
#
# Set $userlist to null to allow anyone access or read API-README
$userlist = null;
#
# Set $notify to false to NOT attempt to display the notify command
# Set $notify to true to attempt to display the notify command
$notify = true;
#
# Set $checklastshare to true to do the following checks:
# If a device's last share is 12x expected ago then display as an error
# If a device's last share is 8x expected ago then display as a warning
# If either of the above is true, also display the whole line highlighted
# This assumes shares are 1 difficulty shares
$checklastshare = true;
#
# Set $poolinputs to true to show the input fields for adding a pool
# and changing the pool priorities
# N.B. also if $readonly is true, it will not display the fields
$poolinputs = false;
#
# Default port to use if any $rigs entries don't specify the port number
$rigport = 4028;
#
# Set $rigs to an array of your cgminer rigs that are running
#  format: 'IP' or 'Host' or 'IP:Port' or 'Host:Port' or 'Host:Port:Name'
$rigs = array('127.0.0.1:4028');
#
# Set $rignames to false, or one of 'ip' or 'ipx'
#  this says what to use if $rigs doesn't have a 'name'
$rignames = false;
#
# Set $rigbuttons to false to display a link rather than a button
$rigbuttons = true;
#
# Set $mcast to true to look for your rigs and ignore $rigs
$mcast = false;
#
# Set $mcastexpect to at least how many rigs you expect it to find
$mcastexpect = 0;
#
# API Multicast address all cgminers are listening on
$mcastaddr = '224.0.0.75';
#
# API Multicast UDP port all cgminers are listening on
$mcastport = 4028;
#
# The code all cgminers expect in the Multicast message sent
$mcastcode = 'FTW';
#
# UDP port cgminers are to reply on (by request)
$mcastlistport = 4027;
#
# Set $mcasttimeout to the number of seconds (floating point)
# to wait for replies to the Multicast message
$mcasttimeout = 1.5;
#
# Set $mcastretries to the number of times to retry the multicast
$mcastretries = 0;
#
# Set $allowgen to true to allow customsummarypages to use 'gen' 
# false means ignore any 'gen' options
$allowgen = false;
#
# Set $rigipsecurity to false to show the IP/Port of the rig
# in the socket error messages and also show the full socket message
$rigipsecurity = true;
#
# Set $rigtotals to true to display totals on the single rig page
# 'false' means no totals (and ignores $forcerigtotals)
# You can force it to always show rig totals when there is only
# one line by setting $forcerigtotals = true;
$rigtotals = true;
$forcerigtotals = false;
#
# These should be OK for most cases
$socksndtimeoutsec = 10;
$sockrcvtimeoutsec = 40;
#
# List of fields NOT to be displayed
# This example would hide the slightly more sensitive pool information
#$hidefields = array('POOL.URL' => 1, 'POOL.User' => 1);
$hidefields = array();
#
# Auto-refresh of the page (in seconds) - integers only
# $ignorerefresh = true/false always ignore refresh parameters
# $changerefresh = true/false show buttons to change the value
# $autorefresh = default value, 0 means dont auto-refresh
$ignorerefresh = false;
$changerefresh = true;
$autorefresh = 0;
#
# Settings for avalon miners - adjust in myminer.php based
# on the type of avalon miner you have
$avatmaxmed = 100;
$avatmaxhi = 110;
$avatempmed = 32;
$avatemphi = 40;
$avaghsmmmin = 8000;
#
# Settings for s9 miners - adjust in myminer.php
$s9tempmed = 100;
$s9temphi = 110;
$s9chainmin = 4000;
$s9acn = 63;
#
# Should we allow custom pages?
# (or just completely ignore them and don't display the buttons)
$allowcustompages = true;
#
# OK this is a bit more complex item: Custom Summary Pages
# As mentioned above, see API-README
# see the example below (if there is no matching data, no total will show)
$mobilepage = array(
 'DATE' => null,
 'RIGS' => null,
 'SUMMARY' => array('Elapsed', 'MHS av', 'MHS 5m', 'Found Blocks=Blks',
			'Difficulty Accepted=DiffA',
			'Difficulty Rejected=DiffR',
			'Hardware Errors=HW',
			'Work Utility=WU'),
 'DEVS+NOTIFY' => array('DEVS.Name=Name', 'DEVS.ID=ID', 'DEVS.Status=Status',
			'DEVS.Temperature=Temp', 'DEVS.MHS av=MHS av',
			'DEVS.MHS 5m=MHS 5m', 'DEVS.Difficulty Accepted=DiffA',
			'DEVS.Difficulty Rejected=DiffR',
			'DEVS.Work Utility=WU',
			'NOTIFY.Last Not Well=Not Well'),
 'POOL' => array('POOL', 'Status', 'Difficulty Accepted=DiffA',
			'Difficulty Rejected=DiffR', 'Last Share Time=LST'));
$mobilesum = array(
 'SUMMARY' => array('MHS av', 'MHS 5m', 'Found Blocks', 'Difficulty Accepted',
			'Difficulty Rejected', 'Hardware Errors',
			'Work Utility'),
 'DEVS+NOTIFY' => array('DEVS.MHS av', 'DEVS.Difficulty Accepted',
			'DEVS.Difficulty Rejected'),
 'POOL' => array('Difficulty Accepted', 'Difficulty Rejected'));
#
$statspage = array(
 'DATE' => null,
 'RIGS' => null,
 'SUMMARY' => array('Elapsed', 'MHS av', 'MHS 5m', 'Found Blocks=Blks',
			'Difficulty Accepted=DiffA',
			'Difficulty Rejected=DiffR',
			'Work Utility=WU', 'Hardware Errors=HW Errs',
			'Network Blocks=Net Blks'),
 'COIN' => array('Current Block Time','Current Block Hash','Network Difficulty'),
 'STATS' => array('*'));
#
$statssum = array(
 'SUMMARY' => array('MHS av', 'MHS 5m', 'Found Blocks',
			'Difficulty Accepted', 'Difficulty Rejected',
			'Work Utility', 'Hardware Errors'));
#
$avalonmmflds = array('#', 'MM', 'ID', 'MMID', 'Connecter', 'Elapsed',
			'Temp=Inflow', 'TMax=Outflow', 'Fan=FanRPM',
			'FanR=Fan%', 'GEN.AVATHS=TH/s||GHSmm', 'Vi', 'Vo',
			'Freq', 'Led', 'PG', 'ECHU', 'ECMM', 'Ver');
#
$avalonpage = array(
 'DATE' => null,
 'COIN' => array('Current Block Time','Current Block Hash','Network Difficulty'),
 'SUMMARY' => array('#', 'Elapsed', 'GEN.THS av=TH/s av||MHS av',
			'GEN.THS 5m=TH/s 5m||MHS 5m',
			'Found Blocks=Blks',
			'Difficulty Accepted=DiffA',
			'Difficulty Rejected=DiffR',
			'Work Utility=WU', 'Hardware Errors=HW Errs',
			'Network Blocks=Net Blks', 'Best Share'),
 $ect => null, # show the EC* code table
 'MM' => $avalonmmflds
);
#
$avalonsum = array(
 'SUMMARY' => array('#', 'GEN.THS av', 'MHS av', 'GEN.THS 5m', 'MHS 5m',
			'Found Blocks', 'Difficulty Accepted',
			'Difficulty Rejected', 'Work Utility',
			'Hardware Errors'),
 'MM' => array('#', 'GEN.AVATHS', 'GHSmm'));
#
$avalonext = array(
 'COIN' => array(
  'group' => array('Current Block Hash', 'Network Difficulty'),
  'calc' => array('Current Block Time' => 'min')),
 'SUMMARY' => array(
  'gen' => array('THS av' => 'MHS av / 1000000.0',
		'THS 5m' => 'MHS 5m / 1000000.0')),
 'MM' => array(
  'gen' => array('AVATHS' => 'GHSmm / 1000.0'))
);
#
$avalonspage = array(
 'DATE' => null,
 'COIN' => array('Current Block Time','Current Block Hash','Network Difficulty'),
 $ect => null, # show the EC* code table
 'MM0' => $avalonmmflds, # hi temp and low hash
 'MM1' => $avalonmmflds, # hi temp only
 'MM2' => $avalonmmflds, # low hash only
 'MM3' => $avalonmmflds # the rest
);
#
$avalonsmmsum = array('#', 'GEN.AVATHS', 'GHSmm');
$avalonssum = array(
 'MM0' => $avalonsmmsum,
 'MM1' => $avalonsmmsum,
 'MM2' => $avalonsmmsum,
 'MM3' => $avalonsmmsum
);
#
$s9page = array(
 'DATE' => null,
 'STATS' => array('#', 'Elapsed', 'fan_num', 'GEN.FanA=FanA||fan3',
		'GEN.FanB=FanB||fan6', 'GEN.TempA=TempA||temp2_6',
		'GEN.TempB=TempB||temp2_7', 'GEN.TempC=TempC||temp2_8',
		'GEN.S9THS=TH/s||total_rate',
		'GEN.AcnA=ChainA||chain_acn6', 'GEN.AcnB=ChainB||chain_acn7',
		'GEN.AcnC=ChainC||chain_acn8', 'GEN.RateA=RateA||chain_rate6',
		'GEN.RateB=RateB||chain_rate7', 'GEN.RateC=RateC||chain_rate8')
);
#
$s9sum = array(
 'STATS' => array('#', 'GEN.S9THS', 'total_rate')
);
#
function s9fmtsub($match, $num, $ch, $alldata, $warnclass, $errorclass)
{
 global $s9tempmed, $s9temphi, $s9chainmin, $s9acn;
 $lab = '';
 $ret = '';
 $class = '';
 $len = strlen($match);
 foreach ($alldata as $fld => $val)
 {
	$rep = preg_replace('/\d*$/', '', $fld);
	if ($rep == $match)
	{
		if ($val != '' && $val != '0')
		{
			if ($num-- > 0)
				continue;
			$lab = $ch.substr($fld, $len).': ';
			$ret = $val;
			break;
		}
	}
 }
 if ($ret == '')
 {
	$lab = '';
	$ret = 'none/0';
	$class = $errorclass;
 }
 else
 {
	switch($match)
	{
	 case 'temp2_':
		if ($ret == 0 || $ret >= $s9temphi)
			$class = $errorclass;
		else
		{
			if ($ret >= $s9tempmed)
				$class = $warnclass;
		}
		$ret .= '&deg;C';
		break;
	 case 'chain_acn':
		if ($ret != $s9acn)
			$class = $warnclass;
		break;
	 case 'chain_rate':
		if ($ret == 0)
			$class = $errorclass;
		else
		{
			if ($ret < $s9chainmin)
				$class = $warnclass;
		}
		$ret = number_format((float)$ret, 2);
		break;
	}
 }
 return array($lab.$ret, $class);
}
#
function s9fmt($section, $name, $value, $when, $alldata, $warnclass,
			$errorclass, $hiclass, $loclass, $totclass)
{
 $ret = '';
 $class = '';
 if ($section == 'total' && $name != 'S9THS')
	 return array($ret, $class);
 switch($name)
 {
  case 'FanA':
	return s9fmtsub('fan', 0, 'f', $alldata, $warnclass, $errorclass);
  case 'FanB':
	return s9fmtsub('fan', 1, 'f', $alldata, $warnclass, $errorclass);
  case 'TempA':
	return s9fmtsub('temp2_', 0, 't', $alldata, $warnclass, $errorclass);
  case 'TempB':
	return s9fmtsub('temp2_', 1, 't', $alldata, $warnclass, $errorclass);
  case 'TempC':
	return s9fmtsub('temp2_', 2, 't', $alldata, $warnclass, $errorclass);
  case 'AcnA':
	return s9fmtsub('chain_acn', 0, 'a', $alldata, $warnclass, $errorclass);
  case 'AcnB':
	return s9fmtsub('chain_acn', 1, 'a', $alldata, $warnclass, $errorclass);
  case 'AcnC':
	return s9fmtsub('chain_acn', 2, 'a', $alldata, $warnclass, $errorclass);
  case 'RateA':
	return s9fmtsub('chain_rate', 0, 'r', $alldata, $warnclass, $errorclass);
  case 'RateB':
	return s9fmtsub('chain_rate', 1, 'r', $alldata, $warnclass, $errorclass);
  case 'RateC':
	return s9fmtsub('chain_rate', 2, 'r', $alldata, $warnclass, $errorclass);
  case 'S9THS':
	if ($value == 0)
	{
		$class = $errorclass;
		$ret = 0;
	}
	else
		$ret = number_format(floatval($value)/1000.0, 2);
	break;
 }
 return array($ret, $class);
}
# use the first non-zero value - allow 1, 2 or 3 parameters
function s9ghs($ghs1 = 0, $ghs2 = 0, $ghs3 = 0)
{
 if ($ghs1 != 0)
	 return($ghs1);
 if ($ghs2 != 0)
	 return($ghs2);
 if ($ghs3 != 0)
	 return($ghs3);
 return 0;
}
# set them to a fixed value then use the above custom fmt to find the values
$s9ext = array(
 'STATS' => array(
  'where' => array(array('ID', '!sub', 'POOL')),
  'gen' => array('FanA' => '1', 'FanB' => '2', 'TempA' => '3',
		'TempB' => '4', 'TempC' => '5',
		'S9THS' => 's9ghs("GHS av","GHS 5s","total_rate")',
		'AcnA' => '7', 'AcnB' => '8', 'AcnC' => '9', 'RateA' => '10',
		'RateB' => '11', 'RateC' => '12'),
  'fmt' => 's9fmt')
);
#
$poolspage = array(
 'DATE' => null,
 'RIGS' => null,
 'SUMMARY' => array('Elapsed', 'MHS av', 'MHS 5m', 'Found Blocks=Blks',
			'Difficulty Accepted=DiffA',
			'Difficulty Rejected=DiffR',
			'Work Utility', 'Hardware Errors=HW',
			'Network Blocks=Net Blks', 'Best Share'),
 'POOL+STATS' => array('STATS.ID=ID', 'POOL.URL=URL',
			'POOL.Difficulty Accepted=DiffA',
			'POOL.Difficulty Rejected=DiffR',
			'POOL.Has Stratum=Stratum',
			'POOL.Stratum Active=StrAct',
			'POOL.Has GBT=GBT', 'STATS.Times Sent=TSent',
			'STATS.Bytes Sent=BSent', 'STATS.Net Bytes Sent=NSent',
			'STATS.Times Recv=TRecv', 'STATS.Bytes Recv=BRecv',
			'STATS.Net Bytes Recv=NRecv', 'GEN.AvShr=AvShr'));
#
$poolssum = array(
 'SUMMARY' => array('MHS av', 'MHS 5m', 'Found Blocks',
			'Difficulty Accepted', 'Difficulty Rejected',
			'Work Utility', 'Hardware Errors'),
 'POOL+STATS' => array('POOL.Difficulty Accepted', 'POOL.Difficulty Rejected',
			'STATS.Times Sent', 'STATS.Bytes Sent',
			'STATS.Net Bytes Sent', 'STATS.Times Recv',
			'STATS.Bytes Recv', 'STATS.Net Bytes Recv'));
#
$poolsext = array(
 'POOL+STATS' => array(
	'where' => null,
	'group' => array('POOL.URL', 'POOL.Has Stratum',
				'POOL.Stratum Active', 'POOL.Has GBT'),
	'calc' => array('POOL.Difficulty Accepted' => 'sum',
			'POOL.Difficulty Rejected' => 'sum',
			'STATS.Times Sent' => 'sum',
			'STATS.Bytes Sent' => 'sum',
			'STATS.Net Bytes Sent' => 'sum',
			'STATS.Times Recv' => 'sum',
			'STATS.Bytes Recv' => 'sum',
			'STATS.Net Bytes Recv' => 'sum',
			'POOL.Accepted' => 'sum'),
	'gen' => array('AvShr' =>
				'round(POOL.Difficulty Accepted/'.
					'max(POOL.Accepted,1)*100)/100'),
	'having' => array(array('STATS.Bytes Recv', '>', 0)))
);
#
$devnotpage = array(
 'DATE' => null,
 'RIGS' => null,
 'DEVS+NOTIFY' => array('DEVS.Name=Name', 'DEVS.ID=ID',
			'DEVS.Temperature=Temp', 'DEVS.MHS av=MHS av',
			'DEVS.Difficulty Accepted=DiffA',
			'DEVS.Difficulty Rejected=DiffR',
			'NOTIFY.Last Not Well=Last Not Well'));
$devnotsum = array(
 'DEVS+NOTIFY' => array('DEVS.MHS av', 'DEVS.Difficulty Accepted',
			'DEVS.Difficulty Rejected'));
#
$devdetpage = array(
 'DATE' => null,
 'RIGS' => null,
 'DEVS+DEVDETAILS' => array('DEVS.Name=Name', 'DEVS.ID=ID',
				'DEVS.Temperature=Temp',
				'DEVS.MHS av=MHS av',
				'DEVS.Difficulty Accepted=DiffA',
				'DEVS.Difficulty Rejected=DiffR',
				'DEVDETAILS.Device Path=Device'));
$devdetsum = array(
 'DEVS+DEVDETAILS' => array('DEVS.MHS av', 'DEVS.Difficulty Accepted',
				'DEVS.Difficulty Rejected'));
#
$protopage = array(
 'DATE' => null,
 'RIGS' => null,
 'CONFIG' => array('ASC Count=ASCs', 'PGA Count=PGAs', 'Pool Count=Pools',
			'Strategy', 'Device Code', 'OS', 'Failover-Only'),
 'SUMMARY' => array('Elapsed', 'MHS av', 'Found Blocks=Blks',
			'Difficulty Accepted=Diff Acc',
			'Difficulty Rejected=Diff Rej',
			'Hardware Errors=HW Errs',
			'Network Blocks=Net Blks', 'Utility', 'Work Utility'),
 'POOL+STATS' => array('STATS.ID=ID', 'POOL.URL=URL', 'POOL.Accepted=Acc',
			'POOL.Difficulty Accepted=DiffA',
			'POOL.Difficulty Rejected=DiffR', 'POOL.Has GBT=GBT',
			'STATS.Max Diff=Max Work Diff',
			'STATS.Times Sent=#Sent', 'STATS.Bytes Sent=Byte Sent',
			'STATS.Net Bytes Sent=Net Sent',
			'STATS.Times Recv=#Recv',
			'STATS.Bytes Recv=Byte Recv',
			'STATS.Net Bytes Recv=Net Recv'));
$protosum = array(
 'SUMMARY' => array('MHS av', 'Found Blocks', 'Difficulty Accepted',
			'Difficulty Rejected', 'Hardware Errors',
			'Utility', 'Work Utility'),
 'POOL+STATS' => array('POOL.Accepted', 'POOL.Difficulty Accepted',
			'POOL.Difficulty Rejected',
			'STATS.Times Sent', 'STATS.Bytes Sent',
			'STATS.Net Bytes Sent', 'STATS.Times Recv',
			'STATS.Bytes Recv', 'STATS.Net Bytes Recv'));
$protoext = array(
 'POOL+STATS' => array(
	'where' => null,
	'group' => array('POOL.URL', 'POOL.Has GBT'),
	'calc' => array('POOL.Accepted' => 'sum',
			'POOL.Difficulty Accepted' => 'sum',
			'POOL.Difficulty Rejected' => 'sum',
			'STATS.Max Diff' => 'max',
			'STATS.Times Sent' => 'sum',
			'STATS.Bytes Sent' => 'sum',
			'STATS.Net Bytes Sent' => 'sum',
			'STATS.Times Recv' => 'sum',
			'STATS.Bytes Recv' => 'sum',
			'STATS.Net Bytes Recv' => 'sum'),
	'having' => array(array('STATS.Bytes Recv', '>', 0)))
);
#
# If 'gen' isn't enabled, the 'GEN' fields won't show but
# where present, will be replaced with the ||SUMMARY fields
$kanogenpage = array(
 'DATE' => null,
 'RIGS' => null,
 'SUMMARY+COIN' => array('SUMMARY.Elapsed=Elapsed',
			'GEN.Mined=Block%', 'GEN.THS Acc=TH/s Acc',
			'GEN.THS av=TH/s av||SUMMARY.MHS av=MHS av',
			'GEN.THS 5m=TH/s 5m||SUMMARY.MHS 5m=MHS 5m',
			'GEN.THS WU=TH/s WU||SUMMARY.Work Utility=WU',
			'SUMMARY.Found Blocks=Blks',
			'SUMMARY.Difficulty Accepted=DiffA',
			'SUMMARY.Difficulty Rejected=DiffR',
			'SUMMARY.Hardware Errors=HW',
			'SUMMARY.Difficulty Stale=DiffS',
			'SUMMARY.Best Share=Best Share',
			'SUMMARY.Device Hardware%=Dev HW%',
			'SUMMARY.Device Rejected%=Dev Rej%',
			'SUMMARY.Pool Rejected%=Pool Rej%',
			'SUMMARY.Pool Stale%=Pool Stale%'),
 'POOL' => array('URL', 'Diff1 Shares=Diff Work',
			'Difficulty Accepted=DiffA',
			'Difficulty Rejected=DiffR',
			'Difficulty Stale=DiffS',
			'Best Share', 'GEN.Acc=Pool Acc%', 'GEN.Rej=Pool Rej%'),
 'COIN' => array('Current Block Time','Current Block Hash','Network Difficulty')
);
# sum should list all fields seperately including GEN/BGEN || replacements
$kanogensum = array(
 'SUMMARY+COIN' => array('GEN.Mined', 'GEN.THS Acc', 'GEN.THS av',
			'GEN.THS 5m', 'GEN.THS WU',
			'SUMMARY.MHS av', 'SUMMARY.MHS 5m',
			'SUMMARY.Work Utility',
			'SUMMARY.Found Blocks',
			'SUMMARY.Difficulty Accepted',
			'SUMMARY.Difficulty Rejected',
			'SUMMARY.Hardware Errors',
			'SUMMARY.Difficulty Stale'),
 'POOL' => array('Diff1 Shares', 'Difficulty Accepted',
			'Difficulty Rejected', 'Difficulty Stale')
);
# 'where', 'calc' and 'having' should list GEN/BGEN || replacements seperately
# 'group' must use the 'name1||name2' format for GEN/BGEN fields
$kanogenext = array(
 'SUMMARY+COIN' => array(
  'gen' => array('THS Acc' =>
			'round(pow(2,32) * SUMMARY.Difficulty Accepted / '.
				'SUMMARY.Elapsed / 10000000) / 100000',
		'Mined' =>
			'SUMMARY.Elapsed * SUMMARY.Work Utility / 60 / '.
				'COIN.Network Difficulty',
		'THS av' =>
			'SUMMARY.MHS av / 1000000.0',
		'THS 5m' =>
			'SUMMARY.MHS 5m / 1000000.0',
		'THS WU' =>
			'round(pow(2,32) * SUMMARY.Work Utility / 60 / '.
				'10000000 ) / 100000')),
 'POOL' => array(
  'group' => array('URL'),
  'calc' => array('Diff1 Shares' => 'sum', 'Difficulty Accepted' => 'sum',
			'Difficulty Rejected' => 'sum',
			'Difficulty Stale' => 'sum', 'Best Share' => 'max'),
  'gen' => array('Rej' => 'Difficulty Rejected / '.
				'max(1,Difficulty Accepted+Difficulty Rejected)',
		'Acc' => 'Difficulty Accepted / '.
				'max(1,Difficulty Accepted+Difficulty Rejected)'))
);
#
$syspage = array(
 'DATE' => null,
 'RIGS' => null,
 'SUMMARY' => array('#', 'Elapsed', 'MHS av', 'MHS 5m', 'Found Blocks=Blks',
			'Difficulty Accepted=DiffA',
			'Difficulty Rejected=DiffR',
			'Difficulty Stale=DiffS', 'Hardware Errors=HW',
			'Work Utility', 'Network Blocks=Net Blks', 'Total MH',
			'Best Share', 'Device Hardware%=Dev HW%',
			'Device Rejected%=Dev Rej%',
			'Pool Rejected%=Pool Rej%', 'Pool Stale%',
			'Last getwork'),
 'DEVS' => array('#', 'ID', 'Name', 'ASC', 'Device Elapsed', 'Enabled',
			'Status', 'No Device', 'Temperature=Temp',
			'MHS av', 'MHS 5s', 'MHS 5m', 'Diff1 Work',
			'Difficulty Accepted=DiffA',
			'Difficulty Rejected=DiffR',
			'Hardware Errors=HW', 'Work Utility',
			'Last Valid Work', 'Last Share Pool',
			'Last Share Time', 'Total MH',
			'Device Hardware%=Dev HW%',
			'Device Rejected%=Dev Rej%'),
 'POOL' => array('POOL', 'URL', 'Status', 'Priority', 'Quota',
			'Getworks', 'Diff1 Shares',
			'Difficulty Accepted=DiffA',
			'Difficulty Rejected=DiffR',
			'Difficulty Stale=DiffS',
			'Last Share Difficulty',
			'Last Share Time',
			'Best Share', 'Pool Rejected%=Pool Rej%',
			'Pool Stale%')
);
$syssum = array(
 'SUMMARY' => array('MHS av', 'MHS 5m', 'Found Blocks',
			'Difficulty Accepted', 'Difficulty Rejected',
			'Difficulty Stale', 'Hardware Errors',
			'Work Utility', 'Total MH'),
 'DEVS' => array('MHS av', 'MHS 5s', 'MHS 5m', 'Diff1 Work',
			'Difficulty Accepted', 'Difficulty Rejected',
			'Hardware Errors', 'Total MH'),
 'POOL' => array('Getworks', 'Diff1 Shares', 'Difficulty Accepted',
			'Difficulty Rejected', 'Difficulty Stale')
);
#
# $customsummarypages is an array of these Custom Summary Pages
# that you can override in myminer.php
# It can be 'Name' => 1 with 'Name' in any of $user_pages or $sys_pages
# and it can be a fully defined 'Name' => array(...) like in $sys_pages below
$customsummarypages = array(
 'Kano' => 1,
 'Mobile' => 1,
 'Stats' => 1,
 'Avalon' => 1,
 'S9' => 1,
 'Avalons' => 1,
 'Pools' => 1
);
#
# $user_pages are the myminer.php definable version of $sys_pages
# It should contain a set of 'Name' => array(...) like in $sys_pages
# that $customsummarypages can refer to by 'Name'
# If a 'Name' is in both $user_pages and $sys_pages, then the one
# in $user_pages will override the one in $sys_pages
$user_pages = array();
#
$here = $_SERVER['PHP_SELF'];
#
global $tablebegin, $tableend, $warnfont, $warnoff, $dfmt;
#
$tablebegin = '<tr><td><table border=1 cellpadding=5 cellspacing=0>';
$tableend = '</table></td></tr>';
$warnfont = '<font color=red><b>';
$warnoff = '</b></font>';
$dfmt = 'H:i:s j-M-Y \U\T\CP';
#
$miner_font_family = 'Verdana, Arial, sans-serif, sans';
$miner_font_size = '13pt';
#
$bad_font_family = '"Times New Roman", Times, serif';
$bad_font_size = '18pt';
#
# List of css names to add to the css style object
#	e.g. array('td.cool' => false);
# true/false to not include the default $miner_font
# The css name/value pairs must be defined in $colouroverride below
$add_css_names = array();
#
# Edit this or redefine it in myminer.php to change the colour scheme
# See $colourtable below for the list of names
$colouroverride = array();
#
# Where to place the buttons: 'top' 'bot' 'both'
#  anything else means don't show them - case sensitive
$placebuttons = 'top';
#
# If 'estats' doesn't work or isn't enabled, replace this with 'stats'
$mmcmd = 'estats';
#
# This below allows you to put your own settings into a seperate file
# so you don't need to update miner.php with your preferred settings
# every time a new version is released
# Just create the file 'myminer.php' in the same directory as
# 'miner.php' - and put your own settings in there
if (file_exists('myminer.php'))
 include_once('myminer.php');
#
# define $avalonsext after myminer.php to allow override of $ava... limits
$avalonsmmggen = array('AVATHS' => 'GHSmm / 1000.0');
$avalonsext = array(
 'COIN' => array(
  'group' => array('Current Block Hash', 'Network Difficulty'),
  'calc' => array('Current Block Time' => 'min')),
 'MM0' => array(
  'where' => array(array('TMax', '>=', $avatmaxmed),
		array('GHSmm', '<', $avaghsmmmin)),
  'gen' => $avalonsmmggen),
 'MM1' => array(
  'where' => array(array('TMax', '>=', $avatmaxmed),
		array('GHSmm', '>=', $avaghsmmmin)),
  'gen' => $avalonsmmggen),
 'MM3' => array(
  'where' => array(array('TMax', '<', $avatmaxmed),
		array('GHSmm', '<', $avaghsmmmin)),
  'gen' => $avalonsmmggen),
 'MM2' => array(
  'where' => array(array('TMax', '<', $avatmaxmed),
		array('GHSmm', '>=', $avaghsmmmin)),
  'gen' => $avalonsmmggen)
);
#
# This is the system default that must always contain all necessary
# colours so it must be a constant
# You can override these values with $colouroverride
# The only one missing is $warnfont
# - which you can override directly anyway
global $colourtable;
$colourtable = array(
	'body bgcolor'		=> '#ecffff',
	'td color'		=> 'blue',
	'td.two color'		=> 'blue',
	'td.two background'	=> '#ecffff',
	'td.h color'		=> 'blue',
	'td.h background'	=> '#c4ffff',
	'td.err color'		=> 'black',
	'td.err background'	=> '#ff3050',
	'td.bad color'		=> 'black',
	'td.bad background'	=> '#ff3050',
	'td.warn color'		=> 'black',
	'td.warn background'	=> '#ffb050',
	'td.sta color'		=> 'green',
	'td.tot color'		=> 'blue',
	'td.tot background'	=> '#fff8f2',
	'td.lst color'		=> 'blue',
	'td.lst background'	=> '#ffffdd',
	'td.hi color'		=> 'blue',
	'td.hi background'	=> '#f6ffff',
	'td.lo color'		=> 'blue',
	'td.lo background'	=> '#deffff'
);
#
# A list of system default summary pages (defined further above)
# that you can use by 'Name' in $customsummarypages
global $sys_pages;
$sys_pages = array(
 'Mobile' => array($mobilepage, $mobilesum),
 'Stats' => array($statspage, $statssum),
 'Avalon' => array($avalonpage, $avalonsum, $avalonext),
 'Avalons' => array($avalonspage, $avalonssum, $avalonsext),
 'S9' => array($s9page, $s9sum, $s9ext),
 'Pools' => array($poolspage, $poolssum, $poolsext),
 'DevNot' => array($devnotpage, $devnotsum),
 'DevDet' => array($devdetpage, $devdetsum),
 'Proto' => array($protopage, $protosum, $protoext),
 'Kano' => array($kanogenpage, $kanogensum, $kanogenext),
 'Summary' => array($syspage, $syssum)
);
#
# Don't touch these
$miner = null;
$port = null;
#
global $rigips;
$rigips = array();
#
# Ensure it is only ever shown once
global $showndate;
$showndate = false;
#
global $rownum;
$rownum = 0;
#
// Login
global $ses;
$ses = 'rutroh';
#
function getcsp($name, $systempage = false)
{
 global $customsummarypages, $user_pages, $sys_pages;

 if ($systempage === false)
 {
	if (!isset($customsummarypages[$name]))
		return false;

	$csp = $customsummarypages[$name];
	if (is_array($csp))
	{
		if (count($csp) < 2 || count($csp) > 3)
			return false;
		else
			return $csp;
	}
 }

 if (isset($user_pages[$name]))
 {
	$csp = $user_pages[$name];
	if (!is_array($csp) || count($csp) < 2 || count($csp) > 3)
		return false;
	else
		return $csp;
 }

 if (isset($sys_pages[$name]))
 {
	$csp = $sys_pages[$name];
	if (!is_array($csp) || count($csp) < 2 || count($csp) > 3)
		return false;
	else
		return $csp;
 }

 return false;
}
#
function degenfields(&$sec, $name, $fields)
{
 global $allowgen;

 if (!is_array($fields))
	return;

 foreach ($fields as $num => $fld)
	if (substr($fld, 0, 5) == 'BGEN.' || substr($fld, 0, 4) == 'GEN.')
	{
		$opts = explode('||', $fld, 2);
		if ($allowgen)
		{
			if (count($opts) > 1)
				$sec[$name][$num] = $opts[0];
		}
		else
		{
			if (count($opts) > 1)
				$sec[$name][$num] = $opts[1];
			else
				unset($sec[$name][$num]);
		}
	}
}
#
# Allow BGEN/GEN fields to have a '||' replacement when gen is disabled
# N.B. if gen is disabled and all page fields are GBEN/GEN without '||' then
# the table will disappear
# Replacements can be in the page fields and then also the ext group fields
# All other $csp sections should list both separately
function degen(&$csp)
{
 $page = 0;
 if (isset($csp[$page]) && is_array($csp[$page]))
	foreach ($csp[$page] as $sec => $fields)
		degenfields($csp[$page], $sec, $fields);

 $ext = 2;
 if (isset($csp[$ext]) && is_array($csp[$ext]))
	foreach ($csp[$ext] as $sec => $types)
		if (is_array($types) && isset($types['group']))
			degenfields($types, 'group', $types['group']);
}
#
function getcss($cssname, $dom = false)
{
 global $colourtable, $colouroverride;

 $css = '';
 foreach ($colourtable as $cssdata => $value)
 {
	$cssobj = explode(' ', $cssdata, 2);
	if ($cssobj[0] == $cssname)
	{
		if (isset($colouroverride[$cssdata]))
			$value = $colouroverride[$cssdata];

		if ($dom == true)
			$css .= ' '.$cssobj[1].'='.$value;
		else
			$css .= $cssobj[1].':'.$value.'; ';
	}
 }
 return $css;
}
#
function getdom($domname)
{
 return getcss($domname, true);
}
#
# N.B. don't call this before calling htmlhead()
function php_pr($cmd)
{
 global $here, $autorefresh;

 return "$here?ref=$autorefresh$cmd";
}
#
function htmlhead($mcerr, $checkapi, $rig, $pg = null, $noscript = false)
{
 global $doctype, $title, $miner_font_family, $miner_font_size;
 global $bad_font_family, $bad_font_size, $add_css_names;
 global $error, $readonly, $poolinputs, $here;
 global $ignorerefresh, $autorefresh;

 $extraparams = '';
 if ($rig != null && $rig != '')
	$extraparams = "&rig=$rig";
 else
	if ($pg != null && $pg != '')
		$extraparams = "&pg=$pg";

 if ($ignorerefresh == true || $autorefresh == 0)
	$refreshmeta = '';
 else
 {
	$url = "$here?ref=$autorefresh$extraparams";
	$refreshmeta = "\n<meta http-equiv='refresh' content='$autorefresh;url=$url'>";
 }

 if ($readonly === false && $checkapi === true)
 {
	$error = null;
	$access = api($rig, 'privileged');
	if ($error != null
	||  !isset($access['STATUS']['STATUS'])
	||  $access['STATUS']['STATUS'] != 'S')
		$readonly = true;
 }
 $miner_font = "font-family:$miner_font_family; font-size:$miner_font_size;";
 $bad_font = "font-family:$bad_font_family; font-size:$bad_font_size;";

 echo "$doctype<html><head>$refreshmeta
<title>$title</title>
<style type='text/css'>
td { $miner_font ".getcss('td')."}
td.two { $miner_font ".getcss('td.two')."}
td.h { $miner_font ".getcss('td.h')."}
td.err { $miner_font ".getcss('td.err')."}
td.bad { $bad_font ".getcss('td.bad')."}
td.warn { $miner_font ".getcss('td.warn')."}
td.sta { $miner_font ".getcss('td.sta')."}
td.tot { $miner_font ".getcss('td.tot')."}
td.lst { $miner_font ".getcss('td.lst')."}
td.hi { $miner_font ".getcss('td.hi')."}
td.lo { $miner_font ".getcss('td.lo')."}\n";
 if (isset($add_css_names))
	foreach ($add_css_names as $css_name => $no_miner_font)
	{
		echo "$css_name { ";
		if ($no_miner_font !== true)
			echo "$miner_font ";
		echo getcss("$css_name")."}\n";
	}
 echo "</style>
</head><body".getdom('body').">\n";
if ($noscript === false)
{
echo "<script type='text/javascript'>
function pr(a,m){if(m!=null){if(!confirm(m+'?'))return}window.location='$here?ref=$autorefresh'+a}\n";

if ($ignorerefresh == false)
 echo "function prr(a){if(a){v=document.getElementById('refval').value}else{v=0}window.location='$here?ref='+v+'$extraparams'}\n";

 if ($readonly === false && $checkapi === true)
 {
echo "function prc(a,m){pr('&arg='+a,m)}
function prs(a,r){var c=a.substr(3);var z=c.split('|',2);var m=z[0].substr(0,1).toUpperCase()+z[0].substr(1)+' GPU '+z[1];prc(a+'&rig='+r,m)}
function prs2(a,n,r){var v=document.getElementById('gi'+n).value;var c=a.substr(3);var z=c.split('|',2);var m='Set GPU '+z[1]+' '+z[0].substr(0,1).toUpperCase()+z[0].substr(1)+' to '+v;prc(a+','+v+'&rig='+r,m)}\n";
	if ($poolinputs === true)
		echo "function cbs(s){var t=s.replace(/\\\\/g,'\\\\\\\\'); return t.replace(/,/g, '\\\\,')}\nfunction pla(r){var u=document.getElementById('purl').value;var w=document.getElementById('pwork').value;var p=document.getElementById('ppass').value;pr('&rig='+r+'&arg=addpool|'+cbs(u)+','+cbs(w)+','+cbs(p),'Add Pool '+u)}\nfunction psp(r){var p=document.getElementById('prio').value;pr('&rig='+r+'&arg=poolpriority|'+p,'Set Pool Priorities to '+p)}\n";
 }
echo "</script>\n";
}
?>
<table width=100% height=100% border=0 cellpadding=0 cellspacing=0 summary='Mine'>
<tr><td align=center valign=top>
<table border=0 cellpadding=4 cellspacing=0 summary='Mine'>
<?php
 echo $mcerr;
}
#
function minhead($mcerr = '')
{
 global $readonly;
 $readonly = true;
 htmlhead($mcerr, false, null, null, true);
}
#
global $haderror, $error;
$haderror = false;
$error = null;
#
function mcastrigs()
{
 global $rigs, $mcastexpect, $mcastaddr, $mcastport, $mcastcode;
 global $mcastlistport, $mcasttimeout, $mcastretries, $error;

 $listname = "0.0.0.0";

 $rigs = array();

 $rep_soc = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
 if ($rep_soc === false || $rep_soc == null)
 {
	$msg = "ERR: mcast listen socket create(UDP) failed";
	if ($rigipsecurity === false)
	{
		$error = socket_strerror(socket_last_error());
		$error = "$msg '$error'\n";
	}
	else
		$error = "$msg\n";

	return;
 }

 $res = socket_bind($rep_soc, $listname, $mcastlistport);
 if ($res === false)
 {
	$msg1 = "ERR: mcast listen socket bind(";
	$msg2 = ") failed";
	if ($rigipsecurity === false)
	{
		$error = socket_strerror(socket_last_error());
		$error = "$msg1$listname,$mcastlistport$msg2 '$error'\n";
	}
	else
		$error = "$msg1$msg2\n";

	socket_close($rep_soc);
	return;
 }

 $retries = $mcastretries;
 $doretry = ($retries > 0);
 do
 {
	$mcast_soc = socket_create(AF_INET, SOCK_DGRAM, SOL_UDP);
	if ($mcast_soc === false || $mcast_soc == null)
	{
		$msg = "ERR: mcast send socket create(UDP) failed";
		if ($rigipsecurity === false)
		{
			$error = socket_strerror(socket_last_error());
			$error = "$msg '$error'\n";
		}
		else
			$error = "$msg\n";

		socket_close($rep_soc);
		return;
	}

	$buf = "cgminer-$mcastcode-$mcastlistport";
	socket_sendto($mcast_soc, $buf, strlen($buf), 0, $mcastaddr, $mcastport);
	socket_close($mcast_soc);

	$stt = microtime(true);
	while (true)
	{
		$got = @socket_recvfrom($rep_soc, $buf, 32, MSG_DONTWAIT, $ip, $p);
		if ($got !== false && $got > 0)
		{
			$ans = explode('-', $buf, 4);
			if (count($ans) >= 3 && $ans[0] == 'cgm' && $ans[1] == 'FTW')
			{
				$rp = intval($ans[2]);

				if (count($ans) > 3)
					$mdes = str_replace("\0", '', $ans[3]);
				else
					$mdes = '';

				if (strlen($mdes) > 0)
					$rig = "$ip:$rp:$mdes";
				else
					$rig = "$ip:$rp";

				if (!in_array($rig, $rigs))
					$rigs[] = $rig;
			}
		}
		if ((microtime(true) - $stt) >= $mcasttimeout)
			break;

		usleep(100000);
	}

	if ($mcastexpect > 0 && count($rigs) >= $mcastexpect)
		$doretry = false;

 } while ($doretry && --$retries > 0);

 socket_close($rep_soc);
}
#
function getrigs()
{
 global $rigs;

 mcastrigs();

 sort($rigs);
}
#
function getsock($rig, $addr, $port)
{
 global $rigport, $rigips, $rignames, $rigipsecurity;
 global $haderror, $error, $socksndtimeoutsec, $sockrcvtimeoutsec;

 $port = trim($port);
 if (strlen($port) == 0)
	$port = $rigport;
 $error = null;
 $socket = null;
 $socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
 if ($socket === false || $socket === null)
 {
	$haderror = true;
	if ($rigipsecurity === false)
	{
		$error = socket_strerror(socket_last_error());
		$msg = "socket create(TCP) failed";
		$error = "ERR: $msg '$error'\n";
	}
	else
		$error = "ERR: socket create(TCP) failed\n";

	return null;
 }

 // Ignore if this fails since the socket connect may work anyway
 //  and nothing is gained by aborting if the option cannot be set
 //  since we don't know in advance if it can connect
 socket_set_option($socket, SOL_SOCKET, SO_SNDTIMEO, array('sec' => $socksndtimeoutsec, 'usec' => 0));
 socket_set_option($socket, SOL_SOCKET, SO_RCVTIMEO, array('sec' => $sockrcvtimeoutsec, 'usec' => 0));

 $res = socket_connect($socket, $addr, $port);
 if ($res === false)
 {
	$haderror = true;
	if ($rigipsecurity === false)
	{
		$error = socket_strerror(socket_last_error());
		$msg = "socket connect($addr,$port) failed";
		$error = "ERR: $msg '$error'\n";
	}
	else
		$error = "ERR: socket connect($rig) failed\n";

	socket_close($socket);
	return null;
 }
 if ($rignames !== false && !isset($rigips[$addr]))
	if (socket_getpeername($socket, $ip) == true)
		$rigips[$addr] = $ip;
 return $socket;
}
#
function readsockline($socket)
{
 $line = '';
 while (true)
 {
	$byte = socket_read($socket, 1);
	if ($byte === false || $byte === '')
		break;
	if ($byte === "\0")
		break;
	$line .= $byte;
 }
 return $line;
}
#
function api_convert_escape($str)
{
 $res = '';
 $len = strlen($str);
 for ($i = 0; $i < $len; $i++)
 {
	$ch = substr($str, $i, 1);
	if ($ch != '\\' || $i == ($len-1))
		$res .= $ch;
	else
	{
		$i++;
		$ch = substr($str, $i, 1);
		switch ($ch)
		{
		case '|':
			$res .= "\1";
			break;
		case '\\':
			$res .= "\2";
			break;
		case '=':
			$res .= "\3";
			break;
		case ',':
			$res .= "\4";
			break;
		default:
			$res .= $ch;
		}
	}
 }
 return $res;
}
#
function revert($str)
{
 return str_replace(array("\1", "\2", "\3", "\4"), array("|", "\\", "=", ","), $str);
}
#
function api($rig, $cmd)
{
 global $haderror, $error;
 global $miner, $port, $hidefields, $mmcmd;

 $socket = getsock($rig, $miner, $port);
 if ($socket != null)
 {
	if ($cmd == 'mm')
		socket_write($socket, $mmcmd, strlen($mmcmd));
	else
		socket_write($socket, $cmd, strlen($cmd));
	$line = readsockline($socket);
	socket_close($socket);

	if (strlen($line) == 0)
	{
		$haderror = true;
		$error = "WARN: '$cmd' returned nothing\n";
		return $line;
	}

#	print "$cmd returned '$line'\n";

	$line = api_convert_escape($line);

	$data = array();

	$objs = explode('|', $line);
	foreach ($objs as $obj)
	{
		if (strlen($obj) > 0)
		{
			$items = explode(',', $obj);
			$item = $items[0];
			$id = explode('=', $items[0], 2);
			if (count($id) == 1 or !ctype_digit($id[1]))
				$name = $id[0];
			else
				$name = $id[0].$id[1];

			if (strlen($name) == 0)
				$name = 'null';

			$sectionname = preg_replace('/\d/', '', $name);

			if (isset($data[$name]))
			{
				$num = 1;
				while (isset($data[$name.$num]))
					$num++;
				$name .= $num;
			}

			$counter = 0;
			foreach ($items as $item)
			{
				$id = explode('=', $item, 2);

				if (isset($hidefields[$sectionname.'.'.$id[0]]))
					continue;

				if (count($id) == 2)
					$data[$name][$id[0]] = revert($id[1]);
				else
					$data[$name][$counter] = $id[0];

				$counter++;
			}
		}
	}
	if ($cmd == 'mm')
	{
		$data2 = array();
		$num = 0;
		foreach ($data as $name => $values)
		{
			if (substr($name, 0, 5) !== 'STATS')
				$data2[$name] = $values;
			else
			{
				if (substr($values['ID'], 0, 4) == 'POOL')
					continue;
				$snum = substr($name, 5);
				#
				if (isset($values['Connector']))
					$auc = $values['Connector'];
				else
					$auc = 'AUC';
				$auc .= $snum;
				#
				foreach ($values as $nam => $val)
				{
					if (substr($nam, 0, 5) !== 'MM ID')
						continue;
					$mm = 'MM'.$num;
					#
					$data2[$mm] = array('MM' => $num,
							'ID' => $values['ID'],
							'MMID' => substr($nam,5),
							'Connecter' => $auc);
					#
					$len = strlen($val);
					$pos = 0;
					while ($pos < $len)
					{
						while ($val[$pos] == ' ')
							$pos++;
						$brl = strpos($val, '[', $pos+1);
						if ($brl === false)
							break;
						$brr = strpos($val, ']', $brl+1);
						if ($brr === false)
							break;
						#
						$subn = substr($val, $pos, $brl - $pos);
						$data2[$mm][$subn] = substr($val, $brl+1, $brr-$brl-1);
						#
						$pos = $brr+1;
					}
					#
					$num++;
				}
			}
		}
		return $data2;
	}
	return $data;
 }
 return null;
}
#
function getparam($name, $both = false)
{
 $a = null;
 if (isset($_POST[$name]))
	$a = $_POST[$name];

 if (($both === true) and ($a === null))
 {
	if (isset($_GET[$name]))
		$a = $_GET[$name];
 }

 if ($a == '' || $a == null)
	return null;

 // limit to 1K just to be safe
 return substr($a, 0, 1024);
}
#
function newtable()
{
 global $tablebegin, $rownum;
 echo $tablebegin;
 $rownum = 0;
}
#
function newrow()
{
 echo '<tr>';
}
#
function othrow($row)
{
 return "<tr>$row</tr>";
}
#
function otherrow($row)
{
 echo othrow($row);
}
#
function endrow()
{
 global $rownum;
 echo '</tr>';
 $rownum++;
}
#
function endtable()
{
 global $tableend;
 echo $tableend;
}
#
function classlastshare($when, $alldata, $warnclass, $errorclass)
{
 global $checklastshare;

 if ($checklastshare === false)
	return '';

 if ($when == 0)
	return '';

 if (!isset($alldata['MHS av']))
	return '';

 if ($alldata['MHS av'] == 0)
	return '';

 if (!isset($alldata['Last Share Time']))
	return '';

 if (!isset($alldata['Last Share Difficulty']))
	return '';

 $expected = pow(2, 32) / ($alldata['MHS av'] * pow(10, 6));

 // If the share difficulty changes while waiting on a share,
 // this calculation will of course be incorrect
 $expected *= $alldata['Last Share Difficulty'];

 $howlong = $when - $alldata['Last Share Time'];
 if ($howlong < 1)
	$howlong = 1;

 if ($howlong > ($expected * 12))
	return $errorclass;

 if ($howlong > ($expected * 8))
	return $warnclass;

 return '';
}
#
function endzero($num)
{
 $rep = preg_replace('/0*$/', '', $num);
 if ($rep === '')
	$rep = '0';
 return $rep;
}
#
function fmt($section, $name, $value, $when, $alldata, $cf = NULL)
{
 global $dfmt, $rownum;
 global $avatmaxmed, $avatmaxhi, $avatempmed, $avatemphi, $avaghsmmmin;
 global $s9tempmed, $s9temphi, $s9chainmin, $s9acn;

 if ($alldata == null)
	$alldata = array();

 $errorclass = 'err';
 $warnclass = 'warn';
 $lstclass = 'lst';
 $hiclass = 'hi';
 $loclass = 'lo';
 $c2class = 'two';
 $totclass = 'tot';
 $b = '&nbsp;';

 $class = '';

 $nams = explode('.', $name);
 if (count($nams) > 1)
	$name = $nams[count($nams)-1];

 $done = false;
 if ($value === null)
 {
	$ret = $b;
	$done = true;
 }
 else
	if ($cf != NULL and function_exists($cf))
	{
		list($ret, $class) = $cf($section, $name, $value, $when, $alldata,
					   $warnclass, $errorclass, $hiclass, $loclass, $totclass);
		if ($ret !== '')
			$done = true;
	}

 if ($done === false)
 {
	$ret = $value;
	/*
	 * To speed up the PHP, the case statement is just $name
	 * It used to be $section.'.'.$name
	 * If any names clash, the case code will need to check the value of
	 *  $section to resolve the clash - as with 'Last Share Time' below
	 * If the code picks up a field that you wish to format differently,
	 *  then you'll need a customsummarypage 'fmt' extension
	 * This also removes trailing numbers so multiple fields like in the
	 *  S9 don't require many case lines
	 */
	switch (preg_replace('/\d*$/', '', $name))
	{
	case '0':
		break;
	case 'Last Share Time':
		if ($section == 'total')
			break;
		if ($section == 'POOL')
		{
			if ($value == 0)
				$ret = 'Never';
			else
				$ret = date('H:i:s d-M', $value);
		}
		else
		{
			if ($value == 0
			||  (isset($alldata['Last Share Pool']) && $alldata['Last Share Pool'] == -1))
			{
				$ret = 'Never';
				$class = $warnclass;
			}
			else
			{
				$ret = date('H:i:s', $value);
				$class = classlastshare($when, $alldata, $warnclass, $errorclass);
			}
		}
		break;
	case 'Last getwork':
	case 'Last Valid Work':
		if ($section == 'total')
			break;
		if ($value == 0)
			$ret = 'Never';
		else
			$ret = ($value - $when) . 's';
		break;
	case 'Last Share Pool':
		if ($section == 'total')
			break;
		if ($value == -1)
		{
			$ret = 'None';
			$class = $warnclass;
		}
		break;
	case 'Elapsed':
	case 'Device Elapsed':
		if ($section == 'total')
			break;
		$s = $value % 60;
		$value -= $s;
		$value /= 60;
		if ($value == 0)
			$ret = $s.'s';
		else
		{
			$m = $value % 60;
			$value -= $m;
			$value /= 60;
			if ($value == 0)
				$ret = sprintf("%dm$b%02ds", $m, $s);
			else
			{
				$h = $value % 24;
				$value -= $h;
				$value /= 24;
				if ($value == 0)
					$ret = sprintf("%dh$b%02dm$b%02ds", $h, $m, $s);
				else
				{
					if ($value == 1)
						$days = '';
					else
						$days = 's';
	
					$ret = sprintf("%dday$days$b%02dh$b%02dm$b%02ds", $value, $h, $m, $s);
				}
			}
		}
		break;
	case 'Last Well':
		if ($section == 'total')
			break;
		if ($value == '0')
		{
			$ret = 'Never';
			$class = $warnclass;
		}
		else
			$ret = date('H:i:s', $value);
		break;
	case 'Last Not Well':
		if ($section == 'total')
			break;
		if ($value == '0')
			$ret = 'Never';
		else
		{
			$ret = date('H:i:s', $value);
			$class = $errorclass;
		}
		break;
	case 'Reason Not Well':
		if ($section == 'total')
			break;
		if ($value != 'None')
			$class = $errorclass;
		break;
	case 'Utility':
		$ret = number_format($value, 2).'/m';
		if ($value == 0)
			$class = $errorclass;
		else
			if (isset($alldata['Difficulty Accepted'])
			&&  isset($alldata['Accepted'])
			&&  isset($alldata['MHS av'])
			&&  ($alldata['Difficulty Accepted'] > 0)
			&&  ($alldata['Accepted'] > 0))
			{
				$expected = 60 * $alldata['MHS av'] * (pow(10, 6) / pow(2, 32));
				if ($expected == 0)
					$expected = 0.000001; // 1 H/s

				$da = $alldata['Difficulty Accepted'];
				$a = $alldata['Accepted'];
				$expected /= ($da / $a);

				$ratio = $value / $expected;
				if ($ratio < 0.9)
					$class = $loclass;
				else
					if ($ratio > 1.1)
						$class = $hiclass;
			}
		break;
	case 'Work Utility':
		$ret = number_format($value, 2).'/m';
		break;
	case 'Temperature':
		if ($section == 'total')
			break;
		$ret = $value.'&deg;C';
		if (!isset($alldata['GPU']))
		{
			if ($value == 0)
				$ret = '&nbsp;';
			break;
		}
	case 'GPU Clock':
	case 'Memory Clock':
	case 'GPU Voltage':
	case 'GPU Activity':
		if ($section == 'total')
			break;
		if ($value == 0)
			$class = $warnclass;
		break;
	case 'Fan Percent':
		if ($section == 'total')
			break;
		if ($value == 0)
			$class = $warnclass;
		else
		{
			if ($value == 100)
				$class = $errorclass;
			else
				if ($value > 85)
					$class = $warnclass;
		}
		break;
	case 'Fan Speed':
		if ($section == 'total')
			break;
		if ($value == 0)
			$class = $warnclass;
		else
			if (isset($alldata['Fan Percent']))
			{
				$test = $alldata['Fan Percent'];
				if ($test == 100)
					$class = $errorclass;
				else
					if ($test > 85)
						$class = $warnclass;
			}
		break;
	case 'MHS av':
	case 'MHS 5s':
	case 'MHS 1m':
	case 'MHS 5m':
	case 'MHS 15m':
		$parts = explode('.', $value, 2);
		if (count($parts) == 1)
			$dec = '';
		else
			$dec = '.'.$parts[1];
		$ret = number_format((float)$parts[0]).$dec;

		if ($value == 0)
			$class = $errorclass;
		else
			if (isset($alldata['Difficulty Accepted'])
			&&  isset($alldata['Accepted'])
			&&  isset($alldata['Utility'])
			&&  ($alldata['Difficulty Accepted'] > 0)
			&&  ($alldata['Accepted'] > 0))
			{
				$expected = 60 * $value * (pow(10, 6) / pow(2, 32));
				if ($expected == 0)
					$expected = 0.000001; // 1 H/s

				$da = $alldata['Difficulty Accepted'];
				$a = $alldata['Accepted'];
				$expected /= ($da / $a);

				$ratio = $alldata['Utility'] / $expected;
				if ($ratio < 0.9)
					$class = $hiclass;
				else
					if ($ratio > 1.1)
						$class = $loclass;
			}
		break;
	case 'Total MH':
	case 'Getworks':
	case 'Works':
	case 'Accepted':
	case 'Rejected':
	case 'Local Work':
	case 'Discarded':
	case 'Diff1 Shares':
	case 'Diff1 Work':
	case 'Times Sent':
	case 'Bytes Sent':
	case 'Net Bytes Sent':
	case 'Times Recv':
	case 'Bytes Recv':
	case 'Net Bytes Recv':
		$parts = explode('.', $value, 2);
		if (count($parts) == 1)
			$dec = '';
		else
			$dec = '.'.$parts[1];
		$ret = number_format((float)$parts[0]).$dec;
		break;
	case 'Hs':
	case 'W':
	case 'history_time':
	case 'Pool Wait':
	case 'Pool Max':
	case 'Pool Min':
	case 'Pool Av':
	case 'Min Diff':
	case 'Max Diff':
	case 'Work Diff':
		$parts = explode('.', $value, 2);
		if (count($parts) == 1)
			$dec = '';
		else
			$dec = '.'.endzero($parts[1]);
		$ret = number_format((float)$parts[0]).$dec;
		break;
	case 'Status':
		if ($section == 'total')
			break;
		if ($value != 'Alive')
			$class = $errorclass;
		break;
	case 'Enabled':
		if ($section == 'total')
			break;
		if ($value != 'Y')
			$class = $warnclass;
		break;
	case 'No Device':
		if ($section == 'total')
			break;
		if ($value != 'false')
			$class = $errorclass;
		break;
	case 'When':
	case 'Current Block Time':
		if ($section == 'total')
			break;
		$ret = date($dfmt, $value);
		break;
	case 'Current Block Version':
		if ($section == 'total')
			break;
		$ret = sprintf("0x%08x", $value);
		break;
	case 'Last Share Difficulty':
		if ($section == 'total')
			break;
	case 'Difficulty Accepted':
	case 'Difficulty Rejected':
	case 'Difficulty Stale':
	case 'Network Difficulty':
		if ($value != '')
			$ret = number_format((float)$value, 2);
		break;
	case 'Device Hardware%':
	case 'Device Rejected%':
	case 'Pool Rejected%':
	case 'Pool Stale%':
		if ($section == 'total')
			break;
		if ($value != '')
			$ret = number_format((float)$value, 2) . '%';
		break;
	case 'Best Share':
		if ($section == 'total')
			break;
	case 'Hardware Errors':
		if ($value != '')
			$ret = number_format((float)$value);
		break;
	// Ava specific stats fields
	case 'GHSmm':
	case 'AVATHS':
		if ($value != '')
			$ret = number_format((float)$value, 2);

		if ($section != 'total')
		{
			if ($value == 0)
				$class = $errorclass;
			else
			{
				if ($name == 'GHSmm')
				{
					if ($value < $avaghsmmmin)
						$class = $warnclass;
				}
				else
				{
					if ($value < ($avaghsmmmin/1000.0))
						$class = $warnclass;
				}
			}
		}
		break;
	case 'Temp':
		if ($section == 'total')
			break;
		$ret = $value.'&deg;C';
		if ($section == 'MM')
		{
			if ($value == 0 || $value >= $avatemphi)
				$class = $errorclass;
			else
				if ($value >= $avatempmed)
					$class = $warnclass;
		}
		break;
	case 'TMax':
		if ($section == 'total')
			break;
		$ret = $value.'&deg;C';
		if ($value == 0 || $value >= $avatmaxhi)
			$class = $errorclass;
		else
			if ($value >= $avatmaxmed)
				$class = $warnclass;
		break;
	case 'PG':
		if ($section == 'total')
			break;
		if ($value != '15')
		{
			if ($value > 10)
				$class = $warnclass;
			else
				$class = $errorclass;

		}
		$ret = $value;
		break;
	case 'ECHU':
		if ($section == 'total')
			break;
		$ret = $value;
		$chk = str_replace(array(' ','0'), '', $value);
		if ($chk != '')
		{
			$class = $warnclass;
			$vals = explode(' ', $value);
			$ret = '';
			$sep = '';
			foreach ($vals as $one)
			{
				if ($one == '0' || $one == '1')
					$ret .= $sep.$one;
				else
					$ret .= $sep.'x'.dechex($one);
				$sep = ' ';
			}
		}
		$ret = str_replace(' ', '&nbsp;', $ret);
		break;
	case 'Vi':
	case 'Vo':
		if ($section == 'total')
			break;
		$ret = str_replace(' ', '&nbsp;', $value);
		break;
	case 'ECMM':
		if ($section == 'total')
			break;
		if ($value != '0')
		{
			$class = $warnclass;
			$ret = 'x'.dechex($value);
		}
		else
			$ret = $value;
		break;
	case 'Ver':
		if ($section == 'total')
			break;
		$ret = str_replace('-', '&#8209;', $value);
		break;
	// S9 specific stats fields
	case 'fan_num':
		if ($section == 'total')
			break;
		$ret = $value;
		if ($value < 1)
			$class = $warnclass;
		break;
	case 'temp2_':
		if ($section == 'total')
			break;
		$ret = $value.'&deg;C';
		if ($value == 0 || $value >= $s9temphi)
			$class = $errorclass;
		else
			if ($value >= $s9tempmed)
				$class = $warnclass;
		break;
	case 'chain_acn':
		if ($section == 'total')
			break;
		$ret = $value;
		if ($value != $s9acn)
			$class = $warnclass;
		break;
	case 'chain_rate':
		if ($value != '')
			$ret = number_format((float)$value, 2);

		if ($section != 'total')
		{
			if ($value == 0)
				$class = $errorclass;
			else
			{
				if ($value < $s9chainmin)
					$class = $warnclass;
			}
		}
		break;
	case 'total_rate':
		if ($value != '')
			$ret = number_format((float)$value, 2);
		if ($value == 0)
			$class = $errorclass;
		break;
	// BUTTON.
	case 'Rig':
	case 'Pool':
	case 'GPU':
		break;
	// Sample GEN fields
	case 'Mined':
		if ($value != '')
			$ret = number_format((float)$value * 100.0, 3) . '%';
		break;
	case 'Acc':
	case 'Rej':
		if ($value != '')
			$ret = number_format((float)$value * 100.0, 2) . '%';
		break;
	case 'GHS av':
	case 'GHS 5m':
	case 'GHS WU':
	case 'GHS Acc':
	case 'THS':
	case 'THS av':
	case 'THS 5m':
	case 'THS WU':
	case 'THS Acc':
		if ($value != '')
			$ret = number_format((float)$value, 2);
		break;
	case 'AvShr':
		if ($section == 'total')
			break;
		if ($value != '')
			$ret = number_format((float)$value, 2);
		if ($value == 0)
			$class = $warnclass;
		break;
	}
 }

 if ($section == 'NOTIFY' && substr($name, 0, 1) == '*' && $value != '0')
	$class = $errorclass;

 if ($class == '' && $section != 'POOL')
	$class = classlastshare($when, $alldata, $lstclass, $lstclass);

 if ($class == '' && $section == 'total')
	$class = $totclass;

 if ($class == '' && ($rownum % 2) == 0)
	$class = $c2class;

 if ($ret === '')
	$ret = $b;

 if ($class !== '')
	$class = " class=$class";

 return array($ret, $class);
}
#
global $poolcmd;
$poolcmd = array(	'Switch to'	=> 'switchpool',
			'Enable'	=> 'enablepool',
			'Disable'	=> 'disablepool',
			'Remove'	=> 'removepool' );
#
function showhead($cmd, $values, $justnames = false)
{
 global $poolcmd, $readonly;

 newrow();

 foreach ($values as $name => $value)
 {
	if ($name == '0' or $name == '')
		$name = '&nbsp;';
	echo "<td valign=bottom class=h>$name</td>";
 }

 if ($justnames === false && $cmd == 'pools' && $readonly === false)
	foreach ($poolcmd as $name => $pcmd)
		echo "<td valign=bottom class=h>$name</td>";

 endrow();
}
#
function showdatetime()
{
 global $dfmt;

 otherrow('<td class=sta>Date: '.date($dfmt).'</td>');
}
#
global $singlerigsum;
$singlerigsum = array(
 'devs' => array('MHS av' => 1, 'MHS 5s' => 1, 'MHS 1m' => 1, 'MHS 5m' => 1,
			'MHS 15m' => 1, 'Accepted' => 1, 'Rejected' => 1,
			'Hardware Errors' => 1, 'Utility' => 1, 'Total MH' => 1,
			'Diff1 Shares' => 1, 'Diff1 Work' => 1,
			'Difficulty Accepted' => 1, 'Difficulty Rejected' => 1),
 'pools' => array('Getworks' => 1, 'Accepted' => 1, 'Rejected' => 1, 'Discarded' => 1,
			'Stale' => 1, 'Get Failures' => 1, 'Remote Failures' => 1,
			'Diff1 Shares' => 1, 'Diff1 Work' => 1,
			'Difficulty Accepted' => 1, 'Difficulty Rejected' => 1,
			'Difficulty Stale' => 1),
 'notify' => array('*' => 1));
#
function showtotal($total, $when, $oldvalues)
{
 global $rigtotals;

 list($showvalue, $class) = fmt('total', '', 'Total:', $when, null);
 echo "<td$class align=right>$showvalue</td>";

 $skipfirst = true;
 foreach ($oldvalues as $name => $value)
 {
	if ($skipfirst === true)
	{
		$skipfirst = false;
		continue;
	}

	if (isset($total[$name]))
		$newvalue = $total[$name];
	else
		$newvalue = '';

	list($showvalue, $class) = fmt('total', $name, $newvalue, $when, null);
	echo "<td$class";
	if ($rigtotals === true)
		echo ' align=right';
	echo ">$showvalue</td>";
 }
}
#
function details($cmd, $list, $rig)
{
 global $dfmt, $poolcmd, $readonly, $showndate;
 global $rownum, $rigtotals, $forcerigtotals, $singlerigsum;

 $when = 0;

 $stas = array('S' => 'Success', 'W' => 'Warning', 'I' => 'Informational', 'E' => 'Error', 'F' => 'Fatal');

 newtable();

 if ($showndate === false)
 {
	showdatetime();

	endtable();
	newtable();

	$showndate = true;
 }

 if (isset($list['STATUS']))
 {
	newrow();
	echo '<td>Computer: '.$list['STATUS']['Description'].'</td>';
	if (isset($list['STATUS']['When']))
	{
		echo '<td>When: '.date($dfmt, $list['STATUS']['When']).'</td>';
		$when = $list['STATUS']['When'];
	}
	$sta = $list['STATUS']['STATUS'];
	echo '<td>Status: '.$stas[$sta].'</td>';
	echo '<td>Message: '.$list['STATUS']['Msg'].'</td>';
	endrow();
 }

 if ($rigtotals === true && isset($singlerigsum[$cmd]))
	$dototal = $singlerigsum[$cmd];
 else
	$dototal = array();

 $total = array();

 $section = '';
 $oldvalues = null;
 foreach ($list as $item => $values)
 {
	if ($item == 'STATUS')
		continue;

	$sectionname = preg_replace('/\d/', '', $item);

	// Handle 'devs' possibly containing >1 table
	if ($sectionname != $section)
	{
		if ($oldvalues != null && count($total) > 0
		&&  ($rownum > 2 || $forcerigtotals === true))
			showtotal($total, $when, $oldvalues);

		endtable();
		newtable();
		showhead($cmd, $values);
		$section = $sectionname;
	}

	newrow();

	foreach ($values as $name => $value)
	{
		list($showvalue, $class) = fmt($section, $name, $value, $when, $values);
		echo "<td$class";
		if ($rigtotals === true)
			echo ' align=right';
		echo ">$showvalue</td>";

		if (isset($dototal[$name])
		||  (isset($dototal['*']) and substr($name, 0, 1) == '*'))
		{
			if (isset($total[$name]))
				$total[$name] += $value;
			else
				$total[$name] = $value;
		}
	}

	if ($cmd == 'pools' && $readonly === false)
	{
		reset($values);
		$pool = current($values);
		foreach ($poolcmd as $name => $pcmd)
		{
			list($ignore, $class) = fmt('BUTTON', 'Pool', '', $when, $values);
			echo "<td$class>";
			if ($pool === false)
				echo '&nbsp;';
			else
			{
				echo "<input type=button value='Pool $pool'";
				echo " onclick='prc(\"$pcmd|$pool&rig=$rig\",\"$name Pool $pool\")'>";
			}
			echo '</td>';
		}
	}
	endrow();

	$oldvalues = $values;
 }

 if ($oldvalues != null && count($total) > 0
 &&  ($rownum > 2 || $forcerigtotals === true))
	showtotal($total, $when, $oldvalues);

 endtable();
}
#
global $devs;
$devs = null;
#
function gpubuttons($count, $rig)
{
 global $devs;

 $basic = array( 'GPU', 'Enable', 'Disable', 'Restart' );

 $options = array(	'intensity' => 'Intensity',
			'fan' => 'Fan Percent',
			'engine' => 'GPU Clock',
			'mem' => 'Memory Clock',
			'vddc' => 'GPU Voltage' );

 newtable();
 newrow();

 foreach ($basic as $head)
	echo "<td class=h>$head</td>";

 foreach ($options as $name => $des)
	echo "<td class=h nowrap>$des</td>";

 $n = 0;
 for ($c = 0; $c < $count; $c++)
 {
	endrow();
	newrow();

	foreach ($basic as $name)
	{
		list($ignore, $class) = fmt('BUTTON', 'GPU', '', 0, null);
		echo "<td$class>";

		if ($name == 'GPU')
			echo $c;
		else
		{
			echo "<input type=button value='$name $c' onclick='prs(\"gpu";
			echo strtolower($name);
			echo "|$c\",$rig)'>";
		}

		echo '</td>';
	}

	foreach ($options as $name => $des)
	{
		list($ignore, $class) = fmt('BUTTON', 'GPU', '', 0, null);
		echo "<td$class>";

		if (!isset($devs["GPU$c"][$des]))
			echo '&nbsp;';
		else
		{
			$value = $devs["GPU$c"][$des];
			echo "<input type=button value='Set $c:' onclick='prs2(\"gpu$name|$c\",$n,$rig)'>";
			echo "<input size=7 type=text name=gi$n value='$value' id=gi$n>";
			$n++;
		}

		echo '</td>';
	}

 }
 endrow();
 endtable();
}
#
function processgpus($rig)
{
 global $error;
 global $warnfont, $warnoff;

 $gpus = api($rig, 'gpucount');

 if ($error != null)
	otherrow("<td>Error getting GPU count: $warnfont$error$warnoff</td>");
 else
 {
	if (!isset($gpus['GPUS']['Count']))
	{
		$rw = '<td>No GPU count returned: '.$warnfont;
		$rw .= $gpus['STATUS']['STATUS'].' '.$gpus['STATUS']['Msg'];
		$rw .= $warnoff.'</td>';
		otherrow($rw);
	}
	else
	{
		$count = $gpus['GPUS']['Count'];
		if ($count == 0)
			otherrow('<td>No GPUs</td>');
		else
			gpubuttons($count, $rig);
	}
 }
}
#
function showpoolinputs($rig, $ans)
{
 global $readonly, $poolinputs;

 if ($readonly === true || $poolinputs === false)
	return;

 newtable();
 newrow();

 $inps = array('Pool URL' => array('purl', 20),
		'Worker Name' => array('pwork', 10),
		'Worker Password' => array('ppass', 10));
 $b = '&nbsp;';

 echo "<td align=right class=h> Add a pool: </td><td>";

 foreach ($inps as $text => $name)
	echo "$text: <input name='".$name[0]."' id='".$name[0]."' value='' type=text size=".$name[1]."> ";

 echo "</td><td align=middle><input type=button value='Add' onclick='pla($rig)'></td>";

 endrow();

 if (count($ans) > 1)
 {
	newrow();

	echo '<td align=right class=h> Set pool priorities: </td>';
	echo "<td> Comma list of pool numbers: <input type=text name=prio id=prio size=20>";
	echo "</td><td align=middle><input type=button value='Set' onclick='psp($rig)'></td>";

	endrow();
 }
 endtable();
}
#
function process($cmds, $rig)
{
 global $error, $devs;
 global $warnfont, $warnoff;

 $count = count($cmds);
 foreach ($cmds as $cmd => $des)
 {
	$process = api($rig, $cmd);

	if ($error != null)
	{
		otherrow("<td colspan=100>Error getting $des: $warnfont$error$warnoff</td>");
		break;
	}
	else
	{
		details($cmd, $process, $rig);

		if ($cmd == 'devs')
			$devs = $process;

		if ($cmd == 'pools')
			showpoolinputs($rig, $process);

		# Not after the last one
		if (--$count > 0)
			otherrow('<td><br><br></td>');
	}
 }
}
#
function rigname($rig, $rigname)
{
 global $rigs, $rignames, $rigips;

 if (isset($rigs[$rig]))
 {
	$parts = explode(':', $rigs[$rig], 3);
	if (count($parts) == 3)
		$rigname = $parts[2];
	else
		if ($rignames !== false)
		{
			switch ($rignames)
			{
			case 'ip':
				if (isset($parts[0]) && isset($rigips[$parts[0]]))
				{
					$ip = explode('.', $rigips[$parts[0]]);
					if (count($ip) == 4)
						$rigname = intval($ip[3]);
				}
				break;
			case 'ipx':
				if (isset($parts[0]) && isset($rigips[$parts[0]]))
				{
					$ip = explode('.', $rigips[$parts[0]]);
					if (count($ip) == 4)
						$rigname = intval($ip[3], 16);
				}
				break;
			}
		}
 }

 return $rigname;
}
#
function riginput($rig, $rigname, $usebuttons)
{
 $rigname = rigname($rig, $rigname);

 if ($usebuttons === true)
	return "<input type=button value='$rigname' onclick='pr(\"&rig=$rig\",null)'>";
 else
	return "<a href='".php_pr("&rig=$rig")."'>$rigname</a>";
}
#
function rigbutton($rig, $rigname, $when, $row, $usebuttons)
{
 list($value, $class) = fmt('BUTTON', 'Rig', '', $when, $row);

 if ($rig === '')
	$ri = '&nbsp;';
 else
	$ri = riginput($rig, $rigname, $usebuttons);

 return "<td align=middle$class>$ri</td>";
}
#
function showrigs($anss, $headname, $rigname)
{
 global $rigbuttons;

 $dthead = array($headname => 1, 'STATUS' => 1, 'Description' => 1, 'When' => 1, 'API' => 1, 'CGMiner' => 1);
 showhead('', $dthead);

 foreach ($anss as $rig => $ans)
 {
	if ($ans == null)
		continue;

	newrow();

	$when = 0;
	if (isset($ans['STATUS']['When']))
		$when = $ans['STATUS']['When'];

	foreach ($ans as $item => $row)
	{
		if ($item != 'STATUS' && $item != 'VERSION')
			continue;

		foreach ($dthead as $name => $x)
		{
			if ($item == 'STATUS' && $name == $headname)
				echo rigbutton($rig, $rigname.$rig, $when, null, $rigbuttons);
			else
			{
				if (isset($row[$name]))
				{
					list($showvalue, $class) = fmt('STATUS', $name, $row[$name], $when, null);
					echo "<td$class align=right>$showvalue</td>";
				}
			}
		}
	}
	endrow();
 }
}
#
function refreshbuttons()
{
 global $ignorerefresh, $changerefresh, $autorefresh;

 if ($ignorerefresh == false && $changerefresh == true)
 {
	echo '&nbsp;&nbsp;&nbsp;&nbsp;';
	echo "<input type=button value='Auto Refresh:' onclick='prr(true)'>";
	echo "<input type=text name='refval' id='refval' size=2 value='$autorefresh'>";
	echo "<input type=button value='Off' onclick='prr(false)'>";
 }
}
#
function pagebuttons($rig, $pg)
{
 global $readonly, $rigs, $rigbuttons, $userlist, $ses;
 global $allowcustompages, $customsummarypages;

 if ($rig === null)
 {
	$prev = null;
	$next = null;

	if ($pg === null)
		$refresh = '';
	else
		$refresh = "&pg=$pg";
 }
 else
 {
	switch (count($rigs))
	{
	case 0:
	case 1:
		$prev = null;
		$next = null;
		break;
	case 2:
		$prev = null;
		$next = ($rig + 1) % count($rigs);
		break;
	default:
		$prev = ($rig - 1) % count($rigs);
		$next = ($rig + 1) % count($rigs);
		break;
	}

	$refresh = "&rig=$rig";
 }

 echo '<tr><td><table cellpadding=0 cellspacing=0 border=0><tr><td nowrap>';
 if ($userlist === null || isset($_SESSION[$ses]))
 {
	if ($prev !== null)
		echo riginput($prev, 'Prev', true).'&nbsp;';

	echo "<input type=button value='Refresh' onclick='pr(\"$refresh\",null)'>&nbsp;";

	if ($next !== null)
		echo riginput($next, 'Next', true).'&nbsp;';
	echo '&nbsp;';
	if (count($rigs) > 1 and getcsp('Summary', true) !== false)
		echo "<input type=button value='Summary' onclick='pr(\"\",null)'>&nbsp;";
 }

 if ($allowcustompages === true)
 {
	if ($userlist === null || isset($_SESSION[$ses]))
		$list = $customsummarypages;
	else
	{
		if ($userlist !== null && isset($userlist['def']))
			$list = array_flip($userlist['def']);
		else
			$list = array();
	}

	foreach ($list as $pagename => $data)
		if (getcsp($pagename) !== false)
			echo "<input type=button value='$pagename' onclick='pr(\"&pg=$pagename\",null)'>&nbsp;";
 }

 echo '</td><td width=100%>&nbsp;</td><td nowrap>';
 if ($rig !== null && $readonly === false)
 {
	$rg = '';
	if (count($rigs) > 1)
		$rg = " Rig $rig";
	echo "<input type=button value='Restart' onclick='prc(\"restart&rig=$rig\",\"Restart CGMiner$rg\")'>";
	echo "&nbsp;<input type=button value='Quit' onclick='prc(\"quit&rig=$rig\",\"Quit CGMiner$rg\")'>";
 }
 refreshbuttons();
 if (isset($_SESSION[$ses]))
	echo "&nbsp;<input type=button value='Logout' onclick='pr(\"&logout=1\",null)'>";
 else
	if ($userlist !== null)
		echo "&nbsp;<input type=button value='Login' onclick='pr(\"&login=1\",null)'>";

 echo "</td></tr></table></td></tr>";
}
#
function doOne($rig, $preprocess)
{
 global $haderror, $readonly, $notify, $rigs;
 global $placebuttons;

 if ($placebuttons == 'top' || $placebuttons == 'both')
	pagebuttons($rig, null);

 if ($preprocess != null)
	process(array($preprocess => $preprocess), $rig);

 $cmds = array(	'devs'    => 'device list',
		'summary' => 'summary information',
		'pools'   => 'pool list');

 if ($notify)
	$cmds['notify'] = 'device status';

 $cmds['config'] = 'cgminer config';

 process($cmds, $rig);

 if ($haderror == false && $readonly === false)
	processgpus($rig);

 if ($placebuttons == 'bot' || $placebuttons == 'both')
	pagebuttons($rig, null);
}
#
global $sectionmap;
# map sections to their api command
# DEVS is a special case that will match GPU, PGA or ASC
# so you can have a single table with both in it
# DATE and $ect are hard coded so not in here
$sectionmap = array(
	'RIGS' => 'version',
	'SUMMARY' => 'summary',
	'POOL' => 'pools',
	'DEVS' => 'devs',
	'EDEVS' => 'edevs',
	'GPU' => 'devs',	// You would normally use DEVS
	'PGA' => 'devs',	// You would normally use DEVS
	'ASC' => 'devs',	// You would normally use DEVS
	'NOTIFY' => 'notify',
	'DEVDETAILS' => 'devdetails',
	'STATS' => 'stats',
	'ESTATS' => 'estats',
	'CONFIG' => 'config',
	'COIN' => 'coin',
	'USBSTATS' => 'usbstats',
	'MM' => 'mm');
#
function joinfields($section1, $section2, $join, $results)
{
 global $sectionmap;

 $name1 = $sectionmap[$section1];
 $name2 = $sectionmap[$section2];
 $newres = array();

 // foreach rig in section1
 foreach ($results[$name1] as $rig => $result)
 {
	$status = null;

	// foreach answer section in the rig api call
	foreach ($result as $name1b => $fields1b)
	{
		if ($name1b == 'STATUS')
		{
			// remember the STATUS from section1
			$status = $result[$name1b];
			continue;
		}

		// foreach answer section in the rig api call (for the other api command)
		foreach ($results[$name2][$rig] as $name2b => $fields2b)
		{
			if ($name2b == 'STATUS')
				continue;

			// If match the same field values of fields in $join
			$match = true;
			foreach ($join as $field)
				if ($fields1b[$field] != $fields2b[$field])
				{
					$match = false;
					break;
				}

			if ($match === true)
			{
				if ($status != null)
				{
					$newres[$rig]['STATUS'] = $status;
					$status = null;
				}

				$subsection = $section1.'+'.$section2;
				$subsection .= preg_replace('/[^0-9]/', '', $name1b.$name2b);

				foreach ($fields1b as $nam => $val)
					$newres[$rig][$subsection]["$section1.$nam"] = $val;
				foreach ($fields2b as $nam => $val)
					$newres[$rig][$subsection]["$section2.$nam"] = $val;
			}
		}
	}
 }
 return $newres;
}
#
function joinlr($section1, $section2, $join, $results)
{
 global $sectionmap;

 $name1 = $sectionmap[$section1];
 $name2 = $sectionmap[$section2];
 $newres = array();

 // foreach rig in section1
 foreach ($results[$name1] as $rig => $result)
 {
	$status = null;

	// foreach answer section in the rig api call
	foreach ($result as $name1b => $fields1b)
	{
		if ($name1b == 'STATUS')
		{
			// remember the STATUS from section1
			$status = $result[$name1b];
			continue;
		}

		// Build L string to be matched
		// : means a string constant otherwise it's a field name
		$Lval = '';
		foreach ($join['L'] as $field)
		{
			if (substr($field, 0, 1) == ':')
				$Lval .= substr($field, 1);
			else
				$Lval .= $fields1b[$field];
		}

		// foreach answer section in the rig api call (for the other api command)
		foreach ($results[$name2][$rig] as $name2b => $fields2b)
		{
			if ($name2b == 'STATUS')
				continue;

			// Build R string and compare
			// : means a string constant otherwise it's a field name
			$Rval = '';
			foreach ($join['R'] as $field)
			{
				if (substr($field, 0, 1) == ':')
					$Rval .= substr($field, 1);
				else
					$Rval .= $fields2b[$field];
			}

			if ($Lval === $Rval)
			{
				if ($status != null)
				{
					$newres[$rig]['STATUS'] = $status;
					$status = null;
				}

				$subsection = $section1.'+'.$section2;
				$subsection .= preg_replace('/[^0-9]/', '', $name1b.$name2b);

				foreach ($fields1b as $nam => $val)
					$newres[$rig][$subsection]["$section1.$nam"] = $val;
				foreach ($fields2b as $nam => $val)
					$newres[$rig][$subsection]["$section2.$nam"] = $val;
			}
		}
	}
 }
 return $newres;
}
#
function joinall($section1, $section2, $results)
{
 global $sectionmap;

 $name1 = $sectionmap[$section1];
 $name2 = $sectionmap[$section2];
 $newres = array();

 // foreach rig in section1
 foreach ($results[$name1] as $rig => $result)
 {
	// foreach answer section in the rig api call
	foreach ($result as $name1b => $fields1b)
	{
		if ($name1b == 'STATUS')
		{
			// copy the STATUS from section1
			$newres[$rig][$name1b] = $result[$name1b];
			continue;
		}

		// foreach answer section in the rig api call (for the other api command)
		foreach ($results[$name2][$rig] as $name2b => $fields2b)
		{
			if ($name2b == 'STATUS')
				continue;

			$subsection = $section1.'+'.$section2;
			$subsection .= preg_replace('/[^0-9]/', '', $name1b.$name2b);

			foreach ($fields1b as $nam => $val)
				$newres[$rig][$subsection]["$section1.$nam"] = $val;
			foreach ($fields2b as $nam => $val)
				$newres[$rig][$subsection]["$section2.$nam"] = $val;
		}
	}
 }
 return $newres;
}
#
function joinsections($sections, $results, $errors)
{
 global $sectionmap, $ect;

 // GPU's don't have Name,ID fields - so create them
 foreach ($results as $section => $res)
	foreach ($res as $rig => $result)
		foreach ($result as $name => $fields)
		{
			$subname = preg_replace('/\d/', '', $name);
			if ($subname == 'GPU' and isset($result[$name]['GPU']))
			{
				$results[$section][$rig][$name]['Name'] = 'GPU';
				$results[$section][$rig][$name]['ID'] = $result[$name]['GPU'];
			}
		}

 foreach ($sections as $section => $fields)
 {
	$sec = preg_replace('/\d/', '', $section);
	if ($sec != 'DATE' && $sec != $ect && !isset($sectionmap[$sec]))
	{
		$both = explode('+', $sec, 2);
		if (count($both) > 1)
		{
			switch($both[0])
			{
			case 'SUMMARY':
				switch($both[1])
				{
				case 'POOL':
				case 'DEVS':
				case 'EDEVS':
				case 'CONFIG':
				case 'COIN':
					$sectionmap[$sec] = $sec;
					$results[$sec] = joinall($both[0], $both[1], $results);
					break;
				default:
					$errors[] = "Error: Invalid section '$section/$sec'";
					break;
				}
				break;
			case 'DEVS':
			case 'EDEVS':
				switch($both[1])
				{
				case 'NOTIFY':
				case 'DEVDETAILS':
				case 'USBSTATS':
					$join = array('Name', 'ID');
					$sectionmap[$sec] = $sec;
					$results[$sec] = joinfields($both[0], $both[1], $join, $results);
					break;
				case 'STATS':
				case 'ESTATS':
					$join = array('L' => array('Name','ID'), 'R' => array('ID'));
					$sectionmap[$sec] = $sec;
					$results[$sec] = joinlr($both[0], $both[1], $join, $results);
					break;
				default:
					$errors[] = "Error: Invalid section '$section/$sec'";
					break;
				}
				break;
			case 'POOL':
				switch($both[1])
				{
				case 'STATS':
					$join = array('L' => array(':POOL','POOL'), 'R' => array('ID'));
					$sectionmap[$sec] = $sec;
					$results[$sec] = joinlr($both[0], $both[1], $join, $results);
					break;
				default:
					$errors[] = "Error: Invalid section '$section/$sec'";
					break;
				}
				break;
			default:
				$errors[] = "Error: Invalid section '$section/$sec'";
				break;
			}
		}
		else
			$errors[] = "Error: Invalid section '$section/$sec'";
	}
 }
 return array($results, $errors);
}
#
function secmatch($section, $field)
{
 $sec = preg_replace('/\d/', '', $section);
 $fld = preg_replace('/\d/', '', $field);

 if ($sec == $fld)
	return true;

 # API estats returns data as STATS
 if ($sec == 'ESTATS' && $fld == 'STATS')
	return true;

 # API devs and edevs return the device type
 if (($sec == 'DEVS' || $sec == 'EDEVS')
 &&  ($fld == 'GPU' || $fld == 'PGA' || $fld == 'ASC'))
	return true;

 return false;
}
#
function customset($showfields, $sum, $section, $rig, $isbutton, $result, $total, $cf = NULL)
{
 global $rigbuttons;

 $rn = 0;
 foreach ($result as $sec => $row)
 {
	$secname = preg_replace('/\d/', '', $sec);

	if ($sec != 'total')
		if (!secmatch($section, $secname))
			continue;

	newrow();

	$when = 0;
	if (isset($result['STATUS']['When']))
		$when = $result['STATUS']['When'];


	if ($isbutton)
		echo rigbutton($rig, $rig, $when, $row, $rigbuttons);
	else
	{
		list($ignore, $class) = fmt('total', '', '', $when, $row, $cf);
		echo "<td align=middle$class>$rig</td>";
	}

	foreach ($showfields as $name => $one)
	{
		if ($name === '#' and $sec != 'total')
		{
			$rn++;
			$value = $rn;
			if (isset($total[$name]))
				$total[$name]++;
			else
				$total[$name] = 1;
		}
		elseif (isset($row[$name]))
		{
			$value = $row[$name];

			if (isset($sum[$section][$name]))
			{
				if (isset($total[$name]))
					$total[$name] += $value;
				else
					$total[$name] = $value;
			}
		}
		else
		{
			if ($sec == 'total' && isset($total[$name]))
				$value = $total[$name];
			else
				$value = null;
		}

		if (strpos($secname, '+') === false)
			list($showvalue, $class) = fmt($secname, $name, $value, $when, $row, $cf);
		else
		{
			if ($name != '#')
				$parts = explode('.', $name, 2);
			else
				$parts[0] = $parts[1] = '#';
			list($showvalue, $class) = fmt($parts[0], $parts[1], $value, $when, $row, $cf);
		}

		echo "<td$class align=right>$showvalue</td>";
	}
	endrow();
 }
 return $total;
}
#
function docalc($func, $data)
{
 switch ($func)
 {
 case 'sum':
	$tot = 0;
	foreach ($data as $val)
		$tot += $val;
	return $tot;
 case 'avg':
	$tot = 0;
	foreach ($data as $val)
		$tot += $val;
	return ($tot / count($data));
 case 'min':
	$ans = null;
	foreach ($data as $val)
		if ($ans === null)
			$ans = $val;
		else
			if ($val < $ans)
				$ans = $val;
	return $ans;
 case 'max':
	$ans = null;
	foreach ($data as $val)
		if ($ans === null)
			$ans = $val;
		else
			if ($val > $ans)
				$ans = $val;
	return $ans;
 case 'lo':
	$ans = null;
	foreach ($data as $val)
		if ($ans === null)
			$ans = $val;
		else
			if (strcasecmp($val, $ans) < 0)
				$ans = $val;
	return $ans;
 case 'hi':
	$ans = null;
	foreach ($data as $val)
		if ($ans === null)
			$ans = $val;
		else
			if (strcasecmp($val, $ans) > 0)
				$ans = $val;
	return $ans;
 case 'count':
	return count($data);
 case 'any':
 default:
	return $data[0];
 }
}
#
function docompare($row, $test)
{
 // invalid $test data means true
 if (count($test) < 2)
	return true;

 if (isset($row[$test[0]]))
	$val = $row[$test[0]];
 else
	$val = null;

 if ($test[1] == 'set')
	return ($val !== null);

 if ($val === null || count($test) < 3)
	return true;

 switch($test[1])
 {
 case '=':
	return ($val == $test[2]);
 case '<':
	return ($val < $test[2]);
 case '<=':
	return ($val <= $test[2]);
 case '>':
	return ($val > $test[2]);
 case '>=':
	return ($val >= $test[2]);
 case 'eq':
	return (strcasecmp($val, $test[2]) == 0);
 case 'lt':
	return (strcasecmp($val, $test[2]) < 0);
 case 'le':
	return (strcasecmp($val, $test[2]) <= 0);
 case 'gt':
	return (strcasecmp($val, $test[2]) > 0);
 case 'ge':
	return (strcasecmp($val, $test[2]) >= 0);
 case 'sub':
	return (strcasecmp(substr($val, 0, strlen($test[2])), $test[2]) == 0);
 case '!sub':
	return (strcasecmp(substr($val, 0, strlen($test[2])), $test[2]) != 0);
 default:
	return true;
 }
}
#
function processcompare($which, $ext, $section, $res)
{
 if (isset($ext[$section][$which]))
 {
	$proc = $ext[$section][$which];
	if ($proc !== null)
	{
		$res2 = array();
		foreach ($res as $rig => $result)
			foreach ($result as $sec => $row)
			{
				$secname = preg_replace('/\d/', '', $sec);
				if (!secmatch($section, $secname))
					$res2[$rig][$sec] = $row;
				else
				{
					$keep = true;
					foreach ($proc as $test)
						if (!docompare($row, $test))
						{
							$keep = false;
							break;
						}
					if ($keep)
						$res2[$rig][$sec] = $row;
				}
			}

		$res = $res2;
	}
 }
 return $res;
}
#
function ss($a, $b)
{
 $la = strlen($a);
 $lb = strlen($b);
 if ($la != $lb)
	return $lb - $la;
 return strcmp($a, $b);
}
#
# If you are developing a customsummarypage that uses BGEN or GEN,
# you may want to remove the '@' in front of '@eval()' to help with debugging
# The '@' removes php comments from the web log about missing fields
# Since there are many forks of cgminer that break the API or do not
# keep their fork up to date with current cgminer, the addition of
# '@' solves the problem of generating unnecessary and excessive web logs
# about the eval()
function genfld($row, $calc)
{
 uksort($row, "ss");

 foreach ($row as $name => $value)
	if (strstr($calc, $name) !== FALSE)
		$calc = str_replace($name, $value, $calc);

 @eval("\$val = $calc;");

 if (!isset($val))
	return '';
 else
	return $val;
}
#
function dogen($ext, $wg, $gname, $section, &$res, &$fields)
{
 $gen = $ext[$section][$wg];

 foreach ($gen as $fld => $calc)
	$fields[] = "$gname.$fld";

 foreach ($res as $rig => $result)
	foreach ($result as $sec => $row)
	{
		$secname = preg_replace('/\d/', '', $sec);
		if (secmatch($section, $secname))
			foreach ($gen as $fld => $calc)
			{
				$name = "$gname.$fld";

				$val = genfld($row, $calc);

				$res[$rig][$sec][$name] = $val;
			}
	}
}
# $section is the full name including digits
function processext($ext, $section, $res, &$fields)
{
 global $allowgen;

 $res = processcompare('where', $ext, $section, $res);

 // Generated fields (functions of other fields before grouping)
 if ($allowgen === true && isset($ext[$section]['bgen']))
	dogen($ext, 'bgen', 'BGEN', $section, $res, $fields);

 if (isset($ext[$section]['group']))
 {
	$grp = $ext[$section]['group'];
	$calc = $ext[$section]['calc'];
	if ($grp !== null)
	{
		$interim = array();
		$res2 = array();
		$cou = 0;
		foreach ($res as $rig => $result)
			foreach ($result as $sec => $row)
			{
				$secname = preg_replace('/\d/', '', $sec);
				if (!secmatch($section, $secname))
				{
					// STATUS may be problematic ...
					if (!isset($res2[$sec]))
						$res2[$sec] = $row;
				}
				else
				{
					$grpkey = '';
					$newrow = array();
					foreach ($grp as $field)
					{
						if (isset($row[$field]))
						{
							$grpkey .= $row[$field].'.';
							$newrow[$field] = $row[$field];
						}
						else
							$grpkey .= '.';
					}

					if (!isset($interim[$grpkey]))
					{
						$interim[$grpkey]['grp'] = $newrow;
						$interim[$grpkey]['sec'] = $secname.$cou;
						$cou++;
					}

					if ($calc !== null)
						foreach ($calc as $field => $func)
						{
							if (isset($row[$field]))
							{
								if (!isset($interim[$grpkey]['cal'][$field]))
									$interim[$grpkey]['cal'][$field] = array();
								$interim[$grpkey]['cal'][$field][] = $row[$field];
							}
						}
				}
			}

		// Build the rest of $res2 from $interim
		foreach ($interim as $rowkey => $row)
		{
			$key = $row['sec'];
			foreach ($row['grp'] as $field => $value)
				$res2[$key][$field] = $value;
			foreach ($row['cal'] as $field => $data)
				$res2[$key][$field] = docalc($calc[$field], $data);
		}

		$res = array('' => $res2);
	}
 }

 // Generated fields (functions of other fields after grouping)
 if ($allowgen === true && isset($ext[$section]['gen']))
	dogen($ext, 'gen', 'GEN', $section, $res, $fields);

 return processcompare('having', $ext, $section, $res);
}
# Avalon Error values in ECMM and ECHU
function showect()
{
 $ecv = array(
	'x00001' => 'Idle W',
	'x00002' => 'MMCRCFailed W',
	'x00004' => 'NoFan F',
	'x00008' => 'Lock W',
	'x00010' => 'APIfifoOverflow W',
	'x00020' => 'RBOverflow W',
	'x00040' => 'TooHot F',
	'x00080' => 'HotBefore W',
	'x00100' => 'LoopFailed F',
	'x00200' => 'CoreTestFailed W',
	'x00400' => 'InvalidPMU F',
	'x00800' => 'PGFailed F',
	'x01000' => 'NTCErr F',
	'x02000' => 'VolErr F',
	'x04000' => 'VCoreERR F',
	'x08000' => 'PMUCRCFailed W',
	'x10000' => 'InvalidPLLValue W',
	'x20000' => 'HUFailed W');

 otherrow("<td class=h colspan=9>ECHU and ECMM error codes (mouse over)</td>");
 $count = 0;
 foreach ($ecv as $val => $name)
 {
	if (($count % 9) == 0)
	{
		if ($count > 0)
		{
			endrow();
		}
		newrow();
	}
	switch (substr($name, -2))
	{
	 case ' W':
		 $name .= 'arning';
		 break;
	 case ' F':
		 $name .= 'atal';
		 break;
	}
	echo "<td><span title='$name'>&nbsp;$val&nbsp;</span></td>";

	$count++;
 }
 endrow();
}
#
function processcustompage($pagename, $sections, $sum, $ext, $namemap)
{
 global $sectionmap, $ect;
 global $miner, $port;
 global $rigs, $error;
 global $warnfont, $warnoff;
 global $dfmt;
 global $readonly, $showndate;

 $cmds = array();
 $errors = array();
 foreach ($sections as $section => $fields)
 {
	$all = explode('+', $section);
	foreach ($all as $section)
	{
		$sec = preg_replace('/\d/', '', $section);
		if (isset($sectionmap[$sec]))
		{
			$cmd = $sectionmap[$sec];
			if (!isset($cmds[$cmd]))
				$cmds[$cmd] = 1;
		}
		else
			if ($sec != 'DATE' && $sec != $ect)
				$errors[] = "Error: unknown section '$section/$sec' in custom summary page '$pagename'";
	}
 }

 $results = array();
 foreach ($rigs as $num => $rig)
 {
	$parts = explode(':', $rig, 3);
	if (count($parts) >= 1)
	{
		$miner = $parts[0];
		if (count($parts) >= 2)
			$port = $parts[1];
		else
			$port = '';

		if (count($parts) > 2)
			$name = $parts[2];
		else
			$name = $rig;

		foreach ($cmds as $cmd => $one)
		{
			$process = api($name, $cmd);

			if ($error != null)
			{
				$errors[] = "Error getting $cmd for $name $warnfont$error$warnoff";
				break;
			}
			else
				$results[$cmd][$num] = $process;
		}
	}
	else
		otherrow('<td class=bad>Bad "$rigs" array</td>');
 }

 // Show API errors at the top
 if (count($errors) > 0)
 {
	foreach ($errors as $err)
		otherrow("<td colspan=100>$err</td>");
	$errors = array();
 }

 $shownsomething = false;
 if (count($results) > 0)
 {
	list($results, $errors) = joinsections($sections, $results, $errors);
	$first = true;
	foreach ($sections as $section => $fields)
	{
		$sec = preg_replace('/\d/', '', $section);
		if ($sec === 'DATE')
		{
			if ($shownsomething)
				otherrow('<td>&nbsp;</td>');

			newtable();
			showdatetime();
			endtable();
			// On top of the next table
			$shownsomething = false;
			continue;
		}

		if ($sec === $ect)
		{
			newtable();
			showect();
			endtable();
			// On top of the next table
			$shownsomething = false;
			continue;
		}

		if ($sec === 'RIGS')
		{
			if ($shownsomething)
				otherrow('<td>&nbsp;</td>');

			newtable();
			showrigs($results['version'], 'Rig', '');
			endtable();
			$shownsomething = true;
			continue;
		}

		if (isset($results[$sectionmap[$sec]]))
		{
			// use the full section name
			if (isset($ext[$section]['fmt']))
				$cf = $ext[$section]['fmt'];
			else
				$cf = NULL;

			$rigresults = processext($ext, $section, $results[$sectionmap[$sec]], $fields);

			$showfields = array();
			$showhead = array();
			foreach ($fields as $field)
				foreach ($rigresults as $result)
					foreach ($result as $secn => $row)
					{
						$secname = preg_replace('/\d/', '', $secn);
						if (secmatch($section, $secname))
						{
							if ($field === '*')
							{
								foreach ($row as $f => $v)
								{
									$showfields[$f] = 1;
									$map = $section.'.'.$f;
									if (isset($namemap[$map]))
										$showhead[$namemap[$map]] = 1;
									else
										$showhead[$f] = 1;
								}
							}
							elseif ($field === '#')
							{
								$showfields[$field] = 1;
								$showhead[$field] = 1;
							}
							elseif (isset($row[$field]))
							{
								$showfields[$field] = 1;
								$map = $section.'.'.$field;
								if (isset($namemap[$map]))
									$showhead[$namemap[$map]] = 1;
								else
									$showhead[$field] = 1;
							}
						}
					}

			if (count($showfields) > 0)
			{
				if ($shownsomething)
					otherrow('<td>&nbsp;</td>');

				newtable();
				if (count($rigresults) == 1 && isset($rigresults['']))
					$ri = array('' => 1) + $showhead;
				else
					$ri = array('Rig' => 1) + $showhead;
				showhead('', $ri, true);

				$total = array();
				$add = array('total' => array());

				foreach ($rigresults as $num => $result)
					$total = customset($showfields, $sum, $section, $num, true, $result, $total, $cf);

				if (count($total) > 0)
					customset($showfields, $sum, $section, '&Sigma;', false, $add, $total, $cf);

				$first = false;

				endtable();
				$shownsomething = true;
			}
		}
	}
 }

 if (count($errors) > 0)
 {
	if (count($results) > 0)
		otherrow('<td>&nbsp;</td>');

	foreach ($errors as $err)
		otherrow("<td colspan=100>$err</td>");
 }
}
#
function showcustompage($pagename, $systempage = false)
{
 global $customsummarypages;
 global $placebuttons;

 if ($placebuttons == 'top' || $placebuttons == 'both')
	pagebuttons(null, $pagename);

 if ($systempage === false && !isset($customsummarypages[$pagename]))
 {
	otherrow("<td colspan=100 class=bad>Unknown custom summary page '$pagename'</td>");
	return;
 }

 $csp = getcsp($pagename, $systempage);
 if ($csp === false)
 {
	otherrow("<td colspan=100 class=bad>Invalid custom summary page '$pagename'</td>");
	return;
 }

 degen($csp);

 $page = $csp[0];
 $namemap = array();
 foreach ($page as $name => $fields)
 {
	if ($fields === null)
		$page[$name] = array();
	else
		foreach ($fields as $num => $field)
		{
			$pos = strpos($field, '=');
			if ($pos !== false)
			{
				$names = explode('=', $field, 2);
				if (strlen($names[1]) > 0)
					$namemap[$name.'.'.$names[0]] = $names[1];
				$page[$name][$num] = $names[0];
			}
		}
 }

 $ext = null;
 if (isset($csp[2]))
	$ext = $csp[2];

 $sum = $csp[1];
 if ($sum === null)
	$sum = array();

 // convert them to searchable via isset()
 foreach ($sum as $section => $fields)
 {
	$newfields = array();
	foreach ($fields as $field)
		$newfields[$field] = 1;
	$sum[$section] = $newfields;
 }

 if (count($page) <= 1)
 {
	otherrow("<td colspan=100 class=bad>Invalid custom summary page '$pagename' no content </td>");
	return;
 }

 processcustompage($pagename, $page, $sum, $ext, $namemap);

 if ($placebuttons == 'bot' || $placebuttons == 'both')
	pagebuttons(null, $pagename);
}
#
function onlylogin()
{
 global $here;

 htmlhead('', false, null, null, true);

?>
<tr height=15%><td>&nbsp;</td></tr>
<tr><td>
 <center>
  <table width=384 height=368 cellpadding=0 cellspacing=0 border=0>
   <tr><td>
    <table width=100% height=100% border=0 align=center cellpadding=5 cellspacing=0>
     <tr><td><form action='<?php echo $here; ?>' method=post>
      <table width=200 border=0 align=center cellpadding=5 cellspacing=0>
       <tr><td height=120 colspan=2>&nbsp;</td></tr>
       <tr><td colspan=2 align=center valign=middle>
        <h2>LOGIN</h2></td></tr>
       <tr><td align=center valign=middle><div align=right>Username:</div></td>
        <td height=33 align=center valign=middle>
        <input type=text name=rut size=18></td></tr>
       <tr><td align=center valign=middle><div align=right>Password:</div></td>
        <td height=33 align=center valign=middle>
        <input type=password name=roh size=18></td></tr>
       <tr valign=top><td></td><td><input type=submit value=Login>
        </td></tr>
      </table></form></td></tr>
    </table></td></tr>
  </table></center>
</td></tr>
<?php
}
#
function checklogin()
{
 global $allowcustompages;
 global $readonly, $userlist, $ses;

 $out = trim(getparam('logout', true));
 if ($out !== null && $out !== '' && isset($_SESSION[$ses]))
	unset($_SESSION[$ses]);

 $login = trim(getparam('login', true));
 if ($login !== null && $login !== '')
 {
	if (isset($_SESSION[$ses]))
		unset($_SESSION[$ses]);

	onlylogin();
	return 'login';
 }

 if ($userlist === null)
	return false;

 $rut = trim(getparam('rut', true));
 $roh = trim(getparam('roh', true));

 if (($rut !== null && $rut !== '') || ($roh !== null && $roh !== ''))
 {
	if (isset($_SESSION[$ses]))
		unset($_SESSION[$ses]);

	if ($rut !== null && $rut !== '' && $roh !== null && $roh !== '')
	{
		if (isset($userlist['sys']) && isset($userlist['sys'][$rut])
		&&  ($userlist['sys'][$rut] === $roh))
		{
			$_SESSION[$ses] = true;
			return false;
		}

		if (isset($userlist['usr']) && isset($userlist['usr'][$rut])
		&&  ($userlist['usr'][$rut] === $roh))
		{
			$_SESSION[$ses] = false;
			$readonly = true;
			return false;
		}
	}
 }

 if (isset($_SESSION[$ses]))
 {
	if ($_SESSION[$ses] == false)
		$readonly = true;
	return false;
 }

 if (isset($userlist['def']) && $allowcustompages === true)
 {
	// Ensure at least one exists
	foreach ($userlist['def'] as $pg)
		if (getcsp($pg) !== false)
			return true;
 }

 onlylogin();
 return 'login';
}
#
function display()
{
 global $miner, $port;
 global $mcast, $mcastexpect;
 global $readonly, $notify, $rigs;
 global $ignorerefresh, $autorefresh;
 global $allowcustompages;
 global $placebuttons;
 global $userlist, $ses;

 $pagesonly = checklogin();

 if ($pagesonly === 'login')
	return;

 $mcerr = '';

 if ($rigs == null or count($rigs) == 0)
 {
	if ($mcast === true)
		$action = 'found';
	else
		$action = 'defined';

	minhead();
	otherrow("<td class=bad>No rigs $action</td>");
	return;
 }
 else
 {
	if ($mcast === true && count($rigs) < $mcastexpect)
		$mcerr = othrow('<td class=bad>Found '.count($rigs)." rigs but expected at least $mcastexpect</td>");
 }

 if ($ignorerefresh == false)
 {
	$ref = trim(getparam('ref', true));
	if ($ref != null && $ref != '')
		$autorefresh = intval($ref);
 }

 if ($pagesonly !== true)
 {
	$rig = trim(getparam('rig', true));

	$arg = trim(getparam('arg', true));
	$preprocess = null;
	if ($arg != null and $arg != '')
	{
		if ($rig != null and $rig != '' and $rig >= 0 and $rig < count($rigs))
		{
			$parts = explode(':', $rigs[$rig], 3);
			if (count($parts) >= 1)
			{
				$miner = $parts[0];
				if (count($parts) >= 2)
					$port = $parts[1];
				else
					$port = '';

				if ($readonly !== true)
					$preprocess = $arg;
			}
		}
	}
 }

 if ($allowcustompages === true)
 {
	$pg = urlencode(trim(getparam('pg', true)));
	if ($pagesonly === true)
	{
		if ($pg !== null && $pg !== '')
		{
			if ($userlist !== null && isset($userlist['def'])
			&&  !in_array($pg, $userlist['def']))
				$pg = null;
		}
		else
		{
			if ($userlist !== null && isset($userlist['def']))
				foreach ($userlist['def'] as $pglook)
					if (getcsp($pglook) !== false)
					{
						$pg = $pglook;
						break;
					}
		}
	}

	if ($pg !== null && $pg !== '')
	{
		htmlhead($mcerr, false, null, $pg);
		showcustompage($pg);
		return;
	}
 }

 if ($pagesonly === true)
 {
	onlylogin();
	return;
 }

 if (count($rigs) == 1)
 {
	$parts = explode(':', $rigs[0], 3);
	if (count($parts) >= 1)
	{
		$miner = $parts[0];
		if (count($parts) >= 2)
			$port = $parts[1];
		else
			$port = '';

		htmlhead($mcerr, true, 0);
		doOne(0, $preprocess);
	}
	else
	{
		minhead($mcerr);
		otherrow('<td class=bad>Invalid "$rigs" array</td>');
	}

	return;
 }

 if ($rig != null and $rig != '' and $rig >= 0 and $rig < count($rigs))
 {
	$parts = explode(':', $rigs[$rig], 3);
	if (count($parts) >= 1)
	{
		$miner = $parts[0];
		if (count($parts) >= 2)
			$port = $parts[1];
		else
			$port = '';

		htmlhead($mcerr, true, 0);
		doOne($rig, $preprocess);
	}
	else
	{
		minhead($mcerr);
		otherrow('<td class=bad>Invalid "$rigs" array</td>');
	}

	return;
 }

 htmlhead($mcerr, false, null);

 if ($preprocess != null)
	process(array($preprocess => $preprocess), $rig);

 if (getcsp('Summary', true) !== false)
	showcustompage('Summary', true);
}
#
if ($mcast === true)
 getrigs();
display();
#
?>
</table></td></tr></table>
</body></html>
