# Prompt y coherencia del chat

El system prompt conversacional está en
`recommendation/infrastructure/RecommendationPrompts.h`, constante
`conversationSystemPrompt`. El adaptador `NvidiaRecommenderAgent.cpp` lo envía a
NVIDIA como el primer mensaje `system`. Se compila con el servicio: editar el
archivo requiere compilar y reiniciar Recommendation.
La fase previa de selección de tools tiene su propio `toolSelectionSystemPrompt`
en el mismo archivo. Solo recibe el mensaje y las propuestas conocidas, sin el
inventario completo, y los títulos de las referencias locales. La respuesta final
recibe los candidatos autorizados y los resultados de tools. Todas las peticiones
conversacionales pasan por el selector: puede llamar search_fashion_web antes de
generar si necesita inspiración externa. Una petición normal con información
suficiente sigue directamente a la fase final, sin buscar en la web.
Protocolo, guards y límites: `TOOLS.md`.

## Decisión estructurada interna

El JSON que devuelve el modelo al backend ahora exige:

- `action`: `CHAT`, `CLARIFY`, `RECOMMEND`, `MODIFY`, `EXPLAIN`, `REDIRECT`,
  `PREFERENCES_READ`, `PREFERENCES_UPDATE`, `SHOW_WARDROBE`, `SHOW_OUTFIT`,
  `SHOW_STYLE_KNOWLEDGE` o `SHOW_FASHION_WEB`.
- `assistant_message`: texto conversacional. No reemplaza los outfits estructurados.
- `outfits`: las combinaciones con `garment_ids` y `explanation`.
- `excluded_colors`: exclusiones explícitas agregadas por el último mensaje; se
  acumulan con el estado guardado. Omitirlas no elimina exclusiones anteriores.
- `dark_only`: exige una paleta oscura confirmada.
- `reference_outfit`: referencia 1–3 de la última propuesta estructurada, o 0.
- `change_category`: `NONE` o la categoría concreta que se modifica.
- `memory_update`: colores que permite volver a usar, permiso para relajar el
  requisito oscuro y cambios de estilo/ocasión (`null` conserva; `""` elimina).
- `preference_update`: operación, campo y códigos de gustos permanentes. Solo se
  aplica con `PREFERENCES_UPDATE`, lenguaje explícito y confirmación de IAM.

Es un contrato entre el adaptador y el modelo. El request público de Postman sigue
usando `message` y `client_message_id`; no necesita recibir ni enviar estos campos.
La respuesta pública conserva `assistant_message` y `recommendation`.
Ahora también devuelve `memory`. Detalles, ejemplos y límites: `MEMORY.md`.

## Reglas que hace cumplir el backend

`application/ConversationPolicy.cpp` valida la decisión antes de persistir:

- `RECOMMEND` y `MODIFY` necesitan outfits; `CHAT`, `CLARIFY` y `EXPLAIN` necesitan
  la lista vacía. Una respuesta inconsistente falla con 502, sin guardar medio turno.
- `MODIFY` referencia la última propuesta guardada, devuelve un outfit, cambia
  realmente la categoría declarada y conserva los IDs de las demás categorías.
- `EXPLAIN` necesita una opción existente y no crea otra propuesta.
- Las prendas de una propuesta no pueden incluir colores explícitamente excluidos.
  Si se exige una exclusión y se desconoce el color de una prenda, no puede usarse
  para afirmar que cumple esa restricción.
- `dark_only` acepta únicamente prendas con todos sus colores `BLACK`: el contrato
  actual describe familias de color y no tiene un tono oscuro confirmado para `BLUE`
  o `GRAY`. La restricción es conservadora y debe ampliarse cuando exista información
  fiable de tono. El prompt pide aclaración antes de aproximar con otra paleta.
- Las categorías y disponibilidad siguen verificándose con las políticas existentes
  y con Digital Closet. Capas y accesorios siguen fuera de las propuestas actuales.

El texto que enumera outfits en `RECOMMEND`/`MODIFY` se construye desde las prendas
validadas, con el número real de opciones y sus nombres. No reutiliza el listado
libre de `assistant_message` que pudo inventar el modelo. Las explicaciones de cada
outfit siguen disponibles en `recommendation.outfits[].explanation`.
Al explicar una opción se usan sus IDs y explicación guardados. Saludos y preguntas
de aclaración conservan texto del modelo, excepto la falta de una paleta oscura
completa, cuya aclaración construye el servicio. La clasificación de
intención y extracción de restricciones siguen siendo tareas del modelo.
El chat usa temperatura 0 para reducir variaciones; esto no garantiza que el modelo
siempre clasifique o extraiga restricciones correctamente.

## Casos de verificación

Pruebas de política y HTTP: saludo aislado, saludo con petición de outfit, propuesta
solo en prosa, acción incompatible con outfits, colores excluidos, paleta oscura sin
confirmación, referencia inexistente, modificación sin cambio y cambio indebido de
otra categoría. Las respuestas inválidas no crean propuestas ni turnos parciales.

Las conversaciones y respuestas antiguas no se reescriben. Para probar un prompt
nuevo, crear una conversación o enviar un mensaje nuevo. Reintentar el mismo
`client_message_id` recupera la respuesta ya guardada con el comportamiento anterior.

## Ajustes de RAG y alternativas

En `conversationSystemPrompt`, sección `OUTFITS VALIDOS`, se añadió que el inventario
es un subconjunto recuperado, que la similitud no demuestra compatibilidad, que
"otra opción" exige otros IDs y que la insuficiencia requiere `CLARIFY`, sin inventar
prendas o tonos. El límite `max_results` sigue siendo máximo, no cuota.
En `toolSelectionSystemPrompt` se distinguió generar de consultar prendas y se
aclaró que `sin blanco` nunca debe convertirse en `color: WHITE`.

La protección no depende solo del prompt: `ConversationPolicy` reconoce algunas
peticiones explícitas en español para enrutar generación, activar un requisito
oscuro inequívoco y preservar exclusiones literales como `sin blanco ni beige`,
aunque el modelo omita uno de esos colores. El modelo sigue interpretando el resto del lenguaje. En RAG,
`RecommendationService` comprueba si existe una combinación completa compatible;
si no existe, responde y guarda una aclaración con la memoria vigente sin llamar
al LLM (`llm_used=false` cuando corresponde). Si pide otra opción, selecciona una
combinación nueva dentro de los candidatos y limita sus IDs para la generación.
La comparación ignora el orden y toma las últimas propuestas estructuradas del chat,
no todos los outfits históricos.

La relajación inequívoca (`puedes relajar que sea todo oscuro`, o aceptar una
aproximación con el jean azul) también tiene una regla explícita: el modelo no
puede reactivar por descuido el flag oscuro antiguo en ese mismo turno. Las
exclusiones de color se conservan. Estos reconocedores son acotados a expresiones
comunes en español; no sustituyen una comprensión general del lenguaje.
