# Mirage Recommendation Service

Base independiente en C++17 y Drogon para el bounded context Recommendation.
Usa capas de dominio, aplicación, infraestructura e interfaces, como IAM.

## Estado actual

- `GET /health`: estado del proceso y capacidades habilitadas.
- Swagger UI en `/docs` (también `/swagger`) y contrato OpenAPI 3.0.3 en `/openapi.json`.
- `POST /api/v1/recommendations`: prendas privadas de Digital Closet en modo `live`; fixtures explícitas en `demo`.
- Combinaciones base: superior + inferior + calzado, o una pieza + calzado.
- Exclusión de prendas no disponibles o indicadas en la solicitud; filtro por etiquetas de estilo conocidas.
- Cliente HTTP de Digital Closet con Bearer de IAM, paginación completa y `resolve` antes/después de generar.
- Metadatos de prendas finales conservados en `garments`: subcategoría, revisión, fecha e imagen versionada.
- Autenticación real delegada a IAM mediante `GET /api/v1/auth/me` en cada POST.
- Preferencias reales de Profile mediante `GET /api/v1/profiles/{userId}/preferences` con el mismo token.
- Persistencia atómica en Supabase; historial, detalle, favoritos y rechazo por usuario.
- Adaptador real de NVIDIA Nemotron con respuesta JSON estructurada y validación antes de persistir.
- Chat con texto libre: crear/listar conversaciones, leer mensajes y enviar mensajes con memoria
  persistida y outfits opcionales. Ver `docs/CONVERSATIONS.md`; hasta 20 turnos por chat.
- Pruebas de dominio, aplicación y HTTP.
- RAG con pgvector en Neon: prendas privadas, criterios de estilo y ejemplos Polyvore.
- Cuatro tools: `search_wardrobe`, `get_outfit`, `search_style_knowledge` y `search_fashion_web`.
  La búsqueda externa consulta FashionStylist bajo demanda, con fuentes y licencia
  CC-BY-NC-4.0 para el prototipo académico/no comercial. Ver [TOOLS](docs/TOOLS.md).

Este repositorio contiene únicamente Recommendation. El frontend Flutter, IAM y
Digital Closet se mantienen en sus propios repositorios. Las claves, sesiones,
imágenes privadas y resultados de compilación están excluidos mediante `.gitignore`.

La demo autentica con IAM, pero usa prendas sintéticas compartidas: no consulta el armario real,
puede usar reglas o Nemotron según `RECOMMENDATION_ENGINE`. Persiste solicitudes y outfits en Supabase.
La identidad del usuario procede exclusivamente de IAM.
El servidor escucha exclusivamente en `127.0.0.1`; el modo predeterminado es `disabled`.
El modo `live` consulta el armario real. El despliegue público sigue pendiente.
El motor de reglas no evalúa la ocasión; Nemotron sí la recibe para proponer y explicar combinaciones.
Se priorizan estilos y colores preferidos de Profile y se excluyen colores evitados conocidos.
No se evalúan clima ni compatibilidad visual. Los colores desconocidos no pueden comprobarse.

## Compilar en Windows

Requisitos: herramientas C++ x64 de Visual Studio, CMake, Ninja y Drogon instalado
en el vcpkg existente. Abrir una consola de desarrollo x64 de Visual Studio y situarse
en esta carpeta. Ajustar la ruta de vcpkg si es diferente:

```powershell
cmake --preset x64-debug -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build --preset x64-debug
ctest --preset x64-debug
```

No instala dependencias ni crea proyectos en Supabase. CMake copia el dataset a
la carpeta de compilación; si se edita, volver a configurar antes de ejecutar.
Las pruebas HTTP se registran si CMake encuentra Python 3.

## Ejecutar

```powershell
./run-recommendation.ps1 -Demo
```

