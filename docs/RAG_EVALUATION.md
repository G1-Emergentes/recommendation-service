# Evaluación del RAG de generación — 2026-11-11

Estado: las 10 suites de CTest pasaron. También pasaron la integración de generación
contra Neon de desarrollo y los seis casos con el modelo real.

## Calidad con el modelo real

`python tests/rag_quality_live.py` usa IAM, Digital Closet, NVIDIA y Neon reales.
Requiere la sesión del entorno Postman privado local y el closet de pruebas actual
con siete prendas. La persistencia del chat es un doble HTTP local: no crea ni
borra conversaciones del usuario en Supabase y no modifica las prendas de Closet.
La sincronización actualiza el índice derivado del propio usuario en Neon.
No forma parte de CTest porque requiere credenciales, red y cuota del proveedor.

| Caso | Resultado observado |
|---|---|
| Outfit casual sin blanco | Polo negro + jean azul + botines negros |
| Otra opción conservando sin blanco | Cambió el jean por pantalón chino beige |
| Oscuro pero llamativo | Aclaró que falta un inferior oscuro confirmado |
| Otra opción después del requisito oscuro | Conservó la restricción y volvió a aclarar |
| Aceptar el jean azul, relajar oscuro, sin blanco ni beige | Un outfit válido; memoria oscura desactivada y ambos colores excluidos |
| Otra opción cuando solo queda esa combinación | Aclaración sin repetir ni crear un outfit |

Los seis turnos respondieron 200. Se verificó que los IDs de las propuestas
pertenecían a los candidatos recuperados. El reporte local, con tiempos y respuestas,
queda en `out/rag-generation-quality.json` y no se versiona.

## Guards y fallos controlados

`python tests/rag_generation_http.py out/build/x64-debug/recommendation-service.exe`
usa Neon real en `dev-vector-store` y dobles HTTP de IAM, Closet, NVIDIA y almacenamiento.
Utiliza únicamente el actor sintético reservado y limpia sus vectores mediante
sincronización autenticada vacía, incluso si falla una aserción.

Comprueba recuperación acotada por categoría con más prendas que el contexto,
memoria sin blanco, rechazo de combinaciones repetidas y de prendas propias fuera
de los candidatos, esquema de la alternativa completo sin IDs duplicados,
exclusiones con `ni` aunque el modelo las omita, aclaración si falta calzado,
revalidación de revisiones antes del modelo (409), y fallo de embeddings (503)
sin invocar al LLM ni recurrir al inventario completo.

`tests/rag_http.py --live-neon` conserva la cobertura del índice: aislamiento entre
usuarios, sincronización idempotente, poda segura, lecturas parciales y vectores inválidos.
Las pruebas unitarias de reglas y conversación también cubren selección exclusiva
de candidatos y ausencia de fallback tras fallo de recuperación.

## Límites de la evidencia

No es una evaluación estadística de gusto estético ni de todos los mensajes
posibles. Los reconocedores explícitos cubren frases comunes en español; las demás
intenciones siguen dependiendo del modelo, y una salida inválida se rechaza sin
guardar medio turno. No se garantiza la exactitud de toda explicación libre.
Se recuperan seis prendas por categoría más referencias recientes: puede haber
alternativas válidas fuera de ese subconjunto en closets grandes. La diversidad
compara la última propuesta estructurada, no todo el historial. No se añadió
corpus externo de moda, reranker aprendido ni despliegue a producción.
