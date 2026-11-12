# Estado de cierre del RAG — 2026-11-11

## Implementación funcional

| Componente | Estado |
|---|---|
| Identidad IAM y aislamiento de prendas por actor | Implementado y probado |
| Prendas reales, embeddings y sincronización idempotente | Implementado |
| Referencias de estilo revisadas y atribuidas | 8 fragmentos publicados |
| Ejemplos Polyvore reproducibles | 128 proyecciones de train; 24 de validación reservadas fuera del índice |
| Recuperación acotada y estructura compatible con el clóset | Implementado |
| Contextos separados: criterios, ejemplos externos y prendas propias | Implementado |
| Guards: UUID propios, restricciones, alternativas y revisiones | Implementado |
| Errores de dependencias sin recomendación parcial o fallback oculto | Implementado |
| Procedencia en JSON de nuevas recomendaciones | style_references y outfit_examples |
| Herramientas conversacionales | search_wardrobe, get_outfit, search_style_knowledge |
| Inspección de fuentes y ejemplos | Endpoints IAM, Swagger y colección local Postman |
| Rol de aplicación de solo lectura para corpus compartidos | Implementado con RLS obligatorio |
| Importación administrativa reproducible | Preparación, caché, manifiesto, licencia y exportación SQL por hash |

No falta una base adicional, otro modelo o más tools para completar este circuito.
La implementación funcional del RAG queda lista para probar el servicio de extremo
a extremo en desarrollo. No equivale a garantizar la calidad de todos los outfits.

## Pendiente para aprobar calidad y producción

1. **Evaluación de relevancia y utilidad.** Revisar casos de moda con usuarios y
   juzgar si los ejemplos recuperados ayudan; medir precisión/relevancia de top-k,
   aceptación de outfits, restricciones y diversidad. La auto-recuperación de ocho
   documentos y el filtro de estructura no demuestran compatibilidad estética.
2. **Calibración con esos juicios.** Ajustar corpus, consulta, cantidad de candidatos
   y filtros según resultados. No se añadió un umbral de coseno o reranker sin
   datos que permitan validarlo. Son decisiones de calidad, no un requisito de
   infraestructura pendiente para que la recuperación funcione.
3. **Entrega al entorno objetivo.** El servicio sigue local en 8085 y las migraciones
   vectoriales están en dev-vector-store. Desplegar/configurar el entorno de entrega,
   comprobar IAM/Closet reales allí y promover migraciones requiere un paso de release
   separado; main no se modificó. Los costes/cuotas y latencias deben verificarse
   en ese entorno, sin habilitar planes de pago automáticamente.

La actualización del corpus es administrativa y por versión revisada. No se
configuró una tarea programada ni navegación web en cada conversación. Polyvore
es histórico, textual y parcial: no cubre todas las ocasiones, estilos o tendencias.
El límite del clóset sigue en 100 prendas y la recuperación en seis por categoría
más referencias recientes. Las explicaciones libres del LLM no tienen verificación
semántica exhaustiva.

## No son requisitos de cierre ahora

No es necesario vectorizar conversaciones, guardar cada outfit generado como
conocimiento, añadir imágenes, entrenar un modelo, instalar otro framework de
agentes, crear otra base o introducir ANN para este tamaño de corpus. Esas
ampliaciones deben responder a fallos o necesidades medidos, no al checklist.

Detalles: `VECTOR_STORE.md`, `STYLE_KNOWLEDGE.md`, `RAG_EVALUATION.md` y
`../knowledge/polyvore/README.md`.

Verificación de esta entrega: 14 suites CTest aprobadas, integración Polyvore con
Neon real y proveedores controlados, regresión de criterios de estilo, controles
de permisos con el rol real y generación con NVIDIA/Closet/IAM/Neon reales.
El endpoint también respondió correctamente en el servicio principal local 8085.