Puerto predeterminado: `8085`, separado del `8080` de IAM. Iniciar IAM antes de probar el POST.
Recommendation utiliza un proyecto Supabase propio, con URL y secret key privada del backend.
El JWT del usuario se envía solo a IAM/Profile y no se propaga a la base de Recommendation.
Conservar `.env` fuera de Git; nunca copiar esta secret key a Flutter, Swagger o al chat.
Para configurar el puerto o el modo, copiar `.env.example` a `.env` y editarlo localmente.
El script carga `RECOMMENDATION_MODE`, `RECOMMENDATION_ENGINE`, `RECOMMENDATION_PORT`, `IAM_BASE_URL` y
`IAM_TIMEOUT_SECONDS`, `IAM_SUPABASE_URL`, `RECOMMENDATION_SUPABASE_URL` y
`RECOMMENDATION_SUPABASE_SECRET_KEY`, además de la configuración `NVIDIA_*` indicada abajo;
`-Demo` activa la generación con prendas sintéticas.
Sin la opción ni configuración de demo, un POST autenticado responde `503 NOT_CONFIGURED`.
El ejecutable no lee `.env` por sí mismo: el script carga sus variables.

Desde otra terminal:

```powershell
Invoke-RestMethod http://127.0.0.1:8085/health

# Usar el access token obtenido al iniciar sesión en IAM. No usar el refresh token.
# Mantenerlo en memoria; no guardarlo en .env ni en archivos del proyecto.
$headers = @{ Authorization = "Bearer $accessToken" }
$body = @{
    occasion = 'Universidad'
    style = 'CASUAL'
    excluded_garments = @()
    max_results = 3
} | ConvertTo-Json
Invoke-RestMethod http://127.0.0.1:8085/api/v1/recommendations -Method Post -ContentType 'application/json' -Headers $headers -Body $body
```

También puedes abrir `http://127.0.0.1:8085/docs`, desplegar el POST y pulsar
**Authorize**, pegar únicamente el access token de IAM y después usar
**Try it out → Execute**. Incluye ejemplos para generar outfits y excluir todo el calzado.
La documentación está disponible en los modos `disabled` y `demo`; generar requiere demo.
Swagger usa archivos locales, sin CDN, y la URL de servidor relativa permite probar
en el mismo host y puerto desde el que se abrió. La autorización no se conserva al recargar.
El contrato solo anuncia endpoints implementados.

## Validación de identidad y sesión

- `/health`, `/docs`, `/swagger` y `/openapi.json` permanecen públicos.
- El POST exige `Authorization: Bearer <access_token>` también en modo demo o disabled.
- Recommendation hace un GET asíncrono a `IAM_BASE_URL/api/v1/auth/me`, propagando únicamente
  el access token, y usa el UUID `id` de la respuesta de IAM. No acepta `user_id` en el body
  ni usa metadata editable o cabeceras del cliente para determinar la identidad.
- IAM ya comprueba la identidad en Supabase y que la sesión siga activa. Recommendation
  no decodifica JWT como prueba de identidad ni duplica las reglas de revocación.
- No se cachean validaciones exitosas: tras un logout, una nueva solicitud consulta otra vez IAM.
- Token ausente/malformado o rechazo de IAM (400/401/403): `401 UNAUTHENTICATED`, con
  `WWW-Authenticate: Bearer`. El cliente renueva sesión a través de IAM cuando corresponda.
- IAM inaccesible, timeout, redirección, respuesta inválida o un estado inesperado:
  `503 IAM_UNAVAILABLE`. Nunca permite acceso por defecto ni devuelve datos privados de IAM.
- `IAM_BASE_URL` usa por defecto `http://127.0.0.1:8080`. Es un origen sin ruta ni slash final.
  Fuera de localhost/127.0.0.1 exige HTTPS, verificando certificados; nunca desactivar TLS.
- Timeout configurable: `IAM_TIMEOUT_SECONDS`, entero de 1 a 30; por defecto 25 segundos
  para cubrir las dos comprobaciones secuenciales de IAM. No se reintenta ni se siguen redirecciones.
- El token se usa en memoria para la comprobación y no se registra ni persiste. Su renovación,
  login y logout pertenecen a IAM/Flutter, no a Recommendation.

Las pruebas HTTP usan un IAM simulado y verifican identidad, expiración/revocación,
propagación del token, rechazos, dependencia caída, timeout, respuestas malformadas y aislamiento
de la identidad frente a datos enviados por el cliente. No necesitan cuentas externas.

