# Análisis técnico: generador bidireccional tests ↔ código C++

## 1. Alcance

Herramienta CLI con dos modos sobre un núcleo común:

| Modo | Entrada | Salida |
|---|---|---|
| `gen-impl` (TDD) | Tests GoogleTest / QTest (`.cpp`) | Cabeceras y *stubs* de clases/funciones que hacen compilar los tests (fase "red") |
| `gen-tests` | Cabeceras/clases existentes (`.h`/`.hpp`) | Esqueletos de tests GoogleTest / QTest |

Objetivo: generar código **determinista, compilable y revisable**. No se pretende inferir lógica de negocio, solo estructura (clases, firmas, includes, esqueleto de tests).

## 2. Arquitectura

```
          +-----------+        +-----------+
 tests -> | Frontend  |        | Frontend  | <- headers
          | tests     |        | código    |
          +-----+-----+        +-----+-----+
                |                    |
                v                    v
          +-----------------------------------+
          |  Modelo intermedio (IR) neutral   |
          |  Class, Method, Param, Type, Test |
          +-----------------+-----------------+
                |                    |
                v                    v
          +-----------+        +-----------+
          | Backend   |        | Backend   |
          | código    |        | tests     |
          | (emisor)  |        | GTest/QTest|
          +-----------+        +-----------+
```

Principios:
- **IR independiente de framework y de parser.** Frontends y backends solo hablan con el IR. Permite añadir Catch2/doctest, o cambiar de parser, sin tocar el resto.
- **Frontends separados por dirección**, porque los requisitos del parser son distintos (ver §3).
- **Emisión por plantillas** (inja o mustache), no concatenando strings en C++. Los cambios de estilo no requieren recompilar.

### IR mínimo

```cpp
struct Type   { std::string spelling; bool isConst, isRef, isPtr; /* + origen: Inferred | Declared */ };
struct Param  { Type type; std::string name; };
struct Method { std::string name; Type ret; std::vector<Param> params;
                bool isConst, isStatic, isVirtual, isCtor, isDtor; Access access; };
struct Class  { std::string name; std::vector<std::string> ns, bases;
                std::vector<Method> methods; std::vector<Field> fields; };
struct TestCase { std::string suite, name; std::vector<Stmt> body; Fixture* fixture; };
```

Importante: `Type` debe marcar si es **declarado** (explícito en el código fuente) o **inferido** (deducido de un literal o del contexto). Esto alimenta los avisos/TODOs del backend.

## 3. Elección de parser (decisión clave)

| Opción | Pros | Contras |
|---|---|---|
| **libclang / LibTooling** | AST semántico completo, tipos resueltos, plantillas, macros expandidas | Dependencia pesada; requiere *compile_commands.json* y *include paths* correctos; en código que no compila (tests sin implementación) produce errores y AST de recuperación poco fiable |
| **tree-sitter-cpp** | Solo sintáctico, tolerante a errores, rápido, sin toolchain | Sin semántica: no resuelve tipos ni sobrecargas |

Recomendación (criterio propio, a validar con un prototipo):
- **tests → código**: tree-sitter. El código de entrada **no compila por definición** (referencia clases inexistentes), así que un parser semántico se rompe justo en el caso de uso. Se extraen sintácticamente: macros `TEST/TEST_F/TEST_P`, `QCOMPARE/QVERIFY`, declaraciones de variables, llamadas a métodos, `#include`.
- **código → tests**: libclang. Aquí el código sí compila y se necesitan tipos reales, constness, visibilidad, herencia y plantillas.

Esto implica dos frontends, pero ambos producen el mismo IR. Alternativa más simple para un MVP: tree-sitter en ambas direcciones y asumir cabeceras "limpias" (sin metaprogramación pesada).

## 4. Dirección tests → código (la difícil)

Un test no determina por sí solo tipos de retorno, constructores ni visibilidad. Hay que fijar un **subconjunto de convenciones** y documentarlo.

### Qué se puede extraer con fiabilidad
- Nombre de clase: `Foo foo(args);` / `Foo::bar()` / `new Foo(...)`.
- Métodos usados: `foo.bar(a, b)` → nombre y aridad.
- Constness: si el objeto es `const Foo&` y se llama al método.
- Tipo de retorno a partir del contexto de la aserción: `EXPECT_EQ(foo.size(), 3u)` → `unsigned`, `EXPECT_TRUE(foo.empty())` → `bool`, `EXPECT_STREQ(foo.name(), "x")` → `const char*`.
- Tipos de parámetros desde literales (`1` → `int`, `1.0` → `double`, `"a"` → `const char*`) o desde variables declaradas con tipo explícito.
- Includes esperados: del `#include "foo.h"` del test.
- Fixtures: `TEST_F(Fixture, ...)` → clase `Fixture : ::testing::Test` y miembros usados.

