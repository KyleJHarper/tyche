#!/bin/bash
set -e

function fail {
  echo "$*"
  exit 1
}



[ -d '/tmp/ram_drive' ] && fail "The /tmp/ram_drive folder already exists."
mkdir '/tmp/ram_drive'

sudo mount -t tmpfs -o size=1g tmpfs /tmp/ram_drive

tar -xzf /mnt/pages/8k/names__tables.tar.gz  -C/tmp/ram_drive
tar -xzf /mnt/pages/8k/names__indexes.tar.gz -C/tmp/ram_drive

df -h
echo $SECONDS

exit 0
