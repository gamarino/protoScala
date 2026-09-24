#include "frontend/AST.h"
#include "frontend/Parser.h"

#include <gtest/gtest.h>

using protoScala::ParseError;
using protoScala::dump;
using protoScala::parseExpressionSource;

namespace {
std::string e(const std::string& src) { return dump(*parseExpressionSource(src)); }

bool incomplete(const std::string& src) {
    try {
        parseExpressionSource(src);
    } catch (const ParseError& err) {
        return err.atEof;
    }
    return false;
}
} // namespace

TEST(Parser, Literals) {
    EXPECT_EQ(e("42"), "(int 42)");
    EXPECT_EQ(e("-5"), "(int -5)");
    EXPECT_EQ(e("2.5"), "(float 2.5)");
    EXPECT_EQ(e("-2.5"), "(float -2.5)");
    EXPECT_EQ(e("\"a\\n\""), "(str \"a\\n\")");
    EXPECT_EQ(e("'c'"), "(char 'c')");
    EXPECT_EQ(e("true"), "true");
    EXPECT_EQ(e("null"), "null");
    EXPECT_EQ(e("()"), "()");
    EXPECT_EQ(e("s\"x $y\""), "(interp s \"x \" y \"\")");
}

TEST(Parser, PrecedenceByFirstCharacter) {
    EXPECT_EQ(e("1 + 2 * 3"), "(infix + (int 1) (infix * (int 2) (int 3)))");
    EXPECT_EQ(e("a - b - c"), "(infix - (infix - a b) c)");
    EXPECT_EQ(e("a max b + 1"), "(infix max a (infix + b (int 1)))");
    EXPECT_EQ(e("a == b && c < d || e"),
              "(infix || (infix && (infix == a b) (infix < c d)) e)");
    EXPECT_EQ(e("x += 1"), "(infix += x (int 1))");
}

TEST(Parser, RightAssociativeColonOperators) {
    EXPECT_EQ(e("a :: b :: c"), "(infix :: a (infix :: b c))");
}

TEST(Parser, MixedAssociativityIsAnError) {
    EXPECT_THROW(parseExpressionSource("a +: b +- c"), ParseError);
}

TEST(Parser, PrefixOperators) {
    EXPECT_EQ(e("-x + !y"), "(infix + (prefix - x) (prefix ! y))");
    EXPECT_EQ(e("~n"), "(prefix ~ n)");
}

TEST(Parser, InfixAcrossLines) {
    EXPECT_EQ(e("a +\n  b"), "(infix + a b)");
    EXPECT_EQ(e("a +\nb"), "(infix + a b)");
    EXPECT_EQ(e("a\n  + b"), "(infix + a b)");
}

TEST(Parser, SelectionApplicationAndTypeArguments) {
    EXPECT_EQ(e("f(1, 2)(3)"), "(apply (apply f (int 1) (int 2)) (int 3))");
    EXPECT_EQ(e("a.b.c(d)"), "(apply (. (. a b) c) d)");
    EXPECT_EQ(e("s.length"), "(. s length)");
    EXPECT_EQ(e("f[Int](x)"), "(apply (tapply f Int) x)");
    EXPECT_EQ(e("f()"), "(apply f)");
}

TEST(Parser, BlockArgumentAndBlockLambda) {
    EXPECT_EQ(e("xs.foreach { x => println(x) }"),
              "(apply (. xs foreach) (block (lambda (x) (block (apply println x)))))");
    EXPECT_EQ(e("xs.foreach { x =>\n  a\n  b\n}"),
              "(apply (. xs foreach) (block (lambda (x) (block a b))))");
}

TEST(Parser, IfBothStyles) {
    EXPECT_EQ(e("if (a) b else c"), "(if a b c)");
    EXPECT_EQ(e("if a then b else c"), "(if a b c)");
    EXPECT_EQ(e("if a then b"), "(if a b)");
    EXPECT_EQ(e("if a then\n  b\nelse\n  c"), "(if a (block b) (block c))");
    EXPECT_EQ(e("if (a) {\n  b\n} else {\n  c\n}"), "(if a (block b) (block c))");
    EXPECT_EQ(e("if (a)\n  b\nelse\n  c"), "(if a (block b) (block c))");
    EXPECT_EQ(e("if (a) || b then c else d"), "(if (infix || (parens a) b) c d)");
    EXPECT_EQ(e("if a then b\nelse c"), "(if a b c)");
}

TEST(Parser, WhileBothStyles) {
    EXPECT_EQ(e("while (i < 10) i += 1"), "(while (infix < i (int 10)) (infix += i (int 1)))");
    EXPECT_EQ(e("while i < 10 do\n  i += 1"),
              "(while (infix < i (int 10)) (block (infix += i (int 1))))");
    EXPECT_EQ(e("while (i < 10) {\n  i += 1\n}"),
              "(while (infix < i (int 10)) (block (infix += i (int 1))))");
}

TEST(Parser, DoWhileIsRejected) {
    EXPECT_THROW(parseExpressionSource("do x while (c)"), ParseError);
}

