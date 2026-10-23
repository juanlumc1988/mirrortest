# mirrortest — plan de implementación

Orden de trabajo. El *qué* y el *porqué* están en [`design.md`](design.md)
(citado como «§n»); aquí solo el *en qué orden* y *cuándo está terminado*.
Marca cada tarea al terminarla (`- [x]`) en el mismo commit que la implementa.

## Reglas para cada tarea

- Una tarea = un commit (o pocos), con tests en el mismo commit.
- **Definición de terminado** (todas las tareas):
  1. `cmake --build build -j && ctest --test-dir build --output-on-failure`
     en verde con GCC **y** Clang, ambos con `-DMT_WERROR=ON`.
  2. Build con `-fsanitize=address,undefined` limpio (ver `CLAUDE.md`).
  3. Tests unitarios nuevos que cubren el camino feliz **y** los casos límite
     listados en la tarea.
  4. Si cambia la salida generada: goldens regenerados con el mecanismo
     explícito (nunca a mano) y el diff revisado.
  5. `design.md` actualizado si la implementación obliga a cambiar una
     decisión (y la decisión, explicada en el commit).
- Ningún `unordered_*` en caminos que afecten a la salida. Ninguna ruta
  absoluta ni timestamp en la salida.
- Errores de la entrada → `Diagnostics`, no excepciones.
- Ante una ambigüedad no resuelta en `design.md`: elegir lo más conservador
  (error explícito antes que adivinar), documentarlo y avisar en el resumen.

---

## Fase 0 — Andamiaje (hecha)

- [x] Licencia Apache-2.0, `.gitignore`, README, `CLAUDE.md`.
- [x] CMake ≥ 3.20, dependencias fijadas por tag + commit verificado
      (`cmake/MtDependencies.cmake`), offline vía `FETCHCONTENT_SOURCE_DIR_*`.
- [x] IR (`ir/model.h`), ortografía de tipos (`ir/type.h`, con tests).
- [x] Wrapper tree-sitter (`syntax/tree.h`) + tests que fijan las formas de
      nodo del spike.
- [x] CLI mínima (`--version`, subcomandos registrados) y CI (GCC/Clang ×
      Debug/RelWithDebInfo, ASan+UBSan).

---

## Fase 1 — Núcleo común

- [x] **1.1 Diagnostics** — `core/diagnostics.h/.cpp`. `Severity`,
  `Diagnostic{severity, code, message, loc, notes}`, `Diagnostics` (acumula,
  `has_errors()`, `promote_warnings()` para `--strict`, `sorted()`,
  `format(std::ostream&)` estilo GCC §12). Tests: orden estable, formato exacto,
  `--strict`.
- [x] **1.2 Lectura de fuentes** — `core/source.h/.cpp`. `SourceFile{path
  (tal cual), text}`; lectura binaria; CRLF tolerado (no se normaliza la
  entrada); error `mt-io`. Tests con ficheros temporales.
- [x] **1.3 Preprocesado (blanking)** — `syntax/preprocess.h/.cpp`, §5.2
  completo. Tests (cada uno comprobando el invariante de longitud y posiciones
  de `\n`): cada macro de la tabla; `slots` **no** se toca fuera de posición de
  acceso (`int slots = 0;`); macros dentro de comentarios, cadenas, *raw
  strings* y líneas `#define` intactas; `Q_PROPERTY` multilínea conserva líneas;
  paréntesis anidados y cadenas con `)` dentro del argumento; macro de export
  entre `class` y nombre; BOM; `ignore_macros` objeto y función; marcadores con
  offsets correctos. Test de integración: `tests/unit/data/spike_header.h` parsea tras el
  blanking con `has_error() == false`; `spike_qtest.cpp` deja como único `ERROR`
  el `QFETCH(int, value)` (forma conocida, §7.3: se trata por texto).
- [x] **1.4 Configuración** — `core/config.h/.cpp`. `Config` con defaults de
  §14; carga JSON con validación estricta (clave desconocida o tipo erróneo →
  `mt-config` con la ruta de la clave); fusión defaults < fichero < CLI.
  Tests: defaults, cada clave, errores.
- [x] **1.5 Naming** — `core/naming.h/.cpp`. `snake_case`, normalización para
  emparejar clase↔cabecera (§8.1), guardas (§8.2), expansión de patrones
  `{stem}`, `{Class}`; saneado de nombres de caso de test (§9.1, tabla de
  operadores). Tests exhaustivos (acrónimos: `HTTPServer` → `http_server`,
  dígitos, `_` iniciales).
