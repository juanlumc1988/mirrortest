# mirrortest — diseño técnico

Documento de referencia para la implementación. Parte de
[`analisis-tecnico.md`](analisis-tecnico.md) (citado como «AT §n»), lo revisa,
corrige lo que no se sostiene y cierra las decisiones que el análisis dejaba
abiertas. Si este documento y el análisis discrepan, **manda este**. El orden de
trabajo está en [`plan.md`](plan.md).

Las decisiones marcadas **[confirmado]** (R8, R15, R18, D1, D2, D6) fueron
validadas por el propietario del proyecto al cierre del diseño.

---

## 1. Revisión del análisis (double-check)

Hallazgos de la revisión. Los marcados *verificado* se han comprobado con código
real en este repositorio (spike de parser, cabeceras de Qt 5.15.13 y 6.4.2,
README de GoogleTest por versión).

| # | AT | Lo que dice | Problema | Resolución |
|---|---|---|---|---|
| R1 | §4 | Fixtures: `TEST_F(Fixture, …)` → generar `class Fixture : ::testing::Test` | En gen-impl la fixture es **entrada**: `TEST_F` exige que esté definida antes en el propio test. No se genera; se lee para tipar sus miembros. | El frontend de tests parsea las fixtures del fichero (§7.4). Solo gen-tests genera fixtures. |
| R2 | §4 | Stub = «valor de retorno por defecto, o `throw`» | Un valor por defecto puede hacer **pasar** el test (`EXPECT_EQ(v.size(), 0u)`, `EXPECT_FALSE(v.empty())`): rompe la fase *red* y contradice AT §5. | Política por defecto `throw`; sin excepciones `abort`; `default` solo opt-in y con aviso `mt-stub-may-pass` (§8.3). |
| R3 | §4 | — | Un destructor que lanza llama a `std::terminate` (implícitamente `noexcept`); un constructor que lanza oculta qué métodos fallan. | Constructores y destructores con cuerpo vacío; solo los métodos fallan. |
| R4 | §4 | `"a"` → `const char*` | En `EXPECT_EQ(foo.name(), "x")` un retorno `const char*` compara **punteros** (GoogleTest lo documenta). Compila y da un resultado sin sentido. | El contexto manda: en `EXPECT_EQ/NE` el literal de cadena da `std::string`; en `EXPECT_STR*` da `const char*`; en `QCOMPARE` da `QString` (§7.6). |
| R5 | §6 | `QCOMPARE` exige tipos idénticos | *Verificado*: Qt 5.15 y Qt 6.4 tienen `template<T1,T2> qCompare(const T1&, const T2&)` vía `==`. La exigencia era de versiones antiguas. | Se sigue infiriendo el tipo exacto del literal (evita `-Wsign-compare` y mantiene paridad con GTest), pero ya no es un requisito de compilación. |
| R6 | §6 | Excepciones: usar `QVERIFY_EXCEPTION_THROWN` | *Verificado*: deprecada desde Qt 6.3 en favor de `QVERIFY_THROWS_EXCEPTION(Tipo, expr)` — **orden de argumentos invertido** — que no existe en Qt 5. | El frontend reconoce ambas; el backend elige según `--qt 5|6` (§9.6). |
| R7 | §7 | Salida compatible con `--std=c++11` | *Verificado*: GoogleTest 1.12.x es la última con C++11; 1.13–1.16 exigen C++14; 1.17 exige C++17. Los tests generados para C++11 solo compilan con GTest ≤ 1.12. | Documentado. Se fija GTest 1.16.0 para poder probar salida C++14. El código de stubs (sin GTest) se prueba en C++11. |
| R8 | §3 | Decidir parser con un spike | *Verificado* (spike): tree-sitter-cpp 0.23.4 parsea GTest sin errores. QTest y cabeceras Qt **no**: `Q_OBJECT`, `slots`, `signals`, `Q_PROPERTY(…)` y macros de export generan nodos `ERROR` y se pierden declaraciones (11 errores en una cabecera típica). Con esas macros sustituidas por espacios: 0 errores. | tree-sitter en ambas direcciones + preprocesado de *blanking* que preserva offsets (§5.2). libclang queda como frontend opcional futuro. **[confirmado]** |
| R9 | §11 | — | tree-sitter no preprocesa: ve **todas** las ramas de `#if/#else`. Código embebido con `#ifdef TARGET_ARM` puede producir declaraciones duplicadas o contradictorias. | Se recorren todas las ramas; duplicados exactos se fusionan; variantes distintas se conservan con nota `mt-conditional` (§6.5). |
| R10 | §4 | Tipos de literales enteros | El tipo de un entero grande depende del modelo de datos: `long` es 32 bits en ARM32 (ILP32) y 64 en x86-64 (LP64). El generador corre en host pero el código es para ARM. | Nunca se deduce `long` por magnitud, solo por sufijo. Sin sufijo y fuera de `int` → `long long` + aviso. Sufijo `l` con valor > 32 bits → aviso de ARM32 (§7.5). |
| R11 | §4 | — | *Verificado*: tree-sitter lexea `-1` como un único `number_literal`, no como menos unario. | El clasificador de literales acepta signo. |
| R12 | §4 | `Foo foo(args);` | `Foo f();` es la *most vexing parse*: declara una función. *Verificado*: tree-sitter lo da como `function_declarator`. | Aviso `mt-vexing-parse`; no se infiere constructor. |
| R13 | §4 | `Foo::bar()` | *Verificado*: `Foo::create()` y `ns::create()` son sintácticamente idénticos (`namespace_identifier`). | Regla explícita de resolución de ámbito (§7.7). |
| R14 | §2 | IR con `Fixture*` y `Type{isConst,isRef,isPtr}` | Puntero crudo en un modelo de valores; los booleanos no representan `const char* const&`, puntero-a-const vs puntero-const, `&&`. Faltan `noexcept`, `= 0`, `override`, `= delete/default`, `explicit`, ref-qualifiers, roles Qt y procedencia. | IR en `include/mirrortest/ir/model.h` (§4): `spelling` autoritativo + campos derivados, `Origin`, `reason`, `SourceLoc`. Nombres en `snake_case` como el resto de tus repos. |
| R15 | §4, §12 | Merge «inteligente» en la fase 6, pero «es el requisito más importante» | Contradicción: sin merge, el bucle TDD (test nuevo → regenerar) acaba siempre en `.new` manual. | Merge por regiones marcadas en la fase 3, justo tras el MVP de gen-impl (§11). **[confirmado]** |
| R16 | §11 (merge) | — | Comparar por firma completa añadiría sobrecargas espurias cuando el usuario cambia `int` → `std::uint32_t`; y el tipo de retorno no distingue sobrecargas. | Clave de merge = (nombre, aridad); por tipos solo con anotación `overload`; diferencias de tipos o `const` → aviso, nunca un miembro nuevo (§11.2). |
| R17 | §2 | Plantillas inja editables «sin recompilar» | Si se cargan de disco, el binario depende de una ruta de instalación; y meter lógica en plantillas la deja sin tests. | Plantillas por defecto **embebidas** en el binario + `--templates DIR` para sobrescribir. La lógica (firmas, TODOs, valores por defecto) vive en C++ y llega a la plantilla ya formateada (§15). |
| R18 | §10 | `tool.yaml` | YAML añade yaml-cpp; nlohmann/json ya es obligatoria por inja. | Configuración en JSON en el MVP; el cargador está aislado para añadir YAML después. **[confirmado]** |
| R19 | §5 | Argumentos placeholder en los tests generados | `{}` es ambiguo entre `f(int)` y `f(double)` y no liga a `T&`. Además, **ejecutar** métodos de código legado con valores cero puede colgar o tocar hardware y tumbar todo el binario de tests. | Locales con nombre y tipo exacto; el placeholder (`FAIL()`/`QFAIL`) va **primero**: lo de después compila (comprueba la API) pero no se ejecuta (§9.2). |
| R20 | §5 | Tests sobre objeto `const` | — | `const Foo& const_sut = sut;` para métodos `const`; `static_assert(noexcept(…))` para `noexcept` (§9.2). |
| R21 | §4 | Funciones libres | Llamadas a macros propias (`MY_EXPECT_OK(x)`) o a la libc (`strlen`) se tomarían por funciones bajo test. | Identificadores TODO_MAYÚSCULAS = macros; lista de funciones C/estándar conocidas + `external_functions` en config (§7.8). |
| R22 | §8 | Stubs | Con `-Werror -Wunused-parameter` (habitual en embebido) los stubs no compilan. | Nombres de parámetro comentados en las definiciones: `int Foo::f(int /*value*/)`. |
| R23 | §2, §8 | Determinista | No se concretaba. | Sin timestamps ni rutas absolutas; orden estable (vectores, nunca `unordered_*` en emisión); LF; salto final; sin espacios finales. Los goldens lo verifican (§16). |
| R24 | §7 | Dependencias por `FetchContent` con tags | Los tags pueden moverse. *Verificado* en este entorno: los tarballs `/archive/` de GitHub están bloqueados por el proxy; `git clone` funciona. | Git superficial por tag **y** verificación del SHA del commit tras la descarga (falla el configure si no coincide). Modo offline con `FETCHCONTENT_SOURCE_DIR_*` (§17). *Implementado y probado.* |
| R25 | §6 | `GTEST_SKIP()`, `INSTANTIATE_TEST_SUITE_P` | Existen desde GTest 1.10; antes `INSTANTIATE_TEST_CASE_P`. | El frontend reconoce ambos nombres; el backend asume GTest ≥ 1.10. |
| R26 | §4 | Stubs con `throw` dan fase *red* | *Verificado* (GTest 1.16, Qt 6.4.2): GTest captura la excepción **por test** y sigue (todos rojos). QTest marca `FAIL! Caught unhandled exception` y **relanza** → `std::terminate`: los slots siguientes no se ejecutan. Sigue siendo rojo, pero solo se ve el primer fallo. | Se mantiene `throw` (rojo garantizado en ambos). Documentado para QTest (se puede ejecutar un slot concreto: `tst_foo nombreSlot`). Mejora futura: política `hook` (§8.3). El round-trip QTest verifica lo que de verdad ocurre (§16). |
| R27 | §8 | — | *Verificado*: con Clang, `-std=c++14 -Wpedantic -Werror`, el propio macro `TEST` de GTest 1.16 falla (`[[maybe_unused]]` es extensión C++17). Sin `-Wpedantic`, o en C++17, compila. | En el round-trip, los TU de test en C++14 se compilan con `-Wall -Wextra -Werror` (sin `-Wpedantic`); los stubs (sin GTest) sí con `-Wpedantic`. |

---

## 2. Decisiones

