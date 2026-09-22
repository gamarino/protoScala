// EXPECT-ERROR: needs to be abstract, since def area is not defined
trait Shape:
  def area: Double
class Blob extends Shape