Verificación local del 10 de noviembre de 2026: el IAM real rechazó un token inválido y
Recommendation devolvió `401`. No se pudo completar el flujo con una sesión real válida:
el login con la cuenta de pruebas existente fue rechazado por IAM (`400 auth_request_rejected`).
No se modificaron credenciales. La aceptación de sesiones válidas y la revocación posterior
se verificaron con el IAM simulado; queda pendiente contrastarlas con un login real vigente.

## Preferencias de Profile

Después de validar la identidad y el body, el servicio consulta únicamente las preferencias del
usuario validado, con el mismo access token. No solicita fotos, avatar, medidas ni el perfil completo;
tampoco modifica o persiste los datos de IAM. Usa el mismo `IAM_BASE_URL` y timeout que autenticación.

Profile entrega `preferredStyles`, `preferredColors`, `avoidedColors` y `preferredCategories`.
El adaptador recorta espacios, normaliza letras ASCII a mayúsculas y elimina duplicados resultantes.
Valida cuatro listas de hasta 50 strings de 1 a 80 bytes y rechaza colores contradictorios tras
normalizar. Las preferencias vacías son válidas. El vocabulario comparable es el del contrato de
Closet; etiquetas libres que no coincidan con códigos de prendas no aportan coincidencias. No se
traducen automáticamente etiquetas como `azul` a `BLUE`.

- Los estilos guardados priorizan prendas si la solicitud no incluye `style`.
- Un `style` explícito sustituye esa prioridad y conserva el filtro por etiquetas conocidas.
- Los colores preferidos priorizan prendas dentro de cada categoría. Estilo y color aportan
  una prioridad cada uno; los empates conservan el orden del armario. No es un ranking de IA.
- Los colores evitados excluyen toda prenda con alguna coincidencia conocida, incluso si tiene
  también colores preferidos. Esto puede dejar un outfit imposible: se devuelve lista vacía.
- Las categorías preferidas se recuperan y devuelven como contexto; todavía no alteran la selección.
- `404` en Profile produce `409 PROFILE_REQUIRED`; crear el perfil pertenece a IAM.
- `401/403` en Profile produce `401 UNAUTHENTICATED`, incluida revocación entre ambas consultas.
- Fallos de red, timeout, redirección, contenido inválido o estados inesperados producen
  `503 PROFILE_UNAVAILABLE`. No se sustituyen silenciosamente por preferencias vacías.
- Las preferencias se consultan en cada solicitud, para reflejar cambios sin reiniciar.

La integración de Profile se verificó con un servidor IAM simulado que respeta las rutas y el
formato del controlador existente, incluidos cambios de preferencias y errores. La prueba con
una sesión real vigente sigue pendiente por el rechazo del login de la cuenta de pruebas indicado
arriba. No se modificó IAM ni se conectó Digital Closet.

## Modelo generativo NVIDIA

La integración usa C++ y Drogon, sin SDK Python en producción. Configurar únicamente en `.env`:

```dotenv
RECOMMENDATION_ENGINE=nemotron
NVIDIA_API_KEY=nvapi-TU_CLAVE_PRIVADA
NVIDIA_MODEL=nvidia/nemotron-3.5-lightning-30b-a3b
NVIDIA_TIMEOUT_SECONDS=30
```

El `.env` local ya tiene seleccionado Nemotron. `RECOMMENDATION_MODE=demo` sigue indicando
que el armario es ficticio; no significa que la respuesta de IA sea simulada.
Para volver al motor determinista, usar `RECOMMENDATION_ENGINE=category_rules` y reiniciar.
El ejecutable carga su configuración del entorno; el script de ejecución lee `.env`.

El adaptador llama a `POST https://integrate.api.nvidia.com/v1/chat/completions` con:

- Modelo indicado arriba, `temperature=1`, `top_p=0.95` y `max_tokens=2048`.
- `stream=false` para obtener y validar el JSON completo antes de guardarlo.
- `chat_template_kwargs.enable_thinking=false`; no se devuelve ni guarda razonamiento interno.
- `response_format=json_schema`, con IDs elegibles enumerados y hasta tres outfits.

El proveedor no admite `uniqueItems` en su esquema: la unicidad se comprueba en el backend.
Se filtran previamente disponibilidad, exclusiones, estilo explícito y colores evitados.
El modelo recibe ocasión, preferencias de estilo/color y prendas con nombre, descripción,
categoría y atributos. Nunca recibe tokens, claves, IDs de usuario, emails, fotos o medidas.
Las descripciones y la ocasión se tratan como datos, no como instrucciones del sistema.