TEST(Parser, Lambdas) {
    EXPECT_EQ(e("x => x * 2"), "(lambda (x) (infix * x (int 2)))");
    EXPECT_EQ(e("(x: Int, y: Int) => x + y"), "(lambda (x:Int y:Int) (infix + x y))");
    EXPECT_EQ(e("() => 42"), "(lambda () (int 42))");
    EXPECT_EQ(e("_ => 1"), "(lambda (_) (int 1))");
    EXPECT_EQ(e("(f: (Int, String) => Boolean) => f"),
              "(lambda (f:(Int, String) => Boolean) f)");
    EXPECT_EQ(e("x =>\n  f(x)\n  x"), "(lambda (x) (block (apply f x) x))");
}

TEST(Parser, ParensTuplesAscriptionSplicesNamedArgs) {
    EXPECT_EQ(e("(a)"), "(parens a)");
    EXPECT_EQ(e("(a, b)"), "(tuple a b)");
    EXPECT_EQ(e("x: Int"), "(typed x Int)");
    EXPECT_EQ(e("f(xs*)"), "(apply f (splice xs))");
    EXPECT_EQ(e("f(xs: _*)"), "(apply f (splice xs))");
    EXPECT_EQ(e("f(x = 1)"), "(apply f (named x (int 1)))");
}

TEST(Parser, AssignmentAndReturn) {
    EXPECT_EQ(e("x = y + 1"), "(= x (infix + y (int 1)))");
    EXPECT_EQ(e("return"), "(return)");
    EXPECT_EQ(e("return x"), "(return x)");
}

TEST(Parser, TypesAreParsedIntoTypeTrees) {
    EXPECT_EQ(e("x: List[Map[String, Int]]"), "(typed x List[Map[String, Int]])");
    EXPECT_EQ(e("x: (Int, String)"), "(typed x (Int, String))");
    EXPECT_EQ(e("x: Int => Int"), "(typed x (Int) => Int)");
    EXPECT_EQ(e("x: A | B"), "(typed x A | B)");
    EXPECT_EQ(e("x: scala.collection.Seq[?]"), "(typed x scala.collection.Seq[?])");
}

// Phase 4 implements `throw`, `try`/`catch`/`finally` and `super[T].m`; what
// stays out is the anonymous-class form `new T { ... }`, which needs a
// per-instance class (a deliberate deviation, see docs/STATUS.md).
TEST(Parser, ThrowAndTryParse) {
    EXPECT_EQ(dump(*parseExpressionSource("throw e")), "(throw e)");
    EXPECT_EQ(dump(*parseExpressionSource("try a finally b")), "(try a (finally b))");
    EXPECT_NE(dump(*parseExpressionSource("try a catch case e: T => b")).find("(case"),
              std::string::npos);
}

TEST(Parser, LaterPhaseConstructsAreReportedClearly) {
    for (const char* src : {"new A { }"}) {
        try {
            parseExpressionSource(src);
            FAIL() << "expected ParseError for " << src;
        } catch (const ParseError& err) {
            EXPECT_NE(std::string(err.what()).find("not implemented yet"), std::string::npos)
                << src << ": " << err.what();
        }
    }
}

TEST(Parser, IncompleteInputIsFlaggedForTheRepl) {
    EXPECT_TRUE(incomplete("1 +"));
    EXPECT_TRUE(incomplete("(1 + "));
    EXPECT_TRUE(incomplete("if a then"));
    EXPECT_TRUE(incomplete("{ a + 1"));
    EXPECT_TRUE(incomplete("\"\"\"abc"));
    EXPECT_FALSE(incomplete("1 + )"));
}

TEST(Parser, ErrorsCarryPositions) {
    try {
        parseExpressionSource("f(1,\n  ]");
        FAIL();
    } catch (const ParseError& err) {
        EXPECT_EQ(err.pos.line, 2);
        EXPECT_EQ(err.pos.column, 3);
    }
}

// Additional coverage beyond the plan: brace and indentation variants must
// produce the same trees, and associativity mixing is caught across nesting.

TEST(Parser, BraceAndIndentedBlocksAreEquivalent) {
    EXPECT_EQ(e("{\n  a\n  b\n}"), "(block a b)");
    EXPECT_EQ(e("{ a; b }"), "(block a b)");
    EXPECT_EQ(e("while i < 3 do\n  f(i)\n  i += 1"), e("while (i < 3) {\n  f(i)\n  i += 1\n}"));
    EXPECT_EQ(e("if a then\n  if b then\n    c\n  else\n    d\nelse\n  e"),
              "(if a (block (if b (block c) (block d))) (block e))");
    EXPECT_EQ(e("if (a) {\n  if (b) {\n    c\n  } else {\n    d\n  }\n} else {\n  e\n}"),
              "(if a (block (if b (block c) (block d))) (block e))");
}

TEST(Parser, StatementsAfterNestedIndentedBlocks) {
    EXPECT_EQ(e("{\n  if a then\n    b\n  c\n}"), "(block (if a (block b)) c)");
    EXPECT_EQ(e("{\n  while a do\n    b\n  c\n}"), "(block (while a (block b)) c)");
}

TEST(Parser, EndMarkersInBlocks) {
    EXPECT_EQ(e("{\n  if a then\n    b\n  end if\n  c\n}"), "(block (if a (block b)) c)");
    EXPECT_EQ(e("{\n  while a do\n    b\n  end while\n}"), "(block (while a (block b)))");
    try {
        parseExpressionSource("{\n  while a do\n    b\n  end if\n}");
        FAIL();
    } catch (const ParseError& err) {
        EXPECT_NE(std::string(err.what()).find("misaligned end marker"), std::string::npos);
    }
}

