// EXPECT: total=9600 actors=64 perActor=150
// Mirrors protoClojure's `concurrent-sends-no-race.clj`, and exists because of a
// specific defect: `finishTurn` used to release the actor's claim and THEN read
// `ActorState`'s cursor and walk the actor's `__pend<n>__` list, so the worker that
// claimed the actor next was writing both while the releasing worker was still
// reading them. ThreadSanitizer reported it once; the fix evaluates the work question
// under the claim and reads nothing afterwards.
//
// The shape matters. The 8 x 25,000-send stress case almost never reaches that window,
// because with senders hammering one actor `sched` is 2 at the end of nearly every turn
// and the re-check happens under the claim. The window needs MANY actors each taking a
// SHORT burst, so a turn often ends with nothing queued and the next message arrives
// just after the release. Six sender threads trickling one message at a time over 64
// actors entered that window ~18,000 times per run.
//
// HONEST LIMIT, stated rather than implied: this fixture has NO mutation that turns it
// red. The pre-fix code produced the correct total on every run, because the cursor
// half of the race is benign -- a stale read implies a concurrent claimer, whose
// existence made the old gating CAS fail, so the stale answer was discarded. What made
// the overlap observable was a temporary `inTurn` assertion on `ActorState` (2 firings
// in ~110,000 window entries, all of them `hasWork` overlapping another worker's turn,
// never two workers inside one turn), which was removed before committing. So this is a
// STRESS fixture that drives the interleaving and verifies the message count, not proof
// of the fix. The proof is the assertion run, recorded in
// `../../../.agent_scratch/pendingidx/`.
@main def run(): Unit =
  val actors = 64
  val senders = 6
  val perActor = 150          // messages each actor receives, from all senders together
  val burstsPerSender = perActor / senders   // 25, exact
  val as = (1 to actors).toList.map { _ =>
    Actor.spawn(0) { (s, m) => (s + m, s + m) }
  }
  val ts = (1 to senders).toList.map { _ =>
    Thread.start { () =>
      var k = 0
      while k < burstsPerSender do
        // One message per actor per burst, so every actor's backlog stays short and
        // its turn ends empty -- which is the `sched == 1` release path.
        as.foreach { a => a ! 1 }
        k += 1
      ()
    }
  }
  ts.foreach(t => t.join())
  var total = 0
  as.foreach { a => total += (a ? 0).await }
  println(s"total=$total actors=$actors perActor=$perActor")
