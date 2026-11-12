# Cliente HTTP de Digital Closet

En esta PC: IAM `8080`, Digital Closet `8081`, Recommendation `8085`.

```dotenv
RECOMMENDATION_MODE=live
DIGITAL_CLOSET_BASE_URL=http://127.0.0.1:8081
DIGITAL_CLOSET_TIMEOUT_SECONDS=10
```

La URL es solo el origen, sin `/api/v1`. Se permite HTTPS remoto o HTTP en loopback.
El timeout de cada llamada admite 1–30 segundos. `run-recommendation.ps1` carga
estas variables desde `.env`. `-Demo` selecciona explícitamente fixtures; sin ese
switch se respeta el modo configurado. El valor predeterminado sin configuración
sigue siendo `disabled`. No se requiere la secret key de Closet en Recommendation.

## Flujo de generación

1. Validar sesión con IAM y consultar las preferencias de Profile.
2. Consultar `GET /api/v1/garments?limit=100`, siguiendo cada `next_cursor` opaco.
3. Validar el read model y filtrar disponibilidad, exclusiones y estilo.
4. Resolver los candidatos con `POST /api/v1/garments/resolve`.
5. Generar outfits con reglas o Nemotron, validando IDs, categorías y explicaciones.
6. Resolver los IDs finales y comprobar revisión y todos sus atributos contra la
   instantánea usada al generar, incluida la referencia/versionado de imagen.
7. Solo entonces persistir la propuesta o el turno completo del chat.

Cada llamada a Closet lleva `Authorization: Bearer` con el token del usuario
validado. No lleva un `user_id` suministrado por el cliente ni claves de otros
proveedores. Closet decide la propiedad de las prendas. Una redirección no se
sigue; una página fallida nunca se interpreta como un armario vacío.

## Errores y límites

- Sesión rechazada por Closet: `401 UNAUTHENTICATED`.
- Datos cambiados, eliminados o no disponibles durante el flujo:
  `409 WARDROBE_CHANGED`. No se persiste nada. Solicitar otra propuesta; no hay
  reintento automático que consuma otra generación.
- Fallo HTTP/red, timeout, respuesta inválida, página repetida, IDs duplicados o
  resolución incompleta: `503` (`WARDROBE_UNAVAILABLE` en generación directa).
  En chat se mantiene el sobre de fallo de generación, sin guardar medio turno.
- Límite inicial: 100 prendas elegibles, 1000 listadas y 100 páginas por recorrido.
  Si se supera, `422 WARDROBE_LIMIT_EXCEEDED`, sin recortar ni cambiar a demo.
  La etapa RAG incorporará selección acotada de candidatos en armarios mayores.
- Armario vacío: en generación directa se devuelve `INSUFFICIENT_GARMENTS`;
  el chat puede responder sin outfits.

La revalidación es una comprobación del estado en ese momento: no reserva prendas
ni crea una transacción distribuida entre Closet y Recommendation. Closet debe
volver a validar al guardar o visualizar un outfit. Los reintentos de mensajes ya
guardados recuperan su instantánea histórica; no vuelven a generar ni a reservar prendas.

## Metadatos conservados

`garments` contiene una sola instantánea por prenda usada, con los once campos del
contrato: `id`, `name`, `category`, `subcategory`, `colors`, `styles`, `description`,
`availability`, `image`, `revision`, `updated_at`. `image` es null o contiene solo
`id` y `version`. No se conservan URLs firmadas.

Estas instantáneas se guardan en el contexto JSON de solicitudes de Recommendation,
también dentro de las recomendaciones asociadas a turnos. Se recuperan al leer
detalle e historial. Registros antiguos pueden no tener `garments`.
Son evidencia histórica de la propuesta; no representan el estado vigente del armario.

La estructura de dominio conserva estos campos para la futura proyección RAG.
El modelo textual recibe la subcategoría y atributos descriptivos, sin credenciales
ni URLs de imágenes. Todavía no hay embeddings, índice vectorial ni sincronizador.

## Verificación

`recommendation-closet-http` ejecuta el servicio C++ real con dependencias HTTP
controladas. Verifica paginación, token, metadatos, dos fases de resolve, cambios de
revisión/imagen, eliminación, disponibilidad, sesiones rechazadas, fallos de páginas,
respuestas inválidas, límites y atomicidad del chat ante un conflicto.
`tests/history_sql.py` comprueba la conservación de `mode=live` y metadatos en PostgreSQL.

Prueba local con servicios reales: IAM, Closet, NVIDIA y Supabase de Recommendation;
generación directa y chat, incluyendo lectura posterior de sus metadatos. Las tres
prendas temporales se retiraron con la API de Closet. Solo las solicitudes y el chat
temporales de esa prueba se eliminaron de Recommendation.