El backend verifica que cada propuesta use solo IDs elegibles, sin prendas repetidas ni
outfits duplicados por permutación, y que tenga una de las dos combinaciones soportadas:
superior + inferior + calzado, o una pieza + calzado. Las explicaciones deben contener texto
no vacío de hasta 1000 bytes. Las capas exteriores y accesorios aún no forman parte de estas plantillas.
Solo después se persiste el resultado en la base independiente de Recommendation.

Si no hay una combinación posible, no se llama a NVIDIA y se devuelve la respuesta de
prendas insuficientes con `llm_used=false`. Las respuestas inválidas o truncadas producen
`502 MODEL_INVALID_RESPONSE`; clave ausente, fallos o timeout producen `503 MODEL_NOT_CONFIGURED`
o `MODEL_UNAVAILABLE`; cuotas del proveedor producen `503 MODEL_RATE_LIMITED`.
No se reintenta ni se sustituye silenciosamente una respuesta fallida por reglas.

`NVIDIA_TIMEOUT_SECONDS` acepta enteros de 1 a 60, por defecto 30.
`NVIDIA_API_ORIGIN` se reserva para pruebas: admite el origen oficial de NVIDIA o HTTP en
loopback. Un origen externo arbitrario se rechaza para evitar enviar la clave a otro proveedor.
Reiniciar el proceso después de modificar `.env`. Las claves nunca se imprimen ni se exponen en Swagger.

Prueba real completada con NVIDIA y Supabase: el modelo generó dos outfits válidos y se
verificaron persistencia, historial y feedback. IAM/Profile se simularon con una identidad
temporal y preferencias ficticias. Los registros temporales se eliminaron después.
La vista legible de esa primera respuesta está en `out/nemotron-first-result.json` (sin claves).
Hay conversaciones persistidas con memoria de mensajes y outfits; todavía no hay embeddings ni RAG.

Referencia del modelo: https://build.nvidia.com/nvidia/nemotron-3.5-lightning-30b-a3b/build

Respuesta demo: `mode`, `engine`, `llm_used`, `model` cuando corresponde, `persisted: true`, `request_id`, `created_at`, `parameters`, `occasion`, `user_id`, `profile_preferences`, `status` y `outfits`.
`profile_preferences` contiene las cuatro listas normalizadas en snake_case, solo del propio usuario.
Cada outfit contiene `garment_ids` y una explicación del modelo o del motor de reglas.
Cada outfit tiene un `id` propio, `request_id`, `created_at`, `is_favorite`, `status`,
`rejected_at` y `rejection_reason`. Usar el ID del outfit para detalle y feedback.
`request_id` agrupa las propuestas de una solicitud. Los turnos del chat conservan las propuestas y sus IDs.
Con prendas insuficientes devuelve `200`, `status: INSUFFICIENT_GARMENTS` y `outfits: []`.
Los errores siguen el sobre `error.code` / `error.message`.

## Historial y feedback

Todas estas operaciones validan la sesión en IAM. Después el backend accede a su base privada
y limita cada RPC al `user_id` validado, sin aceptar esa identidad desde el body del cliente:

| Método y ruta | Comportamiento |
| --- | --- |
| `POST /api/v1/recommendations` | Genera y guarda la solicitud y hasta tres outfits en una transacción. |
| `GET /api/v1/recommendations` | Historial propio, incluidos rechazados; más recientes primero. |
| `GET /api/v1/recommendations/{id}` | Snapshot de una propuesta propia con sus parámetros y feedback. |
| `POST /api/v1/recommendations/{id}/favorite` | Body vacío o `{}` marca favorito; `{"favorite":false}` lo quita. |
| `POST /api/v1/recommendations/{id}/reject` | Body vacío o `{}` rechaza sin motivo; `{"reason":"..."}` registra un motivo de hasta 500 bytes UTF-8. |