TEST(Parser, MixedAssociativityIsCaughtInNestedOperands) {
    EXPECT_THROW(parseExpressionSource("a +: b +: c +- d"), ParseError);
    EXPECT_THROW(parseExpressionSource("a +- b +: c"), ParseError);
    EXPECT_EQ(e("a +: b * c +: d"), "(infix +: a (infix +: (infix * b c) d))");
    // A tighter operator in between does not reset the level's associativity.
    EXPECT_THROW(parseExpressionSource("a +: b * c +- d"), ParseError);
    EXPECT_THROW(parseExpressionSource("a :: b + c :+ d"), ParseError);
    // A looser operator does.
    EXPECT_EQ(e("a +- b < c +: d"), "(infix < (infix +- a b) (infix +: c d))");
}

TEST(Parser, NegativeLiteralFoldAndSelections) {
    EXPECT_EQ(e("-1.abs"), "(. (int -1) abs)");
    EXPECT_EQ(e("- x.abs"), "(prefix - (. x abs))");
    EXPECT_EQ(e("a - 1"), "(infix - a (int 1))");
    EXPECT_EQ(e("- 5"), "(prefix - (int 5))");
    EXPECT_EQ(e("- 2.5"), "(prefix - (float 2.5))");
    EXPECT_EQ(e("-9223372036854775808L"), "(int -9223372036854775808)");
}

TEST(Parser, LambdasAsArgumentsAndApplicationOnNextLineIsNotAnArgument) {
    EXPECT_EQ(e("xs.map(x => x + 1)"), "(apply (. xs map) (lambda (x) (infix + x (int 1))))");
    EXPECT_EQ(e("f((a) + 1)"), "(apply f (infix + (parens a) (int 1)))");
    EXPECT_EQ(e("xs.map { (a, b) => a }"),
              "(apply (. xs map) (block (lambda (a b) (block a))))");
    EXPECT_EQ(e("{\n  f\n  (a)\n}"), "(block f (parens a))");
}

TEST(Parser, AssignmentTargets) {
    EXPECT_EQ(e("a.b = 1"), "(= (. a b) (int 1))");
    EXPECT_EQ(e("a(0) = 1"), "(= (apply a (int 0)) (int 1))");
    EXPECT_THROW(parseExpressionSource("1 = 2"), ParseError);
}

TEST(Parser, OperatorTableHelpers) {
    using protoScala::isAssignmentOperator;
    using protoScala::isRightAssociative;
    using protoScala::precedence;
    EXPECT_TRUE(isAssignmentOperator("+="));
    EXPECT_TRUE(isAssignmentOperator("::="));
    EXPECT_FALSE(isAssignmentOperator("=="));
    EXPECT_FALSE(isAssignmentOperator("<="));
    EXPECT_FALSE(isAssignmentOperator("!="));
    EXPECT_FALSE(isAssignmentOperator("="));
    EXPECT_TRUE(isRightAssociative("::"));
    EXPECT_FALSE(isRightAssociative("++"));
    EXPECT_LT(precedence("max"), precedence("|"));
    EXPECT_LT(precedence("+="), precedence("max"));
    EXPECT_LT(precedence("+"), precedence("*"));
    EXPECT_LT(precedence("*"), precedence("#"));
}

TEST(Parser, CompilationUnitOfExpressions) {
    EXPECT_EQ(dump(*protoScala::parseSource("f(1)\ng(2); h")),
              "(unit (apply f (int 1)) (apply g (int 2)) h)");
    EXPECT_EQ(dump(*protoScala::parseSource("")), "(unit)");
}

TEST(Parser, OldStyleConditionFollowedByNestedNewStyleConstruct) {
    EXPECT_EQ(e("if (x) 1 else if y then 2 else 3"), "(if x (int 1) (if y (int 2) (int 3)))");
    EXPECT_EQ(e("if (a) if b then c else d"), "(if a (if b c d))");
    EXPECT_EQ(e("while (a) while b do c"), "(while a (while b c))");
    EXPECT_EQ(e("if (a) xs.map(x => x) else b"),
              "(if a (apply (. xs map) (lambda (x) x)) b)");
    EXPECT_EQ(e("if (a) || b then c else d"), "(if (infix || (parens a) b) c d)");
}

namespace {
std::string u(const std::string& src) { return dump(*protoScala::parseSource(src)); }

std::string unitError(const std::string& src) {
    try {
        protoScala::parseSource(src);
    } catch (const ParseError& err) {
        return err.what();
    }
    return "";
}
} // namespace

TEST(ParserDefs, ValVarLazy) {
    EXPECT_EQ(u("val x: Int = 1"), "(unit (val x : Int (int 1)))");
    EXPECT_EQ(u("var y = 2"), "(unit (var y (int 2)))");
    EXPECT_EQ(u("lazy val z = f()"), "(unit (lazy-val z (apply f)))");
}

