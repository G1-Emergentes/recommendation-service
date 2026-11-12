# Almacenamiento vectorial de prendas en Neon

Proyecto: **mirage-recommendation-vectors** (`dry-credit-06448578`), organización
**mateo**, plan Free. Región `aws-us-east-1`, PostgreSQL 17, compute limitado a
0.25 CU con suspensión automática del plan.

Rama activa: **main** (`br-soft-meadow-bczoxhyn`).
Rama de pruebas: **dev-vector-store** (`br-fancy-wildflower-bcxev7zf`).
Base: **recommendation_vectors**. Tabla: **recommendation.wardrobe_embeddings**.
Las tres migraciones están aplicadas en main. El servicio local ya usa esa rama.

Para verla, abrir el proyecto en Neon Console, seleccionar la rama
`main`, base `recommendation_vectors` y esquema `recommendation`
en Tables o SQL Editor. `neon/project.json` conserva los identificadores públicos.

## Datos y responsabilidades

Una fila por `(user_id, garment_id)`: nombre y descripción de Closet, categoría,
colores, estilos, disponibilidad, revisión y fecha de origen, imagen por ID/versión,
texto para búsqueda, SHA-256, versión de formato y embedding `vector(1024)` del
modelo `nvidia/nemotron-3.5-embed-1b`. `indexed_at` identifica la generación del
embedding; `synced_at` registra sincronizaciones sin recalcularlo.

La dimensión, el modelo, códigos y tamaños se validan en base de datos. No se
admiten vectores nulos, de dimensión incorrecta o de norma cero. La clave primaria
impide duplicar una prenda por usuario y sirve como índice por usuario. No se creó
un índice aproximado HNSW; primero se medirá búsqueda exacta.

Closet conserva las prendas originales. Supabase sigue almacenando conversaciones
y outfits. Neon guarda exclusivamente el read-model vectorial. No hay claves
foráneas entre proyectos. No se activaron Neon Auth, Functions ni AI Gateway; los
embeddings seguirán generándose mediante NVIDIA.

## Seguridad de la conexión

El rol de aplicación es `mirage_vector_app`, con permisos explícitos:
CRUD sobre `wardrobe_embeddings` y solo SELECT sobre referencias publicadas. No es propietario, superusuario, miembro de
`neon_superuser` ni tiene BYPASSRLS. RLS está habilitado y forzado.

La conexión privada de aplicación está en `.env` como `NEON_VECTOR_DATABASE_URL`. No se
almacenó una conexión de administrador. El runner carga esa variable, pero el
servicio C++ consulta este almacén cuando `RECOMMENDATION_RAG_ENABLED=true`.

Cada operación del backend debe abrir una transacción y establecer
`app.user_id` mediante `set_config(..., true)`, con el UUID derivado de IAM y
parámetros SQL enlazados. La política aplica `USING` y `WITH CHECK` a ese actor.
Sin actor se ven cero filas. El contexto local desaparece al finalizar la
transacción, incluso al usar pooler. El frontend y el modelo nunca reciben la
conexión ni eligen el actor. Este contexto es una responsabilidad del backend;
no autentica por sí solo a usuarios de IAM.

## Estado y verificación

La migración `neon/migrations/001_wardrobe_embeddings.sql` está aplicada en
`main` y `dev-vector-store`. Se verificó pgvector 0.8.0 y columna `vector(1024)`.
La propuesta previa de Supabase nunca se aplicó y se archivó bajo
`supabase/legacy/unapplied-vector/` para no ejecutarla accidentalmente.

Prueba real: `python neon/tests/test_vector_store.py`. Requiere psycopg:
`python -m pip install --target out/neon-validation "psycopg[binary]"`.

La prueba lee `.env.rag-test`, cuya rama debe ser exclusivamente dev-vector-store.
La prueba conecta con el rol limitado, comprueba búsqueda coseno, aislamiento
entre usuarios sin filtros WHERE, denegación de escrituras ajenas, ausencia de
permisos de creación y rechazos de duplicados, revisión, modelo, dimensión, color
y vector cero. Los datos son sintéticos y siempre se deshacen con ROLLBACK.

**Almacén conectado al servicio C++ y búsqueda semántica habilitada contra main.**
Se indexaron las siete prendas reales de la cuenta autorizada; una segunda
sincronización generó cero embeddings nuevos. Una búsqueda real en español
recuperó primero el polo negro para la consulta de un polo negro discreto.

- `POST /api/v1/wardrobe-index/sync` con `{}` sincroniza solo el inventario del actor.
- `POST /api/v1/wardrobe-index/search` con `query` no vacío y filtros opcionales
  recupera prendas por similitud; sincroniza antes y revalida los resultados en Closet.
- La tool `search_wardrobe` admite `search_mode: semantic`; conserva el modo
  `literal` para filtros/subcadenas existentes. No hay fallback semántico silencioso.

