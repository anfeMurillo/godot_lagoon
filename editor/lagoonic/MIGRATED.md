# Migrado: el dock vive en modules/lagoonic/editor/
El dock Lagoonic ahora vive en `modules/lagoonic/editor/` (ver HOOK.md alli).
Se compila desde `modules/lagoonic/SCsub` (patron gridmap, solo editor).
El registro sale del propio modulo; `editor/` upstream no se toca.
Prohibido recrear docks aqui: FORK_POLICY solo permite anadir en modules/.