TEST(ParserDefs, DefShapes) {
    EXPECT_EQ(u("def add(a: Int, b: Int): Int = a + b"),
              "(unit (def add ((a:Int b:Int)) : Int (infix + a b)))");
    EXPECT_EQ(u("def curried(a: Int)(b: Int) = a * b"),
              "(unit (def curried ((a:Int) (b:Int)) (infix * a b)))");
    EXPECT_EQ(u("def sum(xs: Int*): Int = 0"), "(unit (def sum ((xs:Int*)) : Int (int 0)))");
    EXPECT_EQ(u("def id[A](x: A): A = x"), "(unit (def id [A] ((x:A)) : A x))");
    EXPECT_EQ(u("def pi = 3.14"), "(unit (def pi (float 3.14)))");
    EXPECT_EQ(u("def f() = 1"), "(unit (def f (()) (int 1)))");
    EXPECT_EQ(u("def f(x: => Int) = x"), "(unit (def f ((x:=> Int)) x))");
    EXPECT_EQ(u("def f(x: Int = 1) = x"), "(unit (def f ((x:Int = (int 1))) x))");
}

TEST(ParserDefs, MainAnnotationAndIndentedBody) {
    EXPECT_EQ(u("@main def hello(): Unit =\n  println(\"hi\")"),
              "(unit (def @main hello (()) : Unit (block (apply println (str \"hi\")))))");
}

TEST(ParserDefs, BraceAndIndentedBodiesAreEquivalent) {
    const std::string braces =
        u("def f(x: Int): Int = {\n  val y = x + 1\n  y * 2\n}");
    const std::string indented =
        u("def f(x: Int): Int =\n  val y = x + 1\n  y * 2");
    EXPECT_EQ(braces, indented);
    EXPECT_EQ(indented,
              "(unit (def f ((x:Int)) : Int (block (val y (infix + x (int 1))) "
              "(infix * y (int 2)))))");
}

TEST(ParserDefs, EndMarkers) {
    EXPECT_EQ(u("def f(x: Int): Int =\n  val y = x\n  y\nend f\nf(1)"),
              "(unit (def f ((x:Int)) : Int (block (val y x) y)) (apply f (int 1)))");
    EXPECT_EQ(u("def g =\n  if a then\n    b\n  else\n    c\n  end if\nend g"),
              "(unit (def g (block (if a (block b) (block c)))))");
    EXPECT_NE(unitError("def f =\n  1\nend g").find("misaligned end marker"),
              std::string::npos);
}

TEST(ParserDefs, LocalDefinitionsInBlocks) {
    EXPECT_EQ(e("{ val a = 1; def f(x: Int) = x + a; f(2) }"),
              "(block (val a (int 1)) (def f ((x:Int)) (infix + x a)) (apply f (int 2)))");
}

TEST(ParserDefs, ScriptModeTopLevelStatements) {
    EXPECT_EQ(u("println(1)\nprintln(2)"), "(unit (apply println (int 1)) (apply println (int 2)))");
    EXPECT_EQ(u("val a = 1; val b = 2"), "(unit (val a (int 1)) (val b (int 2)))");
}

TEST(ParserDefs, ModifiersAndOtherAnnotationsAreKeptOrIgnored) {
    EXPECT_EQ(u("private final def f = 1"), "(unit (def private f (int 1)))");
    EXPECT_EQ(u("@tailrec def f(n: Int): Int = n"), "(unit (def @tailrec f ((n:Int)) : Int n))");
}

TEST(ParserDefs, Imports) {
    EXPECT_EQ(u("import scala.math.*"), "(unit (import scala.math.*))");
    EXPECT_EQ(u("import a.{b, c as d}"), "(unit (import a.{b, c as d}))");
}

TEST(ParserDefs, UnsupportedDefinitionsAreReportedClearly) {
    EXPECT_NE(unitError("enum Color { case Red }").find("not implemented yet"), std::string::npos);
    EXPECT_NE(unitError("type T = Int").find("not implemented yet"), std::string::npos);
    EXPECT_NE(unitError("lazy val (a, b) = p").find("lazy pattern definitions"), std::string::npos);
    EXPECT_NE(unitError("given x: Int = 1").find("(D3)"), std::string::npos);
    EXPECT_NE(unitError("def f(using x: Int) = x").find("(D3)"), std::string::npos);
    EXPECT_NE(unitError("def f() { 1 }").find("procedure syntax"), std::string::npos);
}

TEST(ParserDefs, IncompleteDefinitionsAskForMoreInput) {
    try {
        protoScala::parseSource("def f(x: Int) =");
        FAIL();
    } catch (const ParseError& err) {
        EXPECT_TRUE(err.atEof);
    }
}

TEST(ParserDefs, AnnotationsModifiersAndEndMarkerVariants) {
    EXPECT_EQ(u("@main\ndef run(args: String*): Unit = ()"),
              "(unit (def @main run ((args:String*)) : Unit ()))");
    EXPECT_EQ(u("@deprecated(\"old\", \"1.0\") def f = 1"), "(unit (def @deprecated f (int 1)))");
    EXPECT_EQ(u("inline transparent def f = 1"), "(unit (def f (int 1)))");
    EXPECT_EQ(u("private[this] val x = 1"), "(unit (val private x (int 1)))");
    EXPECT_EQ(u("def f(using: Int) = using"), "(unit (def f ((using:Int)) using))");
    EXPECT_EQ(u("open(1)"), "(unit (apply open (int 1)))");
    EXPECT_EQ(u("val x =\n  1\nend val"), "(unit (val x (block (int 1))))");
    EXPECT_EQ(u("val x =\n  1\nend x"), "(unit (val x (block (int 1))))");
    EXPECT_NE(unitError("while a do\n  b\nend if").find("misaligned end marker"),
              std::string::npos);
}

