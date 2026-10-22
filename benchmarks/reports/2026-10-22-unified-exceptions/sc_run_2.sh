#!/bin/bash
# protoScala BEFORE (3783caa8 on protoCore cf52f9f5) vs AFTER (1e1cf911 on
# protoCore 65760e4a), Release, clean worktrees: the interpreter harness
# (with Scala 3.9.0 on the JVM and CPython), the transpiled path, the
# protoscalac compile time and the exception probes. Each run behind the quiet
# gate (QUIET_CORES=4.0) and on a fresh copy of the variant's libprotoCore.
set -u
export QUIET_CORES=${QUIET_CORES:-4.0}
WS=/home/gamarino/Documentos/proyectos
M=$WS/.agent_scratch/meas_1022; OUT=$M/sc_out_2; mkdir -p $OUT/libs; H=$M/sc_harness_copies; JP=$M/sc_out/jvm_probes
Q="python3 $WS/protoCore/docs/reports/data/2026-10-12-mark-stack-int-compare/quiet.py"
CP=/usr/local/bin/python3.14
export SCALA_HOME=$WS/tools/scala3-dl/scala3-3.9.0-x86_64-pc-linux
T() { echo $M/$1/protoScala/build_release; }
lib() { echo $M/$1/protoCore/build_release/libprotoCore.so.4; }
fresh() { local d=$OUT/libs/$2; mkdir -p $d; cp -L $(lib $1) $d/libprotoCore.so.4; export LD_LIBRARY_PATH=$d
          echo "== $2" >> $OUT/ldd.txt; ldd $(T $1)/protoscala $(T $1)/protoscalac | grep -iE "proto|^/" >> $OUT/ldd.txt; }
# 1. self-report verification is in sc_verify.sh (run first, separately; its output is in sc_out/).
# The harnesses run from copies in sc_harness_copies/ whose output directories
# (build_jvm, the transpiled scratch) are new per run, so nothing is deleted.
# 2. interpreter harness (AFTER's benchmarks/run_benchmarks.py; benchmarks/ is
#    identical in both commits), three rounds, order alternated per round. The
#    harness's own load-average wait is disabled (--max-load 100): the quiet gate
#    gates instead.
for r in 1 2 3; do
  order="after before"; [ $r = 2 ] && order="before after"
  for v in $order; do
    $Q > $OUT/quiet_interp_${v}_r$r.txt 2>&1; fresh $v interp_${v}_r$r; echo "$(date -u +%FT%TZ) interp $v r$r"
    PROTOSCALA_BIN=$(T $v)/protoscala PROTOSCALA_RELEASE_BIN=/nonexistent CPYTHON_BIN=$CP \
      PROTOPY_BIN=/nonexistent PROTOST_BIN=/nonexistent PROTOCLJ_BIN=/nonexistent \
      UEX_JVM_DIR=$OUT/build_jvm_${v}_r$r python3 $H/run_benchmarks.py --max-load 100 \
        --output $OUT/interp_${v}_r$r.md > $OUT/interp_${v}_r$r.log 2>&1
    echo "rc=$?" >> $OUT/interp_${v}_r$r.log
  done
done
# 3. transpiled vs interpreted (each variant's own run_transpiled_benchmarks.py,
#    which uses its worktree's build_release), three rounds, order alternated
for r in 1 2 3; do
  order="after before"; [ $r = 2 ] && order="before after"
  for v in $order; do
    $Q > $OUT/quiet_trans_${v}_r$r.txt 2>&1; fresh $v trans_${v}_r$r; echo "$(date -u +%FT%TZ) trans $v r$r"
    UEX_TRANS_SCRATCH=$OUT/trans_scratch_${v}_r$r python3 $H/run_transpiled_benchmarks_$v.py > $OUT/trans_${v}_r$r.json 2> $OUT/trans_${v}_r$r.err
    echo "rc=$?" >> $OUT/trans_${v}_r$r.err
  done
done
# 4. compile time: protoscalac X.scala -o DIR --build-so, wall, 3 reps, interleaved
$Q > $OUT/quiet_compile.txt 2>&1
fresh before cmp_before; LB=$LD_LIBRARY_PATH; fresh after cmp_after; LA=$LD_LIBRARY_PATH; unset LD_LIBRARY_PATH
echo "$(date -u +%FT%TZ) compile"
C=$M/after/protoScala/benchmarks/comparable
SRCS="$C/fib.scala $C/tak.scala $C/object_tree.scala $C/list_ops.scala $C/case_copy.scala $M/sc_probes/throw_catch.scala $M/sc_probes/throw_across5.scala $M/sc_probes/zerodiv.scala"
for rep in 1 2 3; do
  for s in $SRCS; do
    for v in before after; do
      L=$LB; [ $v = after ] && L=$LA
      wd=$OUT/compile/rep$rep/$v/$(basename $s .scala); mkdir -p $wd
      t0=$(date +%s.%N); env LD_LIBRARY_PATH=$L $(T $v)/protoscalac $s -o $wd --build-so > $wd/../$(basename $s .scala).build_log.txt 2>&1; rc=$?; t1=$(date +%s.%N)
      echo "$(basename $s .scala) $v rep$rep rc=$rc ms=$(echo "($t1-$t0)*1000" | bc) so_bytes=$(stat -c %s $wd/module.so 2>/dev/null || echo 0) cpp_lines=$(cat $wd/*.cpp 2>/dev/null | wc -l)" >> $OUT/compile.txt
    done
  done
done
# 5. probes: interpreted and transpiled, BEFORE and AFTER, and the JVM; perf stat -r 3
$Q > $OUT/quiet_probes.txt 2>&1; echo "$(date -u +%FT%TZ) probes"
for f in $M/sc_probes/*.scala; do
  n=$(basename $f .scala)
  for v in before after; do
    L=$LB; [ $v = after ] && L=$LA
    wd=$OUT/probe_so/$v/$n; mkdir -p $wd
    env LD_LIBRARY_PATH=$L $(T $v)/protoscalac $f -o $wd --build-so > $wd.build_log.txt 2>&1
  done
  main=$(grep -o '@main def [A-Za-z0-9]*' $f | awk '{print $3}')
  for k in interp_before interp_after trans_before trans_after jvm; do
    v=${k#*_}; L=$LB; [ $v = after ] && L=$LA
    case $k in
      interp_*) c="env LD_LIBRARY_PATH=$L $(T $v)/protoscala $f";;
      trans_*) [ -f $OUT/probe_so/$v/$n/module.so ] || { echo "$n $k NO_SO" >> $OUT/probes.txt; continue; }
               c="env LD_LIBRARY_PATH=$L $(T $v)/protoscala --run-module $OUT/probe_so/$v/$n/module.so";;
      jvm) c="java -XX:-UsePerfData -cp $SCALA_HOME/lib/*:$JP/$n $main";;
    esac
    chk=$($c 2>&1 | grep CHECK | tail -n 1)
    o=$(perf stat -r 3 -x, -e cycles,task-clock $c 2>&1 >/dev/null | grep -E 'cycles|task-clock' | cut -d, -f1 | paste -sd' ')
    echo "$n $k $o | $chk" >> $OUT/probes.txt
  done
done
echo DONE
