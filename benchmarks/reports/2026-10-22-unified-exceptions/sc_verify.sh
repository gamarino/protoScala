#!/bin/bash
# protoScala self-report check: every comparable and probe once per variant,
# interpreted and transpiled, against its // EXPECT: line; the probes also
# compiled with scalac 3.9.0 and run on the JVM (classes kept for sc_run.sh).
set -u
WS=/home/gamarino/Documentos/proyectos
M=$WS/.agent_scratch/meas_1022; OUT=$M/sc_out; mkdir -p $OUT/libs $OUT/verify_so $OUT/jvm_probes/.home
export SCALA_HOME=$WS/tools/scala3-dl/scala3-3.9.0-x86_64-pc-linux
T() { echo $M/$1/protoScala/build_release; }
for v in before after; do
  d=$OUT/libs/verify_$v; mkdir -p $d; cp -L $M/$v/protoCore/build_release/libprotoCore.so.4 $d/
  echo "== verify_$v" >> $OUT/ldd.txt; LD_LIBRARY_PATH=$d ldd $(T $v)/protoscala | grep -i proto >> $OUT/ldd.txt
done
for f in $M/after/protoScala/benchmarks/comparable/*.scala $M/sc_probes/*.scala; do
  n=$(basename $f .scala); exp=$(head -n 1 $f | sed -n 's#^// *EXPECT: *##p')
  env=""; case $n in object_tree|map_build|list_ops) env="PROTOCORE_HEAP_LIMIT_CELLS=2000000";; esac
  for v in before after; do
    L=$OUT/libs/verify_$v
    o=$(env $env LD_LIBRARY_PATH=$L timeout 600 $(T $v)/protoscala $f 2>&1 | grep -v '^\s*$' | tail -n 1)
    [ "$o" = "$exp" ] && s=OK || s=BAD; echo "$s interp $v $n last=[$o] expect=[$exp]" >> $OUT/verify.txt
    wd=$OUT/verify_so/$v/$n; rm -rf $wd; mkdir -p $wd
    env LD_LIBRARY_PATH=$L $(T $v)/protoscalac $f -o $wd --build-so > $wd.build_log.txt 2>&1
    if [ -f $wd/module.so ]; then
      o=$(env $env LD_LIBRARY_PATH=$L timeout 600 $(T $v)/protoscala --run-module $wd/module.so 2>&1 | grep -v '^\s*$' | tail -n 1)
      [ "$o" = "$exp" ] && s=OK || s=BAD; echo "$s trans  $v $n last=[$o] expect=[$exp]" >> $OUT/verify.txt
      echo "   module.so ldd: $(LD_LIBRARY_PATH=$L ldd $wd/module.so | grep -i proto | tr -s ' \t' ' ' | paste -sd';')" >> $OUT/verify.txt
    else
      echo "NOSO trans $v $n $(grep -m1 -i error $wd.build_log.txt)" >> $OUT/verify.txt
    fi
  done
done
for f in $M/sc_probes/*.scala; do
  n=$(basename $f .scala); exp=$(head -n 1 $f | sed -n 's#^// *EXPECT: *##p'); main=$(grep -o '@main def [A-Za-z0-9]*' $f | awk '{print $3}')
  mkdir -p $OUT/jvm_probes/$n
  HOME=$OUT/jvm_probes/.home $SCALA_HOME/bin/scalac -d $OUT/jvm_probes/$n $f > $OUT/jvm_probes/$n.scalac_log.txt 2>&1
  o=$(java -XX:-UsePerfData -cp "$SCALA_HOME/lib/*:$OUT/jvm_probes/$n" $main 2>&1 | tail -n 1)
  [ "$o" = "$exp" ] && s=OK || s=BAD; echo "$s jvm    -      $n last=[$o] expect=[$exp]" >> $OUT/verify.txt
done
echo "BAD/NOSO count: $(grep -cE '^(BAD|NOSO)' $OUT/verify.txt)" >> $OUT/verify.txt
