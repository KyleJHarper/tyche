#!/bin/bash

trap 'exit' SIGINT SIGTERM

src='/tmp/ram_drive/source'
comp='/tmp/ram_drive/compressed'
decomp='/tmp/ram_drive/decompressed'
start=''
log='/home/kyle/Desktop/in_case.log'


declare -A compressors=([gzip]=9 [bzip2]=9 [xz]=9 [lzop]=9 [lz4]=9 [zstd]=19)

# $1 should be a compressor above.
[ -z "${1}" ] && echo "You messed up.  Send the right args." && exit 1

# -- Run compression/decompression.
echo "Starting tests for ${1} @ $(date +'%F %R')" | tee -a "${log}"
for level in $(seq 1 1 ${compressors[$1]}) ; do
  # Reset vars and run tests.
  ct=''
  dt=''
  for t in 0 1 2 3 ; do
    [ -f ${comp} ]     && rm ${comp}
    [ -f  ${decomp} ]  && rm ${decomp}
    # Skip the first round (round 0) to warm the cache.
    case $1 in
      'bzip2') start=$(date +'%s%N')
               bzip2 --stdout -${level} ${src} >${comp}
               [ ${t} -gt 0 ] && ct+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               start=$(date +'%s%N')
               bunzip2 --stdout ${comp} >${decomp}
               [ ${t} -gt 0 ] && dt+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               ;;
      'gzip')  start=$(date +'%s%N')
               gzip --stdout -${level} ${src} >${comp}
               [ ${t} -gt 0 ] && ct+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               start=$(date +'%s%N')
               gunzip --stdout ${comp} >${decomp}
               [ ${t} -gt 0 ] && dt+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               ;;
      'lz4')   start=$(date +'%s%N')
               lz4 -${level} -q ${src} ${comp}
               [ ${t} -gt 0 ] && ct+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               start=$(date +'%s%N')
               lz4 -d ${comp} -q ${decomp}
               [ ${t} -gt 0 ] && dt+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               ;;
      'lzop')  start=$(date +'%s%N')
               lzop -${level} ${src} -o ${comp}
               [ ${t} -gt 0 ] && ct+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               start=$(date +'%s%N')
               lzop -d ${comp} -o ${decomp}
               [ ${t} -gt 0 ] && dt+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               ;;
      'xz')    start=$(date +'%s%N')
               xz --stdout -${level} ${src} >${comp}
               [ ${t} -gt 0 ] && ct+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               start=$(date +'%s%N')
               unxz --stdout ${comp} >${decomp}
               [ ${t} -gt 0 ] && dt+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               ;;
      'zstd')  start=$(date +'%s%N')
               zstd -${level} -q ${src} -o ${comp}
               [ ${t} -gt 0 ] && ct+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               start=$(date +'%s%N')
               zstd -d ${comp} -q -o ${decomp}
               [ ${t} -gt 0 ] && dt+=" $(( ($(date +'%s%N') - ${start}) / 1000000 ))"
               ;;
    esac
  done
  comp_size=$(bc <<<"scale=1; $(du -B KB ${comp} | grep -oP '^[0-9]+') / 1000 * 1.0" )
  echo "${comp_size}  ${ct} ${dt}" | tee -a "${log}"
done
