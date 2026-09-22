// EXPECT-ERROR: illegal inheritance
class A
class B
trait T extends A
class C extends B with T