TEST(ParserDefs, ImportsInsideBlocksAndSelectors) {
    EXPECT_EQ(e("{ import a.b; b }"), "(block (import a.b) b)");
    EXPECT_EQ(u("import a.{given, b => c}"), "(unit (import a.{given, b => c}))");
}

TEST(ParserDefs, MoreUnsupportedAndInvalidDefinitions) {
    EXPECT_NE(unitError("enum E { case A }").find("'enum' definitions are not implemented yet"),
              std::string::npos);
    EXPECT_NE(unitError("type T = Int").find("'type' definitions are not implemented yet"),
              std::string::npos);
    EXPECT_NE(unitError("extension (x: Int) def y = x").find("not implemented yet"),
              std::string::npos);
    EXPECT_NE(unitError("implicit val x: Int = 1").find("(D3)"), std::string::npos);
    EXPECT_NE(unitError("def f(implicit x: Int) = x").find("(D3)"), std::string::npos);
    EXPECT_NE(unitError("val a, b = 1")
                  .find("several names in one value definition are not implemented yet"),
              std::string::npos);
    EXPECT_NE(unitError("lazy var x = 1").find("lazy"), std::string::npos);
    EXPECT_NE(unitError("def f: Int").find("'=' expected"), std::string::npos);
}

TEST(ParserDefs, TruncatedDefinitionsAskForMoreInput) {
    for (const char* src : {"val", "val x", "val x =", "def", "def f(", "def f(x: Int",
                            "@main", "import", "def f =\n  val y = 1\n  y +"}) {
        try {
            protoScala::parseSource(src);
            ADD_FAILURE() << src;
        } catch (const ParseError& err) {
            EXPECT_TRUE(err.atEof) << src << ": " << err.what();
        }
    }
}

namespace {
std::string parseErrorOf(const std::string& src) {
    try {
        protoScala::parseSource(src);
    } catch (const ParseError& err) {
        return err.what();
    }
    return "";
}
} // namespace

TEST(ParserTemplates, ClassWithParametersBothSyntaxes) {
    const std::string expected =
        "(unit (class Point (val x:Int var y:Int z:Int) (body "
        "(def sum (infix + x y)) (val scaled (infix * x z)))))";
    EXPECT_EQ(u("class Point(val x: Int, var y: Int, z: Int) {\n"
                "  def sum = x + y\n"
                "  val scaled = x * z\n"
                "}\n"),
              expected);
    EXPECT_EQ(u("class Point(val x: Int, var y: Int, z: Int):\n"
                "  def sum = x + y\n"
                "  val scaled = x * z\n"),
              expected);
    EXPECT_EQ(u("class Point(val x: Int, var y: Int, z: Int):\n"
                "  def sum = x + y\n"
                "  val scaled = x * z\n"
                "end Point\n"),
              expected);
    EXPECT_EQ(u("class Empty"), "(unit (class Empty (body)))");
    EXPECT_EQ(u("class Unit0()"), "(unit (class Unit0 () (body)))");
}

TEST(ParserTemplates, CaseClassesObjectsAndTraits) {
    EXPECT_EQ(u("case class P(x: Int, y: Int)"), "(unit (case-class P (x:Int y:Int) (body)))");
    EXPECT_EQ(u("case object Nada"), "(unit (case-object Nada (body)))");
    EXPECT_EQ(u("object O:\n  val a = 1\n  def f(n: Int) = n + a\n"),
              "(unit (object O (body (val a (int 1)) (def f ((n:Int)) (infix + n a)))))");
    EXPECT_EQ(u("trait Shape {\n  def area: Double\n  def describe = \"area \" + area\n}"),
              "(unit (trait Shape (body (def area : Double) "
              "(def describe (infix + (str \"area \") area)))))");
    EXPECT_EQ(u("sealed abstract class Expr"), "(unit (class abstract sealed Expr (body)))");
    EXPECT_EQ(u("final case class Box[+A](value: A)"),
              "(unit (case-class final Box [A] (value:A) (body)))");
}

TEST(ParserTemplates, ExtendsWithArgumentsAndMixins) {
    EXPECT_EQ(u("class C(n: Int) extends B(n, 2) with T1 with T2"),
              "(unit (class C (n:Int) (extends (B n (int 2)) T1 T2) (body)))");
    EXPECT_EQ(u("class C extends B, T1, T2"), "(unit (class C (extends B T1 T2) (body)))");
    EXPECT_EQ(u("class C(x: Int)\n    extends B(x)\n    with T:\n  def f = 1\n"),
              "(unit (class C (x:Int) (extends (B x) T) (body (def f (int 1)))))");
    EXPECT_EQ(u("case object None extends Option[Nothing]"),
              "(unit (case-object None (extends Option[Nothing]) (body)))");
    EXPECT_EQ(u("trait Greeter(val greeting: String)"),
              "(unit (trait Greeter (val greeting:String) (body)))");
}

TEST(ParserTemplates, MembersModifiersAndAuxiliaryConstructors) {
    EXPECT_EQ(u("class A {\n  private val secret = 1\n  override def toString = \"A\"\n"
                "  abstract override def put(x: Int) = super.put(x)\n}"),
              "(unit (class A (body (val private secret (int 1)) "
              "(def override toString (str \"A\")) "
              "(def override abstract put ((x:Int)) (apply (. super put) x)))))");
    EXPECT_EQ(u("class R(val n: Int, val d: Int):\n  def this(n: Int) = this(n, 1)\n"),
              "(unit (class R (val n:Int val d:Int) (body (def this ((n:Int)) "
              "(apply this n (int 1))))))");
    EXPECT_EQ(u("class A(private val k: Int)"), "(unit (class A (private val k:Int) (body)))");
    EXPECT_EQ(u("abstract class Q { val size: Int }"),
              "(unit (class abstract Q (body (val size : Int))))");
}

