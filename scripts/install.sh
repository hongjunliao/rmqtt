#!/bin/sh
# author hongjun.iao <docici@126.com>

echo "installing rmqtt..."

#
rootdir=`pwd`;
ln -sf ${rootdir}/rmqtt /usr/local/bin/
ln -sf ${rootdir}/rmqtt.conf /etc/
ln -sf ${rootdir}/zlog.conf /etc/
ln -sf ${rootdir}/*.so.* /usr/loca/lib/
ldconfig
# systemctl
ln -sf ${rootdir}/rmqtt.service /usr/lib/systemd/system/
systemctl daemon-reload
systemctl status rmqtt.service