| # | Decisión | Motivo |
|---|---|---|
| D1 | **tree-sitter en ambas direcciones** (MVP). Interfaz de frontend preparada para un frontend libclang opcional (`MT_WITH_LIBCLANG`, futuro). **[confirmado]** | Una sola dependencia de parser, fijada y sin toolchain; tolera código que no compila; mismas utilidades de extracción de tipos en las dos direcciones. Para *esqueletos* basta la sintaxis: la cabecera declara explícitamente lo que hace falta. libclang exigiría include paths de Qt y del proyecto solo para leer firmas. |
| D2 | Nombre del binario `mirrortest`; namespace C++ `mt`; prefijo de anotaciones `// @mt:`; marcadores `// <mt:begin X>` / `// <mt:end X>`; TODOs `TODO(mt):`; códigos de diagnóstico `mt-*`. **[confirmado]** | Coherente con el repo; `@gen:` es demasiado genérico y podría colisionar. |
| D3 | Generador en C++17. Salida: el **mismo** código para todos los `--std`, válido desde C++11, salvo lo listado en §13.3. | Menos variantes = menos superficie de error. `--std` solo cambia lo que de verdad depende del estándar. |
| D4 | Política de stubs por defecto `throw std::logic_error`; `--no-exceptions` ⇒ `std::abort()`. | Fase *red* garantizada en GTest y QTest (ambos marcan el fallo; QTest aborta tras el primero, R26), y sin excepciones también. |
| D5 | `auto` sobre un resultado de la API bajo test ⇒ **error** por defecto (`--auto-policy=todo` lo degrada a aviso + tipo placeholder). | AT §11: «fallar ruidosamente». Un error claro con `fichero:línea` es mejor que un tipo inventado. |
| D6 | Placeholder por defecto en gen-tests: `FAIL()` / `QFAIL` (alternativa `--placeholder=skip`). **[confirmado]** | Más estricto y coherente con TDD; `skip` está disponible para no romper CI al generar en bloque sobre código legado. |
| D7 | Modo de escritura por defecto `--overwrite=never`: si el fichero existe y difiere, se escribe `<fichero>.new` y se avisa; si es idéntico, no se toca. | No destructivo e idempotente (AT §4). |
| D8 | Todo o nada: si hay cualquier diagnóstico de error, no se escribe ningún fichero. Escritura atómica (temporal + `rename`). | Nunca dejar un árbol a medio generar. |
| D9 | Configuración: defaults < `--config fichero.json` < línea de comandos. Sin descubrimiento implícito de ficheros de configuración. Claves desconocidas = error. | Predecible; las erratas en la config fallan en vez de ignorarse. |
| D10 | Guardas `#ifndef` por defecto (`"guard": "pragma-once"` opcional). | Portabilidad y convención habitual en embebido. |
| D11 | Todo texto generado (comentarios, `TODO(mt)`, mensajes de stubs y diagnósticos) en inglés. Documentación de diseño en español; README, comentarios del código y documentación de usuario en inglés. | Coherente con tus otros repositorios; el código generado acaba en proyectos de terceros. |

---

## 3. Arquitectura

```
                 ┌──────────────┐        ┌──────────────┐
  tests (.cpp) ─▶│ preprocess   │        │ preprocess   │◀─ cabeceras (.h)
                 │ (blanking)   │        │ (blanking)   │
                 └──────┬───────┘        └──────┬───────┘
                        ▼                       ▼
                 ┌──────────────┐        ┌──────────────┐
                 │ syntax::Tree │        │ syntax::Tree │   tree-sitter-cpp
                 └──────┬───────┘        └──────┬───────┘
                        ▼                       ▼
            ┌───────────────────────┐   ┌───────────────┐
            │ TestFrontend          │   │ CodeFrontend  │──┐ también lee
            │  recognizer (gtest/qt)│   │ (headers→IR)  │  │ fixtures y
            │  → UsageFacts         │   └───────┬───────┘  │ cabeceras
            │  → Inference          │◀──────────┼──────────┘ existentes
            └──────────┬────────────┘           │
                       ▼                        ▼
                ┌──────────────────────────────────────┐
                │   ir::Model   (clases, funciones)    │
                └──────┬────────────────────┬──────────┘
                       ▼                    ▼
              ┌────────────────┐   ┌─────────────────┐
              │ ImplBackend    │   │ TestPlanner     │ → ir::TestPlan
              │ (.h + .cpp)    │   └────────┬────────┘
              └───────┬────────┘   ┌────────┴────────┐
                      │            │ GTest / QTest   │
                      │            │ backends        │
                      │            └────────┬────────┘
                      ▼                     ▼
                ┌──────────────────────────────────────┐
                │ OutputPlan  (ruta, contenido, acción)│  ← puro, en memoria
                └──────────────────┬───────────────────┘
                                   ▼
                ┌──────────────────────────────────────┐
                │ Writer: never/merge/force, .new,     │  ← única capa con E/S
                │ dry-run (diff), check, atómico       │
                └──────────────────────────────────────┘
```

Principios:

- **Núcleo puro, E/S en los bordes.** Todo lo anterior al `Writer` son
  funciones de datos a datos. Golden tests, `--dry-run`, `--check` y merge
  salen gratis de ahí.
- **Diagnósticos acumulados**, no excepciones, para errores de la *entrada*
  (§12). Las excepciones quedan para fallos internos o de E/S.
- **La CLI también está en la librería**: `mt::cli::run(args, env)` devuelve el
  código de salida; `apps/cli/main.cpp` solo la llama. Los goldens ejecutan la
  CLI real en proceso. `env` lleva el directorio base para rutas relativas y los
  `std::ostream` de salida y error: nada de `chdir` ni de estado global.

### 3.1 Módulos y ficheros

```
include/mirrortest/
  ir/          model.h  type.h  classify.h        IR, ortografía de tipos, clasificación (by-value, includes)
  syntax/      tree.h  preprocess.h  extract.h    tree-sitter RAII, blanking, utilidades de nodos/tipos
  core/        diagnostics.h  config.h  naming.h  output.h  writer.h  diff.h  merge.h  templates.h  source.h
  frontend/    code_frontend.h  test_frontend.h  facts.h  literals.h  annotations.h  inference.h
  backend/     impl_backend.h  test_planner.h  gtest_backend.h  qtest_backend.h
  cli/         run.h
src/<mismos módulos>.cpp
templates/     impl/{header,source}.inja   gtest/test_file.inja   qtest/test_file.inja
apps/cli/main.cpp
tests/unit/  tests/golden/  tests/roundtrip/
cmake/       MtDependencies.cmake  MtEmbedFiles.cmake
```

Una sola librería estática `mt_core`; el ejecutable `mirrortest`; ejecutables de
test `mt_unit_tests` y `mt_golden_tests`; los round-trip son scripts CTest.

---

## 4. IR

Fuente: [`include/mirrortest/ir/model.h`](../include/mirrortest/ir/model.h) (ya
implementado; es el contrato entre módulos). Puntos clave:

- `Type::spelling` es **autoritativo**: es lo que se emite. Los campos
  descompuestos (`base`, `is_const`, `ref`, `pointer_depth`) sirven para
  decisiones (paso por valor o `const&`, valor por defecto) y solo describen las
  formas comunes; lo exótico conserva su `spelling` y se trata de forma
  conservadora.
- `normalize_spelling()` solo toca espacios, nunca reordena (`T const&` se
  respeta). `make_type()` construye la forma canónica. Ambos implementados y
  con tests (`src/ir/type.cpp`).
- `Origin` ordena la confianza: `Defaulted < Inferred < Declared < Annotated`.
  Ante conflicto gana el mayor y se avisa.
- `Method::notes` → comentarios `// TODO(mt): …` encima de la declaración.
- `TestPlan`/`Stmt` es neutral: el código de *arrange/act* es C++ válido en
  ambos frameworks y viaja como texto; lo que depende del framework
  (placeholder, aserciones) es un `Stmt::Kind` propio que pinta cada backend.

---

## 5. Capa sintáctica

### 5.1 `syntax::Tree` / `syntax::Node`

Implementado (`include/mirrortest/syntax/tree.h`). `Tree` posee árbol y copia del
fuente (tras `unique_ptr`, así mover el `Tree` no invalida los `string_view` de
los `Node`). `parse_cpp()` nunca falla por entrada mala (da nodos `ERROR`); solo
lanza si el runtime rechaza la gramática (ABI). `tests/unit/test_syntax_tree.cpp`
fija las formas de nodo de las que dependen los frontends (resultado del spike).

### 5.2 Preprocesado: *blanking* que preserva offsets

`syntax/preprocess.h`:

```cpp
struct MacroTable { std::vector<std::string> ignore; };          // config.ignore_macros
struct QtMarker { enum class Kind { QObject, Gadget, SlotsSection, SignalsSection,
                                    Invokable, DisableCopy };
                  Kind kind; unsigned offset; };
struct BlankedRange { unsigned begin, end; };                      // [begin, end)
struct Preprocessed { std::string text; std::vector<QtMarker> markers;
                      std::vector<BlankedRange> blanked; };
Preprocessed preprocess(std::string_view source, const MacroTable& table = {});
```

Implementado en `include/mirrortest/syntax/preprocess.h`.

Reglas (todas con tests unitarios):

1. **Longitud idéntica** a la entrada; los `\n` (y `\r`) dentro de un tramo
   borrado se conservan → líneas y columnas de los diagnósticos siguen siendo
   las del fichero original. Invariante comprobado en cada test:
   `out.text.size() == in.size()` y mismas posiciones de `\n`.