El frontend, Postman, Digital Closet y Nemotron no reciben credenciales de Neon.
La conexión directa es exclusiva del backend de Recommendation. Los scripts de
administración y pruebas usan acceso de desarrollo fuera del flujo del producto.

## Consistencia y límites

La sincronización usa dos lecturas completas de Closet, hasta 100 prendas por
usuario, para comprobar cambios mientras se generan embeddings. No re-embebe si
el hash del documento no cambió. La escritura es atómica: upsert y poda del propio
usuario en una transacción. Un advisory lock por usuario y un fingerprint previo
rechazan actualizaciones concurrentes del índice. Una lectura incompleta o un fallo
al generar embeddings no ejecuta la poda. Se guardan también metadatos de prendas
no disponibles, que quedan fuera de los resultados de búsqueda.

La búsqueda aplica usuario, disponibilidad y filtros exactos en SQL, ordena por
coseno y devuelve hasta 10 candidatos. El score no es probabilidad de compatibilidad
ni una garantía de relevancia. Antes de responder se usa `/garments/resolve` con la
sesión del actor y se comprueban revisión y metadatos. Si cambiaron, devuelve 409.

Se valida el host Neon, rol restringido, TLS, modelo y dimensión de configuración.
Los vectores de respuesta de NVIDIA deben ser finitos, de norma positiva, dimensión
1024 y con índices de lote únicos. Los lotes tienen hasta ocho documentos y un
presupuesto de 90 segundos comprobado entre etapas. SQL usa parámetros enlazados,
statement timeout de 10 s y lock timeout de 5 s. Hay hasta ocho sincronizaciones
simultáneas y una por usuario dentro del proceso. El pool tiene dos conexiones.

Prueba de contrato local: `python tests/rag_http.py out/build/x64-debug/recommendation-service.exe`.
Prueba de integración de desarrollo: añadir `--live-neon`. Usa dobles HTTP de
IAM/Closet/NVIDIA, la conexión real restringida de Neon y actores sintéticos
reservados; verifica aislamiento, idempotencia, fallos parciales, actualizaciones
y poda. Limpia esos actores a través de su sincronización autenticada vacía.

## Recuperación obligatoria para generar

Con `RECOMMENDATION_RAG_ENABLED=true`, `RecommendationService` obtiene los candidatos
mediante `WardrobeIndex::candidates` antes de invocar al agente, tanto en generación
directa como en mensajes del chat. No añade el inventario completo ni tiene fallback
ante un fallo de Neon/embeddings. `false` mantiene el modo explícito anterior para
fixtures/pruebas; la configuración local live lo tiene activado.

Se sincroniza el índice y se genera un embedding de consulta con el mensaje/ocasión/
estilo actuales. SQL aplica disponibilidad, usuario/RLS, exclusiones de prendas,
colores evitados de Profile y estilo explícito. Recupera hasta seis prendas por cada
categoría admitida (TOP, BOTTOM, ONE_PIECE, FOOTWEAR), conservando también las prendas
de las últimas propuestas para permitir modificaciones. Máximo 33 candidatos.
Las restricciones temporales conocidas priorizan los candidatos dentro de cada
categoría; se conservan opciones para permitir que el usuario las relaje explícitamente.
Para "otra opción", el backend busca una combinación completa distinta de las
últimas propuestas y limita el contexto de generación a esas prendas. Cuando
las restricciones conocidas no dejan una combinación válida, devuelve una
aclaración sin invocar al LLM. Las restricciones nuevas y vigentes completas
se comprueban nuevamente tras la decisión
tipada del modelo. No se guarda una propuesta que las viole.

La recuperación revalida metadatos en Closet. El servicio vuelve a resolver antes
del modelo y después para las prendas finalmente elegidas. Un ID ajeno a los
candidatos se rechaza aunque pertenezca al mismo usuario. Los mensajes de consulta,
saludo o Profile también pasan por esta preparación en esta versión; por tanto,
con RAG activo dependen de Neon y embeddings.

No se fija un umbral global de coseno: es relevancia de una prenda, no compatibilidad
entre prendas; un umbral arbitrario podría eliminar calzado necesario. La cobertura
por categorías y la validación estructural evitan eso. El límite por categoría puede
omitir alternativas en un closet grande; el bot debe hablar de candidatos disponibles,
no asegurar que examinó todas las combinaciones. El límite de sincronización sigue
siendo 100 prendas por usuario.

El corpus inicial de referencias externas y guía editorial ya está en
`recommendation.style_knowledge`. Se consulta antes de los candidatos, con permisos
de solo lectura para la aplicación (`STYLE_KNOWLEDGE.md`). Las verificaciones de
prendas y sus límites están en `RAG_EVALUATION.md`.


La recuperación de prendas ahora alimenta el filtro de estructura de los ejemplos
Polyvore (`../knowledge/polyvore/README.md`). Checklist de cierre: `RAG_COMPLETION.md`.