TEST(ParserTemplates, NewThisSuperAndSelfAlias) {
    EXPECT_EQ(e("new Point(1, 2)"), "(new Point (int 1) (int 2))");
    EXPECT_EQ(e("new Point(1, 2).x"), "(. (new Point (int 1) (int 2)) x)");
    EXPECT_EQ(e("new Box[Int](3)"), "(new Box[Int] (int 3))");
    EXPECT_EQ(e("new Empty"), "(new Empty)");
    EXPECT_EQ(e("this.x"), "(. this x)");
    EXPECT_EQ(e("super.toString"), "(. super toString)");
    EXPECT_EQ(u("class A { self =>\n  def me = self\n}"),
              "(unit (class A (self self) (body (def me self))))");
}

TEST(ParserTemplates, Errors) {
    EXPECT_NE(parseErrorOf("case class P").find("case class must have a parameter list"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("new T { def f = 1 }").find("anonymous classes are not implemented yet"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("class A(x: Int)(y: Int)")
                  .find("multiple constructor parameter lists are not implemented yet"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("def f: Int").find("'=' expected"), std::string::npos);  // outside a template
    EXPECT_NE(parseErrorOf("super.f[Int]; super[T].f").find("super[T]"), std::string::npos);
    EXPECT_NE(parseErrorOf("case 1 => 2").find("'case'"), std::string::npos);
}

TEST(ParserTemplates, IncompleteTemplatesAskForMoreInput) {
    EXPECT_TRUE(parseErrorOf("class A {").find("unexpected end of input") == 0 ||
                parseErrorOf("class A {").find("unclosed") != std::string::npos);
    EXPECT_NE(parseErrorOf("class A:\n").find("indented template body"), std::string::npos);
}
TEST(ParserPatterns, MatchBothSyntaxes) {
    const std::string expected =
        "(match x (case (int 1) (str \"one\")) (case n (if (infix > n (int 1))) (str \"many\")) "
        "(case _ (str \"none\")))";
    EXPECT_EQ(e("x match {\n  case 1 => \"one\"\n  case n if n > 1 => \"many\"\n"
                "  case _ => \"none\"\n}"),
              expected);
    EXPECT_EQ(e("x match { case 1 => \"one\" case n if n > 1 => \"many\" case _ => \"none\" }"),
              expected);
    EXPECT_EQ(dump(*protoScala::parseSource(
                  "val r = x match\n  case 1 => \"one\"\n  case n if n > 1 => \"many\"\n"
                  "  case _ => \"none\"\nprintln(r)\n")),
              "(unit (val r " + expected + ") (apply println r))");
    // A multi-line case body is an indented block.
    EXPECT_EQ(e("x match\n  case 1 =>\n    val y = 2\n    y\n  case _ => 0"),
              "(match x (case (int 1) (block (val y (int 2)) y)) (case _ (int 0)))");
}

TEST(ParserPatterns, EveryPatternKind) {
    auto p = [](const std::string& pat) {
        const std::string d = e("v match { case " + pat + " => 0 }");
        // "(match v (case <pat> (int 0)))": 15 characters before <pat>, 10 after it
        return d.substr(15, d.size() - 15 - 10);
    };
    EXPECT_EQ(p("-1"), "(int -1)");
    EXPECT_EQ(p("\"s\""), "(str \"s\")");
    EXPECT_EQ(p("'c'"), "(char 'c')");
    EXPECT_EQ(p("true"), "true");
    EXPECT_EQ(p("null"), "null");
    EXPECT_EQ(p("()"), "()");
    EXPECT_EQ(p("_"), "_");
    EXPECT_EQ(p("x"), "x");
    EXPECT_EQ(p("Nil"), "(stable Nil)");
    EXPECT_EQ(p("`x`"), "(stable x)");
    EXPECT_EQ(p("Color.Red"), "(stable (. Color Red))");
    EXPECT_EQ(p("i: Int"), "(: i Int)");
    EXPECT_EQ(p("_: List[Int]"), "(: _ List[Int])");
    EXPECT_EQ(p("p @ Point(x, _)"), "(@ p (unapply Point x _))");
    EXPECT_EQ(p("1 | 2 | 3"), "(| (int 1) (int 2) (int 3))");
    EXPECT_EQ(p("(a, b)"), "(tuple-pat a b)");
    EXPECT_EQ(p("(a)"), "a");
    EXPECT_EQ(p("h :: t"), "(unapply :: h t)");
    EXPECT_EQ(p("a :: b :: rest"), "(unapply :: a (unapply :: b rest))");
    EXPECT_EQ(p("List(a, _*)"), "(unapply List a _*)");
    EXPECT_EQ(p("List(a, rest*)"), "(unapply List a (_* rest))");
    EXPECT_EQ(p("List(a, rest @ _*)"), "(unapply List a (_* rest))");
    EXPECT_EQ(p("Some(Point(1, y))"), "(unapply Some (unapply Point (int 1) y))");
    EXPECT_EQ(p("Empty()"), "(unapply Empty)");
}

TEST(ParserPatterns, CaseLambdasAndPatternVals) {
    EXPECT_EQ(e("xs.map { case (a, b) => a + b }"),
              "(apply (. xs map) (lambda (x$1) (match x$1 (case (tuple-pat a b) "
              "(infix + a b)))))");
    EXPECT_EQ(dump(*protoScala::parseSource("val (a, b) = pair\nvar h :: t = xs")),
              "(unit (val-pat (tuple-pat a b) pair) (var-pat (unapply :: h t) xs))");
    EXPECT_EQ(dump(*protoScala::parseSource("val Point(x, y) = p")),
              "(unit (val-pat (unapply Point x y) p))");
}

TEST(ParserPatterns, ForComprehensions) {
    const std::string yield =
        "(for-yield (<- x xs) (if (infix > x (int 1))) (<- y ys) (= z (infix * x y)) "
        "(infix + z (int 1)))";
    EXPECT_EQ(e("for (x <- xs if x > 1; y <- ys; z = x * y) yield z + 1"), yield);
    EXPECT_EQ(e("for { x <- xs if x > 1\n y <- ys\n z = x * y } yield z + 1"), yield);
    EXPECT_EQ(e("for\n  x <- xs if x > 1\n  y <- ys\n  z = x * y\nyield z + 1"), yield);
    EXPECT_EQ(e("for x <- xs do println(x)"), "(for-do (<- x xs) (apply println x))");
    EXPECT_EQ(e("for (x <- xs) println(x)"), "(for-do (<- x xs) (apply println x))");
    EXPECT_EQ(e("for ((a, b) <- ps) yield a"), "(for-yield (<- (tuple-pat a b) ps) a)");
    EXPECT_EQ(e("for (Some(v) <- os) yield v"), "(for-yield (<- (unapply Some v) os) v)");
}

TEST(ParserPatterns, PlaceholderSyntax) {
    EXPECT_EQ(e("_ + 1"), "(lambda (_$1) (infix + _$1 (int 1)))");
    EXPECT_EQ(e("xs.map(_ * 2)"), "(apply (. xs map) (lambda (_$1) (infix * _$1 (int 2))))");
    EXPECT_EQ(e("xs.map(_.toString)"), "(apply (. xs map) (lambda (_$1) (. _$1 toString)))");
    EXPECT_EQ(e("f(_)"), "(lambda (_$1) (apply f _$1))");
    EXPECT_EQ(e("_ + _"), "(lambda (_$1 _$2) (infix + _$1 _$2))");
    EXPECT_EQ(e("xs.foreach(println(_))"),
              "(apply (. xs foreach) (lambda (_$1) (apply println _$1)))");
}

TEST(ParserPatterns, PatternErrors) {
    EXPECT_NE(parseErrorOf("x match { }").find("'case' expected"), std::string::npos);
    EXPECT_NE(parseErrorOf("x match\n1").find("'{' or an indented block of cases"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("for (x <- xs)").find("unexpected end of input"), std::string::npos);
    EXPECT_NE(parseErrorOf("for x <- xs println(x)").find("'do' or 'yield' expected"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("lazy val (a, b) = p").find("lazy pattern definitions"),
              std::string::npos);
}

TEST(ParserPatterns, CasePrefixedGenerators) {
    // Scala 3 `case p <- e`: the pattern is filtered (Desugar); the tree is the same.
    EXPECT_EQ(e("for (case (a, b) <- ps) yield a"), "(for-yield (<- (tuple-pat a b) ps) a)");
    EXPECT_EQ(e("for\n  case Some(v) <- os\n  case w: Int <- ws\ndo println(v)"),
              "(for-do (<- (unapply Some v) os) (<- (: w Int) ws) (apply println v))");
}

TEST(ParserTemplates, AnonymousClassBodyOnTheNextLine) {
    EXPECT_NE(parseErrorOf("new T\n{ def f = 1 }").find("anonymous classes are not implemented yet"),
              std::string::npos);
}

TEST(ParserPatterns, MoreMatchAndPlaceholderForms) {
    // A typed placeholder `(_: T)` is still a bare placeholder of its argument.
    EXPECT_EQ(e("(_: Int) + 1"), "(lambda (_$1) (infix + (parens (typed _$1 Int)) (int 1)))");
    // match binds looser than infix operators and chains.
    EXPECT_EQ(e("a + b match { case _ => 0 } match { case n => n }"),
              "(match (match (infix + a b) (case _ (int 0))) (case n n))");
    // An indented match inside a def body, followed by another statement.
    EXPECT_EQ(dump(*protoScala::parseSource("def f(x: Int) =\n  x match\n    case 0 => \"z\"\n"
                                            "    case _ => \"nz\"\nf(1)\n")),
              "(unit (def f ((x:Int)) (block (match x (case (int 0) (str \"z\")) "
              "(case _ (str \"nz\"))))) (apply f (int 1)))");
    // Several statements in a brace-syntax case body form a block.
    EXPECT_EQ(e("x match { case 1 => val y = 2; y case _ => 0 }"),
              "(match x (case (int 1) (block (val y (int 2)) y)) (case _ (int 0)))");
    EXPECT_EQ(e("v match { case Color.Red | Color.Green => 1 case `x` :: _ => 2 }"),
              "(match v (case (| (stable (. Color Red)) (stable (. Color Green))) (int 1)) "
              "(case (unapply :: (stable x) _) (int 2)))");
}

TEST(ParserPatterns, MorePatternErrors) {
    EXPECT_NE(parseErrorOf("val x = _").find("unbound placeholder"), std::string::npos);
    EXPECT_NE(parseErrorOf("v match { case List(_*, a) => 0 }").find("sequence wildcard"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("v match { case 1.abs => 0 }").find("literal pattern"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("for (x = 1) yield x").find("must start with a generator"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("for (x in xs) yield x").find("'<-' or '=' expected"),
              std::string::npos);
}

TEST(ParserPatterns, ClonePatternIsDeep) {
    auto unit = protoScala::parseSource("val p @ Some((a: Int, Color.Red, -1)) = v");
    const auto& vd = static_cast<const protoScala::ValDef&>(*unit->stats[0]);
    auto copy = protoScala::clonePattern(*vd.pattern);
    EXPECT_EQ(dump(*copy), dump(*vd.pattern));
    EXPECT_EQ(dump(*copy), "(@ p (unapply Some (tuple-pat (: a Int) (stable (. Color Red)) (int -1))))");
    EXPECT_NE(copy->args[0].get(), vd.pattern->args[0].get());
}

TEST(ParserPatterns, EndMarkersAndSeparators) {
    EXPECT_EQ(dump(*protoScala::parseSource("x match\n  case _ => 1\nend match\nfor\n  a <- as\n"
                                            "do\n  println(a)\nend for\n")),
              "(unit (match x (case _ (int 1))) (for-do (<- a as) (block (apply println a))))");
    EXPECT_NE(parseErrorOf("x match { case 1 => 2 3 }").find("';' or newline expected"),
              std::string::npos);
    EXPECT_NE(parseErrorOf("for x <- xs\ny <- ys do println(x)").find("'do' or 'yield'"),
              std::string::npos);
}

TEST(ParserPatterns, TypedPatternTypes) {
    auto p = [](const std::string& pat) {
        const std::string d = e("v match { case " + pat + " => 0 }");
        return d.substr(15, d.size() - 15 - 10);
    };
    // A parenthesised type ends at `)`: the case arrow is not a function type.
    EXPECT_EQ(p("t: (Int, Int)"), "(: t (Int, Int))");
    EXPECT_EQ(p("x: (A | B)"), "(: x A | B)");
    EXPECT_EQ(p("f: (Int => Int)"), "(: f (Int) => Int)");
    // `&` binds inside the typed pattern; a top-level `|` separates alternatives.
    EXPECT_EQ(p("x: A & B"), "(: x A & B)");
    EXPECT_EQ(p("_: A | _: B"), "(| (: _ A) (: _ B))");
    EXPECT_EQ(e("v match { case t: (Int, Int) => t }"), "(match v (case (: t (Int, Int)) t))");
}

TEST(ParserPatterns, ParenthesisedBarePlaceholders) {
    EXPECT_EQ(e("k((_: Int))"), "(lambda (_$1) (apply k (parens (typed _$1 Int))))");
    EXPECT_EQ(e("f((_))"), "(lambda (_$1) (apply f (parens _$1)))");
}

TEST(ParserPatterns, BraceCasesAtTheBodyIndentation) {
    EXPECT_EQ(e("x match { case 1 =>\n  2\n  case 3 => 4 }"),
              "(match x (case (int 1) (block (int 2))) (case (int 3) (int 4)))");
    EXPECT_EQ(e("xs.map { case 1 =>\n    val y = 2\n    y\n  case n =>\n    n }"),
              "(apply (. xs map) (lambda (x$1) (match x$1 (case (int 1) (block (val y (int 2)) y)) "
              "(case n (block n)))))");
}

// --- Phase 3: string interpolation is parsed, not carried as raw text ----------

TEST(ParserInterpolation, SplitsLiteralsAndHoles) {
    EXPECT_EQ(e(R"(s"x=${1 + 2}!")"), R"((interp s "x=" (infix + (int 1) (int 2)) "!"))");
    EXPECT_EQ(e(R"(s"$name is $age")"), R"((interp s "" name " is " age ""))");
}

TEST(ParserInterpolation, FInterpolatorTakesTheSpecifierOffTheFollowingLiteral) {
    EXPECT_EQ(e(R"(f"pi=$pi%.2f!")"), R"((interp f "pi=" pi "%.2f" "!"))");
    EXPECT_EQ(e(R"(f"$n%05d")"), R"((interp f "" n "%05d" ""))");
}

TEST(ParserInterpolation, SInterpolatorLeavesAPercentAlone) {
    EXPECT_EQ(e(R"(s"$n%done")"), R"((interp s "" n "%done"))");
}

TEST(ParserInterpolation, AnEscapedPercentIsNotASpecifier) {
    EXPECT_EQ(e(R"(f"$n%%")"), R"((interp f "" n "%%"))");
}

TEST(ParserInterpolation, AHoleMayHoldAnotherInterpolation) {
    EXPECT_EQ(e(R"(s"a${s"b$c"}d")"), R"((interp s "a" (interp s "b" c "") "d"))");
}

TEST(ParserInterpolation, AHoleHoldingTwoExpressionsIsRejected) {
    EXPECT_THROW(e(R"(s"${1 2}")"), ParseError);
}