- [x] **1.6 Diff** — `core/diff.h/.cpp`. Diff unificado determinista, 3 líneas
  de contexto, cabeceras `--- a/` `+++ b/`, fichero nuevo contra `/dev/null`,
  manejo de «sin salto final». Tests con casos conocidos.
- [x] **1.7 OutputPlan + Writer** — `core/output.h`, `core/writer.h/.cpp`, §10
  (sin merge todavía: `never`, `force`, `.new`, `Unchanged`, `--dry-run`,
  `--check`, atómico, crear directorios, todo-o-nada). Tests sobre un directorio
  temporal, incluido «`.new` previo se sobrescribe» e «idéntico → no se toca
  (mtime intacto)».
- [ ] **1.8 Plantillas** — `cmake/MtEmbedFiles.cmake` (§15; el build falla si
  una plantilla contiene `)mt_tpl"`), `core/templates.h/.cpp` (embebidas +
  `--templates DIR`, inja con `trim_blocks`/`lstrip_blocks`, error claro con
  nombre de plantilla). `docs/templates.md` con las variables. Tests: override
  parcial, plantilla inexistente, variable indefinida.
- [ ] **1.9 CLI en librería** — `cli/run.h/.cpp`: `int run(const
  std::vector<std::string>& args, const Env& env)` con `Env{cwd, out, err}`;
  todas las opciones de §13 parseadas y validadas (CLI11), códigos de salida
  §13.2; `apps/cli/main.cpp` solo llama a `run`. Subcomandos aún devuelven
  «not implemented» pero con la config ya resuelta. Tests de parseo y códigos
  de salida.
- [ ] **1.10 Runner de goldens** — `tests/golden/runner.cpp` (§16.2):
  descubrimiento de casos, sustitución `${IN}`/`${OUT}`, doble ejecución,
  comparación de árbol + diagnósticos, `MT_UPDATE_GOLDEN=1`, target
  `update-golden`, comprobación global (LF, salto final, sin espacios finales,
  sin rutas absolutas). Un caso trivial que fije `--version`/uso incorrecto.

## Fase 2 — gen-impl con GoogleTest (MVP)

- [ ] **2.1 Utilidades de extracción** — `syntax/extract.h/.cpp` (§5.3): texto
  de tipo de parámetros y retornos, descomposición, búsqueda de nodos.
  Tests sobre las formas del spike.
- [ ] **2.2 Frontend de código, subconjunto MVP** — `frontend/code_frontend`
  (§6): namespaces, clases con cuerpo, métodos, ctores/dtor, campos, acceso,
  `static/const/virtual/=0/override/noexcept/=delete/=default/explicit`,
  enums, funciones libres. (Se necesita ya para fixtures y cabeceras
  existentes; Qt y `#if` en 4.1.) Tests: `tests/unit/data/spike_header.h`
  (tras blanking) → modelo esperado campo a campo.
- [ ] **2.3 Clasificador de literales** — `frontend/literals` (§7.5) completo.
  Tabla de tests: cada fila de §7.5, incluidos `-1`, `-1u`, `0xFFFFFFFF`,
  `4294967296`, `1'000'000`, `5000000000l` (aviso ARM32), `1e-9`, `0x1p3`,
  `'ab'`, `u8"x"` con `--std` 17 y 20, `"x"s`, `10ms`, `1.5_km`.
- [ ] **2.4 Recognizer GTest + tabla de símbolos + hechos** —
  `frontend/test_frontend`, `frontend/facts` (§7.1–7.4, §7.6). Tests
  *C++ → hechos*: cada fila de §7.6; receptores (§7.3) incluido `->` con
  punteros inteligentes y `(*p).m()`; fixtures (`TEST_F` usa miembros, métodos
  de la fixture se analizan); `TEST_P` + `GetParam()`; `QFETCH` no aplica aún;
  `ASSERT_NO_THROW({ … })`; `<< "msg"`; *most vexing parse*; `auto`.
- [ ] **2.5 Clasificación de nombres y namespaces** — (§7.7, §7.8). Tests:
  externos (std, Q*, fixtures, cabeceras existentes), `X::m()` en sus tres
  ramas, `using namespace`, funciones libres vs. macros vs. libc.
