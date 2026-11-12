# Postman: Mirage Recommendation + IAM

Importar la colección `Mirage-Recommendation.postman_collection.json` y un entorno.
La plantilla pública `Mirage-Local.postman_environment.example.json` no contiene credenciales.
El entorno privado preparado para esta PC está en
`../out/postman/Mirage-Local.private.postman_environment.json`, fuera de Git.

Seleccionar **Mirage Local (privado)** en Postman. IAM debe estar en `8080` y
Recommendation en `8085`. En modo `live`, Digital Closet debe estar en `8081`.
Se puede usar Postman Desktop para acceder a esos servicios locales.

## Probar

1. Si el entorno privado tiene un `access_token` válido, empezar directamente por
   **02 Recomendaciones → Generar outfits - guardar IDs**.
2. Si no tiene token, completar `login_email` y `login_password` en el entorno local
   de Postman y ejecutar **01 Sesion IAM → Login - guardar tokens**.
3. El login guarda `access_token`, `refresh_token`, expiración y `user_id` automáticamente.
4. Generar outfits guarda `request_id` y el ID del primer outfit en `recommendation_id`.
   Detalle y feedback utilizan esa variable; no hace falta copiar IDs manualmente.
5. Probar historial, marcar/quitar favorito y rechazo en ese orden. Una propuesta rechazada
   no puede volver a marcarse favorita; para otra prueba generar nuevos outfits.
6. Ejecutar **04 Cerrar sesion** al terminar. Invalida la sesión y limpia los tokens del entorno.

**Renovar sesion - rotar tokens** actualiza ambos tokens. Además, el script de la colección
intenta refrescar antes de una petición protegida cuando el token vence en menos de 30 segundos.
Si el refresh fue revocado o falla, volver a iniciar sesión. No se reintenta automáticamente
el POST de generación para evitar crear solicitudes duplicadas.

Historial guarda `next_cursor`. Para otra página, agregar `cursor={{next_cursor}}` a su URL
solo cuando no esté vacío. `page_limit` controla el tamaño de página.

Cada envío de **Generar outfits** crea propuestas persistidas y puede consumir cuota de NVIDIA.
La fuente depende del servidor: `live` consulta prendas propias de Digital Closet;
`demo` usa fixtures. `/health` informa `mode` y `wardrobe_source`.
Si tu armario está vacío, primero crea prendas en Digital Closet. `garments` en la
respuesta contiene las instantáneas versionadas de las prendas usadas. Un `409
WARDROBE_CHANGED` indica que el armario cambió y no se guardó ninguna propuesta.
No hay fallback a prendas ficticias cuando falla Closet.
Ese request no crea conversaciones. Para chat, usar la carpeta **05 Chat conversacional**.

## Chat con texto libre

Reimportar la colección actualizada y seleccionar el entorno privado.
1. Ejecutar **05 Chat conversacional → Crear conversacion - guardar ID**.
2. Abrir **Enviar mensaje - escribe en Body** y cambiar el texto de `message` en Body.
3. Pulsar Send. El texto del asistente aparece en `assistant_message`; los outfits
   opcionales están en `recommendation.outfits`. Los IDs sirven para el feedback existente.
4. Cambiar `message` y volver a enviar para continuar el mismo chat. Por ejemplo:
   “Cambia los zapatos de la primera opción”. Cada envío genera un ID nuevo.
5. Para recuperar una respuesta tras perder la conexión, usar **Reintentar ultimo
   mensaje - mismo ID**. El script conserva el texto y el ID antes del envío, incluso
   si no llega la respuesta. Reutilizar el mismo ID/texto evita duplicar turnos guardados.

**Leer conversacion y mensajes** consulta el historial; **Listar mis conversaciones**
permite recuperar chats anteriores. El límite inicial es 20 turnos por conversación.
No enviar mensajes simultáneos al mismo chat. La búsqueda RAG se habilita con `RECOMMENDATION_RAG_ENABLED=true`; ver carpeta `08 RAG - Neon`.
Contrato y límites: `../docs/CONVERSATIONS.md`.

