# Memoria conversacional y gustos permanentes

## Alcance

El chat se limita a moda, prendas, estilo y uso de Mirage. `REDIRECT` produce una
frase breve que devuelve la conversación a ese alcance. No desarrolla consultas
de cultura general, programación u otros temas.

## Memoria de una conversación

Cada turno devuelve `memory` y lo guarda atómicamente con el mensaje y los outfits:

```json
{
  "excluded_colors": ["WHITE"],
  "dark_only": false,
  "style": "CASUAL",
  "occasion": "Universidad",
  "profile_updated": false
}
```

`profile_updated` indica que ESE turno confirmó un cambio en Profile; no es un gusto.
Las demás propiedades son el estado acumulado de ese chat. El servicio lo recupera
antes de cada generación, incluso después de reiniciar. "Otra opción" no lo borra.
Los colores de una propuesta se validan contra el estado acumulado, aunque el modelo
omita la exclusión en su respuesta. "Ya puedes usar blanco" permite retirarla;
aceptar una aproximación permite desactivar `dark_only`. Estilo y ocasión se
conservan hasta que se cambian o se solicita eliminarlos. Una conversación nueva
empieza sin estas restricciones temporales.

No se reconstruyen restricciones de conversaciones anteriores a esta migración:
sus turnos reciben un estado inicial vacío. Para la nueva semántica, comenzar un
chat nuevo o volver a indicar las restricciones. Sigue el límite de 20 turnos.

## Gustos entre conversaciones

Profile en IAM conserva la única copia de gustos permanentes:

| Campo | Qué se puede recordar |
|---|---|
| `preferredStyles` | Estilos preferidos, como `MINIMALIST` o `CASUAL` |
| `preferredColors` | Familias de colores preferidos |
| `avoidedColors` | Familias de colores que se evitan |
| `preferredCategories` | Categorías de prendas preferidas |

Se leen de Profile en cada mensaje. `preferredCategories` se puede consultar y
editar; por ahora no influye en el orden de las propuestas. Colores evitados se
filtran antes de generar; estilos y colores preferidos son prioridades suaves.
Las restricciones explícitas del chat tienen prioridad sobre gustos suaves y no
pueden levantar un color evitado en Profile: primero hay que modificar ese gusto.

- "Hoy sin blanco" cambia este chat; no escribe en Profile.
- "Recuerda que evito blanco" solicita agregar `WHITE` a `avoidedColors`.
- "¿Qué gustos tienes guardados?" devuelve los valores actuales de Profile.
- "Olvida que evito blanco" elimina ese gusto para futuras conversaciones.

No se guardan automáticamente inferencias, ocasiones puntuales, clima, datos
sensibles ni medidas. Para varias categorías de gustos en una misma petición, el
prompt pide aclaración y hace una modificación por mensaje. Un cambio de Profile
no elimina automáticamente una restricción temporal ya activa en el chat.

## Escrituras y errores

No se aceptan `user_id`, memoria ni cambios de gustos enviados como campos extra
del request de chat. El usuario se deriva de IAM. El modelo devuelve una decisión
tipada `PREFERENCES_UPDATE`; el backend valida campo, operación, códigos y lenguaje
explícito de escritura antes de llamar al PUT existente de IAM con el token del
usuario. Operaciones: agregar, quitar, reemplazar o limpiar un campo.
Se preservan los otros campos y se rechazan colores preferidos/evitados en conflicto.
La confirmación se construye con la respuesta de IAM, no con la prosa del modelo.

El turno guardado hace que reintentar el mismo `client_message_id` no repita la
escritura. Profile y Recommendation son servicios distintos: no hay una transacción
distribuida. Si IAM no confirma, se devuelve `503 PROFILE_UPDATE_UNCONFIRMED` y hay
que consultar gustos antes de reintentar. Si Profile se actualizó pero falla guardar
el chat, se devuelve `PROFILE_UPDATED_CHAT_NOT_SAVED`, conservando el estado HTTP
del error de almacenamiento. El usuario debe recargar sus gustos y el chat.

IAM reemplaza el documento completo de preferencias con PUT; modificaciones
simultáneas desde distintos chats o clientes pueden competir y gana la última.
La memoria del chat mantiene el control de versión existente. Una mejora posterior
en IAM puede añadir actualizaciones condicionales por versión.

La interpretación del lenguaje, la clasificación y los cambios explícitos que
extrae el modelo siguen siendo probabilísticos; la validación estructural no
garantiza entender toda formulación posible. No se añadieron embeddings ni RAG.