Historial admite `limit` (1–100, por defecto 20), `cursor` (UUID del último outfit de la
página anterior) y `favorite=true/false` opcional. Responde `items` y `next_cursor`;
`null` indica el final. Orden por fecha e ID descendentes evita saltos en empates de fecha.
Las inserciones nuevas no alteran las páginas siguientes; cambios de favorito entre consultas
pueden cambiar las páginas filtradas. Un cursor inexistente o ajeno devuelve `400`.

Un ID inexistente o ajeno devuelve el mismo `404 RECOMMENDATION_NOT_FOUND`, incluso al
modificar feedback. Marcar/quitar favorito y repetir el mismo rechazo es idempotente.
Rechazar quita favorito y conserva la primera fecha de rechazo; un motivo posterior reemplaza
al anterior. Marcar favorito una propuesta rechazada devuelve `409 RECOMMENDATION_REJECTED`.
No se añade una acción de restaurar un rechazo en esta entrega.

Historial y feedback funcionan también con generación `disabled` y no consultan Profile.
Mostrar historial no vuelve a comprobar las prendas: muestra lo que se recomendó en ese momento.
Guardar el outfit definitivo pertenece a Digital Closet y queda fuera de estas acciones.

Si Supabase falla, el servicio devuelve `503 HISTORY_UNAVAILABLE` en vez de afirmar que se
guardó una propuesta. Configuración ausente devuelve `503 HISTORY_NOT_CONFIGURED`.
No hay reintentos automáticos de creación; repetir manualmente un POST de generación puede crear
otra solicitud. Las acciones de feedback sí se pueden repetir con el mismo resultado.

## Persistencia y comprobaciones

Proyecto de Auth: `larpiojfyzlqhmxfhbxm` (`auth database`).
Proyecto de Recommendation: `mhtdipluiehprnmigtkx` (`recommendation database`).
Ambos pertenecen a la misma organización, pero tienen bases y credenciales independientes.
El proyecto nuevo se creó con una cotización de US$0/mes.

La migración `supabase/migrations/20261110235558_recommendation_isolated_history.sql` está
aplicada solo al proyecto de Recommendation. Crea `recommendation.requests` y
`recommendation.outfits`, cuatro RPC y dos helpers. No depende de `auth.users`,
`auth.sessions`, `auth.uid()` ni `mirage_session_active()`. `user_id` es una referencia lógica
a la identidad de IAM. Las relaciones internas impiden vincular un outfit a la solicitud de otro usuario.

El servicio verifica identidad/sesión en IAM antes de cada operación y construye `p_user_id`
con la identidad validada. Cada RPC filtra explícitamente por ese actor; las funciones son
`SECURITY INVOKER`, con search_path vacío y permisos solo para `service_role` del backend.
`anon` y `authenticated` no pueden ejecutar RPC ni acceder a las tablas. La secret key se
envía únicamente como `apikey` a la base propia; no se envía el JWT del usuario a ese proyecto.
Un rechazo de la credencial del backend se informa como `503`, no como fallo del login del usuario.