### Qué NO se puede inferir (requiere convención o anotación)
- Tipos con `auto`. Política: error con mensaje claro, o `TODO` en el stub.
- Tipo de retorno cuando el resultado no se compara (`foo.run();`) → `void` por defecto, con aviso.
- Visibilidad, `virtual`, herencia, constructores de copia/movimiento.
- Espacios de nombres (solo si el test usa `ns::Foo` o `using`).

### Convenciones propuestas
1. Sin `auto` en variables que ejerciten la API bajo test.
2. Una clase bajo test por *suite* (nombre de suite = nombre de clase, o `Foo` en `FooTest`).
3. Los literales se escriben con sufijo cuando importa (`3u`, `1.0f`).
4. Anotaciones opcionales en comentario para lo ambiguo, p. ej. `// @gen: returns std::vector<int>`.

### Comportamiento del emisor
- Genera `foo.h` y `foo.cpp` con **stubs que compilan y hacen fallar el test** (valor de retorno por defecto, o `throw std::logic_error("not implemented")`). Así se garantiza la fase "red" real.
- **Idempotente y no destructivo**: si el fichero existe, no lo sobreescribe; fusiona solo miembros nuevos (marcadores `// <gen:begin> ... // <gen:end>`) o escribe a un `.new` y avisa. Es el requisito más importante para que sea usable en un flujo TDD real.
- Cada tipo o decisión inferida lleva comentario `// TODO(inferred): ...`.

## 5. Dirección código → tests

### Qué se genera
- Un fichero de tests por clase; un caso por método público (más constructores).
- Casos base: construcción, método con valores por defecto/límite del tipo (0, -1, `max`, vacío), comparación del retorno con `TODO`.
- Para constness y `noexcept`: tests que comprueban que compilan sobre objeto `const`.
- Clases con dependencias virtuales puras: opcionalmente generar **mocks gmock** (`MOCK_METHOD`) para interfaces.
- Plantillas: un caso por instanciación configurada en un fichero de configuración, no inferida.

### Principio de diseño
Los tests generados son **esqueletos con aserciones placeholder** (`FAIL() << "TODO"` o `GTEST_SKIP()`), nunca aserciones inventadas que pasen por casualidad. Un test generado que pasa sin que nadie lo haya revisado es peor que ninguno.

## 6. Backends de framework

### GoogleTest
- `TEST`, `TEST_F` (fixture), `TEST_P` + `INSTANTIATE_TEST_SUITE_P` (parametrizados).
- Aserciones: `EXPECT_*`/`ASSERT_*`; gmock para mocks.
- Entrada de proceso: `gtest_main`, sin código extra.

### QTest
Diferencias a tener en cuenta (los más fáciles de romper):
- Un test es una **clase `QObject`** con `Q_OBJECT` y los casos como **`private slots`**; requiere **moc** (AUTOMOC en CMake).
- Ciclo de vida: `initTestCase()`, `init()`, `cleanup()`, `cleanupTestCase()`.
- Data-driven: para `void foo()` existe `void foo_data()` con `QTest::addColumn` / `QTest::newRow`.
- Aserciones: `QVERIFY`, `QCOMPARE` (tipos deben coincidir exactamente; ojo con `int` vs `unsigned`), `QVERIFY2`, `QTEST_THROW`-like no existe: usar `QVERIFY_EXCEPTION_THROWN`.
- Punto de entrada: `QTEST_MAIN`, `QTEST_APPLESS_MAIN` (sin `QApplication`) o `QTEST_GUILESS_MAIN`; si la clase está en el `.cpp` hay que incluir el `.moc` al final.

La asimetría GTest/QTest (macros vs. clases+slots+moc) es la razón para que el IR modele `TestCase`/`Fixture` de forma abstracta y cada backend decida la forma concreta.

## 7. Estándar y dependencias

El generador corre en host (no en el target ARM), así que no tiene limitaciones de memoria/tamaño; sí conviene que la **salida generada** sea compatible con el estándar del proyecto destino (flag `--std=c++11|14|17`).

