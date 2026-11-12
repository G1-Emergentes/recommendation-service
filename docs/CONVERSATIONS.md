# Conversaciones de recomendaciones

API protegida con IAM, persistida en la base propia de Recommendation.

- `POST /api/v1/conversations` con `{ "title": "Universidad" }`: crea un chat (201).
- `GET /api/v1/conversations?limit=20&cursor=<uuid>`: lista chats propios, con `next_cursor`.
- `GET /api/v1/conversations/{id}`: devuelve el chat y sus mensajes en orden.
- `POST /api/v1/conversations/{id}/messages` con
  `{ "message": "Quiero algo cómodo para clases", "client_message_id": "<uuid>" }`.
  Devuelve el turno con mensaje del usuario, respuesta y recomendaciones opcionales.

`client_message_id` identifica un envío: reutilizarlo con el mismo texto devuelve el
turno ya guardado sin volver a llamar al modelo. Otro texto con el mismo ID es 409.
No enviar dos mensajes simultáneos al mismo chat: el control de versión devuelve
409 si otra respuesta se guardó durante la generación. Los errores del modelo no
guardan medio turno. El texto tiene un límite de 2000 bytes UTF-8.

La primera versión admite 20 turnos por conversación. Al alcanzar el límite,
crear otra conversación; no se descarta memoria silenciosamente. El modelo ve
texto, preferencias y prendas de Digital Closet en `live` (fixtures en `demo`), sin
token, correo ni identificador de cuenta. Cada propuesta conserva `garments` con
los metadatos de las prendas finales revalidadas. Si cambia una prenda, el mensaje
falla con `409 WARDROBE_CHANGED` sin guardar medio turno.
El historial de outfits del chat conserva sus IDs para favoritos y rechazo.
Cambiar prendas depende del inventario disponible; no se garantiza una alternativa.
No ejecuta favoritos a través de texto: usar sus endpoints. Sí permite consultar,
agregar y olvidar gustos permanentes mediante Profile cuando se pide explícitamente.
La memoria temporal, las reglas de guardado y los errores entre servicios se
describen en `MEMORY.md`. Cada turno ahora incluye `memory`.

El prompt distingue conversación, aclaración, recomendación, modificación y
explicación. Una petición de outfit conserva esa intención aunque empiece por
“hola”. Las propuestas nuevas requieren outfits estructurados; sus nombres y
cantidad en la respuesta se construyen desde las prendas validadas. Las referencias
de modificación/explicación apuntan a la última propuesta estructurada del chat.
Reglas de colores y ubicación del prompt: `PROMPTS.md`.

Las tools `search_wardrobe`, `get_outfit` y `search_style_knowledge` ya están conectadas al chat con validación
por sesión, límites y evidencia obligatoria (`TOOLS.md`). No son rutas públicas
nuevas y sus consultas no crean outfits. `search_wardrobe` admite búsqueda semántica
cuando el backend activa RAG.
El servicio ya sincroniza descripciones y metadatos de Closet en pgvector en el
proyecto separado de Neon, con permisos por usuario, revisiones y revalidación de
disponibilidad (`VECTOR_STORE.md`). Con RAG activo, esa recuperación ya es un paso
obligatorio antes del agente: recibe candidatos por categoría, no el closet completo.
Las referencias externas de moda y guía editorial se recuperan antes de las prendas (`STYLE_KNOWLEDGE.md`). "Otra opción" no puede repetir una
combinación de la última propuesta; si no hay alternativa, corresponde aclarar.
La conversación no necesita vectores para recordar sus mensajes.

Con RAG activo se recuperan también ejemplos Polyvore compatibles con las categorías
disponibles. Se guardan como `outfit_examples` en propuestas nuevas; nunca son
prendas propias ni habilitan IDs externos. Ver `RAG_COMPLETION.md`.
