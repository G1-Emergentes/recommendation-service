# Historial de la corrección de aislamiento

Estos scripts son únicamente un registro de los cambios realizados en el proyecto antiguo
de Auth (`larpiojfyzlqhmxfhbxm`). No deben ejecutarse en la base nueva de Recommendation.

- `20261110233019_recommendation_history.sql` creó inicialmente los objetos de Recommendation
  en Auth. Esa ubicación fue corregida por solicitud del usuario.
- El script `*_remove_recommendation_from_auth.sql` bloquea las tablas, comprueba que están
  vacías y retira exclusivamente los cuatro RPC, dos helpers, dos tablas y el esquema que
  se habían añadido. Si hay datos o dependencias inesperadas, la transacción falla sin borrar.

La limpieza ya se aplicó en Auth y se verificó que Profile y la validación de sesiones
permanecen presentes. La migración vigente para instalaciones nuevas está únicamente en
`supabase/migrations/` y pertenece al proyecto `mhtdipluiehprnmigtkx`.
