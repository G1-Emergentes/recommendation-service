# RAG de criterios de estilo

## Flujo implementado

Con `RECOMMENDATION_RAG_ENABLED=true`:

1. IAM autentica la sesión y Profile devuelve sus preferencias.
2. El backend consulta los fragmentos publicados de `recommendation.style_knowledge`.
3. Recupera obligatoriamente los candidatos privados del clóset mediante pgvector.
4. Recupera proyecciones Polyvore compatibles con las estructuras disponibles; Nemotron recibe
   `style_knowledge`, `outfit_examples` y `garments` como conjuntos separados.
5. Se validan estructura, IDs, memoria, restricciones y disponibilidad antes de guardar.

Una referencia no puede añadir prendas ni autorizar OUTERWEAR/ACCESSORY. La guía es
orientativa y sus textos no son instrucciones ejecutables. Un fallo de lectura o
de embeddings detiene el flujo sin fallback ni medio resultado. Una búsqueda válida
sin coincidencias devuelve un conjunto vacío, sin inventar fuentes. La preparación
actual se aplica también a otros turnos que pasan por el servicio, por lo que con
RAG activo esos turnos también dependen de Neon y embeddings.

`style_references` en nuevas propuestas conserva los fragmentos recuperados con
fuente, licencia y atribución. Se conserva dentro del contexto JSON de Recommendation,
sin nuevas tablas en Supabase. No demuestra que el modelo haya usado cada fragmento.

## Tool y API

`search_style_knowledge(query, style?, limit?)` consulta como máximo tres fragmentos
publicados. `query` admite 512 bytes UTF-8, no NUL ni solo espacios. `style` usa el
vocabulario de estilos y también incluye guías generales sin etiquetas.
Rechaza campos desconocidos, IDs de usuario, URLs y límites fuera de 1–3.

El modelo puede llamar la tool ante una consulta de criterios. Si tiene éxito,
el backend construye `SHOW_STYLE_KNOWLEDGE` con extractos atribuidos y enlaces
verificados, sin una segunda reformulación del LLM. No crea outfits ni modifica
la memoria del chat o Profile. Las llamadas inválidas mantienen el protocolo de
corrección y los límites existentes. Los prompts y contexto de generación sí
reciben las referencias para orientar outfits nuevos.

Ruta de inspección con token IAM:

```http
POST /api/v1/style-knowledge/search
Content-Type: application/json
Authorization: Bearer <sesión IAM>

{"query":"criterios para streetwear discreto","style":"STREETWEAR","limit":3}
```

Postman: carpeta `08 RAG - Neon`, peticiones `Consultar criterios de estilo y fuentes`
y `Chat - consultar criterios de estilo`. Swagger/OpenAPI: versión 0.8.0.

## Datos y permisos

La tabla está en la base `recommendation_vectors`, rama `dev-vector-store`, esquema
`recommendation`. Cada fila tiene texto, etiquetas, fuente, atribución, licencia,
revisión, fecha de revisión, publicación, hash y embedding `vector(1024)`.
Se usa el mismo modelo NVIDIA del índice de prendas. Solo se publican adaptaciones
con procedencia revisada; corpus y licencia: `../knowledge/README.md`.

Es conocimiento compartido, no información personal. RLS obligatorio permite al
rol `mirage_vector_app` leer únicamente filas publicadas con actor transaccional
establecido por el backend. Sin actor obtiene cero filas. El rol solo tiene SELECT;
la edición queda en el administrador. El índice privado de prendas conserva su
propia política por usuario. Frontend, IAM, Closet y modelo no reciben credenciales
ni conexión directa a Neon.

## Verificación y límites

- CTest incluye contrato HTTP, guards, flujo de servicio y regresiones existentes.
- `python tests/style_knowledge_http.py out/build/x64-debug/recommendation-service.exe --live-neon`
  usa Neon real y proveedores HTTP controlados. Verifica fuentes compartidas,
  contexto antes de prendas, persistencia de procedencia, llamada de tool, respuesta
  de solo lectura y parada ante embeddings inválidos. Limpia solamente los índices
  de actores sintéticos reservados; no modifica el corpus publicado.
- Las pruebas de prendas y generación siguen en `RAG_EVALUATION.md`.

Ocho fragmentos no cubren toda la moda ni validan calidad estética profesional.
La búsqueda devuelve los más cercanos, sin umbral de relevancia calibrado ni
reranker. No hay navegación web en tiempo real. Los textos y fuentes pueden
necesitar revisión; ampliar el corpus requiere curación y pruebas de recuperación,
no cargar páginas indiscriminadamente.

Prueba real adicional realizada el 2026-11-11: una búsqueda de streetwear recuperó
primero `mirage-streetwear-v1`; Nemotron llamó efectivamente a
`search_style_knowledge` y la respuesta mostró fuentes y licencia. Otra petición
generó un outfit sin blanco usando referencias y prendas reales; `style_references`
coincidió con el contexto enviado al modelo. IAM, Closet, NVIDIA y Neon fueron
reales; el chat usó almacenamiento HTTP local de prueba para no crear conversaciones
en Supabase. Reporte privado local: `out/style-knowledge-quality.json`.
Se comprobó además con el rol real que sin actor obtiene cero filas, que un actor
ve las ocho publicadas y que no tiene INSERT, UPDATE ni DELETE en el corpus.

Polyvore y cierre funcional: `../knowledge/polyvore/README.md` y `RAG_COMPLETION.md`.
