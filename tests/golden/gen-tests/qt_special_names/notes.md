A method called init, cleanup, initTestCase or cleanupTestCase, or one that ends in _data, must not become a slot Qt Test calls on its own: its test gets _call after the name.