2. BOM UTF-8 inicial → 3 espacios.
3. Escáner léxico mínimo que **no toca**: comentarios `//` (con continuación
   `\`) y `/* */`; literales de cadena y carácter con escapes; *raw strings*
   `R"delim(…)delim"` con prefijos `u8R`, `uR`, `UR`, `LR`; líneas de
   preprocesador (`#` como primer carácter no blanco, con continuaciones).
4. Solo se reconocen identificadores completos (límites de token).
5. Tabla de macros por defecto:

   | Macro | Tratamiento |
   |---|---|
   | `Q_OBJECT` | espacios; marcador `QObject` |
   | `Q_GADGET` | espacios; marcador `Gadget` (un gadget **no** es un `QObject`: sin señales ni slots) |
   | `Q_NAMESPACE`, `Q_REQUIRED_RESULT`, `Q_DECL_DEPRECATED` | espacios |
   | `Q_INVOKABLE` | espacios; marcador `Invokable` |
   | `slots`, `Q_SLOTS` | solo si va seguido de un único `:` (espacios/tabuladores entre medias) **y** el token anterior es `public`/`protected`/`private` → espacios; marcador `SlotsSection` en el offset de ese especificador. En cualquier otro caso (`int slots = 0;`, `c ? slots : x`) no se toca. |
   | `signals`, `Q_SIGNALS` | solo si va seguido de un único `:` y el token anterior es un especificador de acceso o un límite de miembro (`{` `}` `;` `:`). Tras un especificador (`public signals:`) → espacios, marcador en el especificador. Tras un límite → `public` + relleno (la sección debe seguir viendo un especificador de acceso), marcador en el offset de `signals`. |
   | `Q_DECL_OVERRIDE`, `Q_DECL_FINAL`, `Q_DECL_NOEXCEPT`, `Q_DECL_CONSTEXPR`, `Q_NULLPTR` | `override`, `final`, `noexcept`, `constexpr`, `nullptr` + relleno |
   | `Q_PROPERTY`, `Q_ENUM`, `Q_ENUMS`, `Q_FLAG`, `Q_FLAGS`, `Q_CLASSINFO`, `Q_INTERFACES`, `Q_DECLARE_METATYPE`, `Q_DECLARE_PRIVATE`, `Q_DECLARE_PUBLIC`, `Q_PRIVATE_SLOT`, `Q_DECL_DEPRECATED_X`, `Q_DISABLE_COPY`, `Q_DISABLE_COPY_MOVE` | tipo función: identificador + `( … )` balanceado (respetando cadenas, comentarios y *raw strings* dentro) → espacios. Solo se admiten **espacios o tabuladores** antes del `(`, no saltos de línea: tras `MY_ATTR⏎(void)f();` el `(` es una sentencia, no los argumentos. `Q_DISABLE_COPY*` deja marcador `DisableCopy`. Si no sigue `(`, o el paréntesis no se cierra, solo se borra el identificador. |
   | `config.ignore_macros` | objeto o función según siga `(` (misma regla) |
   | Macro de export: identificador `[A-Z][A-Z0-9_]*` entre `class`/`struct` y otro identificador que no sea `final` (`class FOO_EXPORT Widget :`) | espacios. `class FOO final` y `class FOO {` conservan `FOO`: es el nombre. |

6. Los marcadores se asocian después por rango: `QObject`/`Gadget` → el
   `class_specifier` que lo contiene; `SlotsSection`/`SignalsSection` → el
   `access_specifier` en ese offset; `Invokable` → la siguiente declaración de
   miembro; `DisableCopy` → el `class_specifier` que lo contiene.
7. Un macro blanqueado es invisible para el escáner: no cambia cuál fue «el
   token anterior» (así `Q_OBJECT` entre `{` y `signals:` no impide reconocerlo).
8. Comprobado con ASan/UBSan sobre 21 000 entradas truncadas y corrompidas
   (invariantes de longitud y saltos de línea; sin accesos fuera de rango). El
   test unitario `PreprocessRobustness` repite una parte con semilla fija.

### 5.3 Extracción de tipos desde nodos (`syntax/extract.h`)

Implementado. Un tipo se lee como **texto**, no reconstruyéndolo de la forma del
árbol de declaradores (que es del revés: en `const char* const names[3]` el
nombre está en medio de su propio tipo):

    tipo   = calificadores y especificador de tipo, en orden de fuente
    sufijo = texto del declarador sin el nombre ni la parte de función

así `const char* const names[3]` es `const char` + `* const[3]`. Vale para
cualquier forma —incluidas las que nadie previó— y conserva exactamente lo que
escribió el usuario. Los campos estructurados de `ir::Type` (`is_const`, `ref`,
`pointer_depth`) solo se rellenan si el sufijo es del tipo simple (`*`, `&`,
`const`); para punteros a función, arrays y punteros a miembro el `spelling` es
exacto y no se afirma nada más.

Formas de nodo de tree-sitter-cpp 0.23.4 de las que depende (verificadas con el
árbol real, fijadas en `test_extract.cpp`):

- `virtual` es un token **anónimo**: se busca entre *todos* los hijos
  (`has_token`). `...` en `f(int, ...)` también lo es y se perdía en silencio
  (`f(int, ...)` se leía como `f(int)`); ahora es un parámetro de tipo `...`.
- `int (*cb)(int)` es un `function_declarator` cuyo declarador es un
  `parenthesized_declarator`: **parece un método y es una variable**
  (`is_function_pointer`, sin `function`).
- Una clase/enum anidado es un `field_declaration` con `struct_specifier` /
  `enum_specifier` como tipo y **sin declarador**.
- `reference_declarator` no etiqueta su declarador interno (`inner_declarator`
  usa el último hijo nombrado); un parámetro sin nombre lleva
  `abstract_*_declarator`.
- `int const x` pone `type_qualifier` *después* del tipo; el orden se conserva.
- `constexpr` y `mutable` son `type_qualifier` pero no forman parte del tipo;
  `static`/`inline`/`extern` son `storage_class_specifier`.
- Un `noexcept(expr)` no constante no se afirma: solo `noexcept` y
  `noexcept(true)` cuentan.
- Un `init_declarator` (`int x = 3`) lleva el valor aparte: no es parte del tipo.
- Packs: `Args&&... args` → `variadic_declarator` anidado; el sufijo conserva
  los `...` (`Args&&...`).

## 6. Frontend de código (cabeceras → `ir::Model`)

Implementado en `frontend/code_frontend.h` (`read_declarations`, `read_header`).
Se usa en tres sitios: entrada de gen-tests; cabeceras existentes en gen-impl
(API ya conocida + merge); fixtures y clases de test dentro de los tests. Por eso
lee **declaraciones**, no cabeceras: sirve cualquier unidad de traducción.

### 6.1 Recorrido

- `translation_unit` → `namespace_definition` (incluido `nested_namespace_specifier`
  `a::b`), `linkage_specification` (`extern "C"`, con o sin llaves),
  `preproc_if`/`preproc_ifdef`/`preproc_elif`/`preproc_else` (todas las ramas, §6.5),
  `preproc_include` (ruta sin delimitadores + `is_system`).
  Namespaces anónimos: se omiten (enlace interno, no testeables desde fuera).
- `class_specifier`/`struct_specifier` **con cuerpo** → `Class`. Sin cuerpo
  (declaración adelantada, `class Foo* f();`) → ignorar. `union` → ignorar.
  `struct S { … } s;` define `S` además de declarar `s`. Dentro de
  `template_declaration` → `template_params` (texto entre `<>`, p. ej.
  `typename T, int N = 4`).
- `enum_specifier` **con nombre y cuerpo** → `Enum` (`is_scoped` por el token
  anónimo `class`/`struct`; `underlying` por el campo `base`; enumeradores por
  su campo `name`). Anónimos y adelantados, ignorados.
- `declaration`/`function_definition` en ámbito de namespace con
  `function_declarator` (no puntero a función) → `Function` (incluye `static`,
  `inline`, operadores y retorno final). **No** son funciones: variables,
  punteros a función, `int Foo::bar() {}` (define un miembro), plantillas de
  función (aviso `mt-unsupported` y se omiten).
- Una clase leída dos veces (ramas `#if`, o dos llamadas) es **una** clase: se
  funden los miembros y no se repite una firma ya presente (nombre + tipos de
  parámetros + `const` + ref-qualifier); tampoco bases, campos ni tipos anidados.

### 6.2 Miembros (formas verificadas con el árbol real)

| Forma tree-sitter | Resultado |
|---|---|
| `field_declaration` / `declaration` / `function_definition` con `function_declarator` que no es puntero a función | método (uno por declarador) |
| `declaration` cuyo declarador es el nombre de la clase y sin tipo | `Constructor`; `explicit_function_specifier` → `is_explicit` |
| `destructor_name` (`~A`) | `Destructor` |
| `operator_cast` (`explicit operator bool() const`, `operator const char*() const`) | `Conversion`, nombre `operator <tipo destino>`, sin `ret` |
| nombre que empieza por `operator` seguido de algo que no continúa un identificador | `Operator`; el nombre se normaliza (`operator ==` → `operator==`, `operator  new [ ]` → `operator new[]`); `operatorX` es un método normal |
| `delete_method_clause` / `default_method_clause` | `is_deleted` / `is_defaulted` |
| `default_value` = `0` | `is_pure` |
| `virtual` (token anónimo), `static`, `virtual_specifier` `override`/`final`, `const`, `noexcept`, ref-qualifier | flags |
| `template_declaration` dentro de la clase | método con `is_template` |
| `field_declaration` con declarador no-función (incluye `int (*cb)(int)`, arrays, bitfields, `static constexpr`) | `Field` (uno por declarador: `int a, *b;`) |
| `field_declaration` sin declarador cuyo tipo es clase/enum, `type_definition`, `alias_declaration` | nombre a `nested_types` (el cuerpo de una clase anidada no se lee: fase 7) |
| `friend_declaration`, `using_declaration`, `static_assert_declaration` | no son miembros |

Un método ya presente (misma firma) no se añade dos veces. El tipo de retorno es el
declarado o el de `-> T`; `auto` sin retorno final queda como `auto`.

Acceso: `private` por defecto en `class`, `public` en `struct`; cada
`access_specifier` lo cambia. **Qt** (con los marcadores del preprocesado, §5.2):
`SlotsSection`/`SignalsSection` en el offset de un `access_specifier` →
`QtRole::Slot`/`Signal` hasta el siguiente especificador; un `Invokable` entre el
final del miembro anterior y el inicio de este → `QtRole::Invokable`; `QObject` →
`is_qobject`, `Gadget` → `is_gadget`, `DisableCopy` → `disable_copy`, siempre que el
marcador esté en la clase y no dentro de una clase anidada.

### 6.3 Clases «construibles»

Una clase es *default-constructible* si: no declara constructores, o declara uno
público, no borrado y sin parámetros obligatorios; y no es abstracta (ningún
`is_pure`). `Q_DISABLE_COPY` no afecta a esto (solo a copiar). La usa el
planner (§9.2).

### 6.4 No soportado (diagnóstico explícito, nunca silencio)

Plantillas de función (`mt-unsupported`, aviso), especializaciones, miembros
generados por macros no reconocidas, herencia de constructores
(`using Base::Base;`), `concept`/`requires`. Las plantillas de clase se leen y
llevan `template_params`; el planner decide qué hacer con ellas.

### 6.5 Compilación condicional

Se recorren todas las ramas. Declaraciones con la misma firma se fusionan (gana
la primera en orden de fuente). Pendiente (tarea 4.1): si dos ramas declaran el
mismo (nombre, aridad) con firmas distintas, se conservan ambas y se emite la
nota `mt-conditional` con las dos líneas: es el usuario quien sabe qué
configuración aplica.

### 6.6 Errores de parseo

El parser siempre da un árbol. Cada nodo `ERROR` (el más externo; hasta 10, luego
una nota «and N more parse errors») es un aviso `mt-parse` con su posición, y se
lee lo que lo rodea: una cabecera que casi parsea es mucho más útil que ninguna.

---

## 7. Frontend de tests (tests → `ir::Model`)

### 7.1 Tubería

1. `preprocess` (§5.2) → `parse_cpp`.
2. Detección de framework (`--framework auto`): GTest si incluye
   `gtest/gtest.h`/`gmock/gmock.h` o usa `TEST`/`TEST_F`/`TEST_P`; QTest si
   incluye `QtTest`/`QTest`/`QtTest/QtTest` o usa `QTEST_*MAIN` o tiene
   secciones `slots`. Ambos o ninguno → error `mt-framework` (pedir
   `--framework`).
3. Recolección de **cuerpos a analizar**: todo cuerpo de función del fichero
   (`TEST*`, métodos de fixture incluidos `SetUp`/`TearDown`/constructor,
   *slots* de QTest dentro y fuera de la clase, funciones auxiliares), cada uno
   con su ámbito (§7.4).
4. Recorrido de sentencias con tabla de símbolos → **hechos** (`facts.h`).
5. Clasificación de nombres (§7.7) → qué clases/funciones hay que generar.
6. **Inferencia** (`inference.h`): hechos → `ir::Model` con `Origin`, `reason`,
   `notes` y diagnósticos.

Separar hechos de inferencia permite testear cada mitad por separado:
*fragmento de C++ → hechos esperados* y *hechos → modelo esperado*.

### 7.2 Hechos

```cpp
struct ValueInfo {            // lo que se sabe de una expresión usada como argumento
    std::optional<ir::Type> type;   // con origin/reason/where
    std::string text;               // texto fuente, para diagnósticos y nombres
    bool is_lvalue = false, is_const = false, is_moved = false, is_address_of = false;
    std::string var_name;           // si es un identificador: nombre del parámetro a generar
};
enum class UseKind { Discarded, Compared, Condition, Assigned, AutoBound, Argument, Unknown };
struct ResultUse { UseKind kind; std::optional<ir::Type> hint; ir::SourceLoc where; };
struct CallFact   { std::string cls; std::vector<std::string> ns; std::string method;
                    std::vector<ValueInfo> args; bool on_const = false; bool is_static = false;
                    ResultUse use; ir::SourceLoc where; std::vector<Annotation> annotations; };
struct CtorFact   { std::string cls; std::vector<std::string> ns; std::vector<ValueInfo> args;
                    bool copy_init = false; ir::SourceLoc where; };
struct FieldFact  { std::string cls; std::string field; ResultUse use; bool on_const; ir::SourceLoc where; };
struct FreeCallFact { std::vector<std::string> ns; std::string name; std::vector<ValueInfo> args;
                      ResultUse use; ir::SourceLoc where; };
struct TypeUseFact  { std::string name; std::vector<std::string> ns; ir::SourceLoc where; };
```

### 7.3 Tabla de símbolos

Ámbitos anidados por `compound_statement`; se declara en orden. Fuentes:
declaraciones locales (`T v;`, `T v(…)`, `T v{…}`, `T v = …`), parámetros de
funciones auxiliares, `QFETCH(T, v)` (texto: se parte en la **última coma de
nivel superior**; *verificado*: el nodo varía según `T` sea primitivo o no), miembros
de fixture/clase de test, `for (T x : …)`. Cada símbolo guarda el tipo declarado
completo (para saber `const`, referencia, puntero, puntero inteligente).

Receptor de una llamada `x.m()` / `x->m()`:
- `x` local/miembro de tipo `T`, `T&`, `const T&` → clase `T`, `on_const` si
  `const`.
- `x->m()` con `T*`, `const T*`, `std::unique_ptr<T>`, `std::shared_ptr<T>`,
  `QScopedPointer<T>`, `QSharedPointer<T>`, `QPointer<T>` → `T`.
- `(*p).m()` igual que `p->m()`.
- Receptor desconocido → nota `mt-unknown-receiver`, se ignora.
- Llamada encadenada `a.b().c()` → el tipo de `a.b()` es una clase desconocida:
  `mt-chained-call` con la política de tipo desconocido (§7.9).

### 7.4 Ámbitos por tipo de cuerpo

| Cuerpo | Símbolos visibles además de los locales |
|---|---|
| `TEST(S, N)` | globales del fichero |
| `TEST_F(F, N)`, `TEST_P(F, N)` | miembros de la clase `F` definida en el fichero (si no está: `mt-unknown-fixture`, aviso); `GetParam()` tipado por `TestWithParam<T>` |
| métodos de la fixture | miembros de la fixture |
| slots/métodos de la clase QTest | miembros de la clase de test |

### 7.5 Clasificador de literales (`literals.h`)

| Literal | Tipo |
|---|---|
| entero sin sufijo, cabe en 32 bits con signo | `int` |
| entero sin sufijo decimal fuera de `int` | `long long` + aviso `mt-literal-range` (en ARM32 y x86-64 el tipo real difiere) |
| hex/oct/bin sin sufijo en `(INT_MAX, UINT_MAX]` | `unsigned int` (igual en ILP32 y LP64) |
| hex/oct/bin sin sufijo > `UINT_MAX` | `long long` si cabe, si no `unsigned long long`; + aviso (el estándar da `long`/`unsigned long` en LP64 y `long long` en ILP32) |
| sufijos `u` · `l` · `ul`/`lu` · `ll` · `ull`/`llu` | `unsigned int` · `long` · `unsigned long` · `long long` · `unsigned long long`; `l`/`ul` con valor fuera de 32 bits → aviso «no cabe en `long` de ARM32» |
| `z` · `uz`/`zu` (C++23) | `std::ptrdiff_t` (nota: tipo con signo correspondiente a `size_t`) · `std::size_t`; si `--std` < 23 → aviso |
| flotante sin sufijo · `f` · `l` | `double` · `float` · `long double` |
| `f16`, `bf16`, … (C++23) | no soportado → `mt-literal` |
| signo `-`/`+` incluido en el token (R11) | conserva el tipo (`-1u` sigue siendo `unsigned int`) |
| separadores de dígitos `'` | se ignoran |
| `'a'` · `u8'a'` · `u'a'` · `U'a'` · `L'a'` · `'ab'` | `char` · `char` (≤C++17) / `char8_t` · `char16_t` · `char32_t` · `wchar_t` · `int` + aviso |
| cadena `"…"`, `R"(…)"`, concatenadas | «cadena de `char`»: el tipo final lo decide el contexto (§7.6) |
| `u8"…"` · `u"…"` · `U"…"` · `L"…"` | `const char*` (≤17) / `const char8_t*` · `const char16_t*` · `const char32_t*` · `const wchar_t*` |
| `true`/`false` · `nullptr` | `bool` · puntero de tipo desconocido (`mt-unknown-param`) |
| `"…"s` · `"…"sv` | `std::string` · `std::string_view` |
| enteros con `h`/`min`/`s`/`ms`/`us`/`ns` | `std::chrono::hours` … `nanoseconds` |
| otros literales de usuario | `mt-literal` |
| `static_cast<T>(e)`, `(T)e`, `T(e)`, `T{e}` | `T` (`Declared`) |
| `sizeof(…)` | `std::size_t` |
| enumerador de enum conocido `E::X` | `E` |

Implementado en `frontend/literals.h` (`classify_number`, `classify_character`,
`classify_string`, `classify_literal`). Trabaja sobre el **texto** del literal, de
modo que se prueba sin parser; `classify_literal` despacha por tipo de nodo
(`number_literal`, `user_defined_literal`, `char_literal`, `string_literal`,
`raw_string_literal`, `concatenated_string`, `true`/`false`, `null`) y fija la
posición del tipo. Detalles fijados al implementarlo:

- Cada tipo inferido lleva `Origin::Inferred` y `reason = "literal <texto>"`, que
  es lo que acaba en `// TODO(mt): … inferred from literal 3u`.
- tree-sitter lexea `-1`, `+2` y `-1u` como **un** `number_literal`, pero `- 3` es
  un `unary_expression`: ese caso lo resuelve el tipado de expresiones (2.4). Un
  `-1u` sigue siendo `unsigned int`; `-2147483648` es `long long`, porque
  `2147483648` no cabe en `int` pase lo que pase delante.
- Entero `u` por encima de `UINT_MAX` → `unsigned long long` + `mt-literal-range`
  (el estándar daría `unsigned long` en LP64). `l` solo da `long`; si el valor no
  cabe en 32 bits, aviso de ARM32. Overflow de `unsigned long long` → ese tipo +
  `mt-literal` «demasiado grande». Un sufijo inválido o definido por el usuario
  (`3f`, `3_km`, `1.5s`, `1.5f16`) es `Unsupported` + `mt-literal`, sin tipo.
- `0x1e5` es el entero 485, no un flotante: en hexadecimal el exponente es `p`.
- Cadena ordinaria (`"x"`, `R"(x)"`, `"a" "b"`): `Kind::String` **sin tipo**; lo
  decide el contexto (§7.6). Con prefijo (`L u U u8`) es un puntero tipado; con
  sufijo `s`/`sv`, el `std::basic_string[_view]` del carácter correspondiente.
- Las características de un estándar mayor que `--std` (separadores `'` y `0b`:
  C++14; `"x"s` y chrono: C++14; `"x"sv` y `u8'a'`: C++17; sufijo `z`: C++23)
  dan `mt-std`, no un tipo distinto.
- `character_count` cuenta cada escape (`\x41`, `\101`, `\u00e9`) y cada punto de
  código UTF-8 como un carácter: `'\x41'` es `char`, `'\x41\x42'` es `int`.
- No es de este módulo: `static_cast<T>(e)`, `(T)e`, `T(e)`, `T{e}`, `sizeof(…)`,
  `E::X`. Son expresiones con tipo propio; las tipa el frontend de tests (2.4).

### 7.6 Contexto de uso → pista de tipo

| Contexto | Pista para el resultado / argumento |
|---|---|
| `EXPECT_/ASSERT_EQ, NE, LT, LE, GT, GE (a, b)` | cada lado toma el tipo del otro; cadena → `std::string` |
| `EXPECT_/ASSERT_TRUE/FALSE(c)` | `bool` |
| `EXPECT_/ASSERT_STREQ, STRNE, STRCASEEQ, STRCASENE` | `const char*` (`const wchar_t*` si el literal es `L"…"`) |
| `EXPECT_/ASSERT_FLOAT_EQ` · `DOUBLE_EQ` · `NEAR(a,b,eps)` | `float` · `double` · `double` |
| `EXPECT_/ASSERT_THROW(stmt, E)`, `ANY_THROW`, `NO_THROW`, `EXPECT_DEATH(stmt, re)` | se analiza `stmt` (puede ser un bloque `{…}`); su resultado es `Discarded` |
| `EXPECT_THAT(v, matcher)` | sin pista (`mt-unknown-return`); matchers: fase posterior |
| `… << "msg"` | se desenvuelve el operando izquierdo |
| `QCOMPARE(a, b)`, `QTRY_COMPARE*` | cada lado el del otro; cadena → `QString` |
| `QVERIFY(c)`, `QVERIFY2(c, m)`, `QTRY_VERIFY*` | `bool` |
| `QVERIFY_EXCEPTION_THROWN(stmt, E)` · `QVERIFY_THROWS_EXCEPTION(E, stmt…)` · `QVERIFY_THROWS_NO_EXCEPTION(stmt)` | se analiza `stmt` (ojo: orden invertido entre las dos primeras) |
| `if`/`while`/`for` (condición), `!x`, `x && y`, `x \|\| y`, `c ? … : …` | `bool` |
| `x == lit`, `!=`, `<`, `<=`, `>`, `>=` | tipo del literal |
| `T v = call;`, `T v(call);`, `T v{call};` | `T` sin `const`/`&` de nivel superior; si `T` es `U&` no-const → retorno `U&` (un temporal no liga) |
| `v = call;` con `v` declarado `T` | `T` |
| `auto v = call;` (también `auto&`, `const auto&`) | `AutoBound` → política `auto` (D5) |
| sentencia de expresión `call;` | `Discarded` → `void` + aviso `mt-unused-result` |
| `GTEST_SKIP`, `FAIL`, `SUCCEED`, `ADD_FAILURE`, `QFAIL`, `QSKIP`, `QEXPECT_FAIL`, `QWARN` | se ignoran |

### 7.7 Clasificación de nombres

**Candidato a clase**: aparece como tipo de una declaración/miembro/`QFETCH`, en
`new N(…)`, como argumento de plantilla de `make_unique`/`make_shared`/puntero
inteligente, como `N(…)`/`N{…}` siendo además usado como tipo en otro sitio, o
como ámbito de una llamada `N::m()` (§ abajo).

**Externo** (nunca se genera): declarado en una cabecera existente analizada
(§7.10); en `std::`, `testing::`, `QTest::`; patrón Qt `^Q[A-Z]`; tipos
fundamentales y de `<cstdint>`/`<cstddef>`; declarado en el propio fichero de
test (fixtures, structs auxiliares); parámetro de plantilla;
`config.external_types`.

**A generar** = candidatos − externos.

Ámbito de `X::m(args)` (R13):
1. `X` es una clase conocida o a generar → método `static` de `X`.
2. `X` aparece en `namespace X {…}` o `using namespace X;` del fichero, o como
   calificador de una clase (`X::Foo`) → función libre en el namespace `X`.
3. Si no: `X` empieza por mayúscula → `static` con aviso `mt-ambiguous-scope`;
   si no, función de namespace con el mismo aviso.

Namespaces de las clases a generar: `ns::Foo` → `ns`; tests dentro de
`namespace ns { … }` → `ns`; exactamente un `using namespace ns;` (no `std`)
→ `ns` con nota; varios → global + aviso; `config.default_namespace`;
anotación `class Foo namespace ns`.

### 7.8 Funciones libres

Llamada no calificada a un identificador que no es: macro (TODO_MAYÚSCULAS,
R21), definida/declarada en el fichero, conocida de cabeceras, de la lista de
funciones C/estándar (`strlen`, `memcpy`, `abs`, `printf`, …) o de
`config.external_functions` → función libre a generar, con nota.

### 7.9 Inferencia (hechos → modelo)

- **Unificación**: los usos de un método se agrupan por (clase, nombre,
  aridad). Distinta aridad = sobrecarga distinta. Misma aridad y tipos de
  argumento distintos → **un** método, gana el de mayor `Origin` y, a igualdad,
  el primero en orden de fuente; aviso `mt-conflict` con las alternativas. Para
  sobrecargas por tipo: anotación `overload`.
- **Parámetros** desde `ValueInfo`:
  - literal → su tipo (por valor); cadena → `config.string_literal_param`
    (por defecto `const char*`).
  - variable de tipo `T`: si `T` es *by-value* (abajo) → `T`; si no `const T&`.
  - `std::move(x)` → `T` por valor (nota); `&x` → `T*`.
  - resultado de otra llamada de la API ya inferida → su tipo (pasada de
    punto fijo, máx. 4 iteraciones; fase 2, tarea propia).
  - desconocido → política (D5): `error` (`mt-unknown-param`) o `todo`
    (`config.placeholder_type`, por defecto `int`, con TODO).
  - nombre del parámetro: el de la variable si el argumento es un identificador;
    si no `arg1…argN`; duplicados → sufijo numérico.
- **Retorno**: pista de mayor `Origin`; solo `Discarded` → `void` + aviso;
  `AutoBound`/`Unknown` sin otra pista → política D5. Conflicto → `mt-conflict`.
- **`const`**: si **algún** uso es sobre un acceso `const`. Sin evidencia, no
  `const` (no se inventa; en métodos de 0 argumentos cuyo resultado se compara
  se añade la nota `TODO(mt): consider making this const`).
- **`static`**: si algún uso es `X::m()` (§7.7).
- **Constructores**: un ctor por aridad usada; constructor por defecto solo si
  se usa *y* hay otro ctor declarado (si no, el implícito basta). Un solo
  parámetro → `explicit` salvo que se observe inicialización por copia
  (`Foo f = 5;`). Copia/movimiento desde otro `Foo` → implícitos, nada que
  generar.
- **Campos**: `obj.x` no llamado → campo público, tipo por el contexto
  (`obj.x = 5;`, comparación). Brace-init con argumentos se trata siempre como
  constructor (no agregado) en el MVP.
- **By-value**: tipos fundamentales (todas las combinaciones), `std::size_t`,
  `size_t`, `std::ptrdiff_t`, `std::nullptr_t`, `std::byte`, `(std::)(u)intN_t`,
  `(u)int_fastN_t`, `(u)int_leastN_t`, `(u)intptr_t`, `(u)intmax_t`, `qint*`,
  `quint*`, `qreal`, `qsizetype`, `uchar`, `ushort`, `uint`, `ulong`, punteros,
  enums conocidos, `std::string_view`, `QStringView`, `std::chrono::*`, y
  `config.by_value_types`. Resto → `const T&`.

### 7.10 Cabeceras existentes

Cada `#include "…"` del test se busca en: directorio del test, `-I…` (en orden),
`--out-include`. Si existe, se analiza con el frontend de código (un nivel, sin
recursión en el MVP): sus clases/funciones/enums pasan a **externos conocidos**
(tipados: aportan firmas `Declared`). Si la cabecera encontrada está en
`--out-include`, es un **objetivo existente** → merge/never (§10, §11).

### 7.11 Anotaciones (`// @mt:`)

```
anotación  := "// @mt:" directiva { ";" directiva }
directiva  := "returns" TIPO                         (corta: aplica a la siguiente sentencia)
            | "overload" | "ignore"                  (corta)
            | MIEMBRO "returns" TIPO                 (larga: ámbito de fichero)
            | MIEMBRO "param" N TIPO                 (N empieza en 1)
            | MIEMBRO ("const" | "static" | "virtual" | "noexcept")
            | "class" NOMBRE "namespace" NS
            | "class" NOMBRE "header" "\"" RUTA "\""
MIEMBRO    := [NS "::"] CLASE "::" MÉTODO
```

- Forma corta: aplica a la sentencia siguiente (o a la de la misma línea si el
  comentario va al final). Error `mt-annotation` si esa sentencia contiene cero
  o más de una llamada de la API.
- `TIPO` = resto del texto hasta `;` o fin, normalizado.
- Las anotaciones son `Origin::Annotated`. `config.overrides` admite las formas
  largas con el mismo significado.

---

## 8. Backend de implementación (modelo → `.h` + `.cpp`)

### 8.1 Asignación de ficheros

- Cabeceras *a generar* = `#include "…"` del test que no existen (§7.10).
- Clase ↔ cabecera por nombre normalizado (`match_key`: minúsculas, sin `_` ni
  `-`; la cabecera, sin directorios ni extensión, vía `file_stem`):
  `foo_bar.h` ↔ `FooBar`. `stem` = `snake_case(Clase)`: `HTTPServer` →
  `http_server`, `Vec3D` → `vec3_d`, `UTF8String` → `utf8_string` (reglas en
  `core/naming.h`).
- Clases/funciones sin pareja → la primera cabecera a generar en orden de
  fuente; si no hay ninguna → `naming.header` (por defecto `{stem}.h` con
  `stem = snake_case(Clase)`) + aviso `mt-no-include`.
- Rutas: cabecera en `<out-include>/<ruta tal como se incluye>` (con
  subdirectorios: `acme/foo.h`); fuente en `<out-src>/<mismo directorio>/<stem>.cpp`.
  `--out` fija ambos si no se dan por separado.

### 8.2 Cabecera

```cpp
// Generated by mirrortest from tests/foo_test.cpp. Review every TODO(mt).
#ifndef ACME_FOO_H
#define ACME_FOO_H

// <mt:begin includes>
// <mt:end includes>

namespace acme {

class Foo {
public:
    // <mt:begin acme::Foo>
    explicit Foo(int arg1);
    // TODO(mt): return type inferred from literal 3u (tests/foo_test.cpp:12)
    unsigned int size() const;
    // TODO(mt): result never used; assumed void (tests/foo_test.cpp:17)
    void run();
    // <mt:end acme::Foo>
};

}  // namespace acme

#endif  // ACME_FOO_H
```

- `config.file_header` (p. ej. cabecera corporativa) se antepone; nunca la
  licencia de mirrortest.
- Guarda (`include_guard`) desde la ruta incluida: mayúsculas, lo no
  alfanumérico → `_`, con las rachas colapsadas y los extremos recortados (un
  `_` inicial o un `__` serían identificadores reservados): `../a/foo.h` →
  `A_FOO_H`; si empieza por dígito, prefijo `H_`.
- Includes desde los tipos usados (`ir/classify.h`): `std::string`→`<string>`,
  `std::string_view`→`<string_view>`, `std::vector`→`<vector>`, `std::map`,
  `std::set`, `std::unordered_map`, `std::array`, `std::list`, `std::deque`,
  `std::optional`, `std::variant`, `std::tuple`, `std::function`→`<functional>`,
  punteros inteligentes→`<memory>`, `std::pair`→`<utility>`,
  `std::size_t`/`ptrdiff_t`/`nullptr_t`→`<cstddef>`, `(std::)(u)intN_t`→`<cstdint>`,
  `std::chrono::*`→`<chrono>`, `Q[A-Z]\w*`→`<QNombre>`; clases generadas en
  otra cabecera → `"otra.h"`; externas de cabeceras existentes → esa cabecera.
  Orden: sistema alfabético, luego locales alfabético.
- Orden de miembros: constructores, luego métodos por primera aparición en el
  test, luego campos. Namespaces como bloques anidados (válido en C++11).
- Funciones libres: región `// <mt:begin functions>` en el namespace.

### 8.3 Fuente (stubs)

```cpp
// Generated by mirrortest from tests/foo_test.cpp. Review every TODO(mt).
#include "acme/foo.h"

#include <stdexcept>

namespace acme {

// <mt:begin acme::Foo>
Foo::Foo(int /*arg1*/) {}

unsigned int Foo::size() const {
    throw std::logic_error("mirrortest: not implemented: acme::Foo::size() const");
}

void Foo::run() {
    throw std::logic_error("mirrortest: not implemented: acme::Foo::run()");
}
// <mt:end acme::Foo>

}  // namespace acme
```

Política `--stub-policy`:

| Política | Métodos (incl. `void`) | Ctor/dtor | Include |
|---|---|---|---|
| `throw` (defecto) | `throw std::logic_error("mirrortest: not implemented: <firma>")` | vacío | `<stdexcept>` |
| `abort` (`--no-exceptions`) | `std::abort();` | vacío | `<cstdlib>` |
| `default` | `return R{};` · `void` → vacío · retorno por referencia o `R` no construible por defecto → `std::abort()` + aviso | vacío | `<cstdlib>` si hace falta |

Con `default` se emite una vez `mt-stub-may-pass`. Nombres de parámetro siempre
comentados (R22). `--no-rtti`: el generador nunca emite `typeid`/`dynamic_cast`;
la opción queda como garantía documentada y se pasa a las plantillas. La región
`includes` se emite aunque esté vacía, para que el merge pueda añadir includes.

Mejora futura (no MVP), por R26: política `hook` — los stubs llaman a
`mt_not_implemented("<firma>")` declarada en una cabecera generada, con una
definición por defecto que lanza o aborta y que el lado de test puede
sustituir (p. ej. por `QTest::qFail` + valor por defecto) para que QTest siga
ejecutando el resto de slots.

---

## 9. Planner de tests y backends (modelo → tests)

### 9.1 Casos

- Por clase no plantilla con algo público: suite `config.naming.suite`
  (`{Class}Test`) en el namespace de la clase.
- Un caso por constructor público (`Constructs`, y si hay varios
  `Constructs_<tipos>`), por método público no borrado ni destructor, y por
  señal Qt (QTest: esqueleto `QSignalSpy`).
- Nombres (`core/naming.h`): el del método; operadores por tabla
  (`operator==`→`OperatorEq`, `operator[]`→`OperatorIndex`,
  `operator()`→`OperatorCall`, `operator bool`→`ConversionToBool`,
  `operator const char*`→`ConversionToConstCharPtr`, …; la tabla completa está
  en `test_base_name`). Si varios casos comparten nombre, los distingue el
  sufijo de tipos (`type_suffix`, sin `const`/`&`, `*`→`ptr`):
  `set_int`, `set_std_string`; lo que aún colisione (`size()` y `size() const`)
  recibe `_2`, `_3`… por orden, también frente a nombres ya tomados por otros
  métodos (`unique_test_names`).

### 9.2 Cuerpo

```cpp
TEST(FooTest, size) {
    FAIL() << "TODO(mt): write the test for acme::Foo::size() const";

    // Arrange
    Foo sut;
    // Act
    const Foo& const_sut = sut;
    const unsigned int result = const_sut.size();
    (void)result;
    // Assert
    // TODO(mt): compare result with the expected value
}
```

- **Placeholder primero** (R19): `FAIL()`/`GTEST_SKIP()`/`QFAIL`/`QSKIP`
  retornan; el resto compila contra la API real pero no se ejecuta. GCC y
  Clang no avisan de código inalcanzable con `-Wall -Wextra`; MSVC sí (C4702
  en `/W4`): limitación documentada, MSVC no es objetivo.
- `sut`: `Foo sut;` si es default-constructible (§6.3); si no, el constructor
  público con todos los parámetros *construibles* y menos parámetros (empate:
  orden de declaración) con locales; si ninguno → línea comentada + TODO.
  Métodos `static` → sin `sut`.
- Argumentos: un local por parámetro obligatorio `T nombre{};` con `T` = tipo
  del parámetro sin `const`/`&` de nivel superior; nombre = el del parámetro (o
  `argN`), sin colisiones. `T&&` → `std::move(nombre)` (+ `<utility>`).
  Parámetros con valor por defecto: se omiten (comentario indicándolo).
- Tipo *construible*: fundamental, enum, puntero, tipos `std::`/Qt de valor
  conocidos, clases del mismo modelo default-constructibles,
  `config.default_constructible_types`. Si algún argumento no lo es → la
  llamada se emite comentada (`Stmt::commented_out`) con TODO.
- Métodos `const` → a través de `const Foo& const_sut = sut;`.
- `noexcept` → `static_assert(noexcept(<llamada>), "…")`.
- Resultado: `const R result = …;` si `R` es valor; `R result = …;` si `R` es
  referencia; `void` → solo la llamada. Siempre `(void)result;`.
- Tipos anidados de la clase en firmas → calificados `Foo::Item` (los tests ya
  están dentro del namespace de la clase).

### 9.3 Includes del test

Framework (`<gtest/gtest.h>` / `<QtTest>`), la cabecera bajo test **tal como la
incluiría el usuario** (ruta relativa al primer `-I` que la contiene; si no, el
nombre; `--include-as` para forzarla), `<utility>` si se usa `std::move`.

### 9.4 GoogleTest

`TEST` por defecto; `TEST_F` con fixture generada (`Foo sut;` como miembro)
cuando `config.gtest.fixture = true` (fase 6). gmock: mocks de interfaces
(fase 6). Entrada: `gtest_main`, sin código extra.

### 9.5 QTest

```cpp
#include <QtTest>
#include "acme/foo.h"

namespace acme {

class TestFoo : public QObject {
    Q_OBJECT
private slots:
    void size();
};

void TestFoo::size() {
    QFAIL("TODO(mt): write the test for acme::Foo::size() const");
    ...
}

}  // namespace acme

QTEST_GUILESS_MAIN(acme::TestFoo)
#include "tst_foo.moc"
```

- Fichero `tst_{stem}.cpp`; clase `Test{Class}` (configurables).
- `#include "<stem>.moc"` al final, **debe coincidir con el nombre del fichero**
  (AUTOMOC). Documentado en la propia salida con un comentario.
- Main: `QTEST_GUILESS_MAIN` por defecto (`--qt-main appless|guiless|gui`).

### 9.6 Diferencias Qt 5 / Qt 6 (`--qt 5|6`, defecto 6)

Solo afecta a macros de excepción (R6). `QSKIP("…")` de un argumento y
`QTEST_*MAIN` son iguales en 5.15 y 6.x.

---

## 10. Escritura de ficheros

Implementado en `core/output.h` (`OutputPlan`: el plan, como datos) y
`core/writer.h` (`write_plan`: la única capa con E/S).

- `OutputPlan` = vector de `{ruta, contenido}`. La ruta es la que verá el
  usuario (ya con el directorio de salida; relativa o absoluta); el writer la
  resuelve contra un directorio base pero **la muestra tal cual**.
- Acción por fichero, calculada contra disco: `Create` (no existe), `Unchanged`
  (mismos bytes: **no se toca**, ni el mtime), y si difiere según
  `--overwrite`: `never` → `WriteNew` (`<fichero>.new` al lado, el original
  intacto), `force` → `Overwrite`, `merge` → `Merge` mediante la `MergeFn`
  (fase 3). Sin `MergeFn`, o si esta devuelve «no se puede fusionar», se
  degrada a `WriteNew` (con aviso `mt-unsupported` en el primer caso). Un
  `MergeResult` idéntico al fichero es `Unchanged`.
- `.new` existente se sobrescribe (es nuestro). Un `.new` viejo junto a un
  fichero ahora `Unchanged` se deja como está.
- **Todo o nada** (D8): primero se decide y valida *todo* (rutas vacías o
  duplicadas tras normalizar, destino que existe y no es fichero regular,
  ilegible…) acumulando todos los errores `mt-io`; con alguno no se escribe nada
  y el informe queda vacío. Después, **stage-then-commit**: se escriben todos
  los `<destino>.mt-tmp` (creando directorios padre) y solo si todos están en
  disco se hace `rename` de cada uno; un fallo antes de los renames borra los
  temporales. Un fallo *durante* los renames (prácticamente imposible tras
  haber escrito los temporales) puede dejar ficheros anteriores colocados.
  Al sobrescribir se conservan los permisos del fichero.
- `--dry-run`: no escribe y rellena `FileResult::diff` (diff unificado, 3 líneas
  de contexto, `--- a/…` / `+++ b/…`) de cada fichero cambiado: para un
  `WriteNew` es el diff contra el propio fichero; para un `Merge`, contra el
  resultado fusionado. Salida 0. `--check`: igual pero el llamador no imprime y
  sale con 4 si `WriteReport::has_changes()` (cualquier acción que no sea
  `Unchanged`). Combinados: se imprime y se sale con 4. Diff propio en
  `core/diff.h`: Myers O(ND) tras recortar la cabeza y la cola comunes, con las
  convenciones de git (`--- /dev/null` para ficheros nuevos, rangos `-3` /
  `-2,0`, `\ No newline at end of file`; las líneas se comparan con su
  terminador, así que añadir o quitar el salto final es un cambio). Pasadas
  `kMaxEdits` (2000) ediciones el tramo distinto se presenta entero como «todo
  lo viejo fuera, todo lo nuevo dentro»: sigue siendo un diff correcto, no
  mínimo. Determinista; verificado con un aplicador de parches de referencia y
  una LCS sobre 600 pares aleatorios.
- Resumen (`format_summary`, por stderr salvo `--quiet`): `created X`,
  `unchanged X`, `overwrote X`, `wrote X.new (X exists and differs)`,
  `merged N members into X` (`1 member`); con `--dry-run` los cambios se leen
  `would create X`, `would overwrite X`, …

---

## 11. Merge por regiones (`--overwrite=merge`)

### 11.1 Regiones

Líneas exactas (tras recortar espacios): `// <mt:begin ETIQUETA>` …
`// <mt:end ETIQUETA>`, con `ETIQUETA` = `includes`, `functions` o el nombre
calificado de la clase. Anidar o duplicar regiones = error `mt-merge-region`.

### 11.2 Clave de miembro

Dos nociones distintas, para no confundirlas:

- **Firma completa** = (nombre, tipos de parámetros normalizados, `const`,
  ref-qualifier). Identifica un miembro de verdad; la usa el frontend de código
  para deduplicar (§6.5). `at(int)` y `at(int) const` son dos miembros.
- **Clave de merge** = (nombre, aridad); si algún tipo de parámetro es
  `Annotated` o hay anotación `overload` → (nombre, tipos normalizados).
  Decide si un miembro inferido «ya existe» en el fichero del usuario. Ni el
  tipo de retorno (no distingue sobrecargas) ni `const` (añadir `size() const`
  junto a la `size()` del usuario sorprendería) forman parte de ella (R16);
  diferencias en ellos → `mt-merge-mismatch`.

### 11.3 Algoritmo (por fichero objetivo existente)

1. Idéntico al deseado → `Unchanged`.
2. Analizar el fichero existente con el frontend de código: miembros de cada
   clase (en **toda** la clase, no solo en la región) o definiciones
   `Clase::metodo(…)` en el `.cpp`.
3. Faltantes = deseados cuya clave no existe. Insertar sus declaraciones (con
   sus TODOs) justo antes de la línea `<mt:end …>`, con la sangría del
   marcador, en el orden del modelo deseado. En el `.cpp`, faltantes =
   declarados en la cabecera (ya fusionada) sin definición, comparando por
   **firma completa** (ahí los dos lados son código declarado).
4. Miembro existente con tipos distintos a los inferidos → aviso
   `mt-merge-mismatch`, sin tocarlo.
5. Includes faltantes → región `includes`; si no existe, solo aviso.
6. Clase sin región → `WriteNew` + aviso `mt-merge-no-region` (no se edita
   código fuera de regiones, nunca).
7. Comprobación final: se re-parsea el resultado; si tiene más nodos de error
   que el original → `WriteNew` + aviso `mt-merge-broke` (aviso y no error:
   un error impediría escribir el `.new`, D8).
8. Nunca se borra nada.

---

## 12. Diagnósticos

Formato GCC (lo entienden IDEs y CI): `fichero:línea:col: error|warning|note:
mensaje [mt-código]`. Ordenados por (fichero, línea, columna, código).
`--strict` convierte avisos en errores.

| Código | Severidad | Cuándo |
|---|---|---|
| `mt-io` | error | no se puede leer/escribir |
| `mt-config` | error | configuración inválida o clave desconocida |
| `mt-framework` | error | framework no detectable o ambiguo |
| `mt-parse` | warning | el parser marcó regiones como `ERROR` (sin contar formas conocidas que se tratan por texto, como `QFETCH(int, v)`) |
| `mt-auto` | error (política) | `auto` recibe un resultado de la API |
| `mt-unknown-return`, `mt-unknown-param` | error (política) | tipo no inferible |
| `mt-chained-call` | error (política) | llamada sobre el resultado de otra llamada |
| `mt-unused-result` | warning | resultado no usado → `void` |
| `mt-conflict` | warning | inferencias incompatibles |
| `mt-literal`, `mt-literal-range` | warning | literal no soportado / fuera de rango |
| `mt-vexing-parse` | warning | `Foo f();` |
| `mt-ambiguous-scope` | warning | `X::m()` sin saber si `X` es clase o namespace |
| `mt-unknown-receiver`, `mt-unknown-fixture` | note / warning | receptor o fixture no localizados |
| `mt-unknown-type` | warning | tipo usado (p. ej. en `EXPECT_THROW`) que no se genera ni se conoce |
| `mt-no-include` | warning | clase sin `#include` que la reclame |
| `mt-annotation` | error | anotación mal formada o sin destino único |
| `mt-unsupported` | warning | construcción fuera del subconjunto soportado |
| `mt-conditional` | note | miembro en `#if` con versiones distintas |
| `mt-stub-may-pass` | warning | política `default` |
| `mt-merge-region` | error | regiones mal formadas o duplicadas |
| `mt-merge-no-region`, `mt-merge-mismatch`, `mt-merge-broke` | warning | ver §11 |
| `mt-std` | warning | tipo que exige un estándar mayor que `--std` |

---

## 13. CLI

### 13.1 Sintaxis

```
mirrortest gen-impl  [--tests] FICHERO... --out DIR [--out-include DIR] [--out-src DIR]
                     [--framework auto|gtest|qtest] [--stub-policy throw|abort|default]
                     [--no-exceptions] [--no-rtti] [--auto-policy error|todo] [COMUNES]
mirrortest gen-tests [--header] FICHERO... --out DIR [--framework gtest|qtest]
                     [--placeholder fail|skip] [--include-as RUTA] [--qt 5|6]
                     [--qt-main appless|guiless|gui] [COMUNES]
COMUNES: -I DIR ... | --std c++11|c++14|c++17|c++20|c++23 (defecto c++17)
         --overwrite never|merge|force (defecto never) | --dry-run | --check
         --config FICHERO.json | --templates DIR | --strict | --quiet | --verbose
         --compile-commands FICHERO (fase 8: solo para extraer -I)
```

Implementado en `cli/run.h` (`parse_options`, `finish_run`, `run`); `main.cpp`
solo construye el `Env` y llama a `run`. `--compile-commands` **no está
registrado** hasta la tarea 8.1: pasarlo es hoy un error de uso, no una opción
aceptada e ignorada.

- **Entradas**: posicionales o con `--tests`/`--header` (AT §10); primero los
  posicionales y luego los de la opción, cada grupo en el orden escrito. Sin
  entradas → error de uso.
- **Salida**: `gen-impl` necesita `--out`, o `--out-include` y `--out-src`;
  `--out-include`/`--out-src` caen cada uno por separado en `--out`
  (`Options::header_dir()` / `source_dir()`). `gen-tests` necesita `--out`.
- **Opciones repetibles** (`-I`, `--tests`, `--header`): un valor por aparición,
  todas las apariciones acumuladas. CLI11 las hace voraces por defecto
  (`-I inc test.cpp` se tragaría `test.cpp` como segundo directorio y dejaría la
  orden sin entradas); hacen falta a la vez `expected(1)`,
  `allow_extra_args(false)` y `multi_option_policy(TakeAll)` (ninguna basta
  sola, comprobado contra esta versión). `-Idir` pegado también funciona.
- **Validación**: valores fuera de su conjunto (`--std c++98`, `--qt 4`,
  `--framework auto` en `gen-tests`, …) son error de uso que dice qué es válido;
  los conjuntos salen de `core/config.h` (`std_names()`, …), una sola fuente de
  verdad para CLI y fichero. `--quiet` y `--verbose` se excluyen;
  `--include-as` exige exactamente una cabecera. Las opciones de un comando que
  no son del otro (`--stub-policy` en `gen-tests`) son error de uso.
- **Precedencia** (D9): defaults → `--config` → línea de comandos, y **solo una
  opción realmente pasada** pisa al fichero (su valor por defecto nunca lo
  hace: `--std` por defecto es c++17 y un fichero con c++11 debe ganar si falta
  `--std`). `--no-exceptions`/`--no-rtti` solo pueden activar. Las rutas
  relativas (config, entradas, plantillas, salida) se resuelven contra
  `Env::cwd`, nunca con `chdir`, y se **muestran como las escribió el usuario**.
- **`--quiet`**: quita las notas y el resumen; los avisos y errores se imprimen
  siempre. **`--verbose`**: más información (p. ej. las plantillas sustituidas).

### 13.2 Códigos de salida

`0` éxito (incluido «nada que hacer», `--help` y `--version`) · `1` hay errores
de diagnóstico que no son de E/S · `2` uso incorrecto · `3` E/S (**todos** los
errores registrados son `mt-io`; si hay mezcla, `1`) · `4` `--check` con
diferencias (solo si no hay errores).

`finish_run` es el final común de cada comando: aplica `--strict` (avisos →
errores), imprime los diagnósticos ordenados por stderr, y con un informe de
escritura: `--dry-run` imprime el diff de cada fichero por stdout; el resumen va
a stderr salvo `--quiet` o `--check` (que queda en silencio, salvo
diagnósticos); un error suprime diffs y resumen.

### 13.3 Lo único que cambia con `--std`

| Aspecto | Efecto |
|---|---|
| `u8'a'`, `u8"…"` | `char`/`const char*` hasta C++17; `char8_t` desde C++20 |
| `std::optional`, `std::variant`, `std::string_view`, `std::byte` en firmas | aviso `mt-std` si `--std` < 17 |
| sufijos `z`/`uz` | aviso `mt-std` si `--std` < 23 |
| plantillas | variable `std` disponible (número: 11, 14, …) |

---

## 14. Configuración (`--config fichero.json`)

```json
{
  "std": "c++17",
  "default_namespace": "",
  "naming": { "header": "{stem}.h", "source": "{stem}.cpp",
              "gtest_file": "{stem}_test.cpp", "qtest_file": "tst_{stem}.cpp",
              "suite": "{Class}Test", "qtest_class": "Test{Class}" },
  "guard": "ifndef",
  "file_header": "",
  "stub_policy": "throw",
  "no_exceptions": false,
  "no_rtti": false,
  "placeholder": "fail",
  "auto_policy": "error",
  "placeholder_type": "int",
  "string_literal_param": "const char*",
  "qt": 6,
  "type_map": { "unsigned int": "std::uint32_t" },
  "by_value_types": [],
  "default_constructible_types": [],
  "external_types": [],
  "external_functions": [],
  "ignore_macros": [],
  "overrides": { "acme::Foo::items": { "returns": "std::vector<int>" } },
  "template_instantiations": { "acme::Box": ["int", "std::string"] }
}
```

Implementado en `include/mirrortest/core/config.h` (`Config`, `apply_config_json`,
`load_config_file`). Reglas:

- **Capas** (D9): `Config{}` (defaults) → `apply_config_json` (fichero) → la CLI
  fija los campos de las opciones que recibió. Cada capa solo cambia lo que
  dice; las **listas se reemplazan**, no se fusionan.
- **Validación estricta, todo en una pasada**: clave desconocida (con su ruta:
  `naming.colour`, `overrides.A::f.retuns`), tipo erróneo o valor fuera de su
  conjunto son errores `mt-config`; se reportan todos y, si hay alguno, el
  `Config` queda **intacto** (se trabaja sobre una copia).
- El JSON admite comentarios `//` y `/* */`. Un JSON inválido es un error con la
  posición que da nlohmann/json.
- `naming.*`: cada patrón admite un único marcador y debe contenerlo
  (`{stem}` en los de fichero, `{Class}` en los de nombre); si no, todas las
  clases irían al mismo fichero o suite.
- `no_exceptions` hace que `stub_policy: throw` se comporte como `abort`
  (`effective_stub_policy`); un `default` explícito se respeta.
- `file_header` debe ser un comentario C++ (`//` o `/*`), porque se emite tal
  cual al principio de cada fichero; se normalizan los saltos de línea y se
  recortan los finales.
- Los tipos (`type_map`, `by_value_types`, `overrides.*.returns`/`params`,
  `template_instantiations`, …) se guardan **normalizados** con
  `normalize_spelling`; dos claves de `type_map` que normalizan igual son un
  error.
- `overrides`: clave `Clase::miembro` o `ns::Clase::miembro`; valor con
  `returns`, `params` (objeto `{"1": "int", "3": "double"}`, posiciones desde 1
  sin ceros a la izquierda), `const`, `static`, `virtual`, `noexcept`. Mismo
  significado que la forma larga de la anotación (§7.11).
- `type_map` se aplica al final a tipos `Inferred`/`Defaulted` (nunca a
  `Declared`/`Annotated`), comparando `base` normalizado.

---

## 15. Plantillas

Implementado en `core/templates.h` (`TemplateStore`) y
`cmake/MtEmbedFiles.cmake`. Documentación de usuario en `docs/templates.md`.

- Por defecto **embebidas**: `templates/**/*.inja` → `mt_embedded_templates.cpp`
  generado en *configure* (con `CONFIGURE_DEPENDS`: editar, añadir o quitar una
  plantilla reconfigura y recompila solo; el fichero solo se reescribe si cambia
  su contenido). Cada una es un *raw string* `R"mt_tpl(…)mt_tpl"`; el configure
  falla nombrando el fichero si una plantilla contiene `)mt_tpl"`. Se normalizan
  a LF al embeber (un `core.autocrlf` en Windows no debe cambiar la salida).
  Probado en aislamiento con un mini-proyecto CMake (`tests/cmake/embed`):
  contenido con `;`, `[ ]`, `"`, `\`, `${…}` y `@…@` intacto, CRLF→LF, orden
  por nombre y no reescritura sin cambios.
- `--templates DIR`: un fichero con la misma ruta relativa sustituye al
  embebido; los demás siguen embebidos. Un `.inja` del directorio que no
  sustituye a ninguno es un **aviso** y se ignora (casi siempre es una errata, y
  un override que nunca se aplica en silencio es peor que un error). Un
  directorio inexistente es un error `mt-io`; un override que no se puede leer,
  también. `overridden()` lista los sustituidos (para `--verbose`).
- inja con `trim_blocks` y `lstrip_blocks`; variable indefinida = error
  (*verificado* en `tests/unit/test_dependencies.cpp`). **Peculiaridad
  verificada**: con `trim_blocks` inja quita los espacios y tabuladores tras
  cualquier `%}` además del salto de línea (Jinja solo el salto), así que
  `{% if x %} de` pierde su espacio inicial; un espacio que deba sobrevivir va
  en una expresión: `{% if x %}{{ " de " }}`. Los `include` se resuelven
  **solo** contra el almacén, nunca contra ficheros del directorio actual.
- Se analizan todas las plantillas la primera vez que se renderiza (o con
  `validate()`), se registran como incluibles y se reportan **todas** las que no
  analizan, cada una con su nombre y la línea que da inja. Mientras alguna esté
  rota no se renderiza ninguna, y el error se repite en cada llamada (no solo en
  la primera): es ruidoso a propósito. Los errores de un override apuntan a su
  fichero; los de una plantilla embebida, a `(built-in) <nombre>`.
- **Contrato**: la plantilla recibe un *view model* JSON con todo ya formateado
  en C++ (`declaration`, `definition_head`, `body`, `notes[]`, `includes[]`,
  `namespaces[]`…). La plantilla solo coloca. El tipo es `nlohmann::json`, que
  es el de inja y **ordena las claves de los objetos**; lo que deba salir en un
  orden concreto va en arrays (corrige la mención a `ordered_json` de la primera
  versión). El merge (§11) reutiliza esas mismas cadenas, no las plantillas.
  Variables de cada plantilla en `docs/templates.md`.
- Plantilla compartida ya existente: `common/file_header.inja` (`file_header`,
  `source`) → `[file_header\n]// Generated by mirrortest[ from <source>]. Review
  every TODO(mt).\n`.

---

## 16. Estrategia de pruebas del generador

1. **Unitarias** (GoogleTest): cada módulo; fragmentos de C++ como cadenas.
   Frontend de tests en dos mitades (*C++ → hechos*, *hechos → modelo*).
2. **Golden** (`tests/golden/`, contrato completo en `tests/golden/golden.h`
   y `tests/golden/README.md`): `tests/golden/<grupo>/<caso>/` con
   - `args.txt` (un argumento por línea; `${IN}` y `${OUT}` → las rutas
     **relativas** `in` y `out`), `input/` (→ `<work>/in`), `existing/`
     (opcional, → `<work>/out` antes de ejecutar) y `notes.md` (ignorado);
   - lo esperado: `expected/` (**solo** el árbol bajo `<work>/out`),
     `stdout.txt`, `stderr.txt` y `exit_code.txt` (ausentes = vacío / 0).
     Se separan del árbol esperado para no mezclar metadatos con ficheros
     generados (cambio respecto a la primera versión, que usaba
     `expected/diagnostics.txt`). Los casos que fijan el comportamiento `.new`
     llevan ficheros `*.new` (desbloqueados en `.gitignore`).
   - El runner ejecuta la CLI real **en proceso** (`mt::cli::run`) con un
     directorio temporal nuevo como `cwd`, así que toda ruta impresa es la
     relativa de `args.txt` y los goldens no dependen de dónde se clone el repo.
     Cada caso se ejecuta **dos veces** en directorios distintos y los
     resultados (código, stdout, stderr, árbol) deben ser idénticos.
   - Convenciones comprobadas sobre la salida real: LF, salto final, sin
     espacios finales en los ficheros (los flujos quedan exentos: un diff
     unificado imprime el contexto vacío como `" "`), y ninguna ruta absoluta
     (ni la del directorio de trabajo ni la del caso).
   - Un fallo muestra el caso, el flujo o fichero y un diff esperado/real. Un
     directorio de caso con una entrada desconocida (casi siempre una errata:
     `expeced/`) es un error de layout.
   - Actualización explícita: `MT_UPDATE_GOLDEN=1 ctest -R golden` o el target
     `update-golden`; nunca a mano. Se **niega a grabar** una salida no
     determinista o que rompa una convención, y borra lo que ya no se produce.
   - El propio runner está probado (`tests/golden/test_golden_runner.cpp`) con
     una CLI falsa: descubrimiento, layout, cada tipo de diferencia, update,
     determinismo y convenciones. Los ficheros de `tests/golden/` se marcan
     `-text` en `.gitattributes` para que git no reescriba sus finales de línea.
3. **Round-trip** (CTest + `cmake -P tests/roundtrip/run_case.cmake`):
   - gen-impl: generar → configurar un subproyecto que reutiliza las fuentes de
     GoogleTest ya descargadas (sin red) → compilar stubs con `-std=c++11
     -Wall -Wextra -Wpedantic -Werror` y tests con `-std=c++14 -Wall -Wextra
     -Werror` (R27) → ejecutar con `--gtest_output=json` → exigir `tests > 0` y
     **`failures == tests`** (todo rojo) → regenerar y exigir `unchanged`
     (idempotencia).
   - gen-tests: compilar la implementación de ejemplo + tests generados →
     exigir `failures == tests` (`fail`) o `skipped == tests` (`skip`).
   - QTest (si `find_package(Qt6|Qt5 COMPONENTS Test)` lo encuentra; si no, el
     test CTest se marca *skipped* con `SKIP_RETURN_CODE`):
     gen-tests → ninguna línea `PASS   : TestX::<slot>()` de un slot generado
     y nº de líneas `FAIL!`/`SKIP` = nº de slots (`Totals:` cuenta también
     `initTestCase`/`cleanupTestCase` como *passed*, no sirve tal cual);
     gen-impl → salida ≠ 0, al menos un `FAIL!` y **ningún** `PASS` de un slot
     de test (R26: tras la primera excepción QTest aborta).
   - **ARM**: si `MT_ARM_CXX` está definido (CI instala
     `g++-arm-linux-gnueabihf`), los stubs generados se compilan con
     `-std=c++11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only`, y con política `abort`
     además `-fno-exceptions -fno-rtti`.
4. **Corpus de casos límite**: plantillas, namespaces anidados,
   `const`/`static`/`noexcept`, sobrecargas, herencia, operadores,
   `enum class`, punteros inteligentes, macros Qt, `#if`.