RLS permanece habilitado con denegación predeterminada para clientes públicos. La secret key
del backend usa `service_role`, que omite RLS: el aislamiento por usuario depende de la validación
de IAM y de los filtros de las RPC, no de `auth.uid()` en esta base independiente.
El aviso informativo [RLS sin políticas](https://supabase.com/docs/guides/database/database-linter?lint=0008_rls_enabled_no_policy)
es intencional en este diseño, pues ningún cliente de usuario debe acceder directamente.

`IAM_SUPABASE_URL` identifica el origen de Auth como protección de configuración; el servicio
rechaza arrancar si `RECOMMENDATION_SUPABASE_URL` coincide con él. Las variables antiguas
`SUPABASE_URL` y `SUPABASE_PUBLISHABLE_KEY` ya no se usan en Recommendation.

Los objetos vacíos creados inicialmente en Auth fueron retirados después de verificar que no
contenían solicitudes ni outfits, sin modificar Profile o Auth. Sus scripts se conservan en
`supabase/legacy/auth-project/` para auditoría y nunca se aplican al proyecto nuevo.
Al borrar una cuenta en IAM hará falta un proceso de limpieza en Recommendation; ya no existe
un borrado en cascada entre bases. Esa integración de ciclo de vida sigue pendiente.

El snapshot conserva parámetros, preferencias, contexto y prendas referenciadas; nunca tokens,
contraseñas, fotos ni datos corporales. Los mensajes del chat se guardan en la base propia;
se envía a NVIDIA el texto de moda, el historial y las prendas, sin credenciales de IAM.
El feedback se registra para poder utilizarlo después; no modifica ni entrena un modelo.

- CTest ejecuta reglas y tres suites HTTP con IAM/Profile, Supabase y NVIDIA simulados.
- `python tests/history_sql.py` verifica la migración en una base nueva y desechable del
  cluster PostgreSQL de pruebas en `127.0.0.1:55440`, con `psycopg2`. Nunca apunta a Supabase.
  Comprueba persistencia sin tablas Auth, atomicidad, paginación, feedback, filtros por actor y
  denegación de acceso a roles de usuario. La revocación de sesión se verifica en las pruebas HTTP de IAM.
- Supabase remoto: proyecto independiente activo, migración registrada, ninguna FK hacia Auth,
  funciones invoker y acceso denegado tanto a `anon` como a `authenticated`.
- HTTP contra Supabase independiente real: creación, historial, detalle, favorito y rechazo
  verificados con IAM/Profile simulados. Los registros sintéticos temporales se eliminaron al
  terminar; la base quedó vacía. No se modificaron cuentas ni credenciales de IAM.
  El flujo completo con una sesión real válida de IAM sigue pendiente.

Solicitud: `occasion` obligatoria, de 1 a 120 bytes UTF-8; `style` opcional con el enum
del contrato; hasta 100 UUID únicos en `excluded_garments`; `max_results` entre 1 y 3
(por defecto 3). Se rechazan campos desconocidos y cuerpos mayores de 16 KiB.

## Estructura

```text
main.cpp                          Composición y servidor local
recommendation/
  domain/OutfitPolicy.*           Tipos y reglas sin Drogon ni JSON
  application/WardrobeClient.h    Puerto de consulta con callback
  application/IdentityVerifier.h  Puerto de verificación de identidad
  application/ProfileClient.h     Acceso autorizado a preferencias
  application/RecommendationRepository.h  Puerto de historial y feedback
  application/RecommendationService.*
  infrastructure/FixtureWardrobeClient.*
  infrastructure/DigitalClosetWardrobeClient.*
  infrastructure/GarmentJson.*
  infrastructure/IamIdentityVerifier.*
  infrastructure/IamProfileClient.*
  infrastructure/NvidiaRecommenderAgent.*
  infrastructure/SupabaseRecommendationRepository.*
  interfaces/RecommendationHttp.*
fixtures/garments.json             Prendas sintéticas con el contrato de Closet
tests/                            Reglas, errores de dependencia y HTTP
```

Solo en modo `demo`, `FixtureWardrobeClient` sirve el mismo dataset sintético a los usuarios autenticados. No representa
el armario personal de ninguno de ellos. El usuario verificado sí se propaga al servicio de aplicación.
Su parser transforma los
campos necesarios para las reglas actuales; no pretende implementar todavía toda
la validación de un proveedor remoto. Una dependencia fallida nunca se interpreta
como un armario vacío. El puerto usa callbacks para permitir un cliente HTTP
asíncrono con Digital Closet, ya implementado en `DigitalClosetWardrobeClient`.

## Siguientes entregas

1. Cliente HTTP de Digital Closet implementado y probado; configuración y límites en `docs/DIGITAL_CLOSET.md`. El siguiente paso es selección semántica de candidatos para armarios grandes.
2. Conversaciones implementadas: ver `docs/CONVERSATIONS.md` y carpeta 05 de Postman. Próxima mejora: resumen de conversaciones largas y evaluación de cambios de prendas.
3. Incorporar embeddings y recuperación vectorial en Supabase, manteniendo el aislamiento por usuario.
4. Evaluar la calidad de Nemotron frente al motor de reglas; el adaptador y validación ya están integrados.

La generación de imágenes de la persona con prendas pertenece a Digital Closet.
Este servicio entrega combinaciones estructuradas; no necesita fotos de la persona.

Contrato compartido: `../DIGITAL_CLOSET_RECOMMENDATION_CONTRACT.md` (propuesta 1.0.1).
