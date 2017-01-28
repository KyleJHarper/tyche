#!/bin/bash

[ -f /tmp/wasteland ] && echo 'nope' && exit 1

mkdir /tmp/wasteland
sudo mount -t tmpfs -o size=10g tmpfs /tmp/wasteland

cp /mnt/pages/8k/mgd__tables.tar.gz /tmp/wasteland
i=1
while cp /tmp/wasteland/mgd__tables.tar.gz /tmp/wasteland/${i} ; do let 'i++' ; done

exit