5. **Sanitizers** ASan/UBSan en CI sobre todos los tests (ya activo).
6. CI: GCC y Clang × Debug/RelWithDebInfo con `-Werror`; job de sanitizers; job
   de round-trip con Qt 6 (y Qt 5 cuando exista el backend QTest); job ARM.

---

## 17. Dependencias

| Dependencia | Versión | Commit | Licencia | Uso |
|---|---|---|---|---|
| tree-sitter | v0.25.10 | `da6fe9be…` | MIT | runtime del parser (ABI 13–15) |
| tree-sitter-cpp | v0.23.4 | `f41e1a04…` | MIT | gramática C++ (ABI 14) |
| nlohmann/json | v3.12.0 | `55f93686…` | MIT | configuración, view models |
| inja | v3.5.0 | `7d1b4600…` | MIT | plantillas |
| CLI11 | v2.5.0 | `4160d259…` | BSD-3 | CLI |
| GoogleTest | v1.16.0 | `6910c9d9…` | BSD-3 | tests (última línea con C++14, R7) |

Commits completos y mecanismo en `cmake/MtDependencies.cmake`. Cambiar una
versión es un cambio revisado: tag y commit juntos, y re-ejecutar goldens y
round-trip. Requisitos de build: CMake ≥ 3.20, compilador C++17 y C11, git.