- Generador: **C++17**. `std::filesystem`, `std::optional`, `std::string_view`, structured bindings. Compilador estable y conocido en cualquier entorno.
- Dependencias mínimas y fijadas por versión: parser (tree-sitter y/o libclang), motor de plantillas (inja), CLI (CLI11), JSON/YAML para configuración (nlohmann/json o yaml-cpp), tests (GoogleTest).
- Gestión: CMake + `FetchContent` con *tags* fijos (o vcpkg/Conan con lockfile) para builds reproducibles.

Nota: el código generado para el target ARM debe respetar la política del proyecto (excepciones, RTTI, STL). Hacer configurable: `--no-exceptions` (stubs sin `throw`), `--no-rtti`, y evitar STL en stubs si el proyecto no la usa.

## 8. Estrategia de pruebas del propio generador

Es un generador de texto; la técnica más fiable es **golden files**:

1. **Unit tests del IR y de los extractores** (entrada: fragmento de C++ como string; salida: IR esperado).
2. **Golden tests de emisión**: IR → fichero esperado versionado en `tests/golden/`. Un diff cambia el test; la actualización de goldens es explícita (`--update-golden`).
3. **Test de round-trip** (el más valioso):
   - tests → `gen-impl` → compilar (debe compilar, tests en rojo).
   - código → `gen-tests` → compilar (debe compilar, tests en `SKIP`/`FAIL` controlado).
   - Ejecutar en CI con GCC y Clang.
4. **Corpus de casos límite**: plantillas, espacios de nombres anidados, `const`/`static`/`noexcept`, sobrecargas, herencia, operadores, `enum class`, punteros inteligentes.
5. **Sanitizers** (ASan/UBSan) en CI sobre el generador.

## 9. Estructura de repo propuesta

```
.
├── CMakeLists.txt
├── cmake/
├── include/ <proyecto>/
│   ├── ir/            # Class, Method, Type, TestCase
│   ├── frontend/      # tests_frontend, code_frontend
│   ├── backend/       # gtest_backend, qtest_backend, impl_backend
│   └── core/          # config, diagnostics, file merge
├── src/
├── templates/         # inja: gtest/, qtest/, impl/
├── apps/cli/          # main: gen-impl, gen-tests
├── tests/
│   ├── unit/
│   ├── golden/
│   └── roundtrip/     # proyectos CMake de ejemplo que se compilan
├── examples/
└── docs/              # convenciones soportadas (§4)
```

## 10. Configuración y CLI

```
tool gen-impl  --tests tests/foo_test.cpp --out src/ --framework gtest --std c++14
tool gen-tests --header include/foo.h     --out tests/ --framework qtest
```

Opciones comunes: `--dry-run` (imprime diff), `--overwrite=never|merge|force`, `--config tool.yaml`, `-I` / `--compile-commands`.
Configuración (`tool.yaml`): plantilla de nombres de fichero, namespace por defecto, estilo de includes, instanciaciones de plantillas, mapa de tipos (`unsigned` → `uint32_t`), política de excepciones.

## 11. Riesgos

| Riesgo | Mitigación |
|---|---|
| Inferencia incorrecta de tipos | Marcar origen inferido, `TODO`, fallar ruidosamente con `auto` |
| Sobrescritura de código del usuario | Modo por defecto no destructivo + marcadores de región generada |
| Parser semántico sobre código que no compila | tree-sitter para tests→código |
| Derivas de macros QTest/GTest entre versiones | Fijar versiones; golden tests por versión |
| Alcance inabarcable (plantillas, metaprogramación) | Declarar subconjunto soportado; el resto, error explícito |
| Tests generados que pasan sin valor | Aserciones placeholder que fallan o se saltan |

## 12. Hoja de ruta

1. **MVP gen-impl, GoogleTest**: clases simples, métodos con tipos inferidos de literales y aserciones, stubs, modo no destructivo, golden + round-trip.
2. **MVP gen-tests, GoogleTest**: clases no plantilla, un test por método público, fixture básica.
3. **Backend QTest** en ambas direcciones (slots, moc, `_data`).
4. Fixtures y tests parametrizados; mocks gmock desde interfaces.
5. Plantillas, herencia y espacios de nombres anidados.
6. Merge inteligente de ficheros existentes y configuración avanzada.

## 13. Decisiones abiertas (para decidir con un spike)

- ¿tree-sitter en ambas direcciones o libclang solo para código → tests? Prototipar ambos con 5–10 casos reales de tu proyecto y comparar errores.
- Granularidad del merge: ¿por región marcada o por AST?
- Formato de anotaciones `// @gen:` y si se admite también por fichero de configuración.
- Política con `auto` y con tipos no inferibles: ¿error o `TODO` + continuar?