- [ ] **2.6 Anotaciones** — `frontend/annotations` (§7.11). Tests: cada
  directiva, forma corta y larga, errores (`mt-annotation`: sin destino, más
  de una llamada, sintaxis), `config.overrides`.
- [ ] **2.7 Inferencia** — `frontend/inference` (§7.9). Tests *hechos →
  modelo*: unificación por aridad, prioridades de `Origin`, `mt-conflict`
  determinista, `const`, `static`, ctores (por defecto solo si hace falta,
  `explicit`), campos, by-value vs `const&`, nombres de parámetros, políticas
  `auto`/desconocido (`error` y `todo`), `type_map`.
- [ ] **2.8 Propagación entre llamadas** — punto fijo (máx. 4 iteraciones) para
  argumentos que son resultado de otra llamada de la API (§7.9). Tests.
- [ ] **2.9 Backend de implementación** — `backend/impl_backend` + plantillas
  `impl/header.inja`, `impl/source.inja` (§8): asignación de ficheros,
  includes por tipo (`ir/classify`), regiones, TODOs, políticas de stub,
  nombres de parámetro comentados, `file_header`, guardas. Tests unitarios del
  view model.
- [ ] **2.10 Subcomando `gen-impl`** — cableado completo frontend → inferencia
  → backend → writer (modos `never`/`force`). Goldens (`tests/golden/gen-impl/`):
  1. `basic_class` (ctor con literales, EQ/TRUE/FALSE/STREQ, `void`)
  2. `literals` (todas las formas de 2.3 con su efecto en la firma)
  3. `declared_vars` (variables tipadas como argumentos y retornos)
  4. `auto_error` (sin ficheros, diagnóstico `mt-auto`, salida 1)
  5. `auto_todo` (`--auto-policy=todo`)
  6. `const_receiver` (`const&` local y miembro `const` de fixture)
  7. `static_methods`
  8. `fixture_members` (`TEST_F`, `SetUp`)
  9. `namespaces` (`ns::Foo`, `using namespace`, test dentro de namespace)
  10. `multi_class_includes` (dos includes, clase sin pareja, `mt-no-include`)
  11. `free_functions` (+ macros y libc ignoradas)
  12. `annotations`
  13. `conflicts`
  14. `unused_result`
  15. `smart_pointers` (`unique_ptr`, `make_unique`, `new`, `->`)
  16. `vexing_parse`
  17. `subdir_include` (`acme/foo.h` → subdirectorios en salida)
  18. `existing_never` (cabecera existente distinta → `.new`)
  19. `existing_identical` (re-ejecución → `unchanged`)
  20. `existing_known_api` (cabecera existente en `-I` aporta tipos y no se
      regenera la clase)
  21. `no_exceptions` (`--no-exceptions` → `std::abort()`, `<cstdlib>`)
  22. `stub_default` (aviso `mt-stub-may-pass`)
- [ ] **2.11 Round-trip gen-impl/GTest** — `tests/roundtrip/run_case.cmake` +
  subproyecto genérico (§16.3): casos `basic_class`, `fixture_members`,
  `namespaces`, `multi_class_includes`, `no_exceptions`. Exigir compilación con
  los flags de §16.3, `failures == tests`, e idempotencia.
- [ ] **2.12 CI: ARM y round-trip** — job que instala
  `g++-arm-linux-gnueabihf` y define `MT_ARM_CXX` (stubs con `-std=c++11
  -Wall -Wextra -Wpedantic -Werror -fsyntax-only`; `no_exceptions` además con
  `-fno-exceptions -fno-rtti`).
- [ ] **2.13 Documentación de usuario** — `docs/conventions.md` (en inglés):
  cómo escribir tests que la herramienta entiende (reglas de §7 en lenguaje de
  usuario, con ejemplos), anotaciones, políticas, códigos de diagnóstico.
  README: sección *Usage* real.

## Fase 3 — Merge por regiones (adelantada, R15)

- [ ] **3.1 Regiones** — `core/merge.h/.cpp`: localizar, validar
  (`mt-merge-region`), insertar antes de `<mt:end>` con la sangría del marcador.
- [ ] **3.2 Merge de cabecera y fuente** — §11.3 completo, con clave §11.2,
  `mt-merge-mismatch`, `mt-merge-no-region`, re-parseo de comprobación
  (`mt-merge-broke`). Tests unitarios por paso.