---

## 18. Riesgos (actualizado)

| Riesgo | Mitigación |
|---|---|
| Inferencia incorrecta de tipos | `Origin` + `TODO(mt)`, error con `auto`, anotaciones, `type_map` |
| Sobrescritura de código del usuario | `never` por defecto, merge solo dentro de regiones, re-parseo de comprobación, todo o nada |
| Macros que rompen el parseo sintáctico | blanking con tabla + `ignore_macros`; aviso `mt-parse` si quedan nodos `ERROR` |
| Ramas `#if` contradictorias | §6.5 |
| Diferencias ILP32/LP64 | §7.5, nunca `long` por magnitud |
| Tests generados que pasan sin valor | placeholder primero; política de stubs `throw` |
| Ejecutar código legado con valores basura | placeholder primero: el cuerpo no se ejecuta |
| Deriva de GTest/Qt/tree-sitter | versiones fijadas por commit; `test_syntax_tree.cpp` fija formas de nodo; goldens |
| Red corporativa sin acceso a GitHub | `FETCHCONTENT_SOURCE_DIR_*` con verificación de commit |
| Alcance inabarcable | subconjunto declarado; el resto `mt-unsupported`, nunca silencio |

## 19. Fuera de alcance (explícito)

Inferir lógica de negocio; resolver sobrecargas por semántica; ejecutar el
preprocesador real; metaprogramación; generar `CMakeLists.txt` del proyecto
usuario; formatear con clang-format (la salida ya sigue un estilo fijo).