La carpeta `06 Memoria y gustos` permite probar redirección, restricciones que
persisten, liberación de colores y consulta/cambio explícito de gustos de Profile.
Recordar/olvidar sí modifica Profile: edita esos mensajes según los gustos que
quieras conservar. Importar la colección no modifica gustos por sí mismo.
La respuesta incluye `memory`; el request sigue usando solo `message` y
`client_message_id`. Ver `../docs/MEMORY.md` para alcance y errores de escritura.

La carpeta `07 Tools de consulta` prueba búsqueda de prendas negras y consulta
de un outfit guardado. Para la segunda, generar una propuesta nueva o establecer
`recommendation_id` con un outfit propio que tenga captura de prendas. Las antiguas
propuestas demo pueden carecer de esa captura. Las tools se llaman desde el mismo
chat; no requieren rutas, claves ni credenciales adicionales. Ver `../docs/TOOLS.md`.

## Datos privados

La colección utiliza exclusivamente el access token de IAM. No requiere ni incluye claves
de NVIDIA o Supabase. No publicar el entorno privado, tokens ni contraseñas.
La colección y la plantilla vacía se pueden compartir con el compañero.

Para que Codex prepare una sesión sin pegar contraseñas en el chat, se pueden guardar
`POSTMAN_EMAIL` y `POSTMAN_PASSWORD` en `../../iam-service/.env.postman-test`, que está
excluido de Git. Se utiliza el login normal de IAM: no se cambian contraseñas ni se crean cuentas.
El entorno exportado puede contener tokens, pero no se le copia la contraseña.

Después de completar ese archivo, ejecutar desde `recommendation-service`:

```powershell
python postman/prepare_session.py
```

El script genera el entorno privado con la sesión vigente y no imprime tokens ni contraseñas.
No ejecuta recomendaciones ni llama a NVIDIA.

Referencia de variables: https://learning.postman.com/docs/sending-requests/variables/environment-variables/

## RAG de prendas en Neon

La carpeta `08 RAG - Neon` contiene sincronización, búsqueda semántica directa y
una petición de chat semántica. Usa el mismo token IAM y puerto 8085. Nunca colocar
la conexión de Neon ni claves de embeddings en Postman. Sync devuelve `indexed`,
`removed` y `embeddings_generated`; repetir sin cambios debe generar cero embeddings.
La búsqueda devuelve metadatos y similitud, sin vectores; no hay umbral de relevancia
calibrado. El corpus inicial de moda se consulta también desde esta carpeta. Ver `../docs/VECTOR_STORE.md`.

### Generación con RAG obligatorio

Con RAG activado localmente, `Generar outfits` y `Enviar mensaje` recuperan candidatos
automáticamente. No hace falta ejecutar `sync` ni pedir `search_mode semantic` antes.
En un chat nuevo probar, en orden: `Un outfit casual para salir, sin blanco`,
`Otra opción`, `Ahora oscuro pero llamativo`, y `Otra opción`.
La restricción debe persistir y puede haber una aclaración si faltan prendas oscuras.
Una caída de embeddings/Neon bloquea el turno sin guardar medio resultado.

### Criterios de estilo y fuentes

En `08 RAG - Neon`, `Consultar criterios de estilo y fuentes` llama a
`POST /api/v1/style-knowledge/search` con query, style opcional y limit hasta 3.
`Chat - consultar criterios de estilo` usa la conversación creada en `05` y la tool
`search_style_knowledge`. La respuesta de lectura no genera outfits ni modifica memoria.
Las propuestas nuevas incluyen `style_references` con lo recuperado; las referencias
pueden no ser utilizadas por el modelo. No se necesita importar una clave de Neon.

### Polyvore

`08 RAG - Neon` incluye `Consultar ejemplos Polyvore`, con query, structure opcional
y limit hasta 3. La generación consulta ejemplos automáticamente después de conocer
las categorías disponibles. Las nuevas propuestas incluyen `outfit_examples`;
sus IDs externos no son garment_ids ni IDs de outfits propios. No hace falta
otra tool en el chat ni una sincronización manual antes de cada mensaje.
