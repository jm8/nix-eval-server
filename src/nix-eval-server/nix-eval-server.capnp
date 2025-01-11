@0x8e1cc1a4435f4553;

interface Evaluator {
  test @0 (x: UInt32) -> (y: UInt32);
  getAttributes @1 (expression: Text) -> (attributes: List(Text));
}

