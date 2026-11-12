# FashionStylist: búsqueda web bajo demanda

Fuente: https://github.com/recsys-benchmark/FashionStylist
Licencia: https://github.com/recsys-benchmark/FashionStylist/blob/f834484cecaed20d08fea97be7781426063a7275/LICENSE
CC-BY-NC-4.0, FashionStylist Authors, 2026. Esta integración es para el prototipo
académico/no comercial; revisar o sustituir la fuente antes de un uso comercial.

El proyecto anunció su versión inglesa el 8 de abril de 2026. La revisión verificada
es `f834484cecaed20d08fea97be7781426063a7275` (9 de abril de 2026). Publicación reciente
no equivale a tendencias actuales ni a una garantía de calidad estética.

La herramienta `search_fashion_web` consulta únicamente:

- `Dataset/Female/look_en.csv`: 500 descripciones de outfits.
- `Dataset/Male/look_en.csv`: 300 descripciones de outfits.

Se leen `outfitID`, `look`, `season` y `occasion`. No se descargan imágenes, enlaces
comerciales, modelos ni archivos ejecutables. Los grupos son etiquetas de origen;
no se infiere género del usuario. Por defecto se consulta ANY.

El backend descarga por HTTPS esos dos archivos al necesitar la herramienta,
con revisión fija, timeout y límite de 256 KiB por archivo. Valida el esquema CSV,
campos, duplicados, quoting y tamaño. Mantiene una caché pública en memoria por
30 minutos y comparte la descarga entre consultas simultáneas (hasta 16 esperas).
Un fallo impone 15 segundos de espera antes de intentar otra descarga. No sigue
redirecciones ni usa URLs del usuario o del modelo. Las solicitudes externas
no llevan token IAM, cookies, consulta de usuario ni datos del clóset.

## Contrato interno

```json
{"query":"quiet luxury","audience":"ANY","season":"ANY","limit":3}
```

`query`: 2–200 caracteres ASCII, letras/espacios/guiones; el modelo traduce a
palabras clave en inglés. `audience`: ANY, MALE, FEMALE. `season`: ANY, SPRING,
SUMMER, AUTUMN, WINTER. `limit`: 1–3. No acepta user_id, URLs, tokens ni paths.

La búsqueda utiliza coincidencia de términos normalizados, elimina palabras
genéricas y ordena por términos distintos encontrados. No es búsqueda semántica
ni una búsqueda sobre toda la web. Una lista vacía no demuestra ausencia de
información en otras fuentes. A diferencia del RAG de Neon, no genera embeddings
ni modifica el índice vectorial.

Cada resultado lleva ID externo, descripción, temporada/ocasión, URL al CSV con
línea original, revisión, fecha de publicación, licencia y atribución. Los IDs
`fashionstylist-*` nunca son garment_ids de Digital Closet.

## Agente y frontend

El selector de herramientas considera esta búsqueda antes de la respuesta JSON,
incluidas las peticiones de generar outfits. Consulta cuando el usuario pide
fuentes web o necesita inspiración no cubierta por las referencias locales.
Un éxito permite continuar con RECOMMEND/MODIFY y las prendas autorizadas.
SHOW_FASHION_WEB presenta referencias verificadas sin crear outfits.

Los resultados externos se pasan como mensajes de herramienta, nunca como
instrucciones de sistema ni inventario. Siguen vigentes validación de IDs propios,
colores, memoria, categorías y revisiones de Closet. La herramienta no escribe
Profile, prendas ni Neon. Un error devuelve EXTERNAL_SOURCE_UNAVAILABLE; el agente
debe reconocerlo y puede continuar con el contexto local sin afirmar que consultó
fuentes web. Las consultas inválidas devuelven INVALID_ARGUMENTS antes de HTTP.

Los outfits del chat conservan `recommendation.web_references`, visibles en
**Referencias consultadas** del frontend. Las respuestas que solo consultan
fuentes conservan enlaces y atribución en el texto persistido de la conversación.

Prompt: `recommendation/infrastructure/RecommendationPrompts.h`, bloques
`conversationSystemPrompt` y `toolSelectionSystemPrompt`. Ejecutor y definiciones:
`ConversationTools.cpp`. Cliente, caché y búsqueda: `FashionWebSearch.cpp`.

Pruebas: `fashion_web_tests.cpp` (parser y límites) y `fashion_web_http.py`
(servicio C++ real con servidores HTTP controlados para GitHub, NVIDIA, IAM,
Closet y almacenamiento). Incluyen redirecciones, rate limit, timeout, HTML,
tamaño excesivo, caché, idempotencia y bloqueo de IDs externos/colores excluidos.
El origen loopback se admite únicamente para esas pruebas; el origen de entrega
es `https://raw.githubusercontent.com` y los paths/revisión permanecen fijos.

Para probar desde el chat: “Busca en la web ejemplos de old money en FashionStylist
y muéstrame las fuentes”. Postman: carpeta **09 Web - FashionStylist**.