- [ ] **3.3 `--overwrite=merge`** en el writer + goldens: `merge_new_method`,
  `merge_new_ctor`, `merge_user_changed_type` (no duplica), `merge_no_region`,
  `merge_includes`, `merge_idempotent`. Round-trip: generar → «implementar» un
  método (parche en el caso) → añadir test → merge → compila, el implementado
  pasa y el nuevo falla.

## Fase 4 — gen-tests con GoogleTest

- [ ] **4.1 Frontend de código completo** — Qt (roles, `is_qobject`),
  `#if` (§6.5), plantillas (marcar), tipos anidados, `mt-unsupported`. Tests.
- [ ] **4.2 Planner** — `backend/test_planner` (§9.1–9.3): casos, nombres,
  `sut`, locales, `const_sut`, `noexcept`, resultado, comentados, includes.
  Tests *modelo → plan*.
- [ ] **4.3 Backend GTest** — `backend/gtest_backend` + `gtest/test_file.inja`
  (§9.4), placeholders `fail`/`skip`.
- [ ] **4.4 Subcomando `gen-tests`** + goldens (`tests/golden/gen-tests/`):
  `simple_class`, `const_noexcept`, `static`, `overloads`, `operators`,
  `non_default_ctor`, `abstract`, `nested_types`, `namespaces`,
  `unknown_arg_type` (llamada comentada), `qt_header_gtest`, `ifdef_branches`,
  `template_skipped`, `placeholder_skip`.
- [ ] **4.5 Round-trip gen-tests/GTest** — implementación de ejemplo en el caso;
  exigir compilación y `failures == tests` / `skipped == tests`.

## Fase 5 — QTest en ambas direcciones

- [ ] **5.1 Recognizer QTest** — clase de test, slots dentro/fuera de clase,
  `QFETCH`, `addColumn`, `QCOMPARE`/`QVERIFY*`/`QTRY_*`, ambas macros de
  excepción (orden invertido), detección de framework (§7.1). Tests.
- [ ] **5.2 Backend QTest** — `backend/qtest_backend` + `qtest/test_file.inja`
  (§9.5–9.6): `.moc`, main, `--qt 5|6`, `QSignalSpy` para señales.
- [ ] **5.3 Goldens y round-trip QTest** — ambas direcciones; expectativas de
  §16.3 (R26). CI: job con `qt6-base-dev`; segundo job con `qtbase5-dev`.

## Fase 6 — Fixtures, parametrizados, mocks

- [ ] **6.1** gen-tests con fixture (`TEST_F`, `sut` miembro, `SetUp`).
- [ ] **6.2** gen-impl desde `MOCK_METHOD` en el test: genera la interfaz con
  métodos virtuales puros (firmas `Declared`).
- [ ] **6.3** gen-tests: mocks gmock para interfaces (todas las funciones
  virtuales puras), incluido el paréntesis en tipos con comas
  (`MOCK_METHOD((std::map<int, int>), …)`).
- [ ] **6.4** `TEST_P` + `INSTANTIATE_TEST_SUITE_P` en gen-tests (opt-in) y
  QTest `_data`.

## Fase 7 — Plantillas, herencia, namespaces anidados, operadores

- [ ] **7.1** Plantillas de clase con `template_instantiations` (gen-tests).
- [ ] **7.2** Herencia: métodos heredados no se testean en la derivada (nota);
  `static_assert(std::is_base_of…)`.
- [ ] **7.3** Clases anidadas completas.
- [ ] **7.4** gen-impl: operadores usados en tests (`a == b`, `a < b`, `a[i]`),
  enums inferidos de `E::X`, clases de excepción de `EXPECT_THROW` (opt-in).
- [ ] **7.5** `= delete` de copia/movimiento → `static_assert`s de
  `is_copy_constructible` (gen-tests).

## Fase 8 — Configuración avanzada

- [ ] **8.1** `--compile-commands`: extraer `-I` del fichero.
- [ ] **8.2** `--no-stl` (§8.3).
- [ ] **8.3** Política de stubs `hook` (R26).
- [ ] **8.4** (Opcional) cargador YAML si se confirma (R18).
- [ ] **8.5** (Opcional) frontend libclang detrás de `MT_WITH_LIBCLANG` (D1).
