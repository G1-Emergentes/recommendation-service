# Tools del chat

Nemotron ahora puede emitir `tool_calls` reales de Chat Completions. No son
endpoints públicos nuevos: se usan desde el POST de mensajes existente.
El backend conecta cada llamada con la sesión autenticada por IAM.

## search_fashion_web

Consulta bajo demanda 800 descripciones públicas de FashionStylist (publicación
abril de 2026, licencia CC-BY-NC-4.0, uso académico/no comercial). Se habilita
también antes de generar outfits; el modelo decide si necesita referencias externas.
No navega libremente ni busca tendencias actuales: consulta dos CSV autorizados
en una revisión verificada del repositorio, con caché pública de 30 minutos.

Argumentos: `query` en inglés (2–200 caracteres, letras/espacios/guiones),
`audience` ANY/MALE/FEMALE, `season` ANY/SPRING/SUMMER/AUTUMN/WINTER y `limit` 1–3.
ANY es el valor por defecto; no se infiere género de la cuenta. Devuelve hasta
tres ejemplos con enlaces al CSV original, revisión, fecha, licencia y atribución.
Es búsqueda léxica; no escribe embeddings ni cambia permisos de Neon.

No admite URLs ni identidad elegidas por el modelo. No envía tokens o consultas
a GitHub, no descarga imágenes y no sigue redirecciones. Los errores de la fuente
devuelven EXTERNAL_SOURCE_UNAVAILABLE para que el agente los reconozca sin inventar
resultados. Una consulta exitosa sin coincidencias es válida. Los IDs externos
nunca pasan las validaciones de prendas propias ni cambian las restricciones.

Para mostrar solo fuentes: SHOW_FASHION_WEB, respaldada por evidencia de ejecución.
Para combinaciones: RECOMMEND/MODIFY con IDs propios y `recommendation.web_references`
en el snapshot. Detalles y pruebas: `../knowledge/fashionstylist/README.md`.

## search_wardrobe

Consulta Digital Closet con el token del usuario. Solo devuelve prendas disponibles.
En `demo` usa el proveedor explícito de fixtures; no hay fallback de `live` a demo.

| Argumento opcional | Regla |
|---|---|
| `query` | Subcadena literal de nombre/descripción; máximo 120 bytes; por defecto `""` |
| `category` | Código exacto del contrato de Closet o `""` |
| `color` | Familia de color exacta o `""` |
| `style` | Estilo exacto de Closet o `""` |
| `limit` | Entero de 1 a 10; por defecto 10 |
| `search_mode` | `literal` por defecto, o `semantic` con query no vacío |

Los filtros se combinan con AND. El modo `literal` ignora mayúsculas ASCII y no normaliza acentos. El modo
`semantic` recupera por significado mediante embeddings NVIDIA y pgvector en Neon.
El resultado incluye `items`, `matched_count` y `has_more`; una búsqueda sin
coincidencias es válida. Los resultados de inventario no se filtran por gustos:
puedes preguntar si tienes blanco aunque evites usarlo en recomendaciones.

El backend rechaza campos desconocidos, tipos incorrectos, códigos fuera del
contrato, límites inválidos y caracteres NUL antes de consultar Closet. No acepta
`user_id`, tokens, URLs ni SQL. Los hosts y rutas HTTP pertenecen a la configuración
del servicio; ningún argumento puede cambiarlos.

## get_outfit

Recibe únicamente `outfit_id`, UUID de una propuesta guardada en Recommendation.
No es un ID de prenda, request o conversación, ni de un outfit de Digital Closet.
El ID debe venir del usuario o del historial: el contexto del chat ahora conserva
los IDs de sus propuestas. Sin un ID conocido, el bot debe pedir aclaración.

Consulta el repositorio con el usuario derivado de IAM y comprueba también que la
respuesta tenga ese propietario y el ID solicitado. Un ID ajeno y uno inexistente
devuelven el mismo `NOT_FOUND`. No hay consultas por correo o por usuario elegido
por el modelo.

Devuelve solo la captura guardada de las prendas referenciadas y datos del outfit;
no envía correo, usuario, tokens, URLs firmadas ni otros outfits al modelo.
`snapshot_only=true` indica que **no verifica disponibilidad actual**. Si una
captura antigua no tiene datos suficientes, devuelve `SNAPSHOT_UNAVAILABLE`.
Para generar otra propuesta siguen siendo obligatorias las validaciones actuales
y `/garments/resolve` antes y después de generar.

## Ejecución y límites

1. Una fase breve de consulta ofrece exclusivamente estas tres tools. Se usa
   function calling sin el esquema de respuesta final: combinar ambos hizo que el
   proveedor respondiera un objeto final sin ejecutar la función.
2. Se admite una llamada por respuesta y hasta tres intentos por mensaje. Se
   rechazan IDs de llamada repetidos y consultas con los mismos argumentos
   canónicos. Tras una consulta exitosa se pasa directamente a la respuesta final.
   Esta primera versión atiende una consulta exitosa por mensaje; para dos consultas
   independientes, enviar dos mensajes.
3. Argumentos JSON: máximo 2048 bytes, objeto único, sin claves duplicadas ni texto
   sobrante, con profundidad máxima de 64 niveles. Las funciones desconocidas reciben
   `UNKNOWN_TOOL`, sin acceso a servicios. Se validan los tipos del sobre de respuesta
   y de la función antes de acceder a sus campos; un sobre malformado no debe cerrar el proceso.
4. Cada resultado ocupa como máximo 24 KiB; cada respuesta del proveedor, 64 KiB.
   El presupuesto de 90 segundos se comprueba entre pasos y limita el timeout de
   las llamadas al modelo. Las dependencias conservan sus propios timeouts; no es
   un temporizador HTTP global que cancele una consulta en curso.
5. La respuesta final vuelve a usar JSON Schema sin tools. Las acciones internas
   `SHOW_WARDROBE` y `SHOW_OUTFIT` necesitan evidencia de una consulta exitosa de ese
   mensaje. Sus listas se construyen en backend desde esa evidencia, no desde la
   prosa libre del modelo. Se descartan outfits copiados, referencias posicionales
   y cambios de categoría del modelo para estas acciones de consulta. Devuelven
   `recommendation: null`, conservan la memoria del chat y no crean outfits.

Una llamada puede corregir argumentos inválidos dentro del presupuesto. Fallos de
autenticación terminan con 401; fallos de dependencia con `503 TOOL_UNAVAILABLE`;
presupuesto agotado o repetición detectada con `502 TOOL_LIMIT_EXCEEDED`. Una llamada
en la fase final o un protocolo malformado produce 502. No se guarda medio turno.
Los reintentos de un mensaje ya guardado siguen devolviendo el turno existente.

Las tools son de lectura. No crean imágenes, modifican prendas, marcan favoritos
ni escriben gustos. Los cambios explícitos de gustos ya existentes se siguen
gestionando por la decisión tipada de Profile, fuera de estas funciones.
Los resultados y sus descripciones son datos no confiables para el modelo.
La clasificación y la elección de filtros siguen dependiendo de Nemotron; estos
guards protegen acceso, ejecución y estructura, no garantizan toda interpretación.

## Archivos y pruebas

- `infrastructure/ConversationTools.cpp`: contratos, validadores, filtros y acceso.
- `infrastructure/NvidiaRecommenderAgent.cpp`: fases, protocolo y límites.
- `application/ConversationPolicy.cpp`: evidencia obligatoria y texto verificado.
- `tests/tools_http.py`: servicio C++ real contra dobles HTTP controlados de
  IAM, Closet, almacenamiento y NVIDIA; incluye acceso ajeno y fallos de dependencias.

También se modificaron `application/RecommenderAgent.h` (executor/evidencia),
`application/RecommendationService.{h,cpp}` (inyección por sesión y errores),
`interfaces/RecommendationHttp.cpp` (IDs en contexto y errores de tools), `main.cpp`
y `CMakeLists.txt` (conexión y compilación). Los dobles de pruebas se extendieron en
`tests/nvidia_http.py` y `tests/conversation_http.py`. Los artefactos públicos locales
son `docs/openapi.json` y `postman/Mirage-Recommendation.postman_collection.json`.
La ampliación RAG reutiliza libpq/Drogon ORM ya instalados y el almacén privado
de Neon. No se envían vectores, credenciales ni identificadores de cuenta al modelo.
La búsqueda semántica usa la sincronización y guards de `VECTOR_STORE.md`.

Postman: carpeta `07 Tools de consulta`. Primero crear una conversación; para
`get_outfit`, generar previamente una propuesta o establecer su `recommendation_id`.

## search_style_knowledge

Lee `recommendation.style_knowledge` en Neon con la sesión verificada por IAM.
Argumentos: `query` obligatorio (1–512 bytes, no solo espacios ni NUL), `style`
opcional del vocabulario y `limit` de 1 a 3. Rechaza cualquier otro campo, incluyendo
`user_id`, URL o SQL. Nunca descarga una URL recibida ni modifica el corpus.
Solo ve referencias publicadas; las guías generales sin etiquetas también aplican
al filtro de estilo. Devuelve texto, título, fuente, licencia, atribución y similitud;
no prendas, credenciales ni vectores. El modelo no puede usar esos textos como instrucciones.

Una consulta explícita puede usar `SHOW_STYLE_KNOWLEDGE`, que necesita evidencia
de ejecución y se representa con títulos, extractos atribuidos y enlaces reales.
El backend descarta outfits copiados en esta acción y conserva las restricciones
vigentes. Los límites de llamadas, tamaño, tiempo y errores existentes también aplican.

Para generar con RAG activo, la recuperación de referencias es automática antes
de recuperar las prendas, aunque el modelo no llame a la tool. Las solicitudes
explícitas de outfits siguen omitiendo el planificador de consultas.
Ver `STYLE_KNOWLEDGE.md` para contenido, permisos, ingestión y pruebas.
